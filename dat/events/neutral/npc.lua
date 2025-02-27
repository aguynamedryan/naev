--[[
<?xml version='1.0' encoding='utf8'?>
<event name="Spaceport Bar NPC">
 <trigger>land</trigger>
 <chance>100</chance>
</event>
--]]

--[[
-- Event for creating random characters in the spaceport bar.
-- The random NPCs will tell the player things about the Naev universe in general, about their faction, or about the game itself.
--]]
local tut = require "common.tutorial"
local portrait = require "portrait"
local vn = require 'vn'
local fmt = require "format"

-- Chance of a jump point message showing up. As this gradually goes
-- down, it is replaced by lore messages. See spawnNPC function.
local jm_chance_min = 0
local jm_chance_max = 0.25
-- State. Nothing persists.
local jm_chance, msg_combined, npcs, seltargets

-- luacheck: globals leave (Hook functions passed by name)
-- luacheck: globals talkNPC (NPC functions passed by name)

-- Factions which will NOT get generic texts if possible.  Factions
-- listed here not spawn generic civilian NPCs or get aftercare texts.
-- Meant for factions which are either criminal (FLF, Pirate) or unaware
-- of the main universe (Thurion, Proteron).
local nongeneric_factions = {
   "Pirate",
   "Marauder",
   "Wild Ones",
   "Raven Clan",
   "Dreamer Clan",
   "Black Lotus",
   "FLF",
   "Thurion",
   "Proteron"
}

-- List to treat special factions differently
local override_list = {
   -- Treat pirate clans the same (at least for now)
   ["Wild Ones"] = "Pirate",
   ["Raven Clan"] = "Pirate",
   ["Dreamer Clan"] = "Pirate",
   ["Black Lotus"] = "Pirate",
   ["Marauder"] = "Pirate",
}

-- Civilian descriptions for the spaceport bar.
-- These descriptions will be picked at random, and may be picked multiple times in one generation.
-- Remember that any description can end up with any portrait, so don't make any assumptions
-- about the appearance of the NPC!
local civ_desc = {
   _("This person seems to be here to relax."),
   _("There is a civilian sitting on one of the tables."),
   _("There is a civilian sitting there, looking somewhere else."),
   _("A worker sits at one of the tables, wearing a name tag saying \"Go away\"."),
   _("A civilian sits at the bar, seemingly serious about the cocktails on offer."),
   _("A civilian wearing a shirt saying: \"Ask me about Jaegnhild\""),
   _("There is a civilian sitting in the corner."),
   _("A civilian feverishly concentrating on a fluorescing drink."),
   _("A civilian drinking alone."),
   _("This person seems friendly enough."),
   _("A civilian sitting at the bar."),
   _("This person is idly browsing a news terminal."),
   _("A worker sips a cold drink after a hard shift."),
   _("A worker slouched against the bar, nursing a drink."),
   _("This worker seems bored with everything but their drink."),
}

-- Lore messages. These come in general and factional varieties.
-- General lore messages will be said by non-faction NPCs, OR by faction NPCs if they have no factional text to say.
-- When adding factional text, make sure to add it to the table of the appropriate faction.
-- Does your faction not have a table? Then just add it. The script will find and use it if it exists.
-- Make sure you spell the faction name exactly the same as in faction.xml though!
local msg_lore = {}
msg_lore["general"] = {
   _([["I heard the nebula is haunted! My uncle Bobby told me he saw one of the ghost ships himself over in Arandon!"]]),
   _([["I don't believe in those nebula ghost stories. The people who talk about it are just trying to scare you."]]),
   _([["I heard the Soromid lost their homeworld Sorom in the Incident. Its corpse can still be found in Basel."]]),
   _([["The Soromid fly organic ships! I heard their ships can even heal themselves in flight. That's so weird."]]),
   _([["Have you seen that ship the Emperor lives on? It's huge! But if you ask me, it looks a bit like a... No, never mind."]]),
   _([["I wonder why the Sirii are all so devout? I heard they have these special priesty people walking around. I wonder what's so special about them."]]),
   _([["They say Eduard Manual Goddard is drifting in space somewhere, entombed amidst a cache of his inventions! What I wouldn't give to rummage through there..."]]),
   _([["Ah man, I lost all my money on Totoran. I love the fights they stage there, but the guys I bet on always lose. What am I doing wrong?"]]),
   _([["Don't try to fly into the inner nebula. I've known people who tried, and none of them came back."]]),
   _([["Have you heard of Captain T. Practice? He's amazing, I'm his biggest fan!"]]),
   _([["I wouldn't travel north from Alteris if I were you, unless you're a good fighter! That area of space has really gone down the drain since the Incident."]]),
   _([["Sometimes I look at the stars and wonder... are we the only sentient species in the universe?"]]),
   _([["Hey, you ever wonder why we're here?" You respond that it's one of the great mysteries of the universe. Why are we here? Are we the product of some cosmic coincidence or is there some great cosmic plan for us? You don't know, but it sometimes keeps you up at night. As you say this, the citizen stares at you incredulously. "What?? No, I mean why are we in here, in this bar?"]]),
   _([["Life is so boring here. I would love to go gamble with all the famous people at Minerva Station."]]),
}

