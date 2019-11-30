/*
** Copyright (C) 2012-2019 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
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

static size_t decodeByte(lua_State *L, const char *buf, size_t pos, size_t size, int *val) {
	if (pos >= size) luaL_error(L, "insufficient data at position %d", pos + 1);
	*val = buf[pos] & 0xff;
	return pos + 1;
}

static size_t decodeU29(lua_State *L, const char *buf, size_t pos, size_t size, int *val) {
	int len = 0, x = 0;
	unsigned char c;
	buf += pos;
	do {
		if (pos + len >= size) luaL_error(L, "insufficient U29 data at position %d", pos + 1);
		c = buf[len++];
		if (len == 4) {
			x <<= 8;
			x |= c;
			break;
		}
		x <<= 7;
		x |= c & 0x7f;
	} while (c & 0x80);
	*val = x;
	return pos + len;
}

static size_t decodeInteger(lua_State *L, const char *buf, size_t pos, size_t size, int sign) {
	int val;
	pos = decodeU29(L, buf, pos, size, &val);
	if (sign && (val & 0x10000000)) val -= 0x20000000;
	lua_pushinteger(L, val);
	return pos;
}

static size_t decodeU32(lua_State *L, const char *buf, size_t pos, size_t size, unsigned *val) {
	union { int i; char c; } t;
	union { unsigned u; char c[4]; } u;
	if (pos + 4 > size) luaL_error(L, "insufficient U32 data at position %d", pos + 1);
	buf += pos;
	t.i = 1;
	if (!t.c) memcpy(u.c, buf, 4);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 4; ++i) u.c[i] = buf[3 - i];
	}
	*val = u.u;
	return pos + 4;
}

static size_t decodeInt32(lua_State *L, const char *buf, size_t pos, size_t size, int sign) {
	unsigned val;
	pos = decodeU32(L, buf, pos, size, &val);
	if (sign) lua_pushinteger(L, (signed)val);
	else { /* 'val' may overfill 'lua_Integer' */
		lua_Number n = val;
		lua_Integer i = (lua_Integer)n;
		if (i == n) lua_pushinteger(L, i);
		else lua_pushnumber(L, n);
	}
	return pos;
}

static size_t decodeDouble(lua_State *L, const char *buf, size_t pos, size_t size) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	if (pos + 8 > size) luaL_error(L, "insufficient IEEE-754 data at position %d", pos + 1);
	buf += pos;
	t.i = 1;
	if (!t.c) memcpy(u.c, buf, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) u.c[i] = buf[7 - i];
	}
	lua_pushnumber(L, u.d);
	return pos + 8;
}

static size_t decodeRef(lua_State *L, const char *buf, size_t pos, size_t size, int ridx, int *val) {
	int pfx, def;
	size_t pos_ = pos;
	pos = decodeU29(L, buf, pos, size, &pfx);
	def = pfx & 1;
	pfx >>= 1;
	if (def) {
		*val = pfx;
		return pos;
	}
	lua_rawgeti(L, ridx, pfx + 1);
	if (lua_isnil(L, -1)) luaL_error(L, "invalid reference %d at position %d", pfx, pos_ + 1);
	*val = -1;
	return pos;
}

static void storeRef(lua_State *L, int ridx) {
	lua_pushvalue(L, -1);
	lua_rawseti(L, ridx, lua_rawlen(L, ridx) + 1);
}

static size_t decodeString(lua_State *L, const char *buf, size_t pos, size_t size, int ridx, int blob) {
	int len;
	pos = decodeRef(L, buf, pos, size, ridx, &len);
	if (len == -1) return pos;
	if (pos + len > size) luaL_error(L, "insufficient data of length %d at position %d", len, pos + 1);
	lua_pushlstring(L, buf + pos, len);
	if (blob || len) storeRef(L, ridx); /* Empty string is never sent by reference */
	return pos + len;
}

static size_t decodeDate(lua_State *L, const char *buf, size_t pos, size_t size, int ridx) {
	int pfx;
	pos = decodeRef(L, buf, pos, size, ridx, &pfx);
	if (pfx == -1) return pos;
	pos = decodeDouble(L, buf, pos, size);
	storeRef(L, ridx);
	return pos;
}

static size_t decodeValue(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx);

static size_t decodeArray(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int len, i;
	pos = decodeRef(L, buf, pos, size, oidx, &len);
	if (len == -1) return pos;
	lua_newtable(L);
	storeRef(L, oidx);
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
	for (i = 0; i < len; ++i) { /* Dense part */
		pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
		lua_rawseti(L, -2, i + 1);
	}
	lua_pushinteger(L, len);
	lua_setfield(L, -2, "__array");
	return pos;
}

