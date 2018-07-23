AMF3 encoding/decoding library for Lua
======================================

[lua-amf3] provides the following API:

### amf3.encode(value, [event])
- Returns a binary string containing an AMF3 representation of `value`.
- Optional `event` may be used to specify a metamethod name (default is `__toAMF3`) to be called for
  every processed value. The value returned by the metamethod is used instead of the original value.
- A table (root or nested) is encoded into a dense array if it has an `__array` field whose value is
  neither `nil` nor `false`. The length of the resulting array can be adjusted by storing an integer
  value in the `__array` field. Otherwise, it is assumed to be equal to the raw length of the table.

### amf3.decode(data, [pos], [handler])
- Returns the value encoded in `data` along with the index of the first unread byte.
- Optional `pos` marks where to start reading in `data` (default is 1).
- Optional `handler` is called for each new table (root or nested), and its return value is used
  instead of the original table.
- The `AMF3 Object` type is converted into a table with optional `__class` (class name) and `__data`
  (externalizable object's data) fields.

### amf3.pack(fmt, ...)

### amf3.unpack(fmt, data, [pos])

### amf3.null
A Lua value that represents the `AMF3 Null` type.


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
