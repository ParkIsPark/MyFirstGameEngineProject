-- Deterministic Task 8 lifecycle failure fixture.
function BeginPlay()
    Engine.Log("END_ERROR_BEGIN")
end

function Tick(_deltaSeconds)
    Engine.Log("END_ERROR_TICK")
end

function EndPlay()
    error("TASK8_END_SENTINEL")
end
