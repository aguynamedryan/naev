require 'ai.guard'

--[[
   Generic Mission baddie AI that tries to stay near a position
]]--

-- Settings
mem.aggressive     = true
mem.safe_distance  = 500
mem.armour_run     = 80
mem.armour_return  = 100
mem.guarddodist   = math.huge
mem.guardreturndist = math.huge

function create ()
   local p = ai.pilot()

   -- Default range stuff
   mem.guardpos      = p:pos() -- Just guard current position
   mem.enemyclose    = mem.guarddodist

   -- Finish up creation
   create_post()
end
