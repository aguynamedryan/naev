--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="An old woman">
 <flags>
  <unique />
 </flags>
 <avail>
  <priority>4</priority>
  <chance>3</chance>
  <location>Bar</location>
  <faction>Dvaered</faction>
 </avail>
 <notes>
  <tier>1</tier>
 </notes>
</mission>
--]]
-- Note: additional conditionals present in mission script!
--[[
--
-- MISSION: The complaining grandma
-- DESCRIPTION: The player is taking an old woman from Dvaered space to Sirius space.
-- The old woman keeps muttering about how the times have changed and how it used to be when she was young.
--
--]]
local car = require "common.cargo"
local fmt = require "format"
local neu = require "common.neutral"
local lmisn = require "lmisn"

local reward = 500e3

-- luacheck: globals date land (Hook functions passed by name)

local complaints = {}
complaints[1] = _([["You youngsters and your newfangled triple redundancy plasma feedback shunts. In my day, we had to use simple monopole instaconductors to keep our hyperdrives running!"]])
complaints[2] = _([["Tell me, youngster, why is everyone so preoccupied with Soromid enhancements these days? Cybernetic implants were good enough for us, why can't they be for you?"]])
complaints[3] = _([["My mother was from Earth, you know. She even took me there once, when I was a little girl. I still remember it like it happened yesterday. They don't make planets like that anymore these days, oh no."]])
complaints[4] = _([["What did you say this thing was? A microblink reinforced packet transmitter? No, no, don't try to explain how it works. I'm too old for this. Anything more complex than a subspace pseudo-echo projector is beyond my understanding."]])
complaints[5] = _([["You don't know how good you have it these days. When I was young, we needed to reprogram our own electronic countermeasures to deal with custom pirate tracking systems."]])
complaints[6] = _([["Time passes by so quickly. I remember when this was all space."]])
complaints[7] = _([["All this automation is making people lax, I tell you. My uncle ran a tight ship. If he caught you cutting corners, you'd be defragmenting the sub-ion matrix filters for a week!"]])

function create ()
   mem.cursys = system.cur()

   local planets = {}
   lmisn.getSysAtDistance( mem.cursys, 1, 6,
      function(s)
         for i, v in ipairs(s:planets()) do
             if v:faction() == faction.get("Sirius") and v:class() == "M" and v:canLand() then
                 planets[#planets + 1] = {v, s}
             end
          end
          return false
      end )

   if #planets == 0 then abort() end -- In case no suitable planets are in range.

   local index = rnd.rnd(1, #planets)
   mem.destplanet = planets[index][1]
   mem.destsys = planets[index][2]

   mem.curplanet = planet.cur()
   misn.setNPC(_("An old woman"), "neutral/unique/oldwoman.webp", _("You see a wrinkled old lady, a somewhat unusual sight in a spaceport bar. She's purposefully looking around."))
end


function accept ()
   if tk.yesno(_("An elderly lady"), fmt.f(_([[You decide to ask the old woman if there's something you can help her with.
   "As a matter of fact, there is," she creaks. "I want to visit my cousin, she lives on {pnt}, you know, in the {sys} system, it's a Sirian place. But I don't have a ship and those blasted passenger lines around here don't fly on Sirius space! I tell you, customer service really has gone down the gutter over the years. In my space faring days, there would always be some transport ready to take you anywhere! But now look at me, I'm forced to get to the spaceport bar to see if there's a captain willing to take me! It's a disgrace, that's what it is. What a galaxy we live in! But I ramble. You seem like you've got time on your hands. Fancy making a trip down to {pnt}? I'll pay you a decent fare, of course."]]), {pnt=mem.destplanet, sys=mem.destsys})) then
      tk.msg(_("An elderly lady"), _([["Oh, that's good of you." The old woman gives you a wrinkly smile. "I haven't seen my cousin in such a long time, it'll be great to see how she's doing, and we can talk about old times. Ah, old times. It was all so different then. The space ways were much safer, for one. And people were politer to each other too, oh yes!"
   You escort the old lady to your ship, trying not to listen to her rambling. Perhaps it would be a good idea to get her to her destination as quickly as you can.]]))
      local c = commodity.new( N_("Old Woman"), N_("A grumbling old woman.") )
      misn.cargoAdd(c, 0)

      misn.accept()
      misn.setTitle("The Old Woman")
      misn.setDesc(fmt.f(_("An aging lady has asked you to ferry her to {pnt} in the {sys} system."), {pnt=mem.destplanet, sys=mem.destsys}))
      misn.setReward(_("Fair monetary compensation"))
      misn.osdCreate(_("The Old Woman"), {
         fmt.f(_("Take the old woman to {pnt} ({sys} system)"), {pnt=mem.destplanet, sys=mem.destsys}),
      })
      misn.markerAdd( mem.destplanet )

      mem.dist_total = car.calculateDistance(system.cur(), planet.cur():pos(), mem.destsys, mem.destplanet)
      mem.complaint = 0

      hook.date(time.create(0, 0, 300), "date")
      hook.land("land")
   else
      misn.finish()
   end
end

-- Date hook.
function date()
   local dist_now = car.calculateDistance(system.cur(), player.pos(), mem.destsys, mem.destplanet)
   local complaint_now = math.floor(((mem.dist_total - dist_now) / mem.dist_total) * #complaints + 0.5)
   if complaint_now > mem.complaint then
      mem.complaint = complaint_now
      tk.msg(_("Grumblings from the old lady"), complaints[mem.complaint])
   end
   -- Uh... yeah.
end

-- Land hook.
function land()
   if planet.cur() == mem.destplanet then
      tk.msg(_("Delivery complete"), _([[You help the old lady to the spacedock elevator. She keeps grumbling about how spaceports these days are so inconvenient and how advertisement holograms are getting quite cheeky of late, they wouldn't allow that sort of thing in her day. But once you deliver her to the exit terminal, she smiles at you.
   "Thank you, young captain, I don't know what I would have done without you. It seems there are still decent folk out there even now. Take this, as a token of my appreciation."
   The lady hands you a credit chip. Then she disappears through the terminal. Well, that was quite a passenger!]]))
      player.pay( reward )
      neu.addMiscLog( _([[You escorted an old woman to her cousin in Sirian space. She was nice, albeit somewhat overly chatty.]]) )
      misn.finish(true)
   end
end

function abort ()
   misn.finish(false)
end
