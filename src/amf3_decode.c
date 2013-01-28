/*
** Copyright (C) 2012-2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#include <string.h>
#include <lauxlib.h>
#include "amf3.h"
#include "amf3_decode.h"


static int decodeU29(lua_State *L, const char *buf, int pos, int size, int *val) {
	int ofs = 0, res = 0, tmp;
	*val = 0;
	do {
		if ((pos + ofs) >= size) return luaL_error(L, "insufficient integer data at position %d", pos);
		tmp = buf[pos + ofs];
		if (ofs == 3) {
			res <<= 8;
			res |= tmp & 0xff;
		} else {
			res <<= 7;
			res |= tmp & 0x7f;
		}
	} while ((++ofs < 4) && (tmp & 0x80));
	*val = res;
	return ofs;
}

static int decodeDouble(lua_State *L, const char *buf, int pos, int size, double *val) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	*val = 0;
	if ((pos + 8) > size) return luaL_error(L, "insufficient number data at position %d", pos);
	t.i = 1;
	if (!t.c) memcpy(u.c, buf + pos, 8); /* big-endian machine */
	else {
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[pos + 7 - i];
	}
	*val = u.d;
	return 8;
}

static int decodeRef(lua_State *L, const char *buf, int pos, int size, int ridx, int *val) {
	int pfx, ofs = decodeU29(L, buf, pos, size, &pfx);
	if (pfx & 1) *val = pfx >> 1;
	else {
		*val = -1;
		lua_rawgeti(L, ridx, (pfx >> 1) + 1);
	}
	return ofs;
}

static int decodeStr(lua_State *L, const char* buf, int pos, int size, int ridx, int loose) {
	int old = pos, len;
	pos += decodeRef(L, buf, pos, size, ridx, &len);
	if (len >= 0) {
		if ((pos + len) > size) return luaL_error(L, "insufficient data of length %d at position %d", len, pos);
		lua_pushlstring(L, buf + pos, len);
		pos += len;
		if (loose || len) { /* empty string is never sent by reference */
			lua_pushvalue(L, -1);
			luaL_ref(L, ridx);
		}
	}
	return pos - old;
}

static int decodeVal(lua_State* L, const char* buf, int pos, int size, int sidx, int oidx, int tidx) {
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
			pos += decodeStr(L, buf, pos, size, sidx, 0);
			break;
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			pos += decodeStr(L, buf, pos, size, oidx, 1);
			break;
		case AMF3_DATE: {
			int tmp;
			double d;
			pos += decodeRef(L, buf, pos, size, oidx, &tmp);
			if (tmp < 0) break;
			pos += decodeDouble(L, buf, pos, size, &d);
			lua_pushnumber(L, d);
			lua_pushvalue(L, -1);
			luaL_ref(L, oidx);
			break;
		}
		case AMF3_ARRAY: {
			int len, n;
			pos += decodeRef(L, buf, pos, size, oidx, &len);
			if (len < 0) break;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_ref(L, oidx);
			for ( ;; ) { /* associative portion */
				pos += decodeStr(L, buf, pos, size, sidx, 0);
				if (!lua_objlen(L, -1)) {
					lua_pop(L, 1);
					break;
				}
				pos += decodeVal(L, buf, pos, size, sidx, oidx, tidx);
				lua_rawset(L, -3);
			}
			for (n = 1; n <= len; ++n) { /* dense portion */
				pos += decodeVal(L, buf, pos, size, sidx, oidx, tidx);
				lua_rawseti(L, -2, n);
			}
			break;
		}
		case AMF3_OBJECT: {
			int pfx, def;
			pos += decodeRef(L, buf, pos, size, oidx, &pfx);
			if (pfx < 0) break;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_ref(L, oidx);
			def = pfx & 1;
			pfx >>= 1;
			if (def) { /* new class definition */
				int n, i;
				lua_newtable(L);
				lua_pushvalue(L, -1);
				luaL_ref(L, tidx);
				lua_pushinteger(L, pfx);
				lua_rawseti(L, -2, 1);
				pos += decodeStr(L, buf, pos, size, sidx, 0); /* class name */
				lua_rawseti(L, -2, 2);
				n = pfx >> 2;
				for (i = 0; i < n; ++i) { /* static member names */
					pos += decodeStr(L, buf, pos, size, sidx, 0);
					lua_rawseti(L, -2, i + 3);
				}
			} else { /* existing class definition */
				lua_rawgeti(L, tidx, pfx + 1);
				if (lua_isnil(L, -1)) return luaL_error(L, "missing class definition #%d at position %d", pfx, pos);
				lua_rawgeti(L, -1, 1);
				pfx = lua_tointeger(L, -1);
				lua_pop(L, 1);
			}
			if (pfx & 1) { /* externalizable */
				pos += decodeVal(L, buf, pos, size, sidx, oidx, tidx);
				lua_setfield(L, -3, "_data");
			} else {
				int n = pfx >> 2;
				int i;
				for (i = 0; i < n; ++i) {
					lua_rawgeti(L, -1, i + 3);
					pos += decodeVal(L, buf, pos, size, sidx, oidx, tidx);
					lua_rawset(L, -4);
				}
				if (pfx & 2) { /* dynamic */
					for ( ;; ) {
						pos += decodeStr(L, buf, pos, size, sidx, 0);
						if (!lua_objlen(L, -1)) {
							lua_pop(L, 1);
							break;
						}
						pos += decodeVal(L, buf, pos, size, sidx, oidx, tidx);
						lua_rawset(L, -4);
					}
				}
			}
			lua_rawgeti(L, -1, 2);
			if (lua_objlen(L, -1)) lua_setfield(L, -3, "_class");
			else lua_pop(L, 1);
			lua_pop(L, 1);
			break;
		}
		default:
			return luaL_error(L, "invalid type at position %d", pos - 1);
	}
	return pos - old;
}

int amf3_decode(lua_State *L) {
	size_t size;
	const char* buf = luaL_checklstring(L, 1, &size);
	int pos = luaL_optint(L, 2, 0);
	luaL_argcheck(L, pos >= 0, 2, "position may not be negative");
	lua_settop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushinteger(L, decodeVal(L, buf, pos, size, 2, 3, 4));
	return 2;
}
