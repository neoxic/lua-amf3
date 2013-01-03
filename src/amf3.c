/*
** Copyright (C) 2012 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#include <lauxlib.h>
#include "amf3.h"
#include "strap.h"
#include "encode.h"
#include "decode.h"


int amf3_encode(lua_State *L) {
	luaL_checkany(L, 1);
	lua_settop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	strap_t strap;
	initStrap(&strap);
	encode(&strap, L, 1, 2, 3);
	flushStrap(&strap, L);
	return 1;
}

int amf3_decode(lua_State *L) {
	size_t size;
	const char* buf = luaL_checklstring(L, 1, &size);
	int pos = luaL_optint(L, 2, 0);
	luaL_argcheck(L, pos >= 0, 2, "position may not be negative");
	lua_settop(L, 1);
	lua_newtable(L);
	lua_newtable(L);
	lua_newtable(L);
	lua_pushinteger(L, decode(L, buf, pos, size, 2, 3, 4));
	return 2;
}

const struct luaL_Reg amf3_lib[] = {
	{ "encode", amf3_encode },
	{ "decode", amf3_decode },
	{ 0, 0 }
};

int luaopen_amf3(lua_State* L) {
	luaL_register(L, "amf3", amf3_lib);
	lua_pushliteral(L, "_COPYRIGHT");
	lua_pushliteral(L, "Copyright (C) 2012 Arseny Vakhrushev");
	lua_settable(L, -3);
	lua_pushliteral(L, "_DESCRIPTION");
	lua_pushliteral(L, "AMF3 encoding/decoding library for Lua");
	lua_settable(L, -3);
	lua_pushliteral(L, "_VERSION");
	lua_pushliteral(L, "amf3 " VERSION);
	lua_settable(L, -3);
	return 1;
}
