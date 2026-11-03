-- Re-submit with probe.ps1 -Action Script after loading a save.
if not IsWorldFlowRunning("SupermenActorWorldFlow") then
    return "waiting for player world flow"
end
local p1 = GetIDFromBinIndex("SUPERMAN", 0)
if not p1 or p1 == 0 or p1 == 255 then return "waiting for P1" end
local x, y, z = GetEntityPosition(p1)
return "P1=" .. tostring(p1) .. " pos=" .. tostring(x) .. "," ..
    tostring(y) .. "," .. tostring(z) .. " control=" .. tostring(GetDASValue("currentControl", p1))
