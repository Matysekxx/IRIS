HeavyObject = {}
HeavyObject.__index = HeavyObject

function HeavyObject.new()
    local self = setmetatable({}, HeavyObject)
    self.a = 0
    self.b = 0
    self.c = 0
    self.d = 0
    return self
end

function HeavyObject:init(v)
    self.a = v
    self.b = v + 1
    self.c = v + 2
    self.d = v + 3
end

function HeavyObject:sum()
    return self.a + self.b + self.c + self.d
end

local total = 0
local start = os.clock()
for i = 0, 999999 do
    local obj = HeavyObject.new()
    obj:init(i)
    total = total + obj:sum()
end
local end_t = os.clock()
print("Total sum: " .. total)
print(string.format("Time: %.2f ms", (end_t - start) * 1000))
