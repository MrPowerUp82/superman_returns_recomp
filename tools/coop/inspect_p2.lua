-- Read explicit actor IDs; this works from a base-world or actor script context.
if not SR_COOP_P2 then return "P2 has not been created" end
local p1 = GetIDFromBinIndex("SUPERMAN", 0)
local x1, y1, z1 = GetEntityPosition(p1)
local x2, y2, z2 = GetEntityPosition(SR_COOP_P2)
local result = "P1=" .. tostring(p1) .. " pos=" .. tostring(x1) .. "," ..
    tostring(y1) .. "," .. tostring(z1) .. " P2=" .. tostring(SR_COOP_P2) ..
    " pos=" .. tostring(x2) .. "," .. tostring(y2) .. "," .. tostring(z2)
if SR_COOP_LAST_POS then
    local a = SR_COOP_LAST_POS
    local d1 = math.sqrt((x1-a[1])^2 + (y1-a[2])^2 + (z1-a[3])^2)
    local d2 = math.sqrt((x2-a[4])^2 + (y2-a[5])^2 + (z2-a[6])^2)
    result = result .. " displacement P1=" .. tostring(d1) .. " P2=" .. tostring(d2)
end
SR_COOP_LAST_POS = {x1,y1,z1,x2,y2,z2}
return result