static size_t decodeObject(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int pfx, def;
	size_t pos_ = pos;
	pos = decodeRef(L, buf, pos, size, oidx, &pfx);
	if (pfx == -1) return pos;
	def = pfx & 1;
	pfx >>= 1;
	if (def) { /* New traits */
		int i, n = pfx >> 2;
		lua_newtable(L);
		storeRef(L, tidx);
		lua_pushinteger(L, pfx);
		lua_rawseti(L, -2, 1);
		pos = decodeString(L, buf, pos, size, sidx, 0); /* Class name */
		lua_rawseti(L, -2, 2);
		for (i = 0; i < n; ++i) { /* Static member names */
			pos = decodeString(L, buf, pos, size, sidx, 0);
			lua_rawseti(L, -2, i + 3);
		}
	} else { /* Existing traits */
		lua_rawgeti(L, tidx, pfx + 1);
		if (lua_isnil(L, -1)) luaL_error(L, "invalid class reference %d at position %d", pfx, pos_ + 1);
		lua_rawgeti(L, -1, 1);
		pfx = lua_tointeger(L, -1);
		lua_pop(L, 1);
	}
	lua_newtable(L);
	storeRef(L, oidx);
	checkStack(L);
	if (pfx & 1) { /* Externalizable */
		pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
		lua_setfield(L, -2, "__data");
	} else {
		int i, n = pfx >> 2;
		for (i = 0; i < n; ++i) {
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
	return pos;
}

static size_t decodeVectorItem(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx, int type) {
	switch (type) {
		case AMF3_VECTOR_INT:
			return decodeInt32(L, buf, pos, size, 1);
		case AMF3_VECTOR_UINT:
			return decodeInt32(L, buf, pos, size, 0);
		case AMF3_VECTOR_DOUBLE:
			return decodeDouble(L, buf, pos, size);
		default:
			return decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
	}
}

static size_t decodeVector(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx, int type) {
	int len, i;
	pos = decodeRef(L, buf, pos, size, oidx, &len);
	if (len == -1) return pos;
	pos = decodeByte(L, buf, pos, size, &i); /* 'fixed-vector' marker */
	if (type == AMF3_VECTOR_OBJECT) { /* 'object-type-name' marker */
		pos = decodeString(L, buf, pos, size, sidx, 0);
		lua_pop(L, 1);
	}
	lua_newtable(L);
	storeRef(L, oidx);
	checkStack(L);
	for (i = 0; i < len; ++i) {
		pos = decodeVectorItem(L, buf, pos, size, hidx, sidx, oidx, tidx, type);
		lua_rawseti(L, -2, i + 1);
	}
	return pos;
}

static size_t decodeDictionary(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int len, i;
	pos = decodeRef(L, buf, pos, size, oidx, &len);
	if (len == -1) return pos;
	pos = decodeByte(L, buf, pos, size, &i); /* 'weak-keys' marker */
	lua_newtable(L);
	storeRef(L, oidx);
	checkStack(L);
	while (len--) {
		pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
		pos = decodeValue(L, buf, pos, size, hidx, sidx, oidx, tidx);
		if (!lua_isnil(L, -2)) lua_rawset(L, -3);
		else lua_pop(L, 2);
	}
	return pos;
}

static size_t decodeValueData(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	int type;
	size_t pos_ = pos;
	pos = decodeByte(L, buf, pos, size, &type);
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
			return decodeInteger(L, buf, pos, size, 1);
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
			luaL_error(L, "invalid value type %d at position %d", type, pos_ + 1);
			break;
	}
	return pos;
}

static size_t decodeValue(lua_State *L, const char *buf, size_t pos, size_t size, int hidx, int sidx, int oidx, int tidx) {
	pos = decodeValueData(L, buf, pos, size, hidx, sidx, oidx, tidx);
	if (lua_isnil(L, hidx) || !lua_istable(L, -1)) return pos;
	lua_pushvalue(L, hidx);
	lua_insert(L, -2);
	lua_call(L, 1, 1); /* Transform value */
	return pos;
}

int amf3__decode(lua_State *L) {
	size_t size;
	const char *buf = luaL_checklstring(L, 1, &size);
	size_t pos = luaL_optinteger(L, 2, 1) - 1;
	checkRange(L, pos <= size, 2);
	lua_settop(L, 3);
	lua_newtable(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushinteger(L, decodeValue(L, buf, pos, size, 3, 4, 5, 6) + 1);
	return 2;
}

int amf3__unpack(lua_State *L) {
	const char *fmt = luaL_checkstring(L, 1);
	size_t size;
	const char *buf = luaL_checklstring(L, 2, &size);
	size_t pos = luaL_optinteger(L, 3, 1) - 1;
	int nres, opt;
	checkRange(L, pos <= size, 3);
	for (nres = 0; (opt = *fmt++); ++nres) {
		switch (opt) {
			case 'b': {
				int val;
				pos = decodeByte(L, buf, pos, size, &val);
				lua_pushinteger(L, val);
				break;
			}
			case 'i':
				pos = decodeInteger(L, buf, pos, size, 1);
				break;
			case 'I':
				pos = decodeInt32(L, buf, pos, size, 1);
				break;
			case 'u':
				pos = decodeInteger(L, buf, pos, size, 0);
				break;
			case 'U':
				pos = decodeInt32(L, buf, pos, size, 0);
				break;
			case 'd':
				pos = decodeDouble(L, buf, pos, size);
				break;
			case 's': {
				int len;
				pos = decodeU29(L, buf, pos, size, &len);
				if (pos + len > size) luaL_error(L, "insufficient data of length %d at position %d", len, pos + 1);
				lua_pushlstring(L, buf + pos, len);
				pos += len;
				break;
			}
			case 'S': {
				unsigned len;
				pos = decodeU32(L, buf, pos, size, &len);
				if (pos + len > size) luaL_error(L, "insufficient data of length %d at position %d", len, pos + 1);
				lua_pushlstring(L, buf + pos, len);
				pos += len;
				break;
			}
			default:
				return luaL_error(L, "invalid format option '%c'", opt);
		}
	}
	lua_pushinteger(L, pos + 1);
	return nres + 1;
}
