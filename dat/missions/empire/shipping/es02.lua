--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="Empire Shipping 3">
 <flags>
  <unique />
 </flags>
 <avail>
  <priority>2</priority>
  <cond>faction.playerStanding("Empire") &gt;= 0 and faction.playerStanding("Dvaered") &gt;= 0 and faction.playerStanding("FLF") &lt; 10</cond>
  <chance>50</chance>
  <done>Empire Shipping 2</done>
  <location>Bar</location>
  <planet>Halir</planet>
 </avail>
 <notes>
  <campaign>Empire Shipping</campaign>
 </notes>
</mission>
--]]
--[[
   Empire VIP Rescue

   Author: bobbens
      minor edits by Infiltrator
      - Mission fixed to suit big systems (Anatolis, 11/02/2011)

   Rescue a VIP stranded on a disabled ship in a system while FLF and Dvaered
    are fighting.

   Stages:

      0) Go to sector.
      1) Board ship and rescue VIP.
      2) Rescued VIP, returning to base.
      3) VIP died or jump out of system without VIP  --> mission failure.
]]--
local fleet = require "fleet"
local fmt = require "format"
local emp = require "common.empire"

-- Mission constants
local destsys     = system.get( "Slaccid" )
local log_text_fail = _([[You failed in your attempt to rescue a VIP for the Empire. Meet with Commander Soldner on Halir to try again.]])

-- luacheck: globals board death delay_flf enter jumpout land (Hook functions passed by name)

function create ()
   -- Target destination
   mem.ret, mem.retsys  = planet.getLandable( "Halir" )
   if mem.ret== nil then
      misn.finish(false)
   end

   -- Must claim system
   if not misn.claim( destsys ) then
      misn.finish(false)
   end

   -- Add NPC.
   misn.setNPC( _("Soldner"), "empire/unique/soldner.webp", _("Commander Soldner is waiting for you.") )
end


function accept ()
   -- Intro text
   if not tk.yesno( _("Commander Soldner"), fmt.f( _([[You meet up once more with Commander Soldner at the bar.
    "Hello again, {player}. Still interested in doing another mission? This one will be more dangerous."]]), {player=player.name()} ) ) then
      misn.finish()
   end

   -- Accept the mission
   misn.accept()

   -- Set marker
   mem.misn_marker = misn.markerAdd( destsys, "low" )

   -- Mission details
   mem.misn_stage = 0
   misn.setTitle(_("Empire VIP Rescue"))
   misn.setReward( fmt.credits( emp.rewards.es02 ) )
   misn.setDesc( fmt.f( _("Rescue the VIP from a transport ship in the {sys} system"), {sys=destsys} ) )

   -- Flavour text and mini-briefing
   tk.msg( _("Commander Soldner"), fmt.f( _([[Commander Soldner nods and continues, "We've had reports that a transport vessel came under attack while transporting a VIP. They managed to escape, but the engine ended up giving out in the {sys} system. The ship is now disabled and we need someone to board the ship and rescue the VIP. There have been many FLF ships detected near the sector, but we've managed to organise a Dvaered escort for you.
    "You're going to have to fly to the {sys} system, find and board the transport ship to rescue the VIP, and then fly back. The sector is most likely going to be hot. That's where your Dvaered escorts will come in. Their mission will be to distract and neutralise all possible hostiles. You must not allow the transport ship to be destroyed before you rescue the VIP. His survival is vital."]]), {sys=destsys} ) )
   tk.msg( _("Commander Soldner"), fmt.f( _([["Be careful with the Dvaered; they can be a bit blunt, and might accidentally destroy the transport ship. If all goes well, you'll be paid {credits} when you return with the VIP. Good luck, pilot."]]), {credits=fmt.credits(emp.rewards.es02)} ) )
   misn.osdCreate(_("Empire VIP Rescue"), {
      fmt.f(_("Rescue the VIP from a transport ship in the {sys} system"), {sys=destsys}),
   })
   -- Set hooks
   hook.land("land")
   hook.enter("enter")
   hook.jumpout("jumpout")

   -- Initiate mission variables (A.)
   mem.prevsys = system.cur()
end


function land ()
   mem.landed = planet.cur()
   if mem.landed == mem.ret then
      -- Successfully rescued the VIP
      if mem.misn_stage == 2 then
         local getlicense = not diff.isApplied( "heavy_combat_vessel_license" )

         -- VIP gets off
         misn.cargoRm(mem.vip)

         -- Rewards
         player.pay( emp.rewards.es02 )
         emp.modReputation( 5 ) -- Bump cap a bit
         faction.modPlayerSingle("Empire",5)
         faction.modPlayerSingle("Dvaered",5)

         -- Flavour text
         if getlicense then
            tk.msg( _("Mission Success"), _([[You land at the starport. It looks like the VIP has already recovered. He thanks you profusely before heading off. You proceed to pay Commander Soldner a visit. He seems to be happy, for once.
    "It seems like you managed to pull it off. I had my doubts at first, but you've proven to be a very skilled pilot. Oh, and I've cleared you for the Heavy Combat Vessel License; congratulations! We have nothing more for you now, but check in periodically in case something comes up for you."]]) )
            emp.addShippingLog( _([[You successfully rescued a VIP for the Empire and have been cleared for the Heavy Combat Vessel License; you can now buy one at the outfitter.]]) )
            diff.apply("heavy_combat_vessel_license")
         else
            tk.msg( _("Mission Success"), _([[You land at the starport. It looks like the VIP has already recovered. He thanks you profusely before heading off. You proceed to pay Commander Soldner a visit. He seems to be happy, for once.
    "It seems like you managed to pull it off. I had my doubts at first, but you've proven to be a very skilled pilot. We have nothing more for you now, but check in periodically in case something comes up for you."]]) )
            emp.addShippingLog( _([[You successfully rescued a VIP for the Empire.]]) )
         end

         misn.finish(true)
      end
   end
