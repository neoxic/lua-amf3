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

#include <stdint.h>
#include <string.h>
#include "amf3.h"

#define MAXSTACK 1000 /* Arbitrary stack size limit to check for recursion */

typedef struct {
	char *buf;
	size_t pos, size;
} Box;

static char *resizeBox(lua_State *L, Box *box, size_t size) {
	void *ud;
	lua_Alloc allocf = lua_getallocf(L, &ud);
	char *buf = allocf(ud, box->buf, box->size, size);
	if (!size) return 0;
	if (!buf) luaL_error(L, "cannot allocate buffer");
	box->buf = buf;
	box->size = size;
	return buf;
}

static int m__gc(lua_State *L) {
	resizeBox(L, lua_touserdata(L, 1), 0);
	return 0;
}

static Box *newBox(lua_State *L) {
	Box *box = lua_newuserdata(L, sizeof *box);
	box->buf = 0;
	box->pos = 0;
	box->size = 0;
	if (luaL_newmetatable(L, MODNAME)) {
		lua_pushcfunction(L, m__gc);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);
	resizeBox(L, box, 100);
	return box;
}

static char *appendData(lua_State *L, Box *box, size_t size) {
	char *buf = box->buf;
	size_t pos = box->pos;
	size_t old = box->size;
	size_t new = pos + size;
	if (new > old) { /* Expand buffer */
		old <<= 1; /* At least twice the old size */
		buf = resizeBox(L, box, new > old ? new : old);
	}
	box->pos = new;
	return buf + pos;
}

static void encodeData(lua_State *L, Box *box, const char *data, size_t size) {
	memcpy(appendData(L, box, size), data, size);
}

static void encodeByte(lua_State *L, Box *box, char val) {
	*appendData(L, box, 1) = val;
}

static void encodeU29(lua_State *L, Box *box, int val) {
	char buf[4];
	int len;
	val &= 0x1fffffff;
	if (val <= 0x7f) {
		buf[0] = val;
		len = 1;
	} else if (val <= 0x3fff) {
		buf[0] = (val >> 7) | 0x80;
		buf[1] = val & 0x7f;
		len = 2;
	} else if (val <= 0x1fffff) {
		buf[0] = (val >> 14) | 0x80;
		buf[1] = (val >> 7) | 0x80;
		buf[2] = val & 0x7f;
		len = 3;
	} else {
		buf[0] = (val >> 22) | 0x80;
		buf[1] = (val >> 15) | 0x80;
		buf[2] = (val >> 8) | 0x80;
		buf[3] = val;
		len = 4;
	}
	encodeData(L, box, buf, len);
}

static void encodeU32(lua_State *L, Box *box, int val) {
	union { int i; char c; } t;
	union { unsigned u; char c[4]; } u;
	char buf[4];
	t.i = 1;
	u.u = val;
	if (!t.c) memcpy(buf, u.c, 4);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 4; ++i) buf[3 - i] = u.c[i];
	}
	encodeData(L, box, buf, 4);
}

static void encodeDouble(lua_State *L, Box *box, double val) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	char buf[8];
	t.i = 1;
	u.d = val;
	if (!t.c) memcpy(buf, u.c, 8);
	else { /* Little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) buf[7 - i] = u.c[i];
	}
	encodeData(L, box, buf, 8);
}

static int encodeRef(lua_State *L, Box *box, int idx, int ridx) {
	int ref;
	lua_pushvalue(L, idx);
	lua_rawget(L, ridx);
	if (!lua_isnil(L, -1)) {
		encodeU29(L, box, lua_tointeger(L, -1) << 1);
		lua_pop(L, 1);
		return 1;
	}
	lua_rawgeti(L, ridx, 1);
	ref = lua_tointeger(L, -1);
	if (ref > AMF3_INT_MAX) luaL_error(L, "reference table overflow");
	lua_pop(L, 2);
	lua_pushvalue(L, idx);
	lua_pushinteger(L, ref);
	lua_rawset(L, ridx);
	lua_pushinteger(L, ref + 1);
	lua_rawseti(L, ridx, 1);
	return 0;
}

static void encodeString(lua_State *L, Box *box, int idx, int ridx) {
	size_t len;
	const char *str = lua_tolstring(L, idx, &len);
	if (len && encodeRef(L, box, idx, ridx)) return; /* Empty string is never sent by reference */
	if (len > AMF3_INT_MAX) luaL_error(L, "string too long");
	encodeU29(L, box, (len << 1) | 1);
	encodeData(L, box, str, len);
}

static int error(lua_State *L, int *nerr, const char *fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	lua_pushvfstring(L, fmt, ap);
	va_end(ap);
	lua_insert(L, -(++(*nerr)));
	return 0;
}

