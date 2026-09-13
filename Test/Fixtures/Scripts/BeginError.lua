-- Deterministic Task 8 lifecycle failure fixture.
function BeginPlay()
    error("TASK8_BEGIN_SENTINEL")
end

function Tick(_deltaSeconds)
    Engine.Log("FORBIDDEN_BEGIN_TICK")
end

function EndPlay()
    Engine.Log("FORBIDDEN_BEGIN_END")
end
