--[[
<?xml version='1.0' encoding='utf8'?>
<event name="Cinematic Dvaered/FLF battle">
 <trigger>enter</trigger>
 <chance>10</chance>
 <cond>system.cur() == system.get("Tuoladis")</cond>
 <flags>
  <unique />
 </flags>
 <notes>
  <campaign>Join the FLF</campaign>
  <tier>1</tier>
 </notes>
</event>
--]]
--[[

    This is the first of many planned eye candy cinematics.
    In this one, there will be a battle between the Dvaered and the FLF in the Tuoladis system.

]]--

local fleet = require "fleet"

local flfwave, dvaeredwave -- Non-persistent state
-- luacheck: globals DvaeredSpawn FLFSpawn leave (Hook functions passed by name)

local articles = {
{
   "Empire",
   _("Dvaered forces engaged in combat with small terrorist group"),
   _("In what the Dvaered call a 'Mighty victory', a Dvaered force was attacked and damaged by a small group of underequipped and unorganised FLF, in the Tuoladis system. The Dvaered managed to lose many tens of millions of credits worth in ships and crew to the small gang worth of ships and equipment, and the relative victory of the FLF will only embolden them, leading to more internal strife and violence."),
   time.get()+time.create(0,30,0)
},

{
   "Dvaered",
   _("FLF terrorists blasted by joint Dvaered military forces"),
   _("A Dvaered patrol in Tuoladis was ambushed by a large gang of FLF terrorists. The Dvaered patrol held  under the assault, until the mighty hammer that is the Dvaered forces came down and crushed the FLF terrorists in a mighty victory. This incident shows the willingness of the terrorists to attack us and kill whomever they please. The Dvaered military is always ready to protect its citizens and kill and conquer those who would harm them."),
   time.get()+time.create(0,30,0)
},

{
   "Frontier",
   _("Dvaered forces engage FLF freedom fighters in open combat"),
   _("A small group of FLF freedom fighters was beset by a Dvaered patrol in Tuoladis, who immediately called backup. The situation escalated, and a large scale battle occurred, where the Dvaered forces lost tens of millions of credits worth in ships. This marks the first time FLF freedom fighters have had the ships and weapons to stand toe to toe against the Dvaered, and shall server as an example for all who dare to stand for freedom, in life or death."),
   time.get()+time.create(0,30,0)
},

{
   "Independent",
   _("Dvaered, FLF clash in Tuoladis"),
   _("In a chance encounter, a Dvaered patrol encountered a group of FLF. The small skirmish quickly escalated to a large scale battle many dozens large, in which the Dvaered proclaimed to have won. The Dvaered High Command used the event as an excuse to call for military action against the frontier worlds."),
   time.get()+time.create(0,30,0)
}

}


function create ()
    pilot.clear()
    pilot.toggleSpawn(false)

    flfwave = 1
    dvaeredwave = 1
    hook.timer(3.0, "FLFSpawn")
    hook.timer(12.0, "DvaeredSpawn")

    news.add(articles)

    hook.jumpout("leave")
    hook.land("leave")
end

function FLFSpawn ()
    local source_system = system.get("Zacron")
    local ships = { "Vendetta", "Vendetta", "Vendetta", "Vendetta", "Pacifier", "Lancelot", "Lancelot" }

    fleet.add( 1, ships, "FLF", source_system )

    flfwave = flfwave + 1
    if flfwave <=5 then
        hook.timer(1.0, "FLFSpawn")
    end
end

function DvaeredSpawn ()
    local source_system = system.get("Doranthex")
    local ships = { "Dvaered Vendetta", "Dvaered Vendetta", "Dvaered Vendetta", "Dvaered Ancestor", "Dvaered Ancestor",
                    "Dvaered Vigilance", "Dvaered Vigilance", "Dvaered Goddard" }

    fleet.add( 1, ships, "Dvaered", source_system )

    dvaeredwave = dvaeredwave + 1
    if dvaeredwave <= 5 then
        hook.timer(3.0, "DvaeredSpawn")
    end
end

function leave () --event ends on player leaving the system or landing
    evt.finish()
end
