AMF3 encoding/decoding library for Lua
======================================

[lua-amf3] provides the following API:

### amf3.encode(value, [event])
Returns a binary string containing an AMF3 representation of `value`. Optional `event` may be used
to specify a metamethod name (default is `__toAMF3`) to be called for every processed value. The
value returned by the metamethod is used instead of the original value.

A table (root or nested) is encoded into a dense array if it has an `__array` field whose value is
neither `nil` nor `false`. The length of the resulting array can be adjusted by storing an integer
value in the `__array` field. Otherwise, it is assumed to be equal to the raw length of the table.

### amf3.decode(data, [pos], [handler])
Returns the value encoded in `data` along with the index of the first unread byte. Optional `pos`
marks where to start reading in `data` (default is 1). Optional `handler` is called for each new
table (root or nested), and its return value is used instead of the original table.

The AMF3 Object type is converted into a table with optional `__class` (class name) and `__data`
(externalizable object's data) fields.

### amf3.pack(fmt, ...)
Returns a binary string containing the values `...` packed according to the format string `fmt`.
A format string is a sequence of the following options:
- `b`: an unsigned byte;
- `i`: a signed integer packed as a U29 value;
- `I`: a signed integer packed as a U32 value;
- `u`: an unsigned integer packed as a U29 value;
- `U`: an unsigned integer packed as a U32 value;
- `d`: a number packed as an 8 byte IEEE-754 double precision value;
- `s`: a string preceded by its length packed as a U29 value;
- `S`: a string preceded by its length packed as a U32 value;

| Format                | Length | Signed range    | Unsigned range |
|-----------------------|--------|-----------------|----------------|
| U29 (variable length) | 1 .. 4 | -2^28 .. 2^28-1 | 0 .. 2^29-1    |
| U32 (fixed length)    | 4      | -2^31 .. 2^31-1 | 0 .. 2^32-1    |

All numeric data is stored as big-endian.

### amf3.unpack(fmt, data, [pos])
Returns the values packed in `data` according to the format string `fmt` (see above) along with the
index of the first unread byte. Optional `pos` marks where to start reading in `data` (default is 1).

### amf3.null
A Lua value that represents the AMF3 Null type.


Code example
------------

```Lua
local amf3 = require 'amf3'
-- TODO
```


Building and installing with LuaRocks
-------------------------------------

To build and install, run:

    luarocks make

To install the latest release using [luarocks.org], run:

    luarocks install lua-amf3


Building and installing with CMake
----------------------------------

To build and install, run:

    cmake .
    make
    make install

To build against a specific Lua version, set the `USE_LUA_VERSION` variable. For example:

    cmake -D USE_LUA_VERSION=5.1 .

or for LuaJIT:

    cmake -D USE_LUA_VERSION=jit .

To build in a separate directory, replace `.` with a path to the source.


[lua-amf3]: https://github.com/neoxic/lua-amf3
[luarocks.org]: https://luarocks.org
