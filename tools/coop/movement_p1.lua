-- sr-coop-context: player1
local id = GetCurrentID()
local x,y,z = GetEntityPosition(id)
return "movement P1="..tostring(id).." pos="..tostring(x)..","..tostring(y)..","..tostring(z)..
    " control="..tostring(GetDASValue("currentControl",id))..
    " mode="..tostring(GetDASValue("CurMoveMode",id))..
    " move="..tostring(GetInputEventValue("INPUT_EVENT_GAME_LOCO_MOVE",false))..
    " strafe="..tostring(GetInputEventValue("INPUT_EVENT_GAME_LOCO_STRAFE",false))..
    " speed="..tostring(GetVelocity(id)).." desired="..tostring(GetDesiredVelocity(id))
