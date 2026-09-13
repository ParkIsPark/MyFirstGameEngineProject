local counter = 0
assert(_G == _ENV)
assert(load == nil)
assert(getmetatable(_G) == false)
assert(not pcall(setmetatable, _G, {}))
assert(instanceGlobal == nil and _G.viaG == nil)
assert(math.instanceValue == nil)
assert(string.instanceValue == nil)
-- A shared primitive metatable would expose the VM's original string table.
local primitiveMetatable = getmetatable('text')
if type(primitiveMetatable) == 'table' then
    primitiveMetatable.__index.instanceValue = 'leaked through string metatable'
end
instanceGlobal = 'mine'
_G.viaG = 'mine'
math.instanceValue = 'mine'

function BeginPlay()
    Engine.Log('begin:' .. counter)
end

function Tick(...)
    assert(select('#', ...) == 1)
    local dt = ...
    assert(type(dt) == 'number')
    counter = counter + 1
    Engine.Log('tick:' .. counter .. ':' .. dt)
end

function EndPlay()
    Engine.Log('end:' .. counter)
end