static int errorTrace(lua_State *L, int *nerr, int idx) {
	switch (lua_type(L, idx)) {
		case LUA_TNUMBER:
#if LUA_VERSION_NUM >= 503
			if (lua_isinteger(L, idx)) return error(L, nerr, "[%I] => ", lua_tointeger(L, idx));
#endif
			return error(L, nerr, "[%f] => ", lua_tonumber(L, idx));
		case LUA_TSTRING:
			return error(L, nerr, "[\"%s\"] => ", lua_tostring(L, idx));
		default:
			return error(L, nerr, "[%s: %p] => ", luaL_typename(L, idx), lua_topointer(L, idx));
	}
}

static int encodeValue(lua_State *L, Box *box, int idx, const char *ev, int sidx, int oidx, int *tf, int *nerr);

static int encodeArray(lua_State *L, Box *box, int idx, const char *ev, int sidx, int oidx, int *tf, int *nerr, int len, int top) {
	int i;
	encodeU29(L, box, (len << 1) | 1);
	encodeByte(L, box, 0x01); /* Empty associative part */
	for (i = 0; i < len; ++i) {
		lua_rawgeti(L, idx, i + 1);
		if (!encodeValue(L, box, top + 1, ev, sidx, oidx, tf, nerr)) return error(L, nerr, "[%d] => ", i + 1);
		lua_pop(L, 1);
	}
	return 1;
}

static int encodeObject(lua_State *L, Box *box, int idx, const char *ev, int sidx, int oidx, int *tf, int *nerr, int top) {
	if (*tf) encodeByte(L, box, 0x01); /* Traits have been encoded earlier */
	else {
		*tf = 1;
		encodeByte(L, box, 0x0b); /* Traits: no static members, externalizable=0, dynamic=1 */
		encodeByte(L, box, 0x01); /* Empty class name */
	}
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		encodeString(L, box, top + 1, sidx);
		if (!encodeValue(L, box, top + 2, ev, sidx, oidx, tf, nerr)) return errorTrace(L, nerr, top + 1);
	}
	encodeByte(L, box, 0x01); /* Empty key */
	return 1;
}

static int encodeDictionary(lua_State *L, Box *box, int idx, const char *ev, int sidx, int oidx, int *tf, int *nerr, int len, int top) {
	encodeU29(L, box, (len << 1) | 1);
	encodeByte(L, box, 0x00); /* weak-keys=0 */
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		if (!encodeValue(L, box, top + 1, ev, sidx, oidx, tf, nerr)) return errorTrace(L, nerr, idx);
		if (!encodeValue(L, box, top + 2, ev, sidx, oidx, tf, nerr)) return errorTrace(L, nerr, top + 1);
	}
	return 1;
}

static int isInteger(lua_State *L, int idx, lua_Integer *val) {
	lua_Integer i;
#if LUA_VERSION_NUM < 503
	lua_Number n;
	if (!lua_isnumber(L, idx)) return 0;
	n = lua_tonumber(L, idx);
	i = (lua_Integer)n;
	if (i != n) return 0;
#else
	int res;
	i = lua_tointegerx(L, idx, &res);
	if (!res) return 0;
#endif
	*val = i;
	return 1;
}

static int getTableType(lua_State *L, int idx, int *len) {
	int res;
	lua_Integer i;
	lua_getfield(L, idx, "__array");
	if (lua_toboolean(L, -1)) {
		res = LUA_TNUMBER; /* Dense array */
		if (!isInteger(L, -1, &i)) i = lua_rawlen(L, idx);
		if (i < 0) i = 0;
	} else {
		res = LUA_TSTRING; /* Associative array */
		for (i = 0, lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1), ++i) {
			if (res == LUA_TNONE) continue; /* Keep counting length */
			if (lua_type(L, -2) == LUA_TSTRING && lua_rawlen(L, -2)) continue;
			res = LUA_TNONE; /* Dictionary */
		}
	}
	lua_pop(L, 1);
	if (i > AMF3_INT_MAX) luaL_error(L, "table too big");
	*len = i;
	return res;
}

