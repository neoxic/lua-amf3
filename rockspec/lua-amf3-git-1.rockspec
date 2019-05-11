package = 'lua-amf3'
version = 'git-1'
source = {
	url = 'git://github.com/neoxic/lua-amf3.git',
}
description = {
	summary = 'AMF3 encoding/decoding library for Lua',
	detailed = [[
		lua-amf3 provides fast AMF3 encoding/decoding routines for Lua:
		- Support for inline data transformation/filtering via metamethods/handlers.
		- Additional binary packing/unpacking routines.
		- Properly protected against memory allocation errors.
		- No external dependencies.
		- Written in C.
	]],
	license = 'MIT',
	homepage = 'https://github.com/neoxic/lua-amf3',
	maintainer = 'Arseny Vakhrushev <arseny.vakhrushev@gmail.com>',
}
dependencies = {
	'lua >= 5.1',
}
build = {
	type = 'builtin',
	modules = {
		amf3 = {
			sources = {
				'src/amf3.c',
				'src/amf3-encode.c',
				'src/amf3-decode.c',
			},
		},
	},
}
