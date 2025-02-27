--[[
-- Based on when pilot is hit by something, then will attempt to retaliate
--]]

-- triggered when pilot is hit by something
function attacked ( attacker )
   task = ai.taskname()
   if task ~= "attack" and task ~= "runaway" then

      -- some taunting
      taunt( attacker )

      -- now pilot fights back
      ai.pushtask( "attack", attacker )

   elseif task == "attack" then
      if ai.targetid() ~= attacker then
            ai.pushtask( "attack", attacker )
      end
   end
end

-- taunts
function taunt( target )
   local taunts = {
      _("You dare attack me!"),
      _("You are no match for the Empire!"),
      _("The Empire will have your head!"),
      _("You'll regret this!"),
   }
   local msg = taunts[ rnd.rnd(1, #taunts) ]
   if msg then ai.pilot():comm( attacker, msg ) end
end

-- attacks
function attack( target )
   -- make sure pilot exists
   if not target:exists() then
      ai.poptask()
      return
   end

   local dir = ai.face( target )
   local dist = ai.dist( ai.pos(target) )

   -- Attack with primary weapon. Real AIs manage weapon sets so they can use missile launchers etc.
   if math.abs(dir) < math.rad(10) and dist > 300 then
      ai.accel()
   elseif (math.abs(dir) < math.rad(10) or ai.hasturrets()) and dist < 300 then
      ai.shoot()
   end
end
