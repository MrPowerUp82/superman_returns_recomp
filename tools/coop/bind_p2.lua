-- Retry binding an already created P2 without spawning another actor.
if not SR_COOP_P2 then return "P2 has not been created" end
return "P2="..tostring(SR_COOP_P2).." bound="..
    tostring(SetActorToDefaultController(SR_COOP_P2,1))
