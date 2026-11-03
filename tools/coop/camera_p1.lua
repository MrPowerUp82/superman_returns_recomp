-- Switch the existing single camera; this does not enable split-screen.
local id=GetIDFromBinIndex("SUPERMAN",0)
SetCameraHomeEntity(id)
return "camera target P1="..tostring(id)