msg_lore["Independent"] = {
   _([["We're not part of any of the galactic superpowers. We can take care of ourselves!"]]),
   _([["Sometimes I worry that our lack of a standing military leaves us vulnerable to attack. I hope nobody will get any ideas about conquering us!"]]),
}

msg_lore["Empire"] = {
   _([["Things are getting worse by the cycle. What happened to the Empire? We used to be the lords and masters over the whole galaxy!"]]),
   _([["Did you know that House Za'lek was originally a Great Project initiated by the Empire? Well, now you do! There was also a Project Proteron, but that didn't go so well."]]),
   _([["The Emperor lives on a giant supercruiser in Gamma Polaris. It's said to be the biggest ship in the galaxy! I totally want one."]]),
   _([["I'm still waiting for my pilot license application to get through. Oh well, it's only been half a cycle, I just have to be patient."]]),
   _([["Between you and me, the laws the Council passes can get really ridiculous! Most planets find creative ways of ignoring them..."]]),
   _([["Don't pay attention to the naysayers. The Empire is still strong. Have you ever seen a Peacemaker up close? I doubt any ship fielded by any other power could stand up to one."]]),
}

msg_lore["Dvaered"] = {
   _([["Our Warlord is currently fighting for control over another planet. We all support him unconditionally, of course! Of course..."]]),
   _([["My great-great-great-grandfather fought in the Dvaered Revolts! We still have the holovids he made. I'm proud to be a Dvaered!"]]),
   _([["I've got lots of civilian commendations! It's important to have commendations if you're a Dvaered."]]),
   _([["You better not mess with House Dvaered. Our military is the largest and strongest in the galaxy. Nobody can stand up to us!"]]),
   _([["House Dvaered? House? The Empire is weak and useless, we don't need them anymore! I say we declare ourselves an independent faction today. What are they going to do, subjugate us? We all know how well that went last time! Ha!"]]),
   _([["I'm thinking about joining the military. Every time I see or hear news about those rotten FLF bastards, it makes my blood boil! They should all be pounded into space dust!"]]),
   _([["FLF terrorists? I'm not too worried about them. You'll see, High Command will have smoked them out of their den soon enough, and then the Frontier will be ours."]]),
   _([["Did you know that House Dvaered was named after a hero of the revolt? At least that's what my grandparents told me."]]),
}

msg_lore["Sirius"] = {
   _([["Greetings, traveler. May Sirichana's wisdom guide you as it guides me."]]),
   _([["I once met one of the Touched in person. Well, it wasn't really a meeting, our eyes simply met... But that instant alone was awe-inspiring."]]),
   _([["They say Sirichana lives and dies like any other man, but each new Sirichana is the same as the last. How is that possible?"]]),
   _([["My cousin was called to Mutris a cycle ago... He must be in Crater City by now. And one day, he will become one of the Touched!"]]),
   _([["Some people say Sirius society is unfair because our echelons are determined by birth. But even though we are different, we are all followers of Sirichana. Spiritually, we are equal."]]),
   _([["House Sirius is officially part of the Empire, but everyone knows that's only true on paper. The Emperor has nothing to say in these systems. We follow Sirichana, and no-one else."]]),
   _([["You can easily tell the different echelons apart. Every Sirian citizen and soldier wears clothing appropriate to his or her echelon."]]),
   _([["I hope to meet one of the Touched one day!"]]),
}

msg_lore["Soromid"] = {
   _("Hello. Can I interest you in one of our galaxy famous cosmetic gene treatments? You look like you could use them..."),
   _([["Can you believe it? I was going to visit Sorom to find my roots, and then boom! It got burnt to a crisp! Even now, cycles later, I still can't believe it."]]),
   _([["Yes, it's true, our military ships are alive. Us normal folk don't get to own bioships though, we have to make do with synthetic constructs just like everyone else."]]),
   _([["Everyone knows that we Soromid altered ourselves to survive the deadly conditions on Sorom during the Great Quarantine. What you don't hear so often is that billions of us died from the therapy itself. We paid a high price for survival."]]),
   _([["Our cosmetic gene treatments are even safer now for non-Soromids, with a rate of survival of 99.4%!"]]),
   _([["We have been rebuilding and enhancing our bodies for so long, I say we've become a new species, one above human."]]),
}

