-- sr-coop-context: player1
-- Experimental actor lifecycle probe. Run only against the isolated probe save.
-- The mailbox waits until the VM is processing P1's own script context.
if SR_COOP_P2 then return "P2 already spawned: " .. tostring(SR_COOP_P2) end
if not IsWorldFlowRunning("SupermenActorWorldFlow") then return "world not ready" end
local p1 = GetIDFromBinIndex("SUPERMAN", 0)
if not p1 or p1 == 0 or p1 == 255 then return "P1 not ready" end
if GetDASValue("currentControl", p1) ~= 2 then return "P1 not in gameplay control" end
local x, y, z = GetEntityPosition(p1)
local p2 = SpawnEntityActive("SupermanEnt", x + 3, y, z)
if not p2 or p2 == 0 or p2 == 255 or p2 == p1 then
    return "spawn did not return a distinct actor: " .. tostring(p2)
end
SR_COOP_P2 = p2 -- retain before any following call, so retry never spawns another
SetDASValue("currentControl", 2, p2)
local bound = SetActorToDefaultController(p2, 1) -- mailbox-only zero-based port extension
if not bound then return "P2 spawned but port 1 binding refused: " .. tostring(p2) end
return "P1=" .. tostring(p1) .. " P2=" .. tostring(p2)
