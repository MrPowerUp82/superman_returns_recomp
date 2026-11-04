-- Read-only explicit actor stamina probe; run after spawn_p2.lua.
if not SR_COOP_P2 then return "P2 has not been created" end
local p1 = GetIDFromBinIndex("SUPERMAN", 0)
local p2 = SR_COOP_P2
return "P1=" .. tostring(p1) .. " current=" .. tostring(GetPowerMagnitude("StaminaPoints", p1)) ..
    " maximum=" .. tostring(GetMaxPowerMagnitude("StaminaPoints", p1)) ..
    " P2=" .. tostring(p2) .. " current=" .. tostring(GetPowerMagnitude("StaminaPoints", p2)) ..
    " maximum=" .. tostring(GetMaxPowerMagnitude("StaminaPoints", p2))
