/*
** Copyright (C) 2012 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#pragma once

#include <lua.h>
#include "strap.h"


void encode(strap_t *strap, lua_State *L, int idx, int sidx, int oidx);
