/*
** Copyright (C) 2012 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#include <stdint.h>
#include <lauxlib.h>
#include "decode.h"
#include "amf3.h"


int decodeU29(int *val, const char *buf, int pos, int size, lua_State *L) {
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

int decodeDouble(double *val, const char *buf, int pos, int size, lua_State *L) {
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

int decodeStr(lua_State *L, const char* buf, int pos, int size, int ridx, int blob) {
	int old = pos, pfx;
	pos += decodeU29(&pfx, buf, pos, size, L);
	if (!(pfx & 1)) lua_rawgeti(L, ridx, (pfx >> 1) + 1);
	else {
		pfx >>= 1;
		if ((pos + pfx) > size) return luaL_error(L, "insufficient data of size %d at position %d", pfx, pos);
		lua_pushlstring(L, buf + pos, pfx);
		pos += pfx;
		if (blob || pfx) { // empty string is never sent by reference
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
			pos += decodeU29(&i, buf, pos, size, L);
			if (i & 0x10000000) i -= 0x20000000;
			lua_pushinteger(L, i);
			break;
		}
		case AMF3_DOUBLE: {
			double d;
			pos += decodeDouble(&d, buf, pos, size, L);
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
			int pfx;
			pos += decodeU29(&pfx, buf, pos, size, L);
			if (!(pfx & 1)) {
				lua_rawgeti(L, oidx, (pfx >> 1) + 1);
				break;
			}
			double d;
			pos += decodeDouble(&d, buf, pos, size, L);
			lua_pushnumber(L, d);
			lua_pushvalue(L, -1);
			luaL_ref(L, oidx);
			break;
		}
		case AMF3_ARRAY: {
			int pfx;
			pos += decodeU29(&pfx, buf, pos, size, L);
			if (!(pfx & 1)) {
				lua_rawgeti(L, oidx, (pfx >> 1) + 1);
				break;
			}
			pfx >>= 1;
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
			for (int n = 1; n <= pfx; ++n) { // dense portion
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