msg_lore["Za'lek"] =       {
   _([["It's not easy, dancing to those scientists' tunes. They give you the most impossible tasks! Like, where am I supposed to get a triple redundant helitron converter? Honestly."]]),
   _([["The Soromids? Hah! We Za'lek are the only true scientists in this galaxy."]]),
   _([["I don't understand why we bother sending our research results to the Empire. These asshats can't understand the simplest formulas!"]]),
   _([["Do you know why many optimization algorithms require your objective function to be convex? It's not only because of the question of local minima, but also because if your function is locally concave around the current iterate, the next one will lead to a greater value of your objective function. There are still too many people who don't know this!"]]),
   _([["There are so many algorithms for solving the non-linear eigenvalues problem, I never know which one to choose. Which one do you prefer?"]]),
   _([["I recently attended a very interesting conference about the history of applied mathematics before the space age. Even in those primitive times, people used to do numerical algebra. They didn't even have quantic computers back at that time! Imagine: they had to wait for hours to solve a problem with only a dozen billion degrees of freedom!"]]),
   _([["Last time I had to solve a deconvolution problem, its condition number was so high that its inverse reached numerical zero on Octuple Precision!"]]),
   _([["I am worried about my sister. She's on trial for 'abusive self-citing' and the public prosecutor has requested a life sentence."]]),
   _([["They opened two professor positions on precision machining in Atryssa Central Manufacturing Lab, and none in Bedimann Advanced Process Lab, but everyone knows that the BAPL needs reinforcement ever since three of its professors retired last cycle. People say it's because a member of Atryssa's lab posted a positive review of the president of the Za'lek central scientific recruitment committee."]]),
   _([["Even if our labs are the best in the galaxy, other factions have their own labs as well. For example, Dvaer Prime Lab for Advanced Mace Rocket Studies used to be very successful until it was nuked by mistake by a warlord during an invasion of the planet."]]),
}

msg_lore["Thurion"] = {
   _([["Did you know that even the slightest bit of brain damage can lead to death during the upload process? That's why we're very careful to not allow our brains to be damaged, even a little."]]),
   _([["My father unfortunately hit his head when he was young, so he couldn't be safely uploaded. It's okay, though; he had a long and fulfilling life, for a non-uploaded human, that is."]]),
   _([["One great thing once you're uploaded is that you can choose to forget things you don't want to remember. My great-grandfather had a movie spoiled for him before he could watch it, so once he got uploaded, he deleted that memory and watched it with a fresh perspective. Cool, huh?"]]),
   _([["The best part of our lives is after we're uploaded, but that doesn't mean we lead boring lives before then. We have quite easy and satisfying biological lives before uploading."]]),
   _([["Being uploaded allows you to live forever, but that doesn't mean you're forced to. Any uploaded Thurion can choose to end their own life if they want, though few have chosen to do so."]]),
   _([["Uploading is a choice in our society. No one is forced to do it. It's just that, well, what kind of person would turn down the chance to live a second life on the network?"]]),
   _([["We were lucky to not get touched by the Incident. In fact, we kind of benefited from it. The nebula that resulted gave us a great cover and sealed off the Empire from us. It also got rid of those dangerous Proterons."]]),
   _([["We don't desire galactic dominance. That being said, we do want to spread our way of life to the rest of the galaxy, so that everyone can experience the joy of being uploaded."]]),
   _([["I think you're from the outside, aren't you? That's awesome! I've never met a foreigner before. What's it like outside the nebula?"]]),
   _([["We actually make occasional trips outside of the nebula, though only rarely, and we always make sure to not get discovered by the Empire."]]),
   _([["The Soromid have a rough history. Have you read up on it? First the Empire confined them to a deadly planet and doomed them to extinction. Then, when they overcame those odds, the Incident blew up their homeworld. The fact that they're still thriving now despite that is phenomenal, I must say."]]),
}

msg_lore["Proteron"] = {
   _([["Our system of government is clearly superior to all others. Nothing could be more obvious."]]),
   _([["The Incident really set back our plan for galactic dominance, but that was only temporary."]]),
   _([["We don't have time for fun and games. The whole reason we're so great is because we're more productive than any other society."]]),
   _([["We are superior, so of course we deserve control over the galaxy. It's our destiny."]]),
   _([["The Empire is weak, obsolete. That is why we must replace them."]]),
   _([["Slaves? Of course we're not slaves. Slaves are beaten and starved. We are in top shape so we can serve our country better."]]),
   _([["I can't believe the Empire continues to allow families. So primitive. Obviously, all this does is make them less productive."]]),
   _([["The exact cause of the Incident is a tightly-kept secret, but the government says it was caused by the Empire's inferiority. I would expect nothing less."]]),
   _([["I came across some heathen a few months back who claimed, get this, that we Proterons were the cause of the Incident! What slanderous nonsense. Being the perfect society we are, of course we would never cause such a massive catastrophe."]]),
}

