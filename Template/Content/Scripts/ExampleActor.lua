function BeginPlay()
    Engine.Log("ExampleActor BeginPlay")
end

function Tick(deltaSeconds)
    Engine.Log("ExampleActor Tick: " .. tostring(deltaSeconds))
end

function EndPlay()
    Engine.Log("ExampleActor EndPlay")
end
