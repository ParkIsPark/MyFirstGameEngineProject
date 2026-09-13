-- Deterministic Task 8 lifecycle failure fixture.
function BeginPlay()
    Engine.Log("TICK_ERROR_BEGIN")
end

function Tick(_deltaSeconds)
    error("TASK8_TICK_SENTINEL")
end

function EndPlay()
    Engine.Log("TICK_ERROR_END")
end
