--[[

   Za'lek Common Functions

--]]
local zlk = {}

function zlk.addNebuResearchLog( text )
   shiplog.create( "zlk_neburesearch", _("Nebula Research"), _("Za'lek") )
   shiplog.append( "zlk_neburesearch", text )
end

-- Function for adding log entries for miscellaneous one-off missions.
function zlk.addMiscLog( text )
   shiplog.create( "zlk_misc", _("Miscellaneous"), _("Za'lek") )
   shiplog.append( "zlk_misc", text )
end

-- Checks to see if the player has a Za'lek ship.
function zlk.hasZalekShip()
   local shipname = player.pilot():ship():nameRaw()
   return string.find( shipname, "Za'lek" ) ~= nil
end

return zlk
