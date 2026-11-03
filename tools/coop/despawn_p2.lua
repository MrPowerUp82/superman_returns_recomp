-- Development lifecycle probe. Restore the camera before removing its target.
if not SR_COOP_P2 then return "P2 has not been created" end
local p1=GetIDFromBinIndex("SUPERMAN",0)
local p2=SR_COOP_P2
SetCameraHomeEntity(p1)
DespawnEntity(p2)
SR_COOP_P2=nil
SR_COOP_LAST_POS=nil
return "removed P2="..tostring(p2)
