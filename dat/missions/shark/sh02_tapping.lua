--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="Unfair Competition">
 <flags>
  <unique />
 </flags>
 <avail>
  <priority>3</priority>
  <cond>diff.isApplied("collective_dead")</cond>
  <done>Sharkman Is Back</done>
  <chance>5</chance>
  <location>Bar</location>
  <faction>Dvaered</faction>
  <faction>Empire</faction>
  <faction>Frontier</faction>
  <faction>Goddard</faction>
  <faction>Independent</faction>
  <faction>Soromid</faction>
  <faction>Traders Guild</faction>
  <faction>Za'lek</faction>
 </avail>
 <notes>
  <requires name="The Collective is dead and no one will miss them"/>
  <campaign>Nexus show their teeth</campaign>
 </notes>
</mission>
--]]
--[[
   This is the third mission of the Shark's teeth campaign. The player has to take illegal holophone recordings in his ship.

   Stages :
   0) Way to Sirius world
   1) Way to Darkshed
--]]
local pir = require "common.pirate"
local fmt = require "format"
local shark = require "common.shark"

local badguys -- Non-persistent state
local add_llama, bombers, corvette, cruiser, hvy_intercept, interceptors, rndNb -- Forward-declared functions
-- luacheck: globals BomberDead CorvetteDead CruiserDead FighterDead InterceptorDead LlamaDead ambush enter land (Hook functions passed by name)
-- luacheck: globals beginrun (NPC functions passed by name)

-- Mission constants
local paypla, paysys = planet.getS("Darkshed")

function create ()
   mem.mispla, mem.missys = planet.getLandable(faction.get("Sirius"))

   if not misn.claim(mem.missys) or not mem.mispla:services()["bar"] then
      misn.finish(false)
   end

   misn.setNPC(_("Arnold Smith"), "neutral/unique/arnoldsmith.webp", _([[Arnold Smith is here. Perhaps he might have another job for you.]]))
end

function accept()
   mem.stage = 0
   mem.proba = 0.4  --the chances you have to get an ambush

   if tk.yesno(_("Nexus Shipyards needs you (again)"), fmt.f(_([[You sit at Smith's table and ask him if he has a job for you. "Of course," he answers. "But this time, it's... well...
    "Listen, I need to give some background. As you know, Nexus designs are used far and wide in smaller militaries. The Empire is definitely our biggest customer, but the Frontier also notably makes heavy use of our Lancelot design, as do many independent systems. Still, competition is stiff; House Dvaered's Vendetta design, for instance, is quite popular with the FLF, ironically enough.
    "But matters just got a little worse for us: it seems that House Sirius is looking to get in on the shipbuilding business as well, and the Frontier are prime targets. If they succeed, the Lancelot design could be completely pushed out of Frontier space, and we would be crushed in that market between House Dvaered and House Sirius. Sure, the FLF would still be using a few Pacifiers, but it would be a token business at best, and not to mention the authorities would start associating us with terrorism.
    "So we've conducted a bit of espionage. We have an agent who has recorded some hopefully revealing conversations between a House Sirius sales manager and representatives of the Frontier. All we need you to do is meet with the agent, get the recordings, and bring them back to me on {pnt} in the {sys} system." You raise an eyebrow.
    "It's not exactly legal. That being said, you're just doing the delivery, so you almost certainly won't be implicated. What do you say? Is this something you can do?"]]), {pnt=paypla, sys=paysys})) then
      misn.accept()
      tk.msg(_("The job"), fmt.f(_([["I'm glad to hear it. Go meet our agent on {pnt} in the {sys} system. Oh, yes, and I suppose I should mention that I'm known as 'James Neptune' to the agent. Good luck!"]]), {pnt=mem.mispla, sys=mem.missys}))

      misn.setTitle(_("Unfair Competition"))
      misn.setReward(fmt.credits(shark.rewards.sh02))
      misn.setDesc(_("Nexus Shipyards is in competition with House Sirius."))
      misn.osdCreate(_("Unfair Competition"), {
         fmt.f(_("Land on {pnt} in {sys} and meet the Nexus agent"), {pnt=mem.mispla, sys=mem.missys}),
         fmt.f(_("Bring the recording back to {pnt} in the {sys} system"), {pnt=paypla, sys=paysys}),
      })
      misn.osdActive(1)

      mem.marker = misn.markerAdd(mem.mispla, "low")

      mem.landhook = hook.land("land")
      mem.enterhook = hook.enter("enter")
   else
      tk.msg(_("Sorry, not interested"), _([["OK, sorry to bother you."]]))
      misn.finish(false)
   end
