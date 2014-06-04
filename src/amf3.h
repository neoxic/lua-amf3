/*
** Copyright (C) 2012-2014 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
** Please read the LICENSE file for license details
*/

#ifndef amf3_h
#define amf3_h

#include <lua.h>


#define VERSION "1.0.2"

#define AMF3_UNDEFINED   0x00
#define AMF3_NULL        0x01
#define AMF3_FALSE       0x02
#define AMF3_TRUE        0x03
#define AMF3_INTEGER     0x04
#define AMF3_DOUBLE      0x05
#define AMF3_STRING      0x06
#define AMF3_XMLDOC      0x07
#define AMF3_DATE        0x08
#define AMF3_ARRAY       0x09
#define AMF3_OBJECT      0x0a
#define AMF3_XML         0x0b
#define AMF3_BYTEARRAY   0x0c

#define AMF3_MAX_INT     268435455 /*  (2^28)-1 */
#define AMF3_MIN_INT    -268435456 /* -(2^28)   */


int amf3_encode(lua_State *L);
int amf3_decode(lua_State *L);


#endif
