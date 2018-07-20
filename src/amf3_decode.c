/*
** Copyright (C) 2012-2018 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
** copies of the Software, and to permit persons to whom the Software is
** furnished to do so, subject to the following conditions:
**
** The above copyright notice and this permission notice shall be included in
** all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
** AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
** LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
** OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
** THE SOFTWARE.
*/

#include <string.h>
#include "amf3.h"

static size_t decodeU8(lua_State *L, const char *buf, size_t pos, size_t size, int *val) {
	if (pos >= size) luaL_error(L, "insufficient U8 data at position %d", pos + 1);
	*val = buf[pos] & 0xff;
	return pos + 1;
}

static size_t decodeU29(lua_State *L, const char *buf, size_t pos, size_t size, int *val) {
	int len = 0, n = 0;
	unsigned char c;
	buf += pos;
	do {
		if (pos + len >= size) luaL_error(L, "insufficient U29 data at position %d", pos + 1);
		c = buf[len++];
		if (len == 4) {
			n <<= 8;
			n |= c;
			break;
		}
		n <<= 7;
		n |= c & 0x7f;
	} while (c & 0x80);
	*val = n;
	return pos + len;
}

static size_t decodeInteger(lua_State *L, const char *buf, size_t pos, size_t size) {
	int n;
	pos = decodeU29(L, buf, pos, size, &n);
	if (n & 0x10000000) n -= 0x20000000;
	lua_pushinteger(L, n);
	return pos;
}

static size_t decodeU32(lua_State *L, const char *buf, size_t pos, size_t size, int sign) {
	union { int n; char c; } t;
	union { unsigned n; char c[4]; } u;
	double n;
	if (pos + 4 > size) luaL_error(L, "insufficient U32 data at position %d", pos + 1);
	buf += pos;
	t.n = 1;
	if (!t.c) memcpy(u.c, buf, 4);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 4; ++i) u.c[i] = buf[3 - i];
	}
	if (sign) n = (signed)u.n;
	else n = u.n;
	lua_pushnumber(L, n);
	return pos + 4;
}

static size_t decodeDouble(lua_State *L, const char *buf, size_t pos, size_t size) {
	union { int n; char c; } t;
	union { double n; char c[8]; } u;
	if (pos + 8 > size) luaL_error(L, "insufficient IEEE-754 data at position %d", pos + 1);
	buf += pos;
	t.n = 1;
	if (!t.c) memcpy(u.c, buf, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
	}
	lua_pushnumber(L, u.n);
	return pos + 8;
}

static size_t decodeRef(lua_State *L, const char *buf, size_t pos, size_t size, int ridx, int *val) {
	int pfx, def, _pos = pos;
	pos = decodeU29(L, buf, pos, size, &pfx);
	def = pfx & 1;
	pfx >>= 1;
	if (def) *val = pfx;
	else {
		*val = -1;
		lua_rawgeti(L, ridx, pfx + 1);
		if (lua_isnil(L, -1)) luaL_error(L, "invalid reference %d at position %d", pfx, _pos + 1);
	}
	return pos;
}

static size_t decodeString(lua_State *L, const char *buf, size_t pos, size_t size, int ridx, int blob) {
	int len, _pos = pos;
	pos = decodeRef(L, buf, pos, size, ridx, &len);
	if (len != -1) {
		if (pos + len > size) luaL_error(L, "invalid length %d at position %d", len, _pos + 1);
		buf += pos;
		pos += len;
		lua_pushlstring(L, buf, len);
		if (blob || len) { /* Empty string is never sent by reference */
			lua_pushvalue(L, -1);
			luaL_ref(L, ridx);
		}
	}
	return pos;
}

static size_t decodeDate(lua_State *L, const char *buf, size_t pos, size_t size, int ridx) {
	int pfx;
	pos = decodeRef(L, buf, pos, size, ridx, &pfx);
	if (pfx != -1) {
		pos = decodeDouble(L, buf, pos, size);
		lua_pushvalue(L, -1);
		luaL_ref(L, ridx);
	}
	return pos;
}

static size_t decodeValue(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx);

static size_t decodeArray(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int len;
	pos = decodeRef(L, buf, pos, size, oidx, &len);
	if (len != -1) {
		int i;
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		checkStack(L);
		for (;;) { /* Associative part */
			pos = decodeString(L, buf, pos, size, sidx, 0);
			if (!lua_rawlen(L, -1)) {
				lua_pop(L, 1);
				break;
			}
			pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
			lua_rawset(L, -3);
		}
		lua_pushnil(L);
		if (lua_next(L, -2)) lua_pop(L, 2);
		else { /* Restore array length */
			lua_pushinteger(L, len);
			lua_setfield(L, -2, "__array");
		}
		for (i = 0; i < len; ++i) { /* Dense part */
			pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
			lua_rawseti(L, -2, i + 1);
		}
	}
	return pos;
}