end

function land()
   --The player is landing on the mission planet to get the box
   if mem.stage == 0 and planet.cur() == mem.mispla then
      mem.agent = misn.npcAdd("beginrun", _("Nexus's agent"), "neutral/unique/nexus_agent.webp", _([[This guy seems to be the agent Arnold Smith was talking about.]]))
   end

   --Job is done
   if mem.stage == 1 and planet.cur() == paypla then
      if misn.cargoRm(mem.records) then
         tk.msg(_("Good job"), _([[The Nexus employee greets you as you reach the ground. "Excellent! I will just need to spend a few hectoseconds analyzing these recordings. See if you can find me in the bar soon; I might have another job for you."]]))
         pir.reputationNormalMission(rnd.rnd(2,3))
         player.pay(shark.rewards.sh02)
         misn.osdDestroy()
         hook.rm(mem.enterhook)
         hook.rm(mem.landhook)
         shark.addLog( _([[You helped Nexus Shipyards gather information in an attempt to sabotage competition from House Sirius. Arnold Smith said to meet him in the bar soon; he may have another job for you.]]) )
         misn.finish(true)
      end
   end
end

function enter()
   -- Ambush !
   if mem.stage == 1 and rnd.rnd() < mem.proba then
      hook.timer( 2.0, "ambush" )
      mem.proba = mem.proba - 0.1
   elseif mem.stage == 1 then
      --the probability of an ambush goes up when you cross a system without meeting any ennemy
      mem.proba = mem.proba + 0.1
   end
end

function beginrun()
   tk.msg(_("Time to go back to Alteris"), _([[You approach the agent and obtain the package without issue. Before you leave, he suggests that you stay vigilant. "They might come after you," he says.]]))
   local c = commodity.new(N_("Recorder"), N_("A holophone recorder."))
   mem.records = misn.cargoAdd(c, 0)  --Adding the cargo
   mem.stage = 1
   misn.osdActive(2)
   misn.markerRm(mem.marker)
   mem.marker2 = misn.markerAdd(paypla, "low")

   --remove the spy
   misn.npcRm(mem.agent)

   -- Initialize number of mercenaries
   mem.nInterce = 5
   mem.nFighter = 4
   mem.nBombers = 3
   mem.nCorvett = 3
   mem.nCruiser = 1
   mem.nLlamas  = 1
end

function ambush()
   --Looking at the player ship's class in order to spawn the most dangerous enemy to him
   local playerSize = player.pilot():ship():size()
   badguys = {}

   -- First: a victim Llama
   if rnd.rnd() < 0.5 then
      add_llama()
   end

   -- The kind of enemy is choosen randomly, but the weight is changed depending on the player's ship
   -- And depending on the number of avaliable ships of the given class
   if playerSize <= 2 then
      choose( 3*mem.nInterce, 2*mem.nFighter, mem.nBombers, mem.nCorvett, mem.nCruiser )
   elseif playerSize <= 4 then
      choose( mem.nInterce, 2*mem.nFighter, mem.nBombers, 4*mem.nCorvett, 5*mem.nCruiser )
   elseif playerSize <= 6 then
      choose( mem.nInterce, mem.nFighter, 5*mem.nBombers, mem.nCorvett, 5*mem.nCruiser )
   else -- We're never too sure
      choose( mem.nInterce, mem.nFighter, mem.nBombers, mem.nCorvett, mem.nCruiser )
   end
