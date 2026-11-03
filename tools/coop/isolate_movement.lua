-- Isolated probe only: stop enemy FSMs and move P2 onto open ground near P1.
if not SR_COOP_P2 then return "P2 has not been created" end
local p1=GetIDFromBinIndex("SUPERMAN",0)
if GetDASValue("CurMoveMode",p1)~=0 then return "P1 must be on foot for this probe" end
local n=GetBinSize("ENEMY")
if not n or n>128 then return "unexpected enemy count" end
for i=0,n-1 do
    local id=GetIDFromBinIndex("ENEMY",i)
    if id and id~=0 and id~=p1 and id~=SR_COOP_P2 then
        PauseFSM(id,true)
        SetVelocity(0,0,0,id)
    end
end
local x,y,z=GetEntityPosition(p1)
SetEntityPosition(SR_COOP_P2,x-12,y,z+5)
SetVelocity(0,0,0,SR_COOP_P2)
SetCameraHomeEntity(SR_COOP_P2)
SR_COOP_LAST_POS=nil
return "isolated P2="..tostring(SR_COOP_P2).." paused enemies="..tostring(n)