static int encodeValueData(lua_State *L, Box *box, int idx, const char *ev, int sidx, int oidx, int *tf, int *nerr) {
	switch (lua_type(L, idx)) {
		case LUA_TNIL:
			encodeByte(L, box, AMF3_UNDEFINED);
			break;
		case LUA_TBOOLEAN:
			encodeByte(L, box, lua_toboolean(L, idx) ? AMF3_TRUE : AMF3_FALSE);
			break;
		case LUA_TNUMBER: {
			lua_Integer i;
			if (isInteger(L, idx, &i) && i >= AMF3_INT_MIN && i <= AMF3_INT_MAX) {
				encodeByte(L, box, AMF3_INTEGER);
				encodeU29(L, box, i);
			} else {
				encodeByte(L, box, AMF3_DOUBLE);
				encodeDouble(L, box, lua_tonumber(L, idx));
			}
			break;
		}
		case LUA_TSTRING:
			encodeByte(L, box, AMF3_STRING);
			encodeString(L, box, idx, sidx);
			break;
		case LUA_TTABLE: {
			int len, top = lua_gettop(L);
			if (top >= MAXSTACK) return error(L, nerr, "recursion detected");
			if (lua_getmetatable(L, idx)) return error(L, nerr, "table with metatable unexpected");
			if (encodeRef(L, box, idx, oidx)) break;
			checkStack(L);
			switch (getTableType(L, idx, &len)) {
				case LUA_TNUMBER:
					encodeByte(L, box, AMF3_ARRAY);
					return encodeArray(L, box, idx, ev, sidx, oidx, tf, nerr, len, top);
				case LUA_TSTRING:
					encodeByte(L, box, AMF3_OBJECT);
					return encodeObject(L, box, idx, ev, sidx, oidx, tf, nerr, top);
				default:
					encodeByte(L, box, AMF3_DICTIONARY);
					return encodeDictionary(L, box, idx, ev, sidx, oidx, tf, nerr, len, top);
			}
		}
		case LUA_TLIGHTUSERDATA:
			if (!lua_touserdata(L, idx)) {
				encodeByte(L, box, AMF3_NULL);
				break;
			} /* Fall through */
		default:
			return error(L, nerr, "%s unexpected", luaL_typename(L, idx));
	}
	return 1;
}

static int encodeValue(lua_State *L, Box *box, int idx, const char *ev, int sidx, int oidx, int *tf, int *nerr) {
	int top = luaL_callmeta(L, idx, ev); /* Transform value */
	if (!encodeValueData(L, box, top ? lua_gettop(L) : idx, ev, sidx, oidx, tf, nerr)) return 0;
	if (top) lua_pop(L, 1); /* Remove modified value */
	return 1;
}

int amf3__encode(lua_State *L) {
	Box *box;
	int tf = 0, nerr = 0;
	const char *ev = luaL_optstring(L, 2, "__toAMF3");
	luaL_checkany(L, 1);
	lua_settop(L, 2);
	lua_newtable(L);
	lua_newtable(L);
	if (!encodeValue(L, box = newBox(L), 1, ev, 3, 4, &tf, &nerr)) {
		lua_concat(L, nerr);
		return luaL_argerror(L, 1, lua_tostring(L, -1));
	}
	lua_pushlstring(L, box->buf, box->pos);
	return 1;
}

int amf3__pack(lua_State *L) {
	const char *fmt = luaL_checkstring(L, 1);
	int arg, opt, top = lua_gettop(L);
	Box *box = newBox(L);
	for (arg = 2; (opt = *fmt++); ++arg) {
		if (arg > top) return luaL_argerror(L, arg, "value expected");
		switch (opt) {
			case 'b': {
				lua_Integer i = luaL_checkinteger(L, arg);
				checkRange(L, i >= 0 && i <= UINT8_MAX, arg);
				encodeByte(L, box, i);
				break;
			}
			case 'i': {
				lua_Integer i = luaL_checkinteger(L, arg);
				checkRange(L, i >= AMF3_INT_MIN && i <= AMF3_INT_MAX, arg);
				encodeU29(L, box, i);
				break;
			}
			case 'I': {
				lua_Integer i = luaL_checkinteger(L, arg); /* May overflow */
				lua_Number n = lua_tonumber(L, arg);
				checkRange(L, n >= INT32_MIN && n <= INT32_MAX, arg);
				encodeU32(L, box, i);
				break;
			}
			case 'u': {
				lua_Integer i = luaL_checkinteger(L, arg);
				checkRange(L, i >= 0 && i <= AMF3_U29_MAX, arg);
				encodeU29(L, box, i);
				break;
			}
			case 'U': {
				lua_Integer i = luaL_checkinteger(L, arg); /* May overflow */
				lua_Number n = lua_tonumber(L, arg);
				checkRange(L, n >= 0 && n <= UINT32_MAX, arg);
				encodeU32(L, box, i);
				break;
			}
			case 'd':
				encodeDouble(L, box, luaL_checknumber(L, arg));
				break;
			case 's': {
				size_t len;
				const char *str = luaL_checklstring(L, arg, &len);
				luaL_argcheck(L, len <= AMF3_U29_MAX, arg, "string too long");
				encodeU29(L, box, len);
				encodeData(L, box, str, len);
				break;
			}
			case 'S': {
				size_t len;
				const char *str = luaL_checklstring(L, arg, &len);
				luaL_argcheck(L, len <= UINT32_MAX, arg, "string too long");
				encodeU32(L, box, len);
				encodeData(L, box, str, len);
				break;
			}
			default:
				return luaL_error(L, "invalid format option '%c'", opt);
		}
	}
	lua_pushlstring(L, box->buf, box->pos);
	return 1;
}