msg_lore["Frontier"] = {
   _([["We value our autonomy. We don't want to be ruled by those Dvaered Warlords! Can't they just shoot at each other instead of threatening us? If it wasn't for the Liberation Front..."]]),
   _([["Have you studied your galactic history? The Frontier worlds were the first to be colonized by humans. That makes our worlds the oldest human settlements in the galaxy, now that Earth is gone."]]),
   _([["We have the Dvaered encroaching on our territory on one side, and the Sirius zealots on the other. Sometimes I worry that in a few decacycles, the Frontier will no longer exist."]]),
   _([["Have you visited the Frontier Museum? They've got a scale model of a First Growth colony ship on display in one of the big rooms. Even scaled down like that, it's massive! Imagine how overwhelming the real ones must have been."]]),
   _([["There are twelve true Frontier worlds, because twelve colony ships successfully completed their journey in the First Growth. But did you know that there were twenty colony ships to begin with? Eight of them never made it. Some are said to have mysteriously disappeared. I wonder what happened to them?"]]),
   _([["We don't have much here in the Frontier, other than our long history leading directly back to Earth. But I don't mind. I'm happy living here, and I wouldn't want to move anywhere else."]]),
   _([["You know the Frontier Liberation Front? They're the guerilla movement that fights for the Frontier. Not to be confused with the Liberation Front of the Frontier, the Frontier Front for Liberation, or the Liberal Frontier's Front!"]]),
}

msg_lore["FLF"] = {
   _([["I can't stand Dvaereds. I just want to wipe them all off the map. Don't you?"]]),
   _([["One of these days, we will completely rid the Frontier of Dvaered oppressors. Mark my words!"]]),
   _([["Have you ever wondered about our chances of actually winning over the Dvaereds? Sometimes I worry a little."]]),
   _([["I was in charge of a bombing run recently. The mission was a success, but I lost a lot of comrades. Oh well... this is the sacrifice we must make to resist the oppressors."]]),
   _([["What after we beat the Dvaereds, you say? Well, our work is never truly done until the Frontier is completely safe from oppression. Even if the Dvaered threat is ended, we'll still have those Sirius freaks to worry about. I don't think our job will ever end in our lifetimes."]]),
   _([["Yeah, it's true, lots of Frontier officials fund our operations. If they didn't, we'd have a really hard time landing on Frontier planets, what with the kinds of operations we perform against the Dvaereds."]]),
   _([["Yeah, some civilians die because of our efforts, but that's just a sacrifice we have to make. It's for the greater good."]]),
   _([["No, we're not terrorists. We're soldiers. True terrorists kill and destroy without purpose. Our operations do have a purpose: to drive out the Dvaered oppressors from the Frontier."]]),
   _([["Riddle me this: how can we be terrorists if the Dvaereds started it by encroaching on Frontier territory? It's the most ridiculous thing I ever heard."]]),
   _([["Well, no, the Dvaereds never actually attacked Frontier ships, but that's not the point. They have their ships in Frontier territory. What other reason could they possibly have them there for if not to set up an invasion?"]]),
}

msg_lore["Pirate"] = {
   _([["Hi mate. Money or your life! Heh heh, just messing with you."]]),
   _([["Hey, look at these new scars I got!"]]),
   _([["Have you heard of the Pirates' Code? They're more guidelines than rules..."]]),
   _([["My gran once said to me, 'Never trust a pirate.' Well, she was right! I got a pretty credit chip outta her wallet last time I saw her, and I'd do it again."]]),
   _([["I don't understand why some pirates talk like 16th-century Earth pirates even though that planet is literally dead."]]),
   _([["I may be a pirate who blows up ships and steals for a living, but that inner nebula still kind of freaks me out."]]),
   _([["Damn Empire stopped my heist a few decaperiods ago. Just wait'll they see me again..."]]),
   _([["There's a pirate clanworld I really wanted to get to, but they wouldn't let me in because I'm a 'small-time pirate'! Sometimes I think I'll never make it in this line of work..."]]),
   _([["Just caught an old mate ferrying tourists for credits. Nearly puked out my grog! Your reputation won't survive for long working for our victims."]]),
   _([["I was around before Haven was destroyed, you know! Funny times. All the pirates were panicking and the Empire was cheering thinking that we were done for. Ha! As if! It barely even made a difference. We just relocated to New Haven and resumed business as usual."]]),
   _([["Y'know, I got into this business by accident to tell the truth. But what can you do? I could get a fake ID and pretend to be someone else but I'd get caught eventually and I'd lose my fame as a pirate."]]),
   _([["One of my favorite things to do is buy a fake ID and then deliver as much contraband as I can before I get caught. It's great fun, and finding out that my identity's been discovered gives me a rush!"]]),
   _([["Back when I started out in this business all you could do was go around delivering packages for other people. Becoming a pirate was real hard back then, but I got so bored I spent several decaperiods doing it. Nowadays things are way more exciting for normies, but I don't regret my choice one bit!"]]),
   _([["Flying a real big ship is impressive, but it's still no pirate ship. I mean, I respect ya more if you're flying a Goddard than if you're flying a civilian Lancelot, but the best pirates fly the good old Pirate Kestrel!"]]),
}

