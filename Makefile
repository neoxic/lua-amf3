include config

PREFIX ?= /usr/local
LIBDIR ?= ${PREFIX}/lib/lua/5.1
LUAINC ?= ${PREFIX}/include
LUALIB ?= lua
LUABIN ?= ${LUALIB}

LIB  = amf3.so
SRCS = src/amf3.c src/amf3_encode.c src/amf3_decode.c
OBJS = ${SRCS:.c=.o}

CFLAGS  += -O2 -fPIC -ansi -pedantic -Wall -Wextra -Wshadow -Wformat -Wundef -Wwrite-strings -Wredundant-decls -Wno-uninitialized -I${LUAINC} ${MYCFLAGS}
LDFLAGS += -l${LUALIB} -L${PREFIX}/lib ${MYLDFLAGS}

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

test: all
	${LUABIN} test.lua
