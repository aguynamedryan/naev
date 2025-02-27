--[[
-- Filesystem
--]]
local love = require 'love'

local filesystem = {}

function filesystem.getInfo( path, filtertype )
   local ftype = naev.file.filetype( path )
   if filtertype ~= nil and filtertype ~= ftype then
      return nil
   end
   if ftype == "directory" then
      return { type = ftype }
   elseif ftype == "file" then
      local info = { type = ftype }
      local f = naev.file.new( path )
      f:open('r')
      info.size = f:getSize()
      f:close()
      return info
   end
   return nil
end
function filesystem.newFile( filename )
   local ftype = naev.file.filetype( filename )
   if ftype == "file" then
      return naev.file.new( filename )
   end
   -- Fallback to love path
   return naev.file.new( love._basepath..filename )
end
function filesystem.read( name, size )
   local f = filesystem.newFile( name )
   local ret, err = f:open('r')
   if ret ~= true then
      return nil, err
   end
   local buf,len
   if size then
      buf,len = f:read( size )
   else
      buf,len = f:read()
   end
   f:close()
   return buf, len
end
function filesystem.enumerate( dir )
   return naev.file.enumerate( dir )
end

return filesystem
