/*
** Copyright (C) 2012-2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#include <stdlib.h>
#include <string.h>
#include <lauxlib.h>
#include "amf3.h"


typedef struct Chunk Chunk;
typedef struct Strap Strap;

struct Chunk {
	char  buf[1000]; /* some suitable chunk size to store both small and big buffers well enough and to fit 1Kb memory block */
	int   size;
	Chunk *next;
};

struct Strap {
	Chunk *first;
	Chunk *last;
};


static void encodeValue(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tfl);

static Chunk *newChunk() {
	Chunk *ch = malloc(sizeof(Chunk));
	if (!ch) return 0;
	ch->size = 0;
	ch->next = 0;
	return ch;
}

static void initStrap(Strap *st) {
	memset(st, 0, sizeof(Strap));
}

static void appendStrap(Strap *st, const char *buf, int size) {
	Chunk *ch = st->last;
	if (!ch) {
		ch = st->first = st->last = newChunk();
		if (!ch) return; /* out of memory */
	}
	for ( ;; ) {
		int avail = sizeof(ch->buf) - ch->size;
		if (avail >= size) break;
		memcpy(ch->buf + ch->size, buf, avail);
		ch->size += avail;
		ch->next = newChunk();
		if (!ch->next) return; /* out of memory */
		ch = st->last = ch->next;
		buf += avail;
		size -= avail;
	}
	memcpy(ch->buf + ch->size, buf, size);
	ch->size += size;
}

static void flushStrap(Strap *st, lua_State *L) {
	luaL_Buffer b;
	Chunk *ch = st->first;
	Chunk *next;
	luaL_buffinit(L, &b);
	while (ch) {
		luaL_addlstring(&b, ch->buf, ch->size);
		next = ch->next;
		free(ch);
		ch = next;
	}
	luaL_pushresult(&b);
	initStrap(st);
}

static int getTableLen(lua_State *L, int idx) {
	int len = 0;
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		++len;
		if ((lua_type(L, -2) != LUA_TNUMBER) || (lua_tointeger(L, -2) != len)) {
			lua_pop(L, 2);
			return -1;
		}
	}
	return len;
}

static void encodeChar(Strap *st, char c) {
	appendStrap(st, &c, 1);
}

static void encodeU29(Strap *st, int val) {
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
	appendStrap(st, buf, size);
}

static void encodeDouble(Strap *st, double val) {
	union { int i; char c; } t;
	union { double d; char c[8]; } u;
	char buf[8];
	t.i = 1;
	u.d = val;
	if (!t.c) memcpy(buf, u.c, 8);
	else { /* little-endian machine */
		int i;
		for (i = 0; i < 8; ++i) buf[7 - i] = u.c[i];
	}
	appendStrap(st, buf, 8);
}

static int encodeRef(Strap *st, lua_State *L, int idx, int ridx) {
	int ref;
	lua_pushvalue(L, idx);
	lua_rawget(L, ridx);
	ref = lua_isnil(L, -1) ? -1 : lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (ref >= 0) {
		encodeU29(st, ref << 1);
		return 1;
	}
	lua_rawgeti(L, ridx, 1);
	ref = lua_tointeger(L, -1);
	lua_pop(L, 1);
	if (ref <= AMF3_MAX_INT) {
		lua_pushvalue(L, idx);
		lua_pushinteger(L, ref);
		lua_rawset(L, ridx);
		lua_pushinteger(L, ref + 1);
		lua_rawseti(L, ridx, 1);
	}
	return 0;
}

static void encodeStr(Strap *st, lua_State *L, int idx, int ridx) {
	size_t len;
	const char *str = lua_tolstring(L, idx, &len);
	if (len && encodeRef(st, L, idx, ridx)) return; /* empty string is never sent by reference */
	if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
	encodeU29(st, (len << 1) | 1);
	appendStrap(st, str, len);
}

static void encodeArray(Strap *st, lua_State *L, int idx, int len, int sidx, int oidx, int *tfl) {
	int n;
	if (encodeRef(st, L, idx, oidx)) return;
	if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
	encodeU29(st, (len << 1) | 1);
	encodeChar(st, 0x01);
	for (n = 1; n <= len; ++n) {
		lua_rawgeti(L, idx, n);
		encodeValue(st, L, -1, sidx, oidx, tfl);
		lua_pop(L, 1);
	}
}

static void encodeObject(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tfl) {
	if (encodeRef(st, L, idx, oidx)) return;
	if (*tfl) encodeChar(st, 0x01); /* traits have been encoded earlier */
	else {
		*tfl = 1;
		encodeChar(st, 0x0b);
		encodeChar(st, 0x01);
	}
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		switch (lua_type(L, -2)) { /* key type */
			case LUA_TNUMBER:
				lua_pushvalue(L, -2);
				lua_tostring(L, -1); /* convert numeric key into string */
				encodeStr(st, L, -1, sidx);
				lua_pop(L, 1);
				break;
			case LUA_TSTRING:
				if (!lua_objlen(L, -2)) continue; /* empty key can't be represented in AMF3 */
				encodeStr(st, L, -2, sidx);
				break;
			default:
				continue;
		}
		encodeValue(st, L, -1, sidx, oidx, tfl);
	}
	encodeChar(st, 0x01);
}

static void encodeValue(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tfl) {
	if (idx < 0) idx = lua_gettop(L) + idx + 1;
	lua_checkstack(L, 5);
	switch (lua_type(L, idx)) {
		default:
		case LUA_TNIL:
			encodeChar(st, AMF3_UNDEFINED);
			break;
		case LUA_TBOOLEAN:
			encodeChar(st, lua_toboolean(L, idx) ? AMF3_TRUE : AMF3_FALSE);
			break;
		case LUA_TNUMBER: {
			double d = lua_tonumber(L, idx);
			int i = (int)d;
			if (((double)i == d) && (i >= AMF3_MIN_INT) && (i <= AMF3_MAX_INT)) {
				encodeChar(st, AMF3_INTEGER);
				encodeU29(st, i);
			} else {
				encodeChar(st, AMF3_DOUBLE);
				encodeDouble(st, d);
			}
			break;
		}
		case LUA_TSTRING:
			encodeChar(st, AMF3_STRING);
			encodeStr(st, L, idx, sidx);
			break;
		case LUA_TTABLE: {
			int len = getTableLen(L, idx);
			if (len >= 0) {
				encodeChar(st, AMF3_ARRAY);
				encodeArray(st, L, idx, len, sidx, oidx, tfl);
			} else {
				encodeChar(st, AMF3_OBJECT);
				encodeObject(st, L, idx, sidx, oidx, tfl);
			}
			break;
		}
	}
}

int amf3_encode(lua_State *L) {
	Strap st;
	int tfl = 0;
	luaL_checkany(L, 1);
	lua_settop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	initStrap(&st);
	encodeValue(&st, L, 1, 2, 3, &tfl);
	flushStrap(&st, L);
	return 1;
}
