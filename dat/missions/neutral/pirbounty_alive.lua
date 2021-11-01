--[[
<?xml version='1.0' encoding='utf8'?>
<mission name="Alive Bounty">
 <avail>
  <priority>4</priority>
  <cond>player.numOutfit("Mercenary License") &gt; 0</cond>
  <chance>360</chance>
  <location>Computer</location>
  <faction>Empire</faction>
  <faction>Frontier</faction>
  <faction>Goddard</faction>
  <faction>Independent</faction>
  <faction>Sirius</faction>
  <faction>Soromid</faction>
  <faction>Za'lek</faction>
 </avail>
 <notes>
  <tier>3</tier>
 </notes>
</mission>
--]]
--[[

   Alive Pirate Bounty

   Bounty mission where you must capture the target alive.
   Can work with any faction.

--]]
local fmt = require "format"
local vntk = require "vntk"
require "missions.neutral.pirbounty_dead"

kill_instead_text    = {}
kill_instead_text[1] = _([[As you return to your ship, you are contacted by an officer. "I see you were unable to capture {plt}," the officer says. "Disappointing. However, we would rather this pirate be dead than roaming free, so you will be paid {credits} if you finish them off right now."]])
kill_instead_text[2] = _([[On your way back to your ship, you receive a message from an officer. It reads, "Your failure to capture {plt} is disappointing. We really wanted to capture this pirate alive. However, we would rather he be dead than roaming free, so if you kill the pirate now, you will be paid the lesser sum of {credits}."]])
kill_instead_text[3] = _([[When you return to your cockpit, you are contacted by an officer. "Pathetic! If I were in charge, I'd say you get no bounty! Can't fight off a couple low-life pirates?!" He sighs. "But lucky for you, I'm not in charge, and my higher-ups would rather {plt} be dead than free. So if you finish that scum off, you'll get {credits}. Just be snappy about it!" Having finished delivering the message, the officer then ceases communication.]])
kill_instead_text[4] = _([[When you get back to the ship, you see a message giving you a new mission to kill {plt}; the reward is {credits}. Well, that's pitiful compared to what you were planning on collecting, but it's better than nothing.]])

pay_capture_text    = {}
pay_capture_text[1] = _("An officer takes {plt} into custody and hands you your pay.")
pay_capture_text[2] = _("The officer seems to think your acceptance of the alive bounty for {plt} was foolish. They carefully take the pirate off your hands, taking precautions you think are completely unnecessary, and then hand you your pay.")
pay_capture_text[3] = _("The officer you deal with seems to especially dislike {plt}. They take the pirate off your hands and hand you your pay without speaking a word.")
pay_capture_text[4] = _("A fearful-looking officer rushes {plt} into a secure hold, pays you the appropriate bounty, and then hurries off.")
pay_capture_text[5] = _("The officer you deal with thanks you profusely for capturing {plt} alive, pays you, and sends you off.")
pay_capture_text[6] = _("Upon learning that you managed to capture {plt} alive, the officer who previously sported a defeated look suddenly brightens up. The pirate is swiftly taken into custody as you are handed your pay.")
pay_capture_text[7] = _("When you ask the officer for your bounty on {plt}, they sigh, take the pirate into custody, go through some paperwork, and hand you your pay, mumbling something about how useless capturing pirates alive is.")

pay_kill_text    = {}
pay_kill_text[1] = _("After verifying that you killed {plt}, an officer hands you your pay.")
pay_kill_text[2] = _("After verifying that {plt} is indeed dead, the officer sighs and hands you your pay.")
pay_kill_text[3] = _("This officer is clearly annoyed that {plt} is dead. They mumble something about incompetent bounty hunters the entire time as they takes care of the paperwork and hand you your bounty.")
pay_kill_text[4] = _("The officer seems disappointed, yet unsurprised that you failed to capture {plt} alive. You are handed your lesser bounty without a word.")
pay_kill_text[5] = _("When you ask the officer for your bounty on {plt}, they sigh, lead you into an office, go through some paperwork, and hand you your pay, mumbling something about how useless bounty hunters are.")
pay_kill_text[6] = _("The officer verifies the death of {plt}, goes through the necessary paperwork, and hands you your pay, looking annoyed the entire time.")

misn_title = {}
misn_title[1] = _("Tiny Alive Bounty in {sys}")
misn_title[2] = _("Small Alive Bounty in {sys}")
misn_title[3] = _("Moderate Alive Bounty in {sys}")
misn_title[4] = _("High Alive Bounty in {sys}")
misn_title[5] = _("Dangerous Alive Bounty in {sys}")
misn_desc   = _("The pirate known as {pirname} was recently seen in the {sys} system. {fct} authorities want this pirate alive. {pirname} is believed to be flying a {shipclass}-class ship.")

osd_msg[2] = _("Capture {plt}")

function pilot_death ()
   if board_failed then
      succeed()
      target_killed = true
   else
      fail( fmt.f( _("MISSION FAILURE! {plt} has been killed."), {plt=name} ) )
   end
end


local _bounty_setup = bounty_setup -- Store original one
-- Set up the ship, credits, and reputation based on the level.
function bounty_setup ()
   local pship, credits, reputation = _bounty_setup()
   credits = credits * 2
   reputation = reputation * 1.5
   return pship, credits, reputation
end


function board_fail ()
   if rnd.rnd() < 0.25 then
      board_failed = true
      credits = credits / 5
      local t = fmt.f( kill_instead_text[ rnd.rnd( 1, #kill_instead_text ) ],
         {plt=name, credits=fmt.credits(credits)} )
      vntk.msg( _("Better Dead than Free"), t )
      osd_msg[2] = fmt.f( _("Kill {plt}"), {plt=name} )
      misn.osdCreate( osd_title, osd_msg )
      misn.osdActive( 2 )
   end
end
