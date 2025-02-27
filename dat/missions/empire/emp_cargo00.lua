--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="Empire Recruitment">
 <flags>
  <unique />
 </flags>
 <avail>
  <priority>4</priority>
  <chance>30</chance>
  <location>Bar</location>
  <faction>Empire</faction>
  <cond>player.credits() &gt;= 200e3</cond>
 </avail>
 <notes>
  <tier>1</tier>
 </notes>
</mission>
--]]
--[[

   Simple cargo mission that opens up the Empire cargo missions.

   Author: bobbens
      minor edits by Infiltrator

]]--
local pir = require "common.pirate"
local fmt = require "format"
local emp = require "common.empire"
local lmisn = require "lmisn"

-- luacheck: globals land (Hook functions passed by name)

function create ()
   -- Note: this mission does not make any system claims.

   -- target destination
   local planets = {}
   lmisn.getSysAtDistance( system.cur(), 1, 6,
       function(s)
           for i, v in ipairs(s:planets()) do
               if v:faction() == faction.get("Empire") and v:canLand() then
                   planets[#planets + 1] = {v, s}
               end
           end
           return false
       end )
   if #planets == 0 then abort() end -- In case no suitable planets are in range.
   local index = rnd.rnd(1, #planets)
   mem.dest = planets[index][1]
   mem.sys = planets[index][2]

   misn.setNPC( _("Lieutenant"), "empire/unique/czesc.webp", _("You see an Empire Lieutenant who seems to be looking at you.") )
end


function accept ()
   -- Intro text
   if not tk.yesno( _("Spaceport Bar"), _([[You approach the Empire Lieutenant.
    "Hello, I'm Lieutenant Czesc from the Empire Armada Shipping Division. We're having another recruitment operation and would be interested in having another pilot among us. Would you be interested in working for the Empire?"]]) ) then
      misn.finish()
   end

   misn.markerAdd( mem.sys, "low" )

   -- Accept the mission
   misn.accept()

   -- Mission details
   misn.setTitle(_("Empire Recruitment"))
   misn.setReward( fmt.credits( emp.rewards.cargo00 ) )
   misn.setDesc( fmt.f(_("Deliver some parcels for the Empire to {pnt} in {sys}."), {pnt=mem.dest, sys=mem.sys}) )

   -- Flavour text and mini-briefing
   tk.msg( _("Empire Recruitment"), fmt.f( _([["Welcome aboard," says Czesc before giving you a firm handshake. "At first you'll just be tested with cargo missions while we gather data on your flying skills. Later on, you could get called upon for more important missions. Who knows? You could be the next Yao Pternov, greatest pilot we ever had in the armada."
    He hits a couple buttons on his wrist computer, which springs into action. "It looks like we already have a simple task for you. Deliver these parcels to {pnt}. The best pilots started delivering papers and ended up flying into combat against gigantic warships with the Interception Division."]]), {pnt=mem.dest} ))
   misn.osdCreate(_("Empire Recruitment"), {fmt.f(_("Deliver some parcels for the Empire to {pnt} in {sys}."), {pnt=mem.dest, sys=mem.sys})})

   -- Set up the goal
   local c = commodity.new( N_("Parcels"), N_("A bunch of boring Empire parcels.") )
   mem.parcels = misn.cargoAdd(c, 0)
   hook.land("land")
end


function land()
   local landed = planet.cur()
   if landed == mem.dest then
      if misn.cargoRm(mem.parcels) then
         player.pay( emp.rewards.cargo00 )
         -- More flavour text
         tk.msg(_("Mission Accomplished"), fmt.f( _([[You deliver the parcels to the Empire Shipping station at the {pnt} spaceport. Afterwards, they make you do some paperwork to formalise your participation with the Empire. They tell you to keep an eye out for missions labeled ES, which stands for Empire Shipping, in the mission computer, to which you now have access.
    You aren't too sure of what to make of your encounter with the Empire. Only time will tell...]]), {pnt=mem.dest} ))
         var.push("es_cargo", true)
         faction.modPlayerSingle("Empire",3);
         emp.addShippingLog( _([[You were recruited into the Empire's shipping division and can now do missions labeled ES, which stands for Empire Shipping. You aren't too sure of what to make of your encounter with the Empire. Only time will tell...]]) )
         pir.reputationNormalMission(3)
         misn.finish(true)
      end
   end
end

function abort()
   misn.finish(false)
end