end

-- Choose a type of attackers with a weighted random pick
function choose( wI, wF, wB, wCo, wCr )
   local ntot = wI + wF + wB + wCo + wCr
   if ntot ==0 then return end

   local rand = rnd.rnd() * ntot
   if rand < wI then
      interceptors()
   elseif rand < wI+wF then
      hvy_intercept()
   elseif rand < wI+wF+wB then
      bombers()
   elseif rand < wI+wF+wB+wCo then
      corvette()
   else
      cruiser()
   end
end

function interceptors()
   --spawning high speed Hyenas
   local nb = rndNb( mem.nInterce )
   for i = 1, nb do
      badguys[i] = pilot.add( "Hyena", "Mercenary", nil, _("Mercenary") )
      badguys[i]:setHostile()

      --Their outfits must be quite good
      badguys[i]:outfitRm("all")
      badguys[i]:outfitRm("cores")

      badguys[i]:outfitAdd("S&K Ultralight Combat Plating")
      badguys[i]:outfitAdd("Milspec Orion 2301 Core System")
      badguys[i]:outfitAdd("Tricon Zephyr Engine")

      badguys[i]:outfitAdd("Gauss Gun",3)
      badguys[i]:outfitAdd("Improved Stabiliser") -- Just try to avoid fight with these fellas

      badguys[i]:setHealth(100,100)
      badguys[i]:setEnergy(100)

      hook.pilot( badguys[i], "death", "InterceptorDead")
   end
end

function hvy_intercept()
   --spawning Lancelots
   local nb = rndNb( mem.nFighter )
   for i = 1, nb do
      badguys[i] = pilot.add( "Lancelot", "Mercenary", nil, _("Mercenary") )
      badguys[i]:setHostile()

      --Their outfits must be quite good
      badguys[i]:outfitRm("all")
      badguys[i]:outfitRm("cores")

      badguys[i]:outfitAdd("Unicorp D-4 Light Plating")
      badguys[i]:outfitAdd("Unicorp PT-68 Core System")
      badguys[i]:outfitAdd("Tricon Zephyr II Engine")

      badguys[i]:outfitAdd("TeraCom Fury Launcher")
      badguys[i]:outfitAdd("Laser Cannon MK2",2)
      badguys[i]:outfitAdd("Laser Cannon MK1")
      badguys[i]:outfitAdd("Reactor Class I")

      badguys[i]:setHealth(100,100)
      badguys[i]:setEnergy(100)

      hook.pilot( badguys[i], "death", "FighterDead")
   end
end

function corvette()
   --spawning Admonishers
   local nb = rndNb( mem.nCorvett )
   for i = 1, nb do
      badguys[i] = pilot.add( "Admonisher", "Mercenary", nil, _("Mercenary") )
      badguys[i]:setHostile()

      badguys[i]:outfitRm("all")
      badguys[i]:outfitRm("cores")

      badguys[i]:outfitAdd("Unicorp D-12 Medium Plating")
      badguys[i]:outfitAdd("Unicorp PT-200 Core System")
      badguys[i]:outfitAdd("Tricon Cyclone Engine")

      badguys[i]:outfitAdd("Unicorp Headhunter Launcher",2)
      badguys[i]:outfitAdd("Razor Turret MK2")
      badguys[i]:outfitAdd("Razor Turret MK1",2)

      badguys[i]:outfitAdd("Shield Capacitor II")
      badguys[i]:outfitAdd("Reactor Class I",2)

      badguys[i]:setHealth(100,100)
      badguys[i]:setEnergy(100)

      hook.pilot( badguys[i], "death", "CorvetteDead")
   end
end

