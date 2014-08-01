/*
** Copyright (C) 2012-2014 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#include <stdlib.h>
#include <string.h>
#include <lauxlib.h>
#include "amf3.h"


typedef struct Chunk Chunk;
typedef struct Strap Strap;

struct Chunk {
	char  buf[1000]; /* Some suitable chunk size to store both small and big buffers well enough and to fit 1Kb memory block */
	int   size;
	Chunk *next;
};

struct Strap {
	Chunk *first;
	Chunk *last;
};


static Chunk *newChunk() {
	Chunk *ch = malloc(sizeof *ch);
	if (!ch) return 0;
	ch->size = 0;
	ch->next = 0;
	return ch;
}

static void initStrap(Strap *st) {
	memset(st, 0, sizeof *st);
}

static void appendStrap(Strap *st, const char *buf, int size) {
	Chunk *ch = st->last;
	if (!ch) {
		ch = st->first = st->last = newChunk();
		if (!ch) return; /* Out of memory */
	}
	for ( ;; ) {
		int avail = sizeof ch->buf - ch->size;
		if (avail >= size) break;
		memcpy(ch->buf + ch->size, buf, avail);
		ch->size += avail;
		ch->next = newChunk();
		if (!ch->next) return; /* Out of memory */
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

static void appendChar(Strap *st, char c) {
	appendStrap(st, &c, 1);
}

static int getTableType(lua_State *L, int idx, int *len) {
	int type, n = 0, res = LUA_TNUMBER;
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		type = lua_type(L, -2); /* Key type */
		if (!n++) res = type;
		if (type == res) {
			if ((type == LUA_TNUMBER) && (lua_tointeger(L, -2) == n)) continue;
			if ((type == LUA_TSTRING) && lua_objlen(L, -2)) continue; /* Empty key can't be represented in AMF3 */
		}
		res = LUA_TNONE;
	}
	*len = n;
	return res;
}

static void encodeValue(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tf);

static void encodeU29(Strap *st, int val) {
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
	appendStrap(st, buf, len);
}

static void encodeDouble(Strap *st, double val) {
	union { int n; char c; } t;
	union { double d; char c[8]; } u;
	char buf[8];
	t.n = 1;
	u.d = val;
	if (!t.c) memcpy(buf, u.c, 8);
	else { /* Little-endian machine */
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
	if (ref != -1) {
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

static void encodeString(Strap *st, lua_State *L, int idx, int ridx) {
	size_t len;
	const char *str = lua_tolstring(L, idx, &len);
	if (len && encodeRef(st, L, idx, ridx)) return; /* Empty string is never sent by reference */
	if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
	encodeU29(st, (len << 1) | 1);
	appendStrap(st, str, len);
}

static void encodeArray(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tf, int len) {
	if (encodeRef(st, L, idx, oidx)) return;
	if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
	encodeU29(st, (len << 1) | 1);
	appendChar(st, 0x01); /* Empty associative part */
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		encodeValue(st, L, -1, sidx, oidx, tf);
	}
}

static void encodeObject(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tf) {
	if (encodeRef(st, L, idx, oidx)) return;
	if (*tf) appendChar(st, 0x01); /* Traits have been encoded earlier */
	else {
		*tf = 1;
		appendChar(st, 0x0b); /* Traits: no static members, externalizable=0, dynamic=1 */
		appendChar(st, 0x01); /* Empty class name */
	}
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		encodeString(st, L, -2, sidx);
		encodeValue(st, L, -1, sidx, oidx, tf);
	}
	appendChar(st, 0x01);
}

static void encodeDictionary(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tf, int len) {
	if (encodeRef(st, L, idx, oidx)) return;
	if (len > AMF3_MAX_INT) len = AMF3_MAX_INT;
	encodeU29(st, (len << 1) | 1);
	appendChar(st, 0x00); /* weak-keys=0 */
	for (lua_pushnil(L); lua_next(L, idx); lua_pop(L, 1)) {
		encodeValue(st, L, -2, sidx, oidx, tf);
		encodeValue(st, L, -1, sidx, oidx, tf);
	}
}

static void encodeValue(Strap *st, lua_State *L, int idx, int sidx, int oidx, int *tf) {
	if (idx < 0) idx = lua_gettop(L) + idx + 1;
	lua_checkstack(L, 5);
	switch (lua_type(L, idx)) {
		default:
		case LUA_TNIL:
			appendChar(st, AMF3_UNDEFINED);
			break;
		case LUA_TBOOLEAN:
			appendChar(st, lua_toboolean(L, idx) ? AMF3_TRUE : AMF3_FALSE);
			break;
		case LUA_TNUMBER: {
			double d = lua_tonumber(L, idx);
			int n = (int)d;
			if (((double)n == d) && (n >= AMF3_MIN_INT) && (n <= AMF3_MAX_INT)) {
				appendChar(st, AMF3_INTEGER);
				encodeU29(st, n);
			} else {
				appendChar(st, AMF3_DOUBLE);
				encodeDouble(st, d);
			}
			break;
		}
		case LUA_TSTRING:
			appendChar(st, AMF3_STRING);
			encodeString(st, L, idx, sidx);
			break;
		case LUA_TTABLE: {
			int len;
			switch (getTableType(L, idx, &len)) {
				case LUA_TNUMBER:
					appendChar(st, AMF3_ARRAY);
					encodeArray(st, L, idx, sidx, oidx, tf, len);
					break;
				case LUA_TSTRING:
					appendChar(st, AMF3_OBJECT);
					encodeObject(st, L, idx, sidx, oidx, tf);
					break;
				default:
					appendChar(st, AMF3_DICTIONARY);
					encodeDictionary(st, L, idx, sidx, oidx, tf, len);
					break;
			}
			break;
		}
	}
}

int amf3_encode(lua_State *L) {
	Strap st;
	int tf = 0;
	luaL_checkany(L, 1);
	lua_settop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	initStrap(&st);
	encodeValue(&st, L, 1, 2, 3, &tf);
	flushStrap(&st, L);
	return 1;
}
