-- Switch the existing single camera; this does not enable split-screen.
if not SR_COOP_P2 then return "P2 has not been created" end
SetCameraHomeEntity(SR_COOP_P2)
return "camera target P2="..tostring(SR_COOP_P2)