end


function enter ()
   mem.sys = system.cur()

   if mem.misn_stage == 0 and mem.sys == destsys then
      -- Force FLF combat music (note: must clear this later on).
      var.push( "music_combat_force", "FLF" )

      -- Put the VIP a ways off of the player but near the jump.
      local enter_vect = jump.pos(mem.sys, mem.prevsys)
      local m,a = enter_vect:polar()
      enter_vect:setP( m-3000, a )
      local v = pilot.add( "Gawain", "Trader", enter_vect, _("Trader Gawain"), {ai="dummy"} )

      v:setPos( enter_vect )
      v:setVel( vec2.new( 0, 0 ) ) -- Clear velocity
      v:disable()
      v:setHilight(true)
      v:setVisplayer(true)
      v:setFaction( "Empire" )
      v:rename(_("VIP"))
      hook.pilot( v, "board", "board" )
      hook.pilot( v, "death", "death" )

      -- FLF Spawn around the Gawain
      local flf_med_force = { "Hyena", "Hyena", "Admonisher", "Vendetta", "Pacifier" }
      local p = fleet.add( 1, flf_med_force, "FLF", enter_vect, _("FLF Ambusher") )
      for k,pk in ipairs(p) do
         pk:setHostile()
      end
      -- To make it more interesting a vendetta will solely target the player.
      p = pilot.add( "Vendetta", "FLF", enter_vect )
      p:setHostile()
      -- If player is seen, have them target player
      local pp = player.pilot()
      if p:inrange( pp ) then
         p:control()
         p:attack( pp )
      end

      -- Now Dvaered
      -- They will jump together with you in the system at the jump point. (A.)
      local dv_med_force = { "Dvaered Vendetta", "Dvaered Vendetta", "Dvaered Ancestor", "Dvaered Ancestor", "Dvaered Phalanx", "Dvaered Vigilance" }
      p = fleet.add( 1, dv_med_force, "Dvaered", mem.prevsys )
      for k,pk in ipairs(p) do
         pk:setFriendly()
      end

      -- Add more ships on a timer to make this messy
      hook.timer(rnd.uniform( 3.0, 5.0 ) , "delay_flf")

      -- Pass to next stage
      mem.misn_stage = 1

   -- Can't run away from combat
   elseif mem.misn_stage == 1 then
      -- Notify of mission failure
      player.msg( _("MISSION FAILED: You abandoned the VIP.") )
      emp.addShippingLog( log_text_fail )
      var.pop( "music_combat_force" )
      misn.finish(false)
   end
end


function jumpout ()
   -- Storing the system the player jumped from.
   mem.prevsys = system.cur()

   if mem.prevsys == destsys then
      var.pop( "music_combat_force" )
   end
end


function delay_flf ()
   if mem.misn_stage ~= 0 then
      return
   end

   -- More ships to pressure player from behind
   local flf_sml_force = { "Hyena", "Admonisher", "Vendetta" }
   local p = fleet.add( 1, flf_sml_force, "FLF", mem.prevsys, _("FLF Ambusher") )
   for k,v in ipairs(p) do
      v:setHostile()
   end
end


function board ()
   -- VIP boards
   local c = commodity.new( N_("VIP"), N_("A Very Important Person.") )
   mem.vip = misn.cargoAdd( c, 0 )
   tk.msg( _("Disabled Ship"), _([[The ship's hatch opens and immediately an unconscious VIP is brought aboard by his bodyguard. Looks like there is no one else aboard.]]) )

   -- Update mission details
   mem.misn_stage = 2
   misn.markerMove( mem.misn_marker, mem.retsys )
   misn.setDesc( fmt.f(_("Return to {pnt} in the {sys} system with the VIP"), {pnt=mem.ret, sys=mem.retsys} ))
   misn.osdCreate(_("Empire VIP Rescue"), {
      fmt.f(_("Return to {pnt} in the {sys} system with the VIP"), {pnt=mem.ret, sys=mem.retsys}),
   })

   -- Force unboard
   player.unboard()
end


function death ()
   if mem.misn_stage == 1 then
      -- Notify of death
      player.msg( _("MISSION FAILED: VIP is dead.") )
      emp.addShippingLog( log_text_fail )
      var.pop( "music_combat_force" )
      misn.finish(false)
   end
end


function abort ()
   -- If aborted you'll also leave the VIP to fate. (A.)
   player.msg( _("MISSION FAILED: You abandoned the VIP.") )
   emp.addShippingLog( log_text_fail )
   if system.cur() == destsys then
      var.pop( "music_combat_force" )
   end
   misn.finish(false)
end
