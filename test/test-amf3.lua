local amf3 = require 'amf3'

local function copy(t)
	local r = {}
	for k, v in pairs(t) do
		r[k] = v
	end
	return r
end

local mt = {__toAMF3 = function (t) return copy(t) end}
local vals = {
	function () return nil end, -- Undefined
	function () return amf3.null end, -- Null
	function () return math.random() < 0.5 end, -- Boolean
	function () return math.random(-2147483648, 2147483647) end, -- Integer
	function () return (math.random() - 0.5) * 1234567890 end, -- Double
	function () -- String
		local t = {}
		for i = 1, math.random(0, 10) do
			t[i] = string.char(math.random(0, 255))
		end
		return table.concat(t)
	end,
}
local refs, any
local objs = {
	function () -- Reference
		local n = #refs
		return n > 0 and refs[math.random(n)] or nil
	end,
	function (d) -- Array
		local n = math.random(0, 10)
		local t = setmetatable({__array = n}, mt)
		for i = 1, n do
			t[i] = any(d + 1)
		end
		table.insert(refs, t)
		return t
	end,
	function (d) -- Object
		local t = setmetatable({}, mt)
		for i = 1, math.random(0, 10) do
			local k = vals[6]() -- Random string key
			if #k > 0 then
				t[k] = any(d + 1)
			end
		end
		table.insert(refs, t)
		return t
	end,
	function (d) -- Dictionary
		local t = setmetatable({}, mt)
		for i = 1, math.random(0, 10) do
			local k = any(d + 1)
			if k ~= nil then
				t[k] = any(d + 1)
			end
		end
		table.insert(refs, t)
		return t
	end,
}