msg_lore["Trader"] = {
   _([["Just another link in the Great Chain, right?"]]),
   _([["You win some, you lose some, but if you don't try you're never going to win."]]),
   _([["If you don't watch the markets then you'll be hopping between planets in a jury-rigged ship in no time."]]),
   _([["Them blimming pirates, stopping honest folk from making an honest living - it's not like we're exploiting the needy!"]]),
}

-- Gameplay tip messages.
-- ALL NPCs have a chance to say one of these lines instead of a lore message.
-- So, make sure the tips are always faction neutral.
local msg_tip = {
   _([["I heard you can set your weapons to only fire when your target is in range, or just let them fire when you pull the trigger. Sounds handy!"]]),
   fmt.f( _([["Did you know that if a planet doesn't like you, you can often bribe the spaceport operators and land anyway? Just hail the planet with {hailkey}, and click the bribe button! Careful though, it doesn't always work."]]), {hailkey=tut.getKey("hail")} ),
   _([["Many factions offer rehabilitation programs to criminals through the mission computer, giving them a chance to get back into their good graces. It can get really expensive for serious offenders though!"]]),
   _([["These new-fangled missile systems! You can't even fire them unless you get a target lock first! But the same thing goes for your opponents. You can actually make it harder for them to lock on to your ship by equipping scramblers or jammers. Scout class ships are also harder to target."]]),
   _([["You know how you can't change your ship or your equipment on some planets? Well, it seems you need an outfitter to change equipment, and a shipyard to change ships! Bet you didn't know that."]]),
   _([["Are you trading commodities? You can hold down #bctrl#0 to buy 50 of them at a time, and #bshift#0 to buy 100. And if you press them both at once, you can buy 500 at a time! You can actually do that with outfits too, but why would you want to buy 50 laser cannons?"]]),
   _([["If you're on a mission you just can't beat, you can open the information panel and abort the mission. There's no penalty for doing it, so don't hesitate to try the mission again later."]]),
   _([["Some weapons have a different effect on shields than they do on armor. Keep that in mind when equipping your ship."]]),
   _([["Afterburners can speed you up a lot, but when they get hot they don't work as well anymore. Don't use them carelessly!"]]),
   _([["There are passive outfits and active outfits. The passive ones modify your ship continuously, but the active ones only work if you turn them on. You usually can't keep an active outfit on all the time, so you need to be careful only to use it when you need it."]]),
   _([["If you're new to the galaxy, I recommend you buy a map or two. It can make exploration a bit easier."]]),
   _([["Scramblers and jammers make it harder for missiles to track you. They can be very handy if your enemies use missiles."]]),
   fmt.f( _([["If you're having trouble with overheating weapons or outfits, you can either press {cooldownkey} or double-tap {reversekey} to put your ship into Active Cooldown; that'll dissipate all heat from your ship and also refill your rocket ammunition. Careful though, your energy and shields won't recharge while you do it!"]]), {cooldownkey=tut.getKey("cooldown"), reversekey=tut.getKey("reverse")} ),
   _([["If you're having trouble shooting other ships face on, try outfitting with turrets or use an afterburner to avoid them entirely!"]]),
   _([["You know how time speeds up when Autonav is on, but then goes back to normal when enemies are around? Turns out you can't disable the return to normal speed entirely, but you can control what amount of danger triggers it. Really handy if you want to ignore enemies that aren't actually hitting you."]]),
   _([["Flying bigger ships is awesome, but it's a bit tougher than flying smaller ships. There's so much more you have to do for the same actions, time just seems to fly by faster. I guess the upside of that is that you don't notice how slow your ship is as much."]]),
   _([["I know it can be tempting to fly the big and powerful ships, but don't underestimate smaller ones! Given their simpler designs and lesser crew size, you have a lot more time to react with a smaller vessel. Some are even so simple to pilot that time seems to slow down all around you!"]]),
   _([["Mining can be an easy way to earn some extra credits, but every once in a while an asteroid will just randomly explode for no apparent reason, so you have to watch out for that. Yeah, I don't know why they do that either."]]),
   _([["Rich folk will pay extra to go on an offworld sightseeing tour in a luxury yacht. I don't get it personally; it's all the same no matter what ship you're in."]]),
   _([["Different ships should be built and piloted differently. One of the hardest lessons I learned as a pilot was to stop worrying so much about the damage my ship was taking in battle while piloting a large ship. These ships are too slow for dodging, not to mention so complicated that they reduce your reaction time, so you need to learn to just take the hits and focus your attention on firing back at your enemies."]]),
   _([["Remember that when you pilot a big ship, you perceive time passing a lot faster than you do when you pilot a small ship. It can be easy to forget just how slow these larger ships are when you're frantically trying to depressurize the exhaust valve while also configuring the capacitance array. In a way the slow speed of the ship becomes a pretty huge relief!"]]),
   _([["There's always an exception to the rule, but I wouldn't recommend using forward-facing weapons on larger ships. Large ships' slower turn rates aren't able to keep up with the dashing and dodging of smaller ships, and aiming is harder anyway what with how complex these ships are. Turrets are much better; they aim automatically and usually do a very good job!"]]),
   _([["Did you know that turrets' automatic tracking of targets is slowed down by cloaking? Well, now you do! Small ships majorly benefit from a scrambler or two; it makes it much easier to dodge those turrets on the larger ships."]]),
   _([["Don't forget to have your target selected. Even if you have forward-facing weapons, the weapons will swivel a bit to track your target. But it's absolutely essential for turreted weapons."]]),
   _([["Did you know that you can automatically follow pilot with Autonav? It's true! Just #bleft-click#0 the pilot to target them and then #bright-click#0 your target to follow! I like to use this feature for escort missions. It makes them a lot less tedious."]]),
   _([["The new aiming helper feature is awesome! Simply turn it on in your ship's weapons configuration and you get little guides telling you where you should aim to hit your target! I use it a lot."]]),
   _([["The '¤' symbol is the official galactic symbol for credits. Supposedly it comes from the currency symbol of an ancient Earth civilization. It's sometimes expressed with SI prefixes: 'k¤' for thousands of credits, 'M¤' for millions of credits, and so on."]]),
   _([["If you're piloting a medium ship, I'd recommend you invest in at least one turreted missile launcher. I had a close call a few decaperiods ago where a bomber nearly blew me to bits outside the range of my Laser Turrets. Luckily I just barely managed to escape to a nearby planet so I could escape the pilot. I've not had that problem ever since I equipped a turreted missile launcher."]]),
   _([["I've heard rumors that a pirate's reputations depends on flying pirate ships, but I think they only loathe peaceful honest work."]]),
   fmt.f(_([["These computer symbols can be confusing sometimes! I've figured it out, though: '{F}' means friendly, '{N}' means neutral, '{H}' means hostile, '{R}' means restricted, and '{U}' means uninhabited but landable. I wish someone had told me that!"]]), {F="#F+#0", N="#N~#0", H="#H!!#0", R="#R*#0", U="#I=#0"} ),
   _([["Trade Lanes are the safest bet to travel around the universe. They have many patrols to keep you safe from pirates."]]),
}

