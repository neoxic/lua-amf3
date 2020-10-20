/*
** Copyright (C) 2012-2020 Arseny Vakhrushev <arseny.vakhrushev@me.com>
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

#include "amf3.h"

static const luaL_Reg funcs[] = {
	{"encode", amf3__encode},
	{"decode", amf3__decode},
	{"pack", amf3__pack},
	{"unpack", amf3__unpack},
	{0, 0}
};

int luaopen_amf3(lua_State *L) {
#if LUA_VERSION_NUM < 502
	luaL_register(L, "amf3", funcs);
#else
	luaL_newlib(L, funcs);
#endif
	lua_pushliteral(L, MODNAME);
	lua_setfield(L, -2, "_NAME");
	lua_pushliteral(L, VERSION);
	lua_setfield(L, -2, "_VERSION");
	lua_pushlightuserdata(L, 0);
	lua_setfield(L, -2, "null");
	return 1;
}
