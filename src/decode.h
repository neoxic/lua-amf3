/*
** Copyright (C) 2012-2013 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#pragma once

#include <lua.h>
#include "strap.h"


int decode(lua_State* L, const char* buf, int pos, int size, int sidx, int oidx, int tidx);
