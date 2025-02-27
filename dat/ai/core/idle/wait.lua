-- luacheck: globals idle (AI Task functions passed by name)
function idle ()
   if mem.aggressive then
      local enemy = ai.getenemy()
      if enemy ~= nil and should_attack( enemy ) then
         ai.pushtask( "attack", enemy )
         return
      end
   end

   -- Stop if necessary
   if not ai.isstopped() then
      ai.pushtask("brake")
      return
   end

   -- Just wait
   ai.settimer( 0, rnd.uniform(3.0, 5.0) )
   ai.pushtask("idle_wait")
end

-- Overwrite the attack function with the generic in case we are overloading idle/pirate
function control_funcs.attack ()
   return control_funcs.generic_attack()
end
