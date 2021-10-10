--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="Fake ID">
 <avail>
  <priority>10</priority>
  <chance>100</chance>
  <location>Computer</location>
  <faction>Wild Ones</faction>
  <faction>Black Lotus</faction>
  <faction>Raven Clan</faction>
  <faction>Dreamer Clan</faction>
  <faction>Pirate</faction>
 </avail>
</mission>
--]]
--[[

   Fake ID

--]]
local pir = require 'common.pirate'
local fmt = require "format"


misn_title = _("Fake ID")
misn_desc = _([[This fake ID will allow you to conduct business with all major locations where you are currently wanted. It will be as if you were a new person. However, committing any crime will risk discovery of your identity by authorities, no gains in reputation under your fake ID will ever improve your real name's reputation under any circumstance, and gaining too much reputation with factions where you are wanted can lead to the discovery of your true identity as well. Note that fake ID does not work on pirates, for whom your reputation will be affected as normal (whether good or bad). Furthermore, being scanned by other ships will lead to your fake ID being detected and your cover being blown.

Cost: %s]])
misn_reward = _("None")

factions = {
   "Empire", "Goddard", "Dvaered", "Za'lek", "Sirius", "Soromid", "Frontier",
   --"Trader", "Miner"
}
misnvars = {
   Empire      = "_fcap_empire",
   Goddard     = "_fcap_goddard",
   Dvaered     = "_fcap_dvaered",
   ["Za'lek"]  = "_fcap_zalek",
   Sirius      = "_fcap_sirius",
   Soromid     = "_fcap_soromid",
   Frontier    = "_fcap_frontier",
   --Trader      = "_fcap_trader",
   --Miner       = "_fcap_miner"
}
orig_standing = {}
orig_standing["__save"] = true
orig_cap = {}
orig_cap["__save"] = true

temp_cap = 10
next_discovered = false


function create ()
   local nhated = 0
   for i, j in ipairs( factions ) do
      if faction.get(j):playerStanding() < 0 then
         nhated = nhated + 1
      end
   end
   if nhated <= 0 then misn.finish( false ) end

   credits = 50e3 * nhated

   misn.setTitle( misn_title )
   misn.setDesc( misn_desc:format( fmt.credits( credits ) ) )
   misn.setReward( misn_reward )
end


function accept ()
   if player.credits() < credits then
      tk.msg( "", _("You don't have enough money to buy a fake ID. The price is %s."):format( fmt.credits( credits ) ) )
      misn.finish()
   end

   player.pay( -credits )
   misn.accept()

   for i, j in ipairs( factions ) do
      local f = faction.get(j)
      if f:playerStanding() < 0 then
         orig_standing[j] = f:playerStanding()
         orig_cap[j] = var.peek( misnvars[j] )
         var.push( misnvars[j], temp_cap )
         f:setPlayerStanding( 0 )
      end
   end

   landed = true
   standhook = hook.standing( "standing" )
   hook.takeoff( "takeoff" )
   hook.land( "land" )
   hook.enter( "enter" )
end


function standing( f, delta )
   local fn = f:nameRaw()
   if orig_standing[fn] ~= nil then
      if delta >= 0 then
         if var.peek( misnvars[fn] ) ~= temp_cap then
            abort()
         end
      else
         abort()
      end
   elseif pir.factionIsPirate( fn ) and delta >= 0 then
      local sf = system.cur():faction()
      if sf ~= nil and orig_standing[sf:nameRaw()] ~= nil then
         -- We delay choice of when you are discovered to prevent players
         -- from subverting the system to eliminate the risk.
         if next_discovered then
            abort()
         else
            next_discovered = rnd.rnd() < 0.1
         end
      end
   end
end


function enter ()
   local pp = player.pilot()
   hook.pilot( pp, "scanned", "player_scanned" )
end


function player_scanned( _pp, scanner )
   player.msg(_("#rYou fake ID has been detected and your cover has been blown!"))
   scanner:setHostile(true)
   restore_standing()
   misn.finish( false )
end


function takeoff ()
   landed = false
end


function land ()
   landed = true
end


function abort ()
   if standhook ~= nil then hook.rm(standhook) end

   restore_standing()

   local msg = _("It seems your actions have led to the discovery of your identity. As a result, your fake ID is now useless.")
   if landed then
      local f = planet.cur():faction()
      if f ~= nil and orig_standing[f:nameRaw()] ~= nil then
         msg = _([[During a routine check, you hand over your fake ID as usual, but the person checking your ID eyes it strangely for a time that seems to be periods long. Eventually you are handed your ID back, but this is not a good sign.
    When you check, you see that the secrecy of your identity is in jeopardy. You're safe for now, but you make a mental note to prepare for the worst when you take off, because your fake ID probably won't be of any further use by then.]])
      end
   end

   tk.msg( "", msg )
   misn.finish( false )
end


function restore_standing ()
   for i, j in ipairs( factions ) do
      if orig_standing[j] ~= nil then
         if misnvars[j] ~= nil then
            if orig_cap[j] ~= nil then
               var.push( misnvars[j], orig_cap[j] + var.peek( misnvars[j] ) - temp_cap )
            else
               var.pop( misnvars[j] )
            end
         end
         faction.get(j):setPlayerStanding( orig_standing[j] )
      end
   end
end
