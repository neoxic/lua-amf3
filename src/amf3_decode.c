/*
** Copyright (C) 2012-2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#include <string.h>
#include <lauxlib.h>
#include "amf3.h"


static int decodeValue(lua_State *L, const char *buf, int pos, int size, int sidx, int oidx, int tidx);

static int decodeU29(lua_State *L, const char *buf, int pos, int size, int *val) {
	int res = 0, n = 0;
	unsigned char b;
	*val = 0;
	buf += pos;
	do {
		if ((pos + n) >= size) return luaL_error(L, "insufficient integer data at position %d", pos);
		b = buf[n++];
		if (n == 4) {
			res <<= 8;
			res |= b;
			break;
		}
		res <<= 7;
		res |= b & 0x7f;
	} while (b & 0x80);
	*val = res;
	return n;
}

static int decodeDouble(lua_State *L, const char *buf, int pos, int size, double *val) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	*val = 0;
	if ((pos + 8) > size) return luaL_error(L, "insufficient number data at position %d", pos);
	buf += pos;
	t.i = 1;
	if (!t.c) memcpy(u.c, buf, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
	}
	*val = u.d;
	return 8;
}

static int decodeRef(lua_State *L, const char *buf, int pos, int size, int ridx, int *val) {
	int pfx, ofs, def;
	ofs = decodeU29(L, buf, pos, size, &pfx);
	def = pfx & 1;
	pfx >>= 1;
	if (def) *val = pfx;
	else {
		*val = -1;
		lua_rawgeti(L, ridx, pfx + 1);
		if (lua_isnil(L, -1)) return luaL_error(L, "missing reference #%d at position %d", pfx, pos);
	}
	return ofs;
}

static int decodeString(lua_State *L, const char *buf, int pos, int size, int ridx, int blob) {
	int old = pos, len;
	pos += decodeRef(L, buf, pos, size, ridx, &len);
	if (len >= 0) {
		if ((pos + len) > size) return luaL_error(L, "insufficient data of length %d at position %d", len, pos);
		buf += pos;
		pos += len;
		lua_pushlstring(L, buf, len);
		if (blob || len) { /* Empty string is never sent by reference */
			lua_pushvalue(L, -1);
			luaL_ref(L, ridx);
		}
	}
	return pos - old;
}

static int decodeDate(lua_State *L, const char *buf, int pos, int size, int ridx) {
	int old = pos, pfx;
	pos += decodeRef(L, buf, pos, size, ridx, &pfx);
	if (pfx >= 0) {
		double d;
		pos += decodeDouble(L, buf, pos, size, &d);
		lua_pushnumber(L, d);
		lua_pushvalue(L, -1);
		luaL_ref(L, ridx);
	}
	return pos - old;
}

static int decodeArray(lua_State *L, const char *buf, int pos, int size, int sidx, int oidx, int tidx) {
	int old = pos, len;
	pos += decodeRef(L, buf, pos, size, oidx, &len);
	if (len >= 0) {
		int n;
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		for ( ;; ) { /* Associative portion */
			pos += decodeString(L, buf, pos, size, sidx, 0);
			if (!lua_objlen(L, -1)) {
				lua_pop(L, 1);
				break;
			}
			pos += decodeValue(L, buf, pos, size, sidx, oidx, tidx);
			lua_rawset(L, -3);
		}
		for (n = 1; n <= len; ++n) { /* Dense portion */
			pos += decodeValue(L, buf, pos, size, sidx, oidx, tidx);
			lua_rawseti(L, -2, n);
		}
	}
	return pos - old;
}

