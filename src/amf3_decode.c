/*
** Copyright (C) 2012-2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#include <string.h>
#include <lauxlib.h>
#include "amf3.h"


static int decodeValue(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx);

static int decodeU29(const char *buf, int pos, int size, lua_State *L, int *val) {
	int n = 0, ofs = 0;
	unsigned char b;
	*val = 0;
	buf += pos;
	do {
		if ((pos + ofs) >= size) return luaL_error(L, "insufficient U29 data at position %d", pos);
		b = buf[ofs++];
		if (ofs == 4) {
			n <<= 8;
			n |= b;
			break;
		}
		n <<= 7;
		n |= b & 0x7f;
	} while (b & 0x80);
	*val = n;
	return ofs;
}

static int decodeInteger(const char *buf, int pos, int size, lua_State *L) {
	int n, ofs = decodeU29(buf, pos, size, L, &n);
	if (n & 0x10000000) n -= 0x20000000;
	lua_pushinteger(L, n);
	return ofs;
}

static int decodeDouble(const char *buf, int pos, int size, lua_State *L) {
	union { int n; char c; } t;
	union { double d; char c[8]; } u;
	if ((pos + 8) > size) return luaL_error(L, "insufficient IEEE-754 data at position %d", pos);
	buf += pos;
	t.n = 1;
	if (!t.c) memcpy(u.c, buf, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
	}
	lua_pushnumber(L, u.d);
	return 8;
}

static int decodeRef(const char *buf, int pos, int size, lua_State *L, int ridx, int *val) {
	int old = pos, pfx, def;
	pos += decodeU29(buf, pos, size, L, &pfx);
	def = pfx & 1;
	pfx >>= 1;
	if (def) *val = pfx;
	else {
		*val = -1;
		lua_rawgeti(L, ridx, pfx + 1);
		if (lua_isnil(L, -1)) return luaL_error(L, "invalid reference %d at position %d", pfx, old);
	}
	return pos - old;
}

static int decodeString(const char *buf, int pos, int size, lua_State *L, int ridx, int blob) {
	int old = pos, len;
	pos += decodeRef(buf, pos, size, L, ridx, &len);
	if (len != -1) {
		if ((pos + len) > size) return luaL_error(L, "invalid length %d at position %d", len, old);
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

static int decodeDate(const char *buf, int pos, int size, lua_State *L, int ridx) {
	int old = pos, pfx;
	pos += decodeRef(buf, pos, size, L, ridx, &pfx);
	if (pfx != -1) {
		pos += decodeDouble(buf, pos, size, L);
		lua_pushvalue(L, -1);
		luaL_ref(L, ridx);
	}
	return pos - old;
}

static int decodeArray(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx) {
	int old = pos, len;
	pos += decodeRef(buf, pos, size, L, oidx, &len);
	if (len != -1) {
		int n;
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		for ( ;; ) { /* Associative portion */
			pos += decodeString(buf, pos, size, L, sidx, 0);
			if (!lua_objlen(L, -1)) {
				lua_pop(L, 1);
				break;
			}
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			lua_rawset(L, -3);
		}
		for (n = 1; n <= len; ++n) { /* Dense portion */
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			lua_rawseti(L, -2, n);
		}
	}
	return pos - old;
}

static int decodeObject(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx) {
	int old = pos, pfx;
	pos += decodeRef(buf, pos, size, L, oidx, &pfx);
	if (pfx != -1) {
		int def = pfx & 1;
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		pfx >>= 1;
		if (def) { /* New class definition */
			int i, n;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_ref(L, tidx);
			lua_pushinteger(L, pfx);
			lua_rawseti(L, -2, 1);
			pos += decodeString(buf, pos, size, L, sidx, 0); /* Class name */
			lua_rawseti(L, -2, 2);
			for (i = 0, n = pfx >> 2; i < n; ++i) { /* Static member names */
				pos += decodeString(buf, pos, size, L, sidx, 0);
				lua_rawseti(L, -2, i + 3);
			}
		} else { /* Existing class definition */
			lua_rawgeti(L, tidx, pfx + 1);
			if (lua_isnil(L, -1)) return luaL_error(L, "invalid class reference %d at position %d", pfx, old);
			lua_rawgeti(L, -1, 1);
			pfx = lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		if (pfx & 1) { /* Externalizable */
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			lua_setfield(L, -3, "_data");
		} else {
			int i, n;
			for (i = 0, n = pfx >> 2; i < n; ++i) {
				lua_rawgeti(L, -1, i + 3);
				pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
				lua_rawset(L, -4);
			}
			if (pfx & 2) { /* Dynamic */
				for ( ;; ) {
					pos += decodeString(buf, pos, size, L, sidx, 0);
					if (!lua_objlen(L, -1)) {
						lua_pop(L, 1);
						break;
					}
					pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
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

static int decodeValue(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx) {
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
		case AMF3_INTEGER:
			pos += decodeInteger(buf, pos, size, L);
			break;
		case AMF3_DOUBLE:
			pos += decodeDouble(buf, pos, size, L);
			break;
		case AMF3_STRING:
			pos += decodeString(buf, pos, size, L, sidx, 0);
			break;
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			pos += decodeString(buf, pos, size, L, oidx, 1);
			break;
		case AMF3_DATE:
			pos += decodeDate(buf, pos, size, L, oidx);
			break;
		case AMF3_ARRAY:
			pos += decodeArray(buf, pos, size, L, sidx, oidx, tidx);
			break;
		case AMF3_OBJECT:
			pos += decodeObject(buf, pos, size, L, sidx, oidx, tidx);
			break;
		default:
			return luaL_error(L, "unsupported value type %d at position %d", buf[old], old);
	}
	return pos - old;
}

int amf3_decode(lua_State *L) {
	size_t size;
	const char *buf = luaL_checklstring(L, 1, &size);
	int pos = luaL_optint(L, 2, 0);
	luaL_argcheck(L, (pos >= 0) && (pos <= (int)size), 2, "invalid position");
	lua_settop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushinteger(L, decodeValue(buf, pos, size, L, 2, 3, 4));
	return 2;
}
