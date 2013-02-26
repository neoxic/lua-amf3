include config

PREFIX ?= /usr/local
LIBDIR ?= ${PREFIX}/lib/lua/5.1
LUAINC ?= ${PREFIX}/include
LUALIB ?= lua-5.1
LUABIN ?= lua

LIB  = amf3.so
SRCS = src/amf3.c src/amf3_encode.c src/amf3_decode.c
OBJS = ${SRCS:.c=.o}

CFLAGS  += -O2 -fPIC -ansi -pedantic -Wall -Wextra -Wshadow -Wformat -Wundef -Wwrite-strings -I${LUAINC}
LDFLAGS += -l${LUALIB} -L${PREFIX}/lib

CC ?= cc

.PHONY: all

all: ${LIB}

${LIB}: ${OBJS}
	${CC} -shared -o $@ ${LDFLAGS} ${OBJS}

clean:
	rm -f ${LIB} ${OBJS}

install: all
	install -d ${LIBDIR}
	install ${LIB} ${LIBDIR}

test:
	${LUABIN} test.lua
