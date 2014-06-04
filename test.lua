--
-- Copyright (C) 2012-2014 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
-- Please read the LICENSE file for license details
--

math.randomseed(os.time())

local amf3 = require 'amf3'

local amf3_encode = amf3.encode
local amf3_decode = amf3.decode

local error = error
local pairs = pairs
local print = print
local type = type
local unpack = unpack
local io_flush = io.flush
local io_write = io.write
local math_random = math.random
local string_char = string.char
local table_insert = table.insert


local vals = {
	function () return nil end, -- Undefined
	function () return math_random() < 0.5 end, -- Boolean
	function () return math_random(-268435456, 268435455) end, -- Integer
	function () return (math_random() - 0.5) * 1234567890 end, -- Double
	function () -- String
		local t = {}
		for i = 1, math_random(0, 30) do
			t[i] = math_random(0, 10)
		end
		return string_char(unpack(t))
	end,
}
local refs, any
local objs = {
	function () -- Reference
		local n = #refs
		return n > 0 and refs[math_random(n)] or nil
	end,
	function (d) -- Dense array
		local t = {}
		for i = 1, math_random(0, 10) do
			local v = any(d + 1)
			if v ~= nil then
				table_insert(t, v)
			end
		end
		table_insert(refs, t)
		return t
	end,
	function (d) -- Associative array
		local t = {}
		for i = 1, math_random(0, 10) do
			local k = vals[5]() -- Random string key
			local v = any(d + 1)
			if #k > 0 and v ~= nil then
				t[k] = v
			end
		end
		table_insert(refs, t)
		return t
	end,
}
any = function (d)
	if d < 4 and math_random() < 0.7 then
		return objs[math_random(#objs)](d)
	end
	return vals[math_random(#vals)]()
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
		if r[v1] and r[v2] then
			return true
		end
		r[v1] = true
		r[v2] = true
		for k, v in pairs(v1) do
			if not compare(v, v2[k]) then
				return false
			end
		end
		for k, v in pairs(v2) do
			if not compare(v, v1[k]) then
				return false
			end
		end
		return true
	end
	return compare(v1, v2)
end

local function stdout(...)
	io_write(...)
	io_flush()
end

local function printf(fmt, ...)
	print(fmt:format(...))
end

local function check(cond)
	if not cond then
		stdout '\n'
		error('check failed!', 2)
	end
end


-- Stress test

local total = 0
local cnt = 0
local max = 0
local sizes = {}

for i = 1, 50 do
	for j = 1, 20 do
		local obj = spawn()
		local str = amf3_encode(obj)
		local size = #str
		local _obj, _size = amf3_decode(str)
		check(size == _size)
		check(compare(obj, _obj))
		total = total + size
		cnt = cnt + 1
		if max < size then
			max = size
		end
		table_insert(sizes, size)

		-- Additional decoder's robustness test
		for pos = 1, size - 1 do
			pcall(amf3_decode, str, pos)
		end
	end
	stdout '.'
end
stdout '\n'

printf('Processed %d bytes in %d chunks', total, cnt)
printf('Max chunk size: %d bytes', max)
print 'Size distribution:'
print '% of max size\t% of chunks'
for i = 1, 10 do
	local a = (i - 1) / 10 * max
	local b = i / 10 * max
	local c = 0
	for _, size in ipairs(sizes) do
		if size > a and size <= b then
			c = c + 1
		end
	end
	printf('%2d...%d \t%5.1f', (i - 1) * 10, i * 10, c / cnt * 100)
end


-- Compliance test

local strs = {
	-- Date, XML, XMLDoc, ByteArray
	string_char(
		0x09, 0x11, 0x01, -- Array (length 8)
			0x08, 0x01, 0x3f, 0xb9, 0x99, 0x99, 0x99, 0x99, 0x99, 0x9a, -- Date (0.1)
			0x0b, 0x07, 0x41, 0x42, 0x43, -- XML ("ABC")
			0x07, 0x07, 0x44, 0x45, 0x46, -- XMLDoc ("DEF")
			0x0c, 0x07, 0x11, 0x22, 0x33, -- ByteArray (11 22 33)
			0x08, 0x02, -- Date (reference 1)
			0x0b, 0x04, -- XML (reference 2)
			0x0b, 0x06, -- XMLDoc (reference 3)
			0x0c, 0x08 -- ByteArray (reference 4)
	),
	-- Mixed array
	string_char(
		0x09, 0x07, -- Array (length 3)
			0x03, 0x41, 0x04, 0x01, 0x03, 0x42, 0x04, 0x02, -- Associative part: A:1, B:2
			0x01, -- End of associative part
			0x06, 0x01, 0x03, 0x02 -- Dense part: ["", true, false]
	),
	-- Object
	string_char(
		0x09, 0x09, 0x01, -- Array (length 4)
			0x0a, 0x3b, 0x07, 0x41, 0x42, 0x43, 0x03, 0x41, 0x03, 0x42, 0x03, 0x43, -- Dynamic class ABC with static members A, B, C
				0x04, 0x01, 0x04, 0x02, 0x04, 0x03, -- Static member values: A:1, B:2, C:3
				0x03, 0x44, 0x04, 0x04, 0x03, 0x45, 0x04, 0x05, -- Dynamic members: D:4, E:5
				0x01, -- End of dymanic part
			0x0a, 0x01, -- Object (class reference 0)
				0x01, 0x02, 0x03, -- Static member values: A:null, B:false, C:true
				0x03, 0x46, 0x02, 0x03, 0x46, 0x03, -- Dynamic members: F:false, F:true (same name!!!)
				0x01, -- End of dymanic part
			0x0a, 0x07, 0x07, 0x44, 0x45, 0x46, -- Externalizable class DEF
				0x02, -- _data:false
			0x0a, 0x05, -- Object (class reference 1)
				0x03 -- _data:true
	),
}

local ba = string_char(0x11, 0x22, 0x33)
local objs = {
	{ 0.1, "ABC", "DEF", ba, 0.1, "ABC", "DEF", ba },
	{ A = 1, B = 2, [1] = '', [2] = true, [3] = false },
	{
		{ A = 1, B = 2, C = 3, D = 4, E = 5, _class = 'ABC' },
		{ A = nil, B = false, C = true, F = true, _class = 'ABC' },
		{ _data = false, _class = 'DEF' },
		{ _data = true, _class = 'DEF' },
	}
}

for i = 1, #strs do
	local str, obj = strs[i], objs[i]
	local size = #str
	local _obj, _size = amf3_decode(str)
	check(size == _size)
	check(compare(obj, _obj))
end