static int decodeObject(lua_State *L, const char *buf, int pos, int size, int sidx, int oidx, int tidx) {
	int old = pos, pfx;
	pos += decodeRef(L, buf, pos, size, oidx, &pfx);
	if (pfx >= 0) {
		int def = pfx & 1;
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		pfx >>= 1;
		if (def) { /* New class definition */
			int i, n = pfx >> 2;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_ref(L, tidx);
			lua_pushinteger(L, pfx);
			lua_rawseti(L, -2, 1);
			pos += decodeString(L, buf, pos, size, sidx, 0); /* Class name */
			lua_rawseti(L, -2, 2);
			for (i = 0; i < n; ++i) { /* Static member names */
				pos += decodeString(L, buf, pos, size, sidx, 0);
				lua_rawseti(L, -2, i + 3);
			}
		} else { /* Existing class definition */
			lua_rawgeti(L, tidx, pfx + 1);
			if (lua_isnil(L, -1)) return luaL_error(L, "missing class definition #%d at position %d", pfx, pos);
			lua_rawgeti(L, -1, 1);
			pfx = lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		if (pfx & 1) { /* Externalizable */
			pos += decodeValue(L, buf, pos, size, sidx, oidx, tidx);
			lua_setfield(L, -3, "_data");
		} else {
			int i, n = pfx >> 2;
			for (i = 0; i < n; ++i) {
				lua_rawgeti(L, -1, i + 3);
				pos += decodeValue(L, buf, pos, size, sidx, oidx, tidx);
				lua_rawset(L, -4);
			}
			if (pfx & 2) { /* Dynamic */
				for ( ;; ) {
					pos += decodeString(L, buf, pos, size, sidx, 0);
					if (!lua_objlen(L, -1)) {
						lua_pop(L, 1);
						break;
					}
					pos += decodeValue(L, buf, pos, size, sidx, oidx, tidx);
					lua_rawset(L, -4);
				}
			}
		}
		lua_rawgeti(L, -1, 2);
		if (lua_objlen(L, -1)) lua_setfield(L, -3, "_class");
		else lua_pop(L, 1);
		lua_pop(L, 1);
	}
	return pos - old;
}

static int decodeValue(lua_State *L, const char *buf, int pos, int size, int sidx, int oidx, int tidx) {
	int old = pos;
	if (pos >= size) return luaL_error(L, "insufficient type data at position %d", pos);
	lua_checkstack(L, 5);
	switch (buf[pos++]) {
		case AMF3_UNDEFINED:
		case AMF3_NULL:
			lua_pushnil(L);
			break;
		case AMF3_FALSE:
			lua_pushboolean(L, 0);
			break;
		case AMF3_TRUE:
			lua_pushboolean(L, 1);
			break;
		case AMF3_INTEGER: {
			int i;
			pos += decodeU29(L, buf, pos, size, &i);
			if (i & 0x10000000) i -= 0x20000000;
			lua_pushinteger(L, i);
			break;
		}
		case AMF3_DOUBLE: {
			double d;
			pos += decodeDouble(L, buf, pos, size, &d);
			lua_pushnumber(L, d);
			break;
		}
		case AMF3_STRING:
			pos += decodeString(L, buf, pos, size, sidx, 0);
			break;
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			pos += decodeString(L, buf, pos, size, oidx, 1);
			break;
		case AMF3_DATE:
			pos += decodeDate(L, buf, pos, size, oidx);
			break;
		case AMF3_ARRAY:
			pos += decodeArray(L, buf, pos, size, sidx, oidx, tidx);
			break;
		case AMF3_OBJECT:
			pos += decodeObject(L, buf, pos, size, sidx, oidx, tidx);
			break;
		default:
			return luaL_error(L, "unsupported value type %d at position %d", buf[pos - 1], pos - 1);
	}
	return pos - old;
}

int amf3_decode(lua_State *L) {
	size_t size;
	const char *buf = luaL_checklstring(L, 1, &size);
	int pos = luaL_optint(L, 2, 0);
	luaL_argcheck(L, pos >= 0, 2, "position may not be negative");
	lua_settop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushinteger(L, decodeValue(L, buf, pos, size, 2, 3, 4));
	return 2;
}
