local sokoban = {}

local love           = require 'love'
local audio          = require 'love.audio'
local lg             = require 'love.graphics'
local love_shaders   = require "love_shaders"
local fmt            = require 'format'

local player            = '@'
local playerOnStorage   = '+'
local box               = '$'
local boxOnStorage      = '*'
local storage           = '.'
local wall              = '#'
local empty             = ' '
local emptyOut          = 'x'
local colours = {
--[[ original colours
   [player]          = {0.64, 0.53, 1.00},
   [playerOnStorage] = {0.62, 0.47, 1.00},
   [box]             = {1.00, 0.79, 0.49},
   [boxOnStorage]    = {0.59, 1.00, 0.50},
   [storage]         = {0.61, 0.90, 1.00},
   [wall]            = {1.00, 0.58, 0.82},
   [empty]           = {1.00, 1.00, 0.75},
--]]
   [empty]           = {0x00/0xFF, 0x00/0xFF, 0x00/0xFF}, -- darkest
   [player]          = {0x1C/0xFF, 0x30/0xFF, 0x4A/0xFF},
   [playerOnStorage] = {0x1C/0xFF, 0x30/0xFF, 0x4A/0xFF}, -- same as player
   [storage]         = {0x04/0xFF, 0x6B/0xFF, 0x99/0xFF},
   [box]             = {0x00/0xFF, 0xCF/0xFF, 0xFF/0xFF},
   [boxOnStorage]    = {0xB3/0xFF, 0xEF/0xFF, 0xFF/0xFF},
   [wall]            = {0xFF/0xFF, 0xFF/0xFF, 0xFF/0xFF}, -- lightest
}
local coloursText = {
   [empty]           = {0xFF/0xFF, 0xFF/0xFF, 0xFF/0xFF},
   [player]          = {0xFF/0xFF, 0xFF/0xFF, 0xFF/0xFF},
   [playerOnStorage] = {0xFF/0xFF, 0xFF/0xFF, 0xFF/0xFF},
   [storage]         = {0xFF/0xFF, 0xFF/0xFF, 0xFF/0xFF},
   [box]             = {0x00/0xFF, 0x00/0xFF, 0x00/0xFF},
   [boxOnStorage]    = {0x00/0xFF, 0x00/0xFF, 0x00/0xFF},
   [wall]            = {0x00/0xFF, 0x00/0xFF, 0x00/0xFF},
}
local cellSize = 30
local headersize = 80
local levels
local prevlevel, level, currentLevel
local lx, ly, lxo, lyo
local bgshader
local transition

