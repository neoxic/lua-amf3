/*
** Copyright (C) 2012-2014 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#include "amf3.h"
#include <string.h>
#include <lauxlib.h>


static int decodeValue(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx);

static int decodeU8(const char *buf, int pos, int size, lua_State *L, unsigned char *val) {
	if (pos >= size) return luaL_error(L, "insufficient U8 data at position %d", pos);
	*val = buf[pos];
	return 1;
}

static int decodeU29(const char *buf, int pos, int size, lua_State *L, int *val) {
	int n = 0, ofs = 0;
	unsigned char b;
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

static int decodeU32(const char *buf, int pos, int size, lua_State *L, int sign) {
	union { int n; char c; } t;
	union { unsigned n; char c[4]; } u;
	lua_Number n;
	if ((pos + 4) > size) return luaL_error(L, "insufficient U32 data at position %d", pos);
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
	return 4;
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
		int i;
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		for ( ;; ) { /* Associative part */
			pos += decodeString(buf, pos, size, L, sidx, 0);
			if (!lua_objlen(L, -1)) {
				lua_pop(L, 1);
				break;
			}
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			lua_rawset(L, -3);
		}
		for (i = 0; i < len; ++i) { /* Dense part */
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			lua_rawseti(L, -2, i + 1);
		}
	}
	return pos - old;
}

static int decodeObject(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx) {
	int old = pos, pfx;
	pos += decodeRef(buf, pos, size, L, oidx, &pfx);
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
			pos += decodeString(buf, pos, size, L, sidx, 0); /* Class name */
			lua_rawseti(L, -2, 2);
			for (i = 0, n = pfx >> 2; i < n; ++i) { /* Static member names */
				pos += decodeString(buf, pos, size, L, sidx, 0);
				lua_rawseti(L, -2, i + 3);
			}
		} else { /* Existing traits */
			lua_rawgeti(L, tidx, pfx + 1);
			if (lua_isnil(L, -1)) return luaL_error(L, "invalid class reference %d at position %d", pfx, old);
			lua_rawgeti(L, -1, 1);
			pfx = lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		if (pfx & 1) { /* Externalizable */
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			lua_setfield(L, -2, "_data");
		} else {
			int i, n;
			for (i = 0, n = pfx >> 2; i < n; ++i) {
				lua_rawgeti(L, -2, i + 3);
				pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
				lua_rawset(L, -3);
			}
			if (pfx & 2) { /* Dynamic */
				for ( ;; ) {
					pos += decodeString(buf, pos, size, L, sidx, 0);
					if (!lua_objlen(L, -1)) {
						lua_pop(L, 1);
						break;
					}
					pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
					lua_rawset(L, -3);
				}
			}
		}
		lua_rawgeti(L, -2, 2);
		if (lua_objlen(L, -1)) lua_setfield(L, -2, "_class");
		else lua_pop(L, 1);
		lua_remove(L, -2);
	}
	return pos - old;
}

static int decodeVectorItem(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx, int type) {
	switch (type) {
		case AMF3_VECTOR_INT:
			return decodeU32(buf, pos, size, L, 1);
		case AMF3_VECTOR_UINT:
			return decodeU32(buf, pos, size, L, 0);
		case AMF3_VECTOR_DOUBLE:
			return decodeDouble(buf, pos, size, L);
		case AMF3_VECTOR_OBJECT:
			return decodeValue(buf, pos, size, L, sidx, oidx, tidx);
		default:
			return 0;
	}
}

static int decodeVector(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx, int type) {
	int old = pos, len;
	pos += decodeRef(buf, pos, size, L, oidx, &len);
	if (len != -1) {
		unsigned char fv;
		int i;
		pos += decodeU8(buf, pos, size, L, &fv); /* 'fixed-vector' marker */
		if (type == AMF3_VECTOR_OBJECT) { /* 'object-type-name' marker */
			pos += decodeString(buf, pos, size, L, sidx, 0);
			lua_pop(L, 1);
		}
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		for (i = 0; i < len; ++i) {
			pos += decodeVectorItem(buf, pos, size, L, sidx, oidx, tidx, type);
			lua_rawseti(L, -2, i + 1);
		}
	}
	return pos - old;
}

static int decodeDictionary(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx) {
	int old = pos, len;
	pos += decodeRef(buf, pos, size, L, oidx, &len);
	if (len != -1) {
		unsigned char wk;
		pos += decodeU8(buf, pos, size, L, &wk); /* 'weak-keys' marker */
		lua_newtable(L);
		lua_pushvalue(L, -1);
		luaL_ref(L, oidx);
		while (len--) {
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			pos += decodeValue(buf, pos, size, L, sidx, oidx, tidx);
			if (!lua_isnil(L, -2)) lua_rawset(L, -3);
			else lua_pop(L, 2);
		}
	}
	return pos - old;
}

static int decodeValue(const char *buf, int pos, int size, lua_State *L, int sidx, int oidx, int tidx) {
	int old = pos;
	unsigned char type;
	pos += decodeU8(buf, pos, size, L, &type);
	lua_checkstack(L, 5);
	switch (type) {
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
		case AMF3_VECTOR_INT:
		case AMF3_VECTOR_UINT:
		case AMF3_VECTOR_DOUBLE:
		case AMF3_VECTOR_OBJECT:
			pos += decodeVector(buf, pos, size, L, sidx, oidx, tidx, type);
			break;
		case AMF3_DICTIONARY:
			pos += decodeDictionary(buf, pos, size, L, sidx, oidx, tidx);
			break;
		default:
			return luaL_error(L, "invalid value type %d at position %d", type, old);
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
