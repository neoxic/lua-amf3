/*
** Copyright (C) 2012 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#pragma once

#include <lua.h>


typedef struct chunk_s chunk_t;
typedef struct strap_s strap_t;


struct chunk_s {
	char    buf[1000]; // some suitable chunk size to store both small and big buffers well enough and to fit 1Kb memory block
	int     size;
	chunk_t *next;
};

struct strap_s {
	chunk_t *first;
	chunk_t *last;
};


void initStrap(strap_t *strap);
void appendStrap(strap_t *strap, const char *buf, int size);
void flushStrap(strap_t *strap, lua_State *L);