-- Jump point messages.
-- For giving the location of a jump point in the current system to the player for free.
-- All messages must contain '{jmp}', this is the name of the target system.
-- ALL NPCs have a chance to say one of these lines instead of a lore message.
-- So, make sure the tips are always faction neutral.
local msg_jmp = {
   _([["Hi there, traveler. Is your system map up to date? Just in case you didn't know already, let me give you the location of the jump from here to {jmp}. I hope that helps."]]),
   _([["Quite a lot of people who come in here complain that they don't know how to get to {jmp}. I travel there often, so I know exactly where the jump point is. Here, let me show you."]]),
   _([["So you're still getting to know about this area, huh? Tell you what, I'll give you the coordinates of the jump to {jmp}. Check your map next time you take off!"]]),
   _([["True fact, there's a direct jump from here to {jmp}. Want to know where it is? It'll cost you! Ha ha, just kidding. Here you go, I've added it to your map."]]),
   _([["There's a system just one jump away by the name of {jmp}. I can tell you where the jump point is. There, I've updated your map. Don't mention it."]]),
}

-- Mission hint messages. Each element should be a table containing the mission name and the corresponding hint.
-- ALL NPCs have a chance to say one of these lines instead of a lore message.
-- So, make sure the hints are always faction neutral.
local msg_mhint = {
   {"Shadowrun", _([["Apparently there's a woman who regularly turns up on planets in and around the Klantar system. I wonder what she's looking for?"]])},
   {"Collective Espionage 1", _([["The Empire is trying to really do something about the Collective, I hear. Who knows, maybe you can even help them out if you make it to Omega Station."]])},
   {"Hitman", _([["There are often shady characters hanging out in the Alteris system. I'd stay away from there if I were you, someone might offer you a dirty kind of job!"]])},
   {"Za'lek Shipping Delivery", _([["So there's some Za'lek scientist looking for a cargo monkey out on Niflheim in the Dohriabi system. I hear it's pretty good money."]])},
}