static size_t decodeObject(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int pfx, _pos = pos;
	pos = decodeRef(L, buf, pos, size, oidx, &pfx);
	if (pfx != -1) {
		int def = pfx & 1;
		pfx >>= 1;
		if (def) { /* New traits */
			int i, n;
			lua_newtable(L);
			lua_pushvalue(L, -1);
			luaL_ref(L, tidx);
			lua_pushinteger(L, pfx);
			lua_rawseti(L, -2, 1);
			pos = decodeString(L, buf, pos, size, sidx, 0); /* Class name */
			lua_rawseti(L, -2, 2);
			for (i = 0, n = pfx >> 2; i < n; ++i) { /* Static member names */
				pos = decodeString(L, buf, pos, size, sidx, 0);
				lua_rawseti(L, -2, i + 3);
			}
		} else { /* Existing traits */
			lua_rawgeti(L, tidx, pfx + 1);
			if (lua_isnil(L, -1)) luaL_error(L, "invalid class reference %d at position %d", pfx, _pos + 1);
			lua_rawgeti(L, -1, 1);
			pfx = lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		checkStack(L);
		if (pfx & 1) { /* Externalizable */
			pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
			lua_setfield(L, -2, "__data");
		} else {
			int i, n;
			for (i = 0, n = pfx >> 2; i < n; ++i) {
				lua_rawgeti(L, -2, i + 3);
				pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
				lua_rawset(L, -3);
			}
			if (pfx & 2) { /* Dynamic */
				for (;;) {
					pos = decodeString(L, buf, pos, size, sidx, 0);
					if (!lua_rawlen(L, -1)) {
						lua_pop(L, 1);
						break;
					}
					pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
					lua_rawset(L, -3);
				}
			}
		}
		lua_rawgeti(L, -2, 2);
		if (lua_rawlen(L, -1)) lua_setfield(L, -2, "__class");
		else lua_pop(L, 1);
		lua_remove(L, -2);
	}
	return pos;
}

static size_t decodeVectorItem(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx, int type) {
	switch (type) {
		case AMF3_VECTOR_INT:
			return decodeU32(L, buf, pos, size, 1);
		case AMF3_VECTOR_UINT:
			return decodeU32(L, buf, pos, size, 0);
		case AMF3_VECTOR_DOUBLE:
			return decodeDouble(L, buf, pos, size);
		default:
			return decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
	}
}

static size_t decodeVector(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx, int type) {
	int len;
	pos = decodeRef(L, buf, pos, size, oidx, &len);
	if (len != -1) {
		int i;
		pos = decodeU8(L, buf, pos, size, &i); /* 'fixed-vector' marker */
		if (type == AMF3_VECTOR_OBJECT) { /* 'object-type-name' marker */
			pos = decodeString(L, buf, pos, size, sidx, 0);
			lua_pop(L, 1);
		}
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		checkStack(L);
		for (i = 0; i < len; ++i) {
			pos = decodeVectorItem(L, buf, pos, size, hidx, sidx, oidx, tidx, type);
			lua_rawseti(L, -2, i + 1);
		}
	}
	return pos;
}

static size_t decodeDictionary(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int len;
	pos = decodeRef(L, buf, pos, size, oidx, &len);
	if (len != -1) {
		int i;
		pos = decodeU8(L, buf, pos, size, &i); /* 'weak-keys' marker */
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		checkStack(L);
		while (len--) {
			pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
			pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
			if (!lua_isnil(L, -2)) lua_rawset(L, -3);
			else lua_pop(L, 2);
		}
	}
	return pos;
}

static size_t decodeValueData(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int type, _pos = pos;
	pos = decodeU8(L, buf, pos, size, &type);
	switch (type) {
		case AMF3_UNDEFINED:
			lua_pushnil(L);
			break;
		case AMF3_NULL:
			lua_pushlightuserdata(L, 0);
			break;
		case AMF3_FALSE:
			lua_pushboolean(L, 0);
			break;
		case AMF3_TRUE:
			lua_pushboolean(L, 1);
			break;
		case AMF3_INTEGER:
			return decodeInteger(L, buf, pos, size);
		case AMF3_DOUBLE:
			return decodeDouble(L, buf, pos, size);
		case AMF3_STRING:
			return decodeString(L, buf, pos, size, sidx, 0);
		case AMF3_XML:
		case AMF3_XMLDOC:
		case AMF3_BYTEARRAY:
			return decodeString(L, buf, pos, size, oidx, 1);
		case AMF3_DATE:
			return decodeDate(L, buf, pos, size, oidx);
		case AMF3_ARRAY:
			return decodeArray(L, buf, pos, size, hidx, sidx, oidx, tidx);
		case AMF3_OBJECT:
			return decodeObject(L, buf, pos, size, hidx, sidx, oidx, tidx);
		case AMF3_VECTOR_INT:
		case AMF3_VECTOR_UINT:
		case AMF3_VECTOR_DOUBLE:
		case AMF3_VECTOR_OBJECT:
			return decodeVector(L, buf, pos, size, hidx, sidx, oidx, tidx, type);
		case AMF3_DICTIONARY:
			return decodeDictionary(L, buf, pos, size, hidx, sidx, oidx, tidx);
		default:
			luaL_error(L, "invalid value type %d at position %d", type, _pos + 1);
			break;
	}
	return pos;
}

static size_t decodeValue(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	pos = decodeValueData(L, buf, pos, size, hidx, sidx, oidx, tidx);
	if (!lua_istable(L, -1) || lua_isnil(L, hidx)) return pos;
	lua_pushvalue(L, hidx);
	lua_insert(L, -2);
	lua_call(L, 1, 1); /* Call handler */
	return pos;
}

int amf3_decode(lua_State *L) {
	size_t size;
	const char *buf = luaL_checklstring(L, 1, &size);
	size_t pos = luaL_optinteger(L, 2, 1) - 1;
	luaL_argcheck(L, pos <= size, 2, "value out of range");
	lua_settop(L, 3);
	lua_newtable(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushinteger(L, decodeValue(L, buf, pos, size, 3, 4, 5, 6) + 1);
	return 2;
}