local function loadLevel()
   local w = 0
   local h
   local lw, lh = love.window.getDesktopDimensions()
   prevlevel = level
   transition = 0
   lxo, lyo = lx, ly

   local loadlev = levels[ currentLevel ]
   h = #loadlev
   for y, row in ipairs( loadlev ) do
      w = math.max( w, #row )
   end

   -- Build up empty level
   level = {}
   for y=1,h do
      level[y] = {}
      for x=1,w do
         level[y][x] = ' '
      end
   end

   -- Load level data
   for y, row in ipairs( loadlev ) do
      for x, cell in ipairs(row) do
         level[y][x] = cell
      end
   end

   -- Remove edges
   for y=1,h do
      for x=1,w do
         if level[y][x] ~= empty then
            break
         end
         level[y][x] = emptyOut
      end
      for x=w,1,-1 do
         if level[y][x] ~= empty then
            break
         end
         level[y][x] = emptyOut
      end
   end
   for x=1,w do
      for y=1,h do
         if level[y][x] ~= empty and level[y][x] ~= emptyOut then
            break
         end
         level[y][x] = emptyOut
      end
      for y=h,1,-1 do
         if level[y][x] ~= empty and level[y][x] ~= emptyOut then
            break
         end
         level[y][x] = emptyOut
      end
   end

   -- header
   lh = lh + headersize

   lx = (lw - cellSize * w)/2
   ly = (lh - cellSize * h)/2 - 100
end

local alpha, done, headerfont, headertext, completed, standalone
local movekeys
function sokoban.load()
   local c = naev.cache()
   c.sokoban.completed = false
   standalone = c.sokoban.standalone
   local params = c.sokoban.params
   params.header = params.header or _("Sokoban")
   params.levels = params.levels or {1,2,3}
   if type(params.levels)=='number' then
      params.levels = {params.levels}
   end

   -- Load audio
   if not sokoban.sfx then
      sokoban.sfx = {
         goal     = audio.newSource( 'snd/sounds/sokoban/goal.ogg' ),
         invalid  = audio.newSource( 'snd/sounds/sokoban/invalid.ogg' ),
         level    = audio.newSource( 'snd/sounds/sokoban/level.ogg' ),
      }
   end

   -- Allow the default keybindings too!
   movekeys = {
      string.lower( naev.keyGet("left") ),
      string.lower( naev.keyGet("right") ),
      string.lower( naev.keyGet("accel") ),
      string.lower( naev.keyGet("reverse") ),
   }

   local levels_all = require "minigames.sokoban.levels"
   levels = {}
   for _k,l in ipairs(params.levels) do
      table.insert( levels, levels_all[l] )
   end

   -- Defaults
   lg.setBackgroundColor(0, 0, 0, 0)
   lg.setNewFont( 16 )
   headerfont = lg.newFont(24)
   headertext = params.header
   bgshader = love_shaders.circuit()

   alpha = 0
   currentLevel = 1
   completed = false
   done = false

   loadLevel()
end

function sokoban.keypressed( key )
   if key=="q" or key=="escape" then
      done = true

   elseif key == 'r' then
      loadLevel()

   elseif key == 'up' or key == 'down' or key == 'left' or key == 'right' or
         key == movekeys[1] or key == movekeys[2] or
         key == movekeys[3] or key == movekeys[4] then
      local playerX
      local playerY

      for testY, row in ipairs(level) do
         for testX, cell in ipairs(row) do
            if cell == player or cell == playerOnStorage then
               playerX = testX
               playerY = testY
            end
         end
      end

      local dx = 0
      local dy = 0
      if key == 'left' then
         dx = -1
      elseif key == 'right' then
         dx = 1
      elseif key == 'up' then
         dy = -1
      elseif key == 'down' then
         dy = 1
      elseif key == movekeys[1] then
         dx = -1
      elseif key == movekeys[2] then
         dx = 1
      elseif key == movekeys[3] then
         dy = -1
      elseif key == movekeys[4] then
         dy = 1
      end

      local current  = level[playerY][playerX]
      local adjacent = level[playerY + dy][playerX + dx]
      local beyond
      if level[playerY + dy + dy] then
         beyond = level[playerY + dy + dy][playerX + dx + dx]
      end

      local nextAdjacent = {
         [empty]   = player,
         [storage] = playerOnStorage,
      }

      local nextCurrent = {
         [player]          = empty,
         [playerOnStorage] = storage,
      }

      local nextBeyond = {
         [empty]   = box,
         [storage] = boxOnStorage,
      }

      local nextAdjacentPush = {
         [box]          = player,
         [boxOnStorage] = playerOnStorage,
      }

      -- Player just moved
      if nextAdjacent[adjacent] then
         level[playerY][playerX]           = nextCurrent[current]
         level[playerY + dy][playerX + dx] = nextAdjacent[adjacent]
         -- TODO play mmotion sound

      -- Player pushed something
      elseif nextBeyond[beyond] and nextAdjacentPush[adjacent] then
         level[playerY][playerX]           = nextCurrent[current]
         level[playerY + dy][playerX + dx] = nextAdjacentPush[adjacent]
         level[playerY + dy + dy][playerX + dx + dx] = nextBeyond[beyond]
         if nextBeyond[beyond] == boxOnStorage then
            sokoban.sfx.goal:play()
         end

      else
         sokoban.sfx.invalid:play()

      end

      local complete = true
      for y, row in ipairs(level) do
         for x, cell in ipairs(row) do
            if cell == box then
               complete = false
            end
         end
      end

      if complete then
         sokoban.sfx.level:play()
         currentLevel = currentLevel + 1
         if currentLevel > #levels then
            done = true
            completed = true
            naev.cache().sokoban.completed = true
            currentLevel = currentLevel-1
         else
            loadLevel()
         end
      end

   end
end

local function setcol( col )
   local r, g, b, a = table.unpack( col )
   a = a or 1
   lg.setColor( r, g, b, a*alpha )
end

local function drawLevel( cx, cy, lvl, t )
   for y, row in ipairs(lvl) do
      for x, cell in ipairs(row) do
         if cell ~= emptyOut then
            local off = cellSize * (1-t) / 2
            lg.push()
            lg.translate( cx + (x-1)*cellSize + off, cy + (y-1)*cellSize + off )
            lg.scale( t, t )
            setcol( colours[cell] )
            lg.rectangle( 'fill', 0, 0, cellSize, cellSize )
            setcol( coloursText[cell] )
            lg.print( lvl[y][x], 0, 0 )
            lg.pop()
         end
      end
   end
end

function sokoban.draw()
   local nw, nh = naev.gfx.dim()
   setcol( {0.2,0.2,0.2,0.85} )
   lg.setShader( bgshader )
   love_shaders.img:draw( 0, 0, 0, nw, nh )
   lg.setShader()

   local y, h
   if transition < 1 and prevlevel then
      local t = transition
      y = t*ly + (1-t)*lyo
      h = (t*#level + (1-t)*#prevlevel) * cellSize
   else
      y = ly
      h = #level*cellSize
   end

   setcol{ 1, 1, 1 }
   lg.printf( headertext, headerfont, 0, y, nw, "center" )
   local subheader
   if completed then
      subheader = _("Completed!")
   else
      subheader = fmt.f(_("Layer {cur} / {total}"), {cur=currentLevel, total=#levels} )
   end
   lg.printf( subheader, 0, y+40, nw, "center" )
   y = y + headersize

   drawLevel( lx, ly+headersize, level, transition )
   if transition < 1 and prevlevel then
      drawLevel( lxo, lyo+headersize, prevlevel, 1-transition )
   end

   local cy = y + h + 20
   local controls = _("#barrow keys#0: move, #br#0: restart, #bq#0: abort")
   local cw = lg.getFont():getWidth( controls ) + 10
   naev.gfx.clearDepth()
   setcol{ 0, 0, 0 }
   lg.rectangle( 'fill', (nw-cw)/2, cy, cw, 20 )
   setcol{ 1, 1, 1 }
   lg.printf( controls, 0, cy, nw, "center" )
end

function sokoban.update( dt )
   transition = math.min( 1, transition + dt * 2 )

   bgshader:update(dt)
   if done then
      alpha = alpha - dt * 2
      if alpha < 0 then
         if standalone then
            love.event.quit()
         end
         return true
      end
      return false
   end
   alpha = math.min( alpha + dt * 2, 1 )
   return false
end

return sokoban
