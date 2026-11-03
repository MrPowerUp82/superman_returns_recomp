if not SR_COOP_P2 then return "P2 has not been created" end
local p1 = GetIDFromBinIndex("SUPERMAN", 0)
local same = SetActorToDefaultController(SR_COOP_P2, 1)
local occupied0 = SetActorToDefaultController(SR_COOP_P2, 0)
local occupied1 = SetActorToDefaultController(p1, 1)
local invalid = SetActorToDefaultController(SR_COOP_P2, 4)
local fraction = SetActorToDefaultController(SR_COOP_P2, 1.5)
return "same=" .. tostring(same) .. " occupied0=" .. tostring(occupied0) ..
    " occupied1=" .. tostring(occupied1) .. " invalid=" .. tostring(invalid) ..
    " fraction=" .. tostring(fraction)