-- Event hint messages. Each element should be a table containing the event name and the corresponding hint.
-- Make sure the hints are always faction neutral.
local msg_ehint = {
   {"FLF/DV Derelicts", _([["The FLF and the Dvaered sometimes clash in Surano. If you go there, you might find something of interest... Or not."]])},
}

-- Mission after-care messages. Each element should be a table containing the mission name and a line of text.
-- This text will be said by NPCs once the player has completed the mission in question.
-- Make sure the messages are always faction neutral.
local msg_mdone = {
   {"Nebula Satellite", _([["Heard some reckless scientists got someone to put a satellite inside the nebula for them. I thought everyone with half a brain knew to stay out of there, but oh well."]])},
   {"Shadow Vigil", _([["Did you hear? There was some big incident during a diplomatic meeting between the Empire and the Dvaered. Nobody knows what exactly happened, but both diplomats died. Now both sides are accusing the other of foul play. Could get ugly."]])},
   {"Operation Cold Metal", _([["Hey, remember the Collective? They got wiped out! I feel so much better now that there aren't a bunch of robot ships out there to get me anymore."]])},
   {"Baron", _([["Some thieves broke into a museum on Varia and stole a holopainting! Most of the thieves were caught, but the one who carried the holopainting offworld is still at large. No leads. Damn criminals..."]])},
   {"Destroy the FLF base!", _([["The Dvaered scored a major victory against the FLF recently. They went into Sigur and blew the hidden base there to bits! I bet that was a serious setback for the FLF."]])},
}

-- Event after-care messages. Each element should be a table containing the event name and a line of text.
-- This text will be said by NPCs once the player has completed the event in question.
-- Make sure the messages are always faction neutral.
local msg_edone = {
   {"Animal trouble", _([["What? You had rodents sabotage your ship? Man, you're lucky to be alive. If it had hit the wrong power line..."]])},
   {"Naev Needs You!", _([["What do you mean, the world ended and then the creator of the universe came and fixed it? What kind of illegal substance are you on?"]])},
}