function any(d)
	if d < 4 and math.random() < 0.7 then
		return objs[math.random(#objs)](d)
	end
	return vals[math.random(#vals)]()
end

local function spawn()
	refs = {}
	return any(0)
end

local function compare(v1, v2)
	local r = {}
	local function compare(v1, v2)
		if type(v1) ~= 'table' or type(v2) ~= 'table' then
			return v1 == v2
		end
		if v1 == v2  then
			return true
		end
		if not compare(getmetatable(v1), getmetatable(v2)) then
			return false
		end
		if r[v1] and r[v2] then
			return true
		end
		r[v1] = true
		r[v2] = true
		local function find(t, xk, xv)
			if t[xk] == xv then
				return true
			end
			for k, v in pairs(t) do
				if compare(k, xk) and compare(v, xv) then
					return true
				end
			end
		end
		for k, v in pairs(v1) do
			if not find(v2, k, v) then
				return false
			end
		end
		for k, v in pairs(v2) do
			if not find(v1, k, v) then
				return false
			end
		end
		r[v1] = nil
		r[v2] = nil
		return true
	end
	return compare(v1, v2)
end

local function handler(t)
	return setmetatable(t, mt)
end

math.randomseed(os.time())

-----------------
-- Stress test --
-----------------

for i = 1, 1000 do
	local obj = spawn()
	local str = amf3.encode(obj)
	local obj_, pos = amf3.decode(str, nil, handler)
	assert(compare(obj, obj_))
	assert(pos == #str + 1)

	-- Extra robustness test
	for pos = 2, pos do
		pcall(amf3.decode, str, pos)
	end
end

---------------------
-- Compliance test --
---------------------

local strs = {
	-- Date, XML, XMLDoc, ByteArray
	string.char(
		0x09, 0x11, 0x01, -- Array (length 8)
			0x08, 0x01, 0x3f, 0xb9, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a, -- Date (0.1)
			0x0b, 0x07, 0x41, 0x42, 0x43, -- XML ('ABC')
			0x07, 0x07, 0x44, 0x45, 0x46, -- XMLDoc ('DEF')
			0x0c, 0x07, 0x11, 0x22, 0x33, -- ByteArray (0x11 0x22 0x33)
			0x08, 0x02, -- Date (reference 1)
			0x0b, 0x04, -- XML (reference 2)
			0x0b, 0x06, -- XMLDoc (reference 3)
			0x0c, 0x08 -- ByteArray (reference 4)
	),
	-- Array
	string.char(
		0x09, 0x05, 0x01, -- Array (length 2)
			0x09, 0x07, -- Array (length 3)
				0x03, 0x41, 0x04, 0x00, 0x03, 0x42, 0x04, 0x01, 0x00, 0x04, 0x02, -- Associative part: A:0, B:1, A:2 (should reset the key)
				0x01, -- End of associative part
				0x02, 0x03, 0x04, 0x00, -- Dense part: [false, true, 0]
			0x09, 0x02 -- Array (reference 1)
	),
	-- Object
	string.char(
		0x09, 0x11, 0x01, -- Array (length 8)
			0x0a, 0x3b, 0x07, 0x41, 0x42, 0x43, 0x03, 0x41, 0x03, 0x42, 0x03, 0x43, -- Dynamic class ABC with static members A, B, C
				0x04, 0x01, 0x04, 0x02, 0x04, 0x03, -- Static member values: A:1, B:2, C:3
				0x03, 0x44, 0x04, 0x04, 0x03, 0x45, 0x04, 0x05, -- Dynamic members: D:4, E:5
				0x01, -- End of dymanic part
			0x0a, 0x01, -- Object (class reference 0)
				0x01, 0x02, 0x03, -- Static member values: A:null, B:false, C:true
				0x03, 0x46, 0x02, 0x03, 0x46, 0x03, -- Dynamic members: F:false, F:true (should reset the key)
				0x01, -- End of dymanic part
			0x0a, 0x07, 0x07, 0x44, 0x45, 0x46, -- Externalizable class DEF
				0x02, -- __data:false
			0x0a, 0x05, -- Object (class reference 1)
				0x03, -- __data:true
			0x0a, 0x02, -- Object (reference 1)
			0x0a, 0x04, -- Object (reference 2)
			0x0a, 0x06, -- Object (reference 3)
			0x0a, 0x08 -- Object (reference 4)
	),
	-- Vector
	string.char(
		0x09, 0x11, 0x01, -- Array (length 8)
			0x0d, 0x05, 0x00, 0x00, 0x01, 0x02, 0x03, 0xff, 0xff, 0xff, 0xff, -- Vector of ints [66051, -1]
			0x0e, 0x05, 0x00, 0x00, 0x01, 0x02, 0x03, 0xff, 0xff, 0xff, 0xff, -- Vector of uints [66051, 4294967295]
			0x0f, 0x05, 0x00, 0x3f, 0xb9, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a, 0x3f, 0xc9, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a, -- Vector of doubles [0.1, 0.2]
			0x10, 0x07, 0x01, 0x03, 0x2a, 0x02, 0x03, 0x04, 0x00, -- Vector of objects (type '*') [false, true, 0]
			0x0d, 0x02, -- Vector of ints (reference 1)
			0x0e, 0x04, -- Vector of uints (reference 2)
			0x0f, 0x06, -- Vector of doubles (reference 3)
			0x10, 0x08 -- Vector of objects (reference 4)
	),
	-- Dictionary
	string.char(
		0x09, 0x05, 0x01, -- Array (length 2)
			0x11, 0x09, 0x00, -- Dictionary (length 4)
				0x0a, 0x0b, 0x01, 0x01, 0x02, -- {} => false
				0x02, 0x0a, 0x04, -- false => {}
				0x0a, 0x04, 0x03, -- {} => true (should reset the key)
				0x00, 0x03, -- undefined => true (should be skipped)
			0x11, 0x02 -- Dictionary (reference 1)
	),
}

local ba = string.char(0x11, 0x22, 0x33)
local ma = {A = 2, B = 1, __array = 3, [1] = false, [2] = true, [3] = 0}
local o1 = {A = 1, B = 2, C = 3, D = 4, E = 5, __class = 'ABC'}
local o2 = {A = amf3.null, B = false, C = true, F = true, __class = 'ABC'}
local o3 = {__data = false, __class = 'DEF'}
local o4 = {__data = true, __class = 'DEF'}
local vi = {66051, -1}
local vu = {66051, 4294967295}
local vd = {0.1, 0.2}
local vo = {false, true, 0}
local di = {[{}] = true, [false] = {}}
local objs = {
	{__array = 8, 0.1, 'ABC', 'DEF', ba, 0.1, 'ABC', 'DEF', ba},
	{__array = 2, ma, ma},
	{__array = 8, o1, o2, o3, o4, o1, o2, o3, o4},
	{__array = 8, vi, vu, vd, vo, vi, vu, vd, vo},
	{__array = 2, di, di},
}

for i = 1, #strs do
	local str, obj = strs[i], objs[i]
	local obj_, pos = amf3.decode(str)
	assert(compare(obj, obj_))
	assert(pos == #str + 1)
end

-- Errors
assert(not pcall(amf3.encode, setmetatable({}, {}))) -- Table with metatable
assert(not pcall(amf3.encode, setmetatable({}, {__toAMF3 = function (t) return {t = t} end}))) -- Recursion
assert(not pcall(amf3.encode, setmetatable({}, {__toAMF3 = function (t) t() end}))) -- Run-time error
assert(not pcall(amf3.encode, {a = print})) -- Invalid value
assert(not pcall(amf3.encode, {[print] = 1})) -- Invalid key

----------------------
-- Pack/unpack test --
----------------------

local fmt = 'biiuIIUfdsSsS'
local args = {255, -268435456, 268435455, 536870911, -2147483648, 2147483647, 4294967295, -123, -10.2, '', '', 'abc', '1234567890'}
local unpack = table.unpack or unpack
local str = amf3.pack(fmt, unpack(args))
table.insert(args, #str + 1)
assert(compare(args, {amf3.unpack(fmt, str)}))

-- Stack growth test
local s1 = "b"
local s2 = amf3.pack(s1, 0)
assert(#{amf3.unpack(s1:rep(1000), s2:rep(1000))} == 1001)
if _VERSION ~= 'Lua 5.1' then -- This test fails on Lua 5.1 built with LUA_USE_APICHECK
	assert(not pcall(amf3.unpack, s1:rep(1000000), s2:rep(1000000))) -- Too many packed values
end

-- Range checks
assert(not pcall(amf3.pack, 'b', -1))
assert(not pcall(amf3.pack, 'b', 256))
assert(not pcall(amf3.pack, 'i', -268435457))
assert(not pcall(amf3.pack, 'i', 268435456))
assert(not pcall(amf3.pack, 'u', -1))
assert(not pcall(amf3.pack, 'u', 536870912))
assert(not pcall(amf3.pack, 'I', -2147483649))
assert(not pcall(amf3.pack, 'I', 2147483648))
assert(not pcall(amf3.pack, 'U', -1))
assert(not pcall(amf3.pack, 'U', 4294967296))