function cruiser()
   --spawning a Kestrel with massive missile weaponry
   if mem.nCruiser == 1 then
      local origin = pilot.choosePoint( faction.get("Mercenary") )

      local badguy = pilot.add( "Kestrel", "Mercenary", origin, _("Mercenary") )
      badguy:setHostile()

      badguy:outfitRm("all")
      badguy:outfitRm("cores")

      badguy:outfitAdd("Unicorp D-48 Heavy Plating")
      badguy:outfitAdd("Unicorp PT-500 Core System")
      badguy:outfitAdd("Krain Remige Engine")

      badguy:outfitAdd("Heavy Laser Turret",2)
      badguy:outfitAdd("Unicorp Headhunter Launcher",4)

      badguy:outfitAdd("Pinpoint Combat AI")
      badguy:outfitAdd("Photo-Voltaic Nanobot Coating")
      badguy:outfitAdd("Reactor Class III")
      badguy:outfitAdd("Improved Stabiliser",3)
      badguy:setHealth(100,100)
      badguy:setEnergy(100)

      hook.pilot( badguy, "death", "CruiserDead")

      -- Escort
      if mem.nCorvett >= 1 then
         badguys[1] = pilot.add( "Admonisher", "Mercenary", origin, _("Mercenary") )
         hook.pilot( badguys[1], "death", "CorvetteDead")
         badguys[1]:setLeader(badguy)
      end
   end

end

function bombers()
   --spawning Ancestors
   local nb = rndNb( mem.nBombers )
   local nI = mem.nInterce
   local nF = mem.nFighter
   for i = 1, nb do
      local origin = pilot.choosePoint( faction.get("Mercenary") )

      badguys[i] = pilot.add( "Ancestor", "Mercenary", origin, _("Mercenary") )
      badguys[i]:setHostile()

      badguys[i]:outfitRm("all")
      badguys[i]:outfitRm("cores")

      badguys[i]:outfitAdd("Unicorp D-2 Light Plating")
      badguys[i]:outfitAdd("Unicorp PT-68 Core System")
      badguys[i]:outfitAdd("Tricon Zephyr II Engine")

      badguys[i]:outfitAdd("Unicorp Caesar IV Launcher",2)
      badguys[i]:outfitAdd("Gauss Gun",2)

      badguys[i]:outfitAdd("Reactor Class I")

      badguys[i]:setHealth(100,100)
      badguys[i]:setEnergy(100)

      hook.pilot( badguys[i], "death", "BomberDead")

      -- Escort
      if nI >= 1 then
         badguys[nb+i] = pilot.add( "Hyena", "Mercenary", origin, _("Mercenary") )
         hook.pilot( badguys[nb+i], "death", "InterceptorDead")
         badguys[nb+i]:setLeader(badguys[i])
         nI = nI - 1
      elseif nF >= 1 then
         badguys[nb+i] = pilot.add( "Lancelot", "Mercenary", origin, _("Mercenary") )
         hook.pilot( badguys[nb+i], "death", "FighterDead")
         badguys[nb+i]:setLeader(badguys[i])
         nF = nF - 1
      end
   end
end

function add_llama()
   --adding an useless Llama
   if mem.nLlamas == 1 then
      local useless = pilot.add( "Llama", "Mercenary", nil, _("Amateur Mercenary") )
      useless:setHostile()
      hook.pilot( useless, "death", "LlamaDead")
   end
end

-- Random utility
function rndNb( nmax )
   if nmax <= 1 then
      return nmax
   else
      local nmin = math.floor(nmax/2)
      return rnd.rnd( nmin, nmax )
   end
end

-- Death functions: decrement enemy numbers
function LlamaDead()
   mem.nLlamas = mem.nLlamas - 1
end
function BomberDead()
   mem.nBombers = mem.nBombers - 1
end
function CruiserDead()
   mem.nCruiser = mem.nCruiser - 1
end
function CorvetteDead()
   mem.nCorvett = mem.nCorvett - 1
end
function FighterDead()
   mem.nFighter = mem.nFighter - 1
end
function InterceptorDead()
   mem.nInterce = mem.nInterce - 1
end

function abort()
   if mem.stage == 1 then
      misn.cargoRm(mem.records)
   end
   misn.finish(false)
end
