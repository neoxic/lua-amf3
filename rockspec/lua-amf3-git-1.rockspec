package = 'lua-amf3'
version = 'git-1'
source = {
	url = 'git://github.com/neoxic/lua-amf3.git',
}
description = {
	summary = 'AMF3 encoding/decoding library for Lua',
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
				'src/amf3_encode.c',
				'src/amf3_decode.c',
			},
		},
	},
}
