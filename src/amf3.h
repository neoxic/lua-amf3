/*
** Copyright (C) 2012-2013 Arseny Vakhrushev <arseny.vakhrushev@gmail.com>
** Please read the LICENSE file for license details
*/

#pragma once

#define VERSION "1.0.0"

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
#define AMF3_OBJECT      0x0a // no support
#define AMF3_XML         0x0b
#define AMF3_BYTEARRAY   0x0c

#define AMF3_MAX_INT     268435455 //  (2^28)-1
#define AMF3_MIN_INT    -268435456 // -(2^28)
