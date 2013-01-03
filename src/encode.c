/*
** Copyright (C) 2012 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#include <stdint.h>
#include <lauxlib.h>
#include "encode.h"
#include "amf3.h"


void encodeChar(strap_t *strap, char c) {
	appendStrap(strap, &c, 1);
}

void encodeU29(strap_t *strap, int val) {
	char buf[4];
	int size;
	val &= 0x1fffffff;
	if (val <= 0x7f) {
		buf[0] = val;
		size = 1;
	} else if (val <= 0x3fff) {
		buf[1] = val & 0x7f;
		val >>= 7;
		buf[0] = val | 0x80;
		size = 2;
	} else if (val <= 0x1fffff) {
		buf[2] = val & 0x7f;
		val >>= 7;
		buf[1] = val | 0x80;
		val >>= 7;
		buf[0] = val | 0x80;
		size = 3;
	} else {
		buf[3] = val;
		val >>= 8;
		buf[2] = val | 0x80;
		val >>= 7;
		buf[1] = val | 0x80;
		val >>= 7;
		buf[0] = val | 0x80;
		size = 4;
	}
	appendStrap(strap, buf, size);
}

void encodeDouble(strap_t *strap, double val) {
	union {
		double d;
		int64_t l;
	} u = { val };
	int64_t l = u.l;
	char buf[8];
	for (int i = 0; i < 8; ++i) {
		buf[7 - i] = l;
		l >>= 8;
	}
	appendStrap(strap, buf, 8);
}

int encodeRef(strap_t *strap, lua_State *L, int idx, int ridx) {
	lua_pushvalue(L, idx);
	lua_rawget(L, ridx);
	int ref = lua_isnil(L, -1) ? -1 : lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (ref >= 0) {
		encodeU29(strap, ref << 1);
		return 1;
	}
	lua_rawgeti(L, ridx, 1);
	ref = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_pushvalue(L, idx);
	lua_pushinteger(L, ref);
	lua_rawset(L, ridx);
	lua_pushinteger(L, ref + 1);
	lua_rawseti(L, ridx, 1);
	return 0;
}

void encodeStr(strap_t *strap, lua_State *L, int idx, int ridx) {
	size_t len;
	const char *str = lua_tolstring(L, idx, &len);
	if (len && encodeRef(strap, L, idx, ridx)) return; // empty string is never sent by reference
	if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
	encodeU29(strap, (len << 1) | 1);
	appendStrap(strap, str, len);
}

void encode(strap_t *strap, lua_State *L, int idx, int sidx, int oidx) {
	if (idx < 0) idx = lua_gettop(L) + idx + 1;
	lua_checkstack(L, 5);
	switch (lua_type(L, idx)) {
		default:
		case LUA_TNIL:
			encodeChar(strap, AMF3_UNDEFINED);
			break;
		case LUA_TBOOLEAN:
			encodeChar(strap, lua_toboolean(L, idx) ? AMF3_TRUE : AMF3_FALSE);
			break;
		case LUA_TNUMBER: {
			double d = lua_tonumber(L, idx);
			int i = (int)d;
			if (((double)i == d) && (i >= AMF3_MIN_INT) && (i <= AMF3_MAX_INT)) {
				encodeChar(strap, AMF3_INTEGER);
				encodeU29(strap, i);
			} else {
				encodeChar(strap, AMF3_DOUBLE);
				encodeDouble(strap, d);
			}
			break;
		}
		case LUA_TSTRING: {
			encodeChar(strap, AMF3_STRING);
			encodeStr(strap, L, idx, sidx);
			break;
		}
		case LUA_TTABLE: {
			encodeChar(strap, AMF3_ARRAY);
			if (encodeRef(strap, L, idx, oidx)) break;
			int dense = 1, len = 0;
			for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
				++len;
				if ((lua_type(L, -2) != LUA_TNUMBER) || (lua_tointeger(L, -2) != len)) {
					lua_pop(L, 2);
					dense = 0;
					break;
				}
			}
			if (dense) { // dense array
				if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
				encodeU29(strap, (len << 1) | 1);
				encodeChar(strap, 0x01);
				for (int n = 1; n <= len; ++n) {
					lua_rawgeti(L, idx, n);
					encode(strap, L, -1, sidx, oidx);
					lua_pop(L, 1);
				}
			} else { // associative array
				encodeChar(strap, 0x01);
				for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
					switch (lua_type(L, -2)) { // key type
						case LUA_TNUMBER: {
							lua_pushvalue(L, -2);
							lua_tostring(L, -1); // convert numeric key into string
							encodeStr(strap, L, -1, sidx);
							lua_pop(L, 1);
							break;
						}
						case LUA_TSTRING: {
							if (!lua_objlen(L, -2)) continue; // empty key can't be represented in AMF3
							encodeStr(strap, L, -2, sidx);
							break;
						}
						default:
							continue;
					}
					encode(strap, L, -1, sidx, oidx);
				}
				encodeChar(strap, 0x01);
			}
			break;
		}
	}
}
