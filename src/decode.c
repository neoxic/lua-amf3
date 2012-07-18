/*
** Copyright (C) 2012 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#include <stdint.h>
#include <lauxlib.h>
#include "decode.h"
#include "amf3.h"


int decodeU29(lua_State *L, const char *buf, int pos, int size, int *val) {
	int ofs = 0, res = 0, tmp;
	do {
		if ((pos + ofs) >= size) return luaL_error(L, "insufficient U29 data at position %d", pos);
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

int decodeDouble(lua_State *L, const char *buf, int pos, int size, double *val) {
	if ((pos + 8) > size) return luaL_error(L, "insufficient DOUBLE data at position %d", pos);
	int64_t l = 0;
	for (int i = 0; i < 8; ++i) {
		l <<= 8;
		l |= buf[pos + i] & 0xff;
	}
	union {
		int64_t l;
		double d;
	} u = { l };
	*val = u.d;
	return 8;
}

int decodeRef(lua_State *L, const char *buf, int pos, int size, int ridx, int *val) {
	int pfx, ofs = decodeU29(L, buf, pos, size, &pfx);
	if (pfx & 1) *val = pfx >> 1;
	else {
		*val = -1;
		lua_rawgeti(L, ridx, (pfx >> 1) + 1);
	}
	return ofs;
}

int decodeStr(lua_State *L, const char* buf, int pos, int size, int ridx, int blob) {
	int old = pos, len;
	pos += decodeRef(L, buf, pos, size, ridx, &len);
	if (len >= 0) {
		if ((pos + len) > size) return luaL_error(L, "insufficient data of length %d at position %d", len, pos);
		lua_pushlstring(L, buf + pos, len);
		pos += len;
		if (blob || len) { // empty string is never sent by reference
			lua_pushvalue(L, -1);
			luaL_ref(L, ridx);
		}
	}
	return pos - old;
}

int decode(lua_State* L, const char* buf, int pos, int size, int sidx, int oidx) {
	if (pos >= size) return luaL_error(L, "insufficient type data at position %d", pos);
	int old = pos;
	lua_checkstack(L, 4);
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
		case AMF3_BYTEARRAY: {
			pos += decodeStr(L, buf, pos, size, oidx, 1);
			break;
		case AMF3_DATE: {
			int tmp;
			pos += decodeRef(L, buf, pos, size, oidx, &tmp);
			if (tmp < 0) break;
			double d;
			pos += decodeDouble(L, buf, pos, size, &d);
			lua_pushnumber(L, d);
			lua_pushvalue(L, -1);
			luaL_ref(L, oidx);
			break;
		}
		case AMF3_ARRAY: {
			int len;
			pos += decodeRef(L, buf, pos, size, oidx, &len);
			if (len < 0) break;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_ref(L, oidx);
			for ( ;; ) { // associative portion
				pos += decodeStr(L, buf, pos, size, sidx, 0);
				if (!lua_objlen(L, -1)) {
					lua_pop(L, 1);
					break;
				}
				pos += decode(L, buf, pos, size, sidx, oidx);
				lua_rawset(L, -3);
			}
			for (int n = 1; n <= len; ++n) { // dense portion
				pos += decode(L, buf, pos, size, sidx, oidx);
				lua_rawseti(L, -2, n);
			}
			break;
		}
		case AMF3_OBJECT:
			return luaL_error(L, "unsupported type OBJECT at position %d", pos - 1);
		}
		default:
			return luaL_error(L, "invalid type at position %d", pos - 1);
	}
	return pos - old;
}
