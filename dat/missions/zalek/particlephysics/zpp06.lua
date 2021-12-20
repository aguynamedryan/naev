--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="Za'lek Particle Physics 6">
 <flags>
  <unique />
 </flags>
 <avail>
  <priority>4</priority>
  <chance>100</chance>
  <planet>Katar I</planet>
  <location>Bar</location>
  <done>Za'lek Particle Physics 5</done>
 </avail>
 <notes>
  <campaign>Za'lek Particle Physics</campaign>
  <tier>1</tier>
 </notes>
</mission>
--]]
--[[
   Za'lek Particle Physics 06

   Conclusion mission. just take a note back to Za'lek research place
]]--
local vn = require "vn"
local fmt = require "format"
local zpp = require "common.zalek_physics"

-- luacheck: globals land (Hook functions passed by name)

local reward = zpp.rewards.zpp06
local destpnt, destsys = planet.getS("PRP-1")

function create ()
   misn.setNPC( _("Noona"), zpp.noona.portrait, zpp.noona.description )
end

function accept ()
   local accepted = false

   vn.clear()
   vn.scene()
   local n = vn.newCharacter( zpp.vn_noona() )
   vn.transition( zpp.noona.transition )
   vn.na(_([[You meet Noona who seems to be a bit more calm than usually.]]))
   n(fmt.f(_([["Thanks for all you've done for me. I've got a ton of data from the experiment that will take me a long time to process. In the meantime, I've written up a preliminary report that I would need taken to {pnt} in the {sys} system. I would go myself, except, I'm… a bit busy. Would you be willing to me this favour and deliver my report to {pnt}?"]]),
      {pnt=destpnt, sys=destsys}))
   vn.menu{
      {_("Accept"), "accept"},
      {_("Decline"), "decline"},
   }

   vn.label("decline")
   n(_([["I see. You must be busy with other things."]]))
   vn.done( zpp.noona.transition )

   vn.label("accept")
   n(_([["Great! I can't thank you enough for all you've helped me."
She hands you the report package, which seems a bit heavier than you expected, and waves you off.]]))
   vn.func( function () accepted = true end )
   vn.done( zpp.noona.transition )
   vn.run()

   -- Must be accepted beyond this point
   if not accepted then return end

   misn.accept()

   local c = commodity.new( N_("Za'lek Report"), N_("A in-depth report filled with jargon and technical details you can't comprehend.") )
   misn.cargoAdd( c, 0 )

   -- mission details
   misn.setTitle( _("Particle Physics") )
   misn.setReward( fmt.reward(reward) )
   misn.setDesc( fmt.f(_("Deliver Noona's preliminary report to {pnt} in the {sys} system."), {pnt=destpnt, sys=destsys} ))

   misn.markerAdd( destpnt )

   misn.osdCreate( _("Particle Physics"), {
      fmt.f(_("Deliver a report to {pnt} ({sys} system)"), {pnt=destpnt, sys=destsys}),
   } )

   hook.land( "land" )
end

function land ()
   if planet.cur() ~= destpnt then
      return
   end

   local getlicense = not diff.isApplied( "heavy_combat_vessel_license" )

   vn.clear()
   vn.scene()
   local n = vn.newCharacter( zpp.noona.name, {color=zpp.noona.colour} ) -- Just for the letter
   vn.transition()
   vn.na(_([[You land and head to where Noona told you to drop off the package. On the way you get lost in the maze of research laboratories, coffee machines, dangerous looking experiments, and Za'lek scientists engaged in heated arguments who don't detect your presence.]]))
   vn.na(_([[Eventually you find the hidden away room labeled "Particle Physics Experiments Registration Department", and enter to meet a non-enthusiastic academic secretary. You hand them the package, and, after sighing, they proceed to inspect the contents. They mention something about Dr. Sanderaite being at it again, and hand you back an envelope that was with the report mentioning that it is not part of the report.]]))
   vn.na(_([[As you wonder about what is in the envelope, suddenly two Za'lek Military officers bust in to the room.]]))
   -- TODO proper graphics
   local z1 = vn.Character.new( _("Za'lek Officer A"), {image="zalek_thug1.png", pos="left"} )
   local z2 = vn.Character.new( _("Za'lek Officer B"), {image="zalek_thug1.png", pos="right"} )
   vn.appear( { z1, z2 }, "slideleft" )
   z1(_([["Where is Chairman Sanderaite!? We just got the signal that she submitted a report here!"]]))
   vn.na(_([[The secretary lazily points at you.]]))
   z1(_([["Damnit, she got someone else to bring the report for her again…"]]))
   z2(_([["The council should have never passed the law permitting third parties to hand-in official reports."]]))
   z1(_([["They're trying to revoke it, but last time the meeting ended in fisticuffs."]]))
   z2(_([["Oh well, at least we have a hint to where she is now. On to Katar I!"]]))
   vn.disappear( { z1, z2 }, "slideleft" ) -- Played in reverse
   vn.na(_([[The Za'lek officers leave as fast as they got here, barely acknowledging your presence. Pondering how House Za'lek manages to get anything done, you take your leave and open up the envelope that you got handed back. It has a letter from Noona.]]))
   if getlicense then
      n(_([[You read the letter:
   Thanks for all your help at Katar I. By know you've probably realised I not just a researcher, but have been bestowed the curse of being the Za'lek chairman. Not often do I get to get away from everything and focus on my research. I have attached a credstick as a reward, and have given you clearance for the Heavy Combat Vessel License. Your help was invaluable and I hope we meet again.]]))
   else
      n(_([[You read the letter:
   Thanks for all your help at Katar I. By know you've probably realised I not just a researcher, but have been bestowed the curse of being the Za'lek chairman. Not often do I get to get away from everything and focus on my research. I have attached a credstick as a reward. Your help was invaluable and I hope we meet again.]]))
   end
   vn.sfxVictory()
   vn.na( fmt.reward(reward) )
   vn.run()

   player.pay( reward )
   if getlicense then
      diff.apply("heavy_combat_vessel_license")
      zpp.log(fmt.f(_("You delivered Noona's report to {pnt} and found out that she is the Za'lek chairman. You also got access to the Heavy Combat Vessel license for your help."),
         {pnt=destpnt}))
   else
      zpp.log(fmt.f(_("You delivered Noona's report to {pnt} and found out that she is the Za'lek chairman."),
         {pnt=destpnt}))
   end
   misn.finish(true)
end