-- Returns a lore message for the given faction.
local function getLoreMessage(fac)
   -- Select the faction messages for this NPC's faction, if it exists.
   local facmsg = msg_lore[fac]
   if facmsg == nil or #facmsg == 0 then
      facmsg = msg_lore["general"]
      if facmsg == nil or #facmsg == 0 then
         evt.finish(false)
      end
   end

   -- Select a string, then remove it from the list of valid strings. This ensures all NPCs have something different to say.
   local r = rnd.rnd(1, #facmsg)
   local pick = facmsg[r]
   table.remove(facmsg, r)
   return pick
end

-- Returns a jump point message and updates jump point known status accordingly. If all jumps are known by the player, defaults to a lore message.
local function getJmpMessage(fac)
   -- Collect a table of jump points in the system the player does NOT know.
   local mytargets = {}
   seltargets = seltargets or {} -- We need to keep track of jump points NPCs will tell the player about so there are no duplicates.
   for _, j in ipairs(system.cur():jumps(true)) do
      if not j:known() and not j:hidden() and not seltargets[j] then
         table.insert(mytargets, j)
      end
   end

   if #mytargets == 0 then -- The player already knows all jumps in this system.
      return getLoreMessage(fac), nil
   end

   -- All jump messages are valid always.
   if #msg_jmp == 0 then
      return getLoreMessage(fac), nil
   end
   local retmsg =  msg_jmp[rnd.rnd(1, #msg_jmp)]
   local sel = rnd.rnd(1, #mytargets)
   local myfunc = function()
                     mytargets[sel]:setKnown(true)
                     mytargets[sel]:dest():setKnown(true, false)
                  end

   -- Don't need to remove messages from tables here, but add whatever jump point we selected to the "selected" table.
   seltargets[mytargets[sel]] = true
   return fmt.f( retmsg, {jmp=mytargets[sel]:dest()} ), myfunc
end

-- Returns a tip message.
local function getTipMessage(fac)
   -- All tip messages are valid always.
   if #msg_tip == 0 then
      return getLoreMessage(fac)
   end
   local sel = rnd.rnd(1, #msg_tip)
   local pick = msg_tip[sel]
   table.remove(msg_tip, sel)
   return pick
end

-- Returns a mission hint message, a mission after-care message, OR a lore message if no missionlikes are left.
local function getMissionLikeMessage(fac)
   if not msg_combined then
      msg_combined = {}

      -- Hints.
      -- Hint messages are only valid if the relevant mission has not been completed and is not currently active.
      for i, j in pairs(msg_mhint) do
         if not (player.misnDone(j[1]) or player.misnActive(j[1])) then
            msg_combined[#msg_combined + 1] = j[2]
         end
      end
      for i, j in pairs(msg_ehint) do
         if not(player.evtDone(j[1]) or player.evtActive(j[1])) then
            msg_combined[#msg_combined + 1] = j[2]
         end
      end

      -- After-care.
      -- After-care messages are only valid if the relevant mission has been completed.
      for i, j in pairs(msg_mdone) do
         if player.misnDone(j[1]) then
            msg_combined[#msg_combined + 1] = j[2]
         end
      end
      for i, j in pairs(msg_edone) do
         if player.evtDone(j[1]) then
            msg_combined[#msg_combined + 1] = j[2]
         end
      end
   end

   if #msg_combined == 0 then
      return getLoreMessage(fac)
   else
      -- Select a string, then remove it from the list of valid strings. This ensures all NPCs have something different to say.
      local sel = rnd.rnd(1, #msg_combined)
      local pick
      pick = msg_combined[sel]
      table.remove(msg_combined, sel)
      return pick
   end
end

-- Spawns an NPC.
local function spawnNPC()
   -- Select a faction for the NPC. NPCs may not have a specific faction.
   local npcname = _("Civilian")
   local factions = {}
   local func = nil
   for i, _ in pairs(msg_lore) do
      factions[#factions + 1] = i
   end

   local nongeneric = false

   -- Choose faction, overriding if necessary
   local f  = planet.cur():faction()
   if not f then evt.finish() end
   local of = override_list[f:nameRaw()]
   if of then f = faction.get(of) end

   local planfaction = f ~= nil and f:nameRaw() or nil
   local fac = "general"
   local sel = rnd.rnd()
   if planfaction ~= nil then
      for i, j in ipairs(nongeneric_factions) do
         if j == planfaction then
            nongeneric = true
            break
         end
      end

      if nongeneric or sel >= 0.5 then
         fac = planfaction
      end
   end

   -- Append the faction to the civilian name, unless there is no faction.
   if fac ~= "general" then
      npcname = fmt.f( _("{fct} Civilian"), {fct=_(fac)} )
   end

   -- Select a portrait
   local image = portrait.get(fac)

   -- Select a description for the civilian.
   local desc = civ_desc[rnd.rnd(1, #civ_desc)]

   -- Select what this NPC should say.
   local r = rnd.rnd()
   local msg
   if r < jm_chance then
      -- Jump point message.
      msg, func = getJmpMessage(fac)
   elseif r <= 0.55 then
      -- Lore message.
      msg = getLoreMessage(fac)
   elseif r <= 0.8 then
      -- Gameplay tip message.
      msg = getTipMessage(fac)
   else
      -- Mission hint message.
      if not nongeneric then
         msg = getMissionLikeMessage(fac)
      else
         msg = getLoreMessage(fac)
      end
   end
   local npcdata = {name = npcname, msg = msg, func = func, image = portrait.getFullPath(image)}

   local id = evt.npcAdd("talkNPC", npcname, image, desc, 10)
   npcs[id] = npcdata
end


function create()
   -- Logic to decide what to spawn, if anything.
   local cur = planet.cur()

   -- Do not spawn any NPCs on restricted assets or that don't want NPC
   local t = cur:tags()
   if t.restricted or t.nonpc then
      evt.finish(false)
   end

   jm_chance = var.peek( "npc_jm_chance" ) or jm_chance_max

   local num_npc = rnd.rnd(1, 5)
   npcs = {}
   for i = 0, num_npc do
      spawnNPC()
   end

   -- End event on takeoff.
   hook.takeoff( "leave" )
end

function talkNPC(id)
   local npcdata = npcs[id]

   if npcdata.func then
      -- Execute NPC specific code
      npcdata.func()
   end

   vn.clear()
   vn.scene()
   local npc = vn.newCharacter( npcdata.name, { image=npcdata.image } )
   vn.transition()
   npc( npcdata.msg )
   vn.run()

   -- Reduce jump message chance
   var.push( "npc_jm_chance", math.max( jm_chance - 0.025, jm_chance_min ) )
end

--[[
--    Event is over when player takes off.
--]]
function leave ()
   evt.finish()
end
