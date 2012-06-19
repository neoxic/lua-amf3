/*
** Copyright (C) 2012 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#include <stdlib.h>
#include <string.h>
#include <lauxlib.h>
#include "strap.h"


chunk_t *newChunk() {
	chunk_t *chunk = malloc(sizeof(chunk_t));
	if (!chunk) return 0;
	chunk->size = 0;
	chunk->next = 0;
	return chunk;
}

void initStrap(strap_t *strap) {
	memset(strap, 0, sizeof(strap_t));
}

void appendStrap(strap_t *strap, const char *buf, int size) {
	chunk_t *chunk = strap->last;
	if (!chunk) {
		chunk = strap->first = strap->last = newChunk();
		if (!chunk) return; // out of memory
	}
	for ( ;; ) {
		int avail = sizeof(chunk->buf) - chunk->size;
		if (avail >= size) break;
		memcpy(chunk->buf + chunk->size, buf, avail);
		chunk->size += avail;
		chunk->next = newChunk();
		if (!chunk->next) return; // out of memory
		chunk = strap->last = chunk->next;
		buf += avail;
		size -= avail;
	}
	memcpy(chunk->buf + chunk->size, buf, size);
	chunk->size += size;
}

void flushStrap(strap_t *strap, lua_State *L) {
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	chunk_t *chunk = strap->first;
	while (chunk) {
		luaL_addlstring(&b, chunk->buf, chunk->size);
		chunk_t *next = chunk->next;
		free(chunk);
		chunk = next;
	}
	luaL_pushresult(&b);
	initStrap(strap);
}
