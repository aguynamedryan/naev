local masslimit = 6000^2 -- squared
local jumpdist = 2000
local cooldown = 8
local warmup = 2
local penalty = -50

function init( _p, po )
   po:state("off")
   mem.timer = 0
   mem.warmup = false
end

function update( p, po, dt )
   mem.timer = mem.timer - dt
   if mem.timer < 0 then
      if mem.warmup then
         -- Blink!
         mem.warmup = false
         local dist = jumpdist
         local m = p:mass()
         m = m*m
         -- We use squared values here so twice the masslimit is 25% efficiency
         if m > masslimit then
            dist = dist * masslimit / m
         end
         -- Direction is random
         p:setPos( p:pos() + vec2.newP( dist, p:dir()+(2*rnd.rnd()-1)*math.pi/6 ) )
         -- TODO Add blink effect and sound effect

         -- Set cooldown and maluses
         po:state("cooldown")
         po:progress(1)
         po:set("thrust_mod", penalty)
         po:set("turn_mod", penalty)
         mem.cooldown = true
         mem.timer = cooldown
      else
         -- Cooldown is over
         if mem.cooldown then
            po:state("off")
            -- Cancel maluses
            po:clear()
            mem.cooldown = false
         end
      end
   else
      -- Update progress
      if mem.warmup then
         po:progress( mem.timer / warmup )
      else
         po:progress( mem.timer / cooldown )
      end
   end
end

function ontoggle( _p, po, on )
   -- Only care about turning on (outfit never has the "on" state)
   if not on then return false end

   -- Not ready yet
   if mem.timer > 0 then return false end

   -- Start the warmup
   mem.warmup = true
   mem.timer = warmup
   po:state("on")
   po:progress(1)
   -- TODO add warm-up sound effect
   return true
end
