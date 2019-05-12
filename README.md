AMF3 encoding/decoding module for Lua
=====================================

[lua-amf3] provides fast AMF3 encoding/decoding routines for Lua:
- Support for inline data transformation/filtering via metamethods/handlers.
- Additional binary packing/unpacking routines.
- Written in C with 32/64-bit little/big-endian awareness.
- No external dependencies.


### amf3.encode(value, [event])
Returns a binary string containing an AMF3 representation of `value`. Optional `event` may be used
to specify a metamethod name (default is `__toAMF3`) that is called for every processed value. The
value returned by the metamethod is used instead of the original value.

A table (root or nested) is encoded into a dense array if it has a field `__array` whose value is
_true_. The length of the resulting array can be adjusted by storing an integer value in that field.
Otherwise, it is assumed to be equal to the raw length of the table.

### amf3.decode(data, [pos], [handler])
Returns the value encoded in `data` along with the index of the first unread byte. Optional `pos`
marks where to start reading in `data` (default is 1). Optional `handler` is called for each new
table (root or nested), and its return value is used instead of the original table.

When an array is decoded, its length is stored in a field `__array`. When an object is decoded,
fields `__class` (class name) and `__data` (externalizable data) are set depending on its type.

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

All numeric data is stored as big-endian. All integral options check overflows.

### amf3.unpack(fmt, data, [pos])
Returns the values packed in `data` according to the format string `fmt` (see above) along with the
index of the first unread byte. Optional `pos` marks where to start reading in `data` (default is 1).

### amf3.null
A Lua value that represents AMF3 Null.


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

To build for a specific Lua version, set `USE_LUA_VERSION`. For example:

    cmake -D USE_LUA_VERSION=5.1 .

or for LuaJIT:

    cmake -D USE_LUA_VERSION=jit .

To build in a separate directory, replace `.` with a path to the source.


Getting started
---------------

```Lua
local amf3 = require 'amf3'

-- Helpers
local function encode_decode(val, ev, h)
    return amf3.decode(amf3.encode(val, ev), nil, h)
end
local function pack_unpack(fmt, ...)
    return amf3.unpack(fmt, amf3.pack(fmt, ...))
end

-- Primitive types
assert(encode_decode(nil) == nil)
assert(encode_decode(amf3.null) == amf3.null)
assert(encode_decode(false) == false)
assert(encode_decode(true) == true)
assert(encode_decode(123) == 123)
assert(encode_decode(123.456) == 123.456)
assert(encode_decode('abc') == 'abc')

-- Complex types
local data = {
    obj = { -- A table with only string keys translates into an object
        str = 'abc',
        len = 3,
        val = -10.2,
        null = amf3.null,
    },
    dict = { -- A table with mixed keys translates into a dictionary
        [-1] = 2,
        [-1.2] = 3.4,
        abc = 'def',
        [amf3.null] = amf3.null,
        [{a = 1}] = {b = 2}, -- A table can be a key
    },
    arr1 = {__array = true, 1, 2, 3}, -- A table with a field '__array' translates into an array
    arr2 = {__array = 5, nil, 2, nil, 4, nil}, -- Array length can be adjusted to form a sparse array
}
data[data] = data -- All kinds of circular references are safe

local out = encode_decode(data)
assert(out[out].obj.str == 'abc') -- Circular references are properly restored
assert(out.obj.null == amf3.null) -- 'null' as a field value
assert(out.dict[amf3.null] == amf3.null) -- 'null' as a key or value
assert(out.arr1.__array == #out.arr1) -- Array length is restored
assert(out.arr2.__array == 5) -- Access to the number of items in a sparse array

-- Packing/unpacking values using AMF3-compatible numeric formats
local b, i, d, s = pack_unpack('bids', 123, 123456, -1.2, 'abc')

-- Serialization metamethods can be used to produce multiple AMF3 representations of the same object.
-- Deserialization handlers can be used to restore Lua objects from complex AMF3 types on the way back.
-- This is helpful, for example, when objects are exchanged with both trusted and untrusted parties.
-- Various custom filters/wrappers can also be implemented using this API.

local mt = {
    __tostring = function (t) return (t.a or '') .. (t.b or '') end,
    __toA = function (t) return {A = t.a} end, -- [a -> A]
    __toB = function (t) return {B = t.b} end, -- [b -> B]
}

local function new(t) return setmetatable(t, mt) end
local function fromA(t) return new{a = t.A} end -- [A -> a]
local function fromB(t) return new{b = t.B} end -- [B -> b]

local obj = new{a = 'a', b = 'b'}
assert(tostring(obj) == 'ab')
assert(tostring(encode_decode(obj, '__toA', fromA)) == 'a')
assert(tostring(encode_decode(obj, '__toB', fromB)) == 'b')
```


[lua-amf3]: https://github.com/neoxic/lua-amf3
[luarocks.org]: https://luarocks.org
