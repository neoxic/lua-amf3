--
-- Copyright (C) 2012-2013 Arseny Vakhrushev <arseny.vakhrushev at gmail dot com>
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


local vals, objs, refs

local function spawn(n)
	n = n or 1
	if n <= 3 and math_random() < 0.7 then
		return objs[math_random(#objs)](n)
	else
		return vals[math_random(#vals)]()
	end
end

vals = {
	function () return nil end, -- nil
	function () return math_random() < 0.5 end, -- boolean
	function () return math_random(-268435456, 268435455) end, -- integer
	function () return (math_random() - 0.5) * 1234567890 end, -- double
	function () -- string
		local t = {}
		for i = 1, math_random(0, 30) do
			t[i] = math_random(0, 255)
		end
		return string_char(unpack(t))
	end,
}

objs = {
	function (n) -- dense array
		local t = {}
		for i = 1, math_random(0, 10) do
			local v = spawn(n + 1)
			if v ~= nil then
				table_insert(t, v)
			end
		end
		table_insert(refs, t)
		return t
	end,
	function (n) -- associative array
		local t = {}
		for i = 1, math_random(0, 10) do
			local k = vals[5]()
			local v = spawn(n + 1)
			if #k > 0 and v ~= nil then
				t[k] = v
			end
		end
		table_insert(refs, t)
		return t
	end,
	function () -- reference
		local n = #refs
		return n > 0 and refs[math_random(n)] or nil
	end
}

refs = {}

local function compare(v1, v2)
	local r = {}
	local function compare(v1, v2)
		if type(v1) ~= 'table' or type(v2) ~= 'table' then
			return v1 == v2
		end
		local t1 = r[v1]
		local t2 = r[v2]
		if t1 or t2 then
			return t1 == v2 and t2 == v1
		end
		r[v1] = v2
		r[v2] = v1
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

local function check(cond, msg)
	if not cond then
		error('check failed!' .. (msg and ' --> ' .. msg or ''), 2)
	end
end


-- Test

local total = 0
local cnt = 0
local max = 0
local stats = {}

stdout 'Testing'
for i = 1, 50 do
	for j = 1, 10 do
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
		stats[size] = (stats[size] or 0)+ 1

		-- additional robustness test
		for pos = 0, size - 1 do
			pcall(amf3_decode, str, pos)
		end
	end
	stdout '.'
end
stdout '\n'

printf('Processed %d bytes in %d chunks', total, cnt)
printf('Max chunk size %d bytes', max)
print 'Size distribution:'
print '% of max size\t% of chunks'
for i = 1, 10 do
	local a = (i - 1) / 10 * max
	local b = i / 10 * max
	local c = 0
	for size, cnt in pairs(stats) do
		if size > a and size <= b then
			c = c + cnt
		end
	end
	printf('%2d...%d \t%5.1f', (i - 1) * 10, i * 10, c / cnt * 100)
end
