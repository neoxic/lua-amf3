/*
** Copyright (C) 2012-2016 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#include "amf3.h"
#include <lauxlib.h>

static const struct luaL_Reg amf3_lib[] = {
	{ "encode", amf3_encode },
	{ "decode", amf3_decode },
	{ 0, 0 }
};

int luaopen_amf3(lua_State *L) {
	luaL_register(L, "amf3", amf3_lib);
	lua_pushliteral(L, "_COPYRIGHT");
	lua_pushliteral(L, "Copyright (C) 2012-2016 Arseny Vakhrushev");
	lua_settable(L, -3);
	lua_pushliteral(L, "_DESCRIPTION");
	lua_pushliteral(L, "AMF3 encoding/decoding library for Lua 5.1");
	lua_settable(L, -3);
	lua_pushliteral(L, "_VERSION");
	lua_pushliteral(L, "amf3 " VERSION);
	lua_settable(L, -3);
	return 1;
}
