--[[
<?xml version='1.0' encoding='utf8'?>
<event name="Prowling baron">
  <trigger>enter</trigger>
  <chance>100</chance>
  <cond>player.misnActive("Baron") == false and player.misnActive("Prince") == false and system.cur() == system.get("Ingot")</cond>
  <flags>
  </flags>
  <notes>
   <campaign>Baron Sauterfeldt</campaign>
  </notes>
 </event>
 --]]
--[[
-- Prowl Event for the Baron mission string. Only used when NOT doing any Baron missions.
--]]

local pnt = planet.get("Ulios")
-- luacheck: globals idle (Hook functions passed by name)

function create()
    -- TODO: Change this to the Krieger once the Baron has it. Needs "King" mission first.
    local baronship = pilot.add( "Proteron Kahan", "Independent", pnt:pos() + vec2.new(-400,-400), _("Pinnacle"), {ai="trader"} )
    baronship:setInvincible(true)
    baronship:setFriendly()
    baronship:control()
    baronship:moveto(pnt:pos() + vec2.new( 500, -500), false, false)
    hook.pilot(baronship, "idle", "idle")
end

function idle(baronship)
    baronship:moveto(pnt:pos() + vec2.new( 500,  500), false, false)
    baronship:moveto(pnt:pos() + vec2.new(-500,  500), false, false)
    baronship:moveto(pnt:pos() + vec2.new(-500, -500), false, false)
    baronship:moveto(pnt:pos() + vec2.new( 500, -500), false, false)
end
