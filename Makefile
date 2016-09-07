include config

PREFIX ?= /usr/local
LIBNAME ?= amf3.so
LIBDIR ?= ${PREFIX}/lib/lua/5.1
LUAINC ?= ${PREFIX}/include/lua5.1
LUALIB ?= lua5.1
LUABIN ?= ${LUALIB}

XCFLAGS ?= -O2 -fPIC
XLDFLAGS ?=

CC ?= cc

SRCS = src/amf3.c src/amf3_encode.c src/amf3_decode.c
OBJS = ${SRCS:.c=.o}

CFLAGS += -ansi -pedantic -Wall -Wextra -Wshadow -Wformat -Wundef -Wwrite-strings -Wredundant-decls -Wno-uninitialized -I${LUAINC} ${XCFLAGS}
LDFLAGS += -l${LUALIB} -L${PREFIX}/lib ${XLDFLAGS}

.PHONY: all

all: ${LIBNAME}

${LIBNAME}: ${OBJS}
	${CC} -shared -o $@ ${OBJS} ${LDFLAGS}

clean:
	rm -f ${LIBNAME} ${OBJS}

install: all
	install -d ${LIBDIR}
	install ${LIBNAME} ${LIBDIR}

test: all
	${LUABIN} test.lua
