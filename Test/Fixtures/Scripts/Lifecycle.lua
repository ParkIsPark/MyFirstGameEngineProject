local count = 0
function BeginPlay() Engine.Log("begin:" .. count) end
function Tick(dt)
    count = count + 1
    Engine.Log("tick:" .. count .. ":" .. dt)
end
function EndPlay() Engine.Log("end:" .. count) end
