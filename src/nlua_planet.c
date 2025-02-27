/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file nlua_planet.c
 *
 * @brief Lua planet module.
 */
/** @cond */
#include "naev.h"

#include <lauxlib.h>
/** @endcond */

#include "nlua_planet.h"

#include "array.h"
#include "land.h"
#include "land_outfits.h"
#include "log.h"
#include "map.h"
#include "nlua_col.h"
#include "nlua_commodity.h"
#include "nlua_faction.h"
#include "nlua_outfit.h"
#include "nlua_ship.h"
#include "nlua_system.h"
#include "nlua_tex.h"
#include "nlua_time.h"
#include "nlua_vec2.h"
#include "nluadef.h"
#include "nmath.h"
#include "nstring.h"
#include "rng.h"

/* Planet metatable methods */
static int planetL_cur( lua_State *L );
static int planetL_get( lua_State *L );
static int planetL_getS( lua_State *L );
static int planetL_getLandable( lua_State *L );
static int planetL_getAll( lua_State *L );
static int planetL_system( lua_State *L );
static int planetL_eq( lua_State *L );
static int planetL_name( lua_State *L );
static int planetL_nameRaw( lua_State *L );
static int planetL_radius( lua_State *L );
static int planetL_faction( lua_State *L );
static int planetL_colour( lua_State *L );
static int planetL_class( lua_State *L );
static int planetL_classLong( lua_State *L );
static int planetL_position( lua_State *L );
static int planetL_services( lua_State *L );
static int planetL_flags( lua_State *L );
static int planetL_canland( lua_State *L );
static int planetL_landOverride( lua_State *L );
static int planetL_getLandOverride( lua_State *L );
static int planetL_gfxSpace( lua_State *L );
static int planetL_gfxExterior( lua_State *L );
static int planetL_shipsSold( lua_State *L );
static int planetL_outfitsSold( lua_State *L );
static int planetL_commoditiesSold( lua_State *L );
static int planetL_isBlackMarket( lua_State *L );
static int planetL_isRestricted( lua_State *L );
static int planetL_isKnown( lua_State *L );
static int planetL_setKnown( lua_State *L );
static int planetL_recordCommodityPriceAtTime( lua_State *L );
static int planetL_tags( lua_State *L );
static const luaL_Reg planet_methods[] = {
   { "cur", planetL_cur },
   { "get", planetL_get },
   { "getS", planetL_getS },
   { "getLandable", planetL_getLandable },
   { "getAll", planetL_getAll },
   { "system", planetL_system },
   { "__eq", planetL_eq },
   { "__tostring", planetL_name },
   { "name", planetL_name },
   { "nameRaw", planetL_nameRaw },
   { "radius", planetL_radius },
   { "faction", planetL_faction },
   { "colour", planetL_colour },
   { "class", planetL_class },
   { "classLong", planetL_classLong },
   { "pos", planetL_position },
   { "services", planetL_services },
   { "flags", planetL_flags },
   { "canLand", planetL_canland },
   { "landOverride", planetL_landOverride },
   { "getLandOverride", planetL_getLandOverride },
   { "gfxSpace", planetL_gfxSpace },
   { "gfxExterior", planetL_gfxExterior },
   { "shipsSold", planetL_shipsSold },
   { "outfitsSold", planetL_outfitsSold },
   { "commoditiesSold", planetL_commoditiesSold },
   { "blackmarket", planetL_isBlackMarket },
   { "restricted", planetL_isRestricted },
   { "known", planetL_isKnown },
   { "setKnown", planetL_setKnown },
   { "recordCommodityPriceAtTime", planetL_recordCommodityPriceAtTime },
   { "tags", planetL_tags },
   {0,0}
}; /**< Planet metatable methods. */

/**
 * @brief Loads the planet library.
 *
 *    @param env Environment to load planet library into.
 *    @return 0 on success.
 */
int nlua_loadPlanet( nlua_env env )
{
   nlua_register(env, PLANET_METATABLE, planet_methods, 1);
   return 0; /* No error */
}

/**
 * @brief This module allows you to handle the planets from Lua.
 *
 * Generally you do something like:
 *
 * @code
 * p,s = planet.get() -- Get current planet and system
 * if p:services()["inhabited"] > 0 then -- planet is inhabited
 *    v = p:pos() -- Get the position
 *    -- Do other stuff
 * end
 * @endcode
 *
 * @luamod planet
 */
/**
 * @brief Gets planet at index.
 *
 *    @param L Lua state to get planet from.
 *    @param ind Index position to find the planet.
 *    @return Planet found at the index in the state.
 */
LuaPlanet lua_toplanet( lua_State *L, int ind )
{
   return *((LuaPlanet*) lua_touserdata(L,ind));
}
/**
 * @brief Gets planet at index raising an error if isn't a planet.
 *
 *    @param L Lua state to get planet from.
 *    @param ind Index position to find the planet.
 *    @return Planet found at the index in the state.
 */
LuaPlanet luaL_checkplanet( lua_State *L, int ind )
{
   if (lua_isplanet(L,ind))
      return lua_toplanet(L,ind);
   luaL_typerror(L, ind, PLANET_METATABLE);
   return 0;
}
/**
 * @brief Gets a planet directly.
 *
 *    @param L Lua state to get planet from.
 *    @param ind Index position to find the planet.
 *    @return Planet found at the index in the state.
 */
Planet* luaL_validplanet( lua_State *L, int ind )
{
   LuaPlanet lp;
   Planet *p;

   if (lua_isplanet(L, ind)) {
      lp = luaL_checkplanet(L, ind);
      p  = planet_getIndex(lp);
   }
   else if (lua_isstring(L, ind))
      p = planet_get( lua_tostring(L, ind) );
   else {
      luaL_typerror(L, ind, PLANET_METATABLE);
      return NULL;
   }

   if (p == NULL)
      NLUA_ERROR(L, _("Planet is invalid"));

   return p;
}
/**
 * @brief Pushes a planet on the stack.
 *
 *    @param L Lua state to push planet into.
 *    @param planet Planet to push.
 *    @return Newly pushed planet.
 */
LuaPlanet* lua_pushplanet( lua_State *L, LuaPlanet planet )
{
   LuaPlanet *p;
   p = (LuaPlanet*) lua_newuserdata(L, sizeof(LuaPlanet));
   *p = planet;
   luaL_getmetatable(L, PLANET_METATABLE);
   lua_setmetatable(L, -2);
   return p;
}
/**
 * @brief Checks to see if ind is a planet.
 *
 *    @param L Lua state to check.
 *    @param ind Index position to check.
 *    @return 1 if ind is a planet.
 */
int lua_isplanet( lua_State *L, int ind )
{
   int ret;

   if (lua_getmetatable(L,ind)==0)
      return 0;
   lua_getfield(L, LUA_REGISTRYINDEX, PLANET_METATABLE);

   ret = 0;
   if (lua_rawequal(L, -1, -2))  /* does it have the correct mt? */
      ret = 1;

   lua_pop(L, 2);  /* remove both metatables */
   return ret;
}

/**
 * @brief Gets the current planet - MUST BE LANDED.
 *
 * @usage p,s = planet.cur() -- Gets current planet (assuming landed)
 *
 *    @luatreturn Planet The planet the player is landed on.
 *    @luatreturn System The system it is in.
 * @luafunc cur
 */
static int planetL_cur( lua_State *L )
{
   LuaSystem sys;
   if (land_planet == NULL) {
      NLUA_ERROR(L,_("Attempting to get landed planet when player not landed."));
      return 0; /* Not landed. */
   }
   lua_pushplanet(L,planet_index(land_planet));
   sys = system_index( system_get( planet_getSystem(land_planet->name) ) );
   lua_pushsystem(L,sys);
   return 2;
}

static int planetL_getBackend( lua_State *L, int system, int landable )
{
   int *factions;
   char **planets;
   const char *rndplanet;
   Planet *pnt;

   rndplanet = NULL;
   planets   = NULL;

   /* If boolean return random. */
   if (lua_isboolean(L,1)) {
      pnt = planet_get( space_getRndPlanet(landable, 0, NULL) );
      rndplanet = pnt->name;
   }

   /* Get a planet by faction */
   else if (lua_isfaction(L,1)) {
      factions = array_create( int );
      array_push_back( &factions, lua_tofaction(L,1) );
      planets  = space_getFactionPlanet( factions, landable );
      array_free( factions );
   }

   /* Get a planet by name */
   else if (lua_isstring(L,1)) {
      rndplanet = lua_tostring(L,1);

      if (landable) {
         pnt = planet_get( rndplanet );
         if (pnt == NULL) {
            NLUA_ERROR(L, _("Planet '%s' not found in stack"), rndplanet);
            return 0;
         }

         /* Check if can land. */
         planet_updateLand( pnt );
         if (!pnt->can_land)
            return 0;
      }
   }

   /* Get a planet from faction list */
   else if (lua_istable(L,1)) {
      /* Get table length and preallocate. */
      factions = array_create_size( int, lua_objlen(L,1) );
      /* Load up the table. */
      lua_pushnil(L);
      while (lua_next(L, -2) != 0) {
         if (lua_isfaction(L, -1))
            array_push_back( &factions, lua_tofaction(L, -1) );
         lua_pop(L,1);
      }

      /* get the planets */
      planets = space_getFactionPlanet( factions, landable );
      array_free(factions);
   }

   /* Just get a planet. */
   else if (lua_isplanet(L,1)) {
      pnt = luaL_validplanet( L, 1 );
      if (landable) {
         /* Check if can land. */
         planet_updateLand( pnt );
         if (!pnt->can_land)
            return 0;
      }
      rndplanet = pnt->name;
   }

   else
      NLUA_INVALID_PARAMETER(L); /* Bad Parameter */

   /* Pick random planet */
   if (rndplanet == NULL) {
      arrayShuffle( (void**)planets );

      for (int i=0; i<array_size(planets); i++) {
         if (landable) {
            /* Check landing. */
            pnt = planet_get( planets[i] );
            if (pnt == NULL)
               continue;

            planet_updateLand( pnt );
            if (!pnt->can_land)
               continue;
         }

         rndplanet = planets[i];
         break;
      }
   }
   array_free(planets);

   /* No suitable planet found */
   if (rndplanet == NULL && array_size( planets ) == 0)
      return 0;

   /* Push the planet */
   pnt = planet_get(rndplanet); /* The real planet */
   if (pnt == NULL) {
      NLUA_ERROR(L, _("Planet '%s' not found in stack"), rndplanet);
      return 0;
   }
   lua_pushplanet(L,planet_index( pnt ));
   if (system) {
      LuaSystem sys;
      const char *sysname = planet_getSystem( rndplanet );
      if (sysname == NULL)
         return 1;
      sys = system_index( system_get( sysname ) );
      lua_pushsystem( L, sys );
      return 2;
   }
   return 1;
}

/**
 * @brief Gets a planet.
 *
 * Possible values of param: <br/>
 *    - bool : Gets a random planet. <br/>
 *    - faction : Gets random planet belonging to faction matching the number. <br/>
 *    - string : Gets the planet by raw (untranslated) name. <br/>
 *    - table : Gets random planet belonging to any of the factions in the
 *               table. <br/>
 *
 * @usage p = planet.get( "Anecu" ) -- Gets planet by name
 * @usage p = planet.get( faction.get( "Empire" ) ) -- Gets random Empire planet
 * @usage p = planet.get(true) -- Gets completely random planet
 * @usage p = planet.get( { faction.get("Empire"), faction.get("Dvaered") } ) -- Random planet belonging to Empire or Dvaered
 *    @luatparam boolean|Faction|string|table param See description.
 *    @luatreturn Planet The matching planet.
 * @luafunc get
 */
static int planetL_get( lua_State *L )
{
   return planetL_getBackend( L, 0, 0 );
}

/**
 * @brief Gets a planet and its corresponding system.
 *
 * Possible values of param: <br/>
 *    - bool : Gets a random planet. <br/>
 *    - faction : Gets random planet belonging to faction matching the number. <br/>
 *    - string : Gets the planet by raw (untranslated) name. <br/>
 *    - table : Gets random planet belonging to any of the factions in the
 *               table. <br/>
 *
 * @usage p,s = planet.get( "Anecu" ) -- Gets planet by name
 * @usage p,s = planet.get( faction.get( "Empire" ) ) -- Gets random Empire planet
 * @usage p,s = planet.get(true) -- Gets completely random planet
 * @usage p,s = planet.get( { faction.get("Empire"), faction.get("Dvaered") } ) -- Random planet belonging to Empire or Dvaered
 *    @luatparam boolean|Faction|string|table param See description.
 *    @luatreturn Planet The matching planet.
 *    @luatreturn System The system it is in.
 * @luafunc getS
 */
static int planetL_getS( lua_State *L )
{
   return planetL_getBackend( L, 1, 0 );
}

/**
 * @brief Gets a planet only if it's landable.
 *
 * It works exactly the same as planet.get(), but it can only return landable
 * planets. So if the target is not landable it returns nil.
 *
 *    @luatparam boolean|Faction|string|table param See planet.get() description.
 *    @luatreturn Planet The matching planet, if it is landable.
 *    @luatreturn System The system it is in.
 * @luafunc getLandable
 */
static int planetL_getLandable( lua_State *L )
{
   return planetL_getBackend( L, 1, 1 );
}

/**
 * @brief Gets all the planets.
 *    @luatreturn {Planet,...} An ordered list of all the planets.
 * @luafunc getAll
 */
static int planetL_getAll( lua_State *L )
{
   Planet *p = planet_getAll();
   lua_newtable(L);
   for (int i=0; i<array_size(p); i++) {
      lua_pushplanet( L, planet_index( &p[i] ) );
      lua_rawseti( L, -2, i+1 );
   }
   return 1;
}

/**
 * @brief Gets the system corresponding to a planet.
 *    @luatparam Planet p Planet to get system of.
 *    @luatreturn System|nil The system to which the planet belongs or nil if it has none.
 * @luafunc system
 */
static int planetL_system( lua_State *L )
{
   LuaSystem sys;
   Planet *p = luaL_validplanet(L,1);
   const char *sysname = planet_getSystem( p->name );
   if (sysname == NULL)
      return 0;
   sys = system_index( system_get( sysname ) );
   lua_pushsystem( L, sys );
   return 1;
}

/**
 * @brief You can use the '==' operator within Lua to compare planets with this.
 *
 * @usage if p.__eq( planet.get( "Anecu" ) ) then -- Do something
 * @usage if p == planet.get( "Anecu" ) then -- Do something
 *    @luatparam Planet p Planet comparing.
 *    @luatparam Planet comp planet to compare against.
 *    @luatreturn boolean true if both planets are the same.
 * @luafunc __eq
 */
static int planetL_eq( lua_State *L )
{
   LuaPlanet a, b;
   a = luaL_checkplanet(L,1);
   b = luaL_checkplanet(L,2);
   lua_pushboolean(L,(a == b));
   return 1;
}

/**
 * @brief Gets the planet's translated name.
 *
 * This translated name should be used for display purposes (e.g.
 * messages). It cannot be used as an identifier for the planet; for
 * that, use planet.nameRaw() instead.
 *
 * @usage name = p:name() -- Equivalent to `_(p:nameRaw())`
 *    @luatparam Planet p Planet to get the translated name of.
 *    @luatreturn string The translated name of the planet.
 * @luafunc name
 */
static int planetL_name( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushstring(L, _(p->name));
   return 1;
}

/**
 * @brief Gets the planet's raw (untranslated) name.
 *
 * This untranslated name should be used for identification purposes
 * (e.g. can be passed to planet.get()). It should not be used directly
 * for display purposes without manually translating it with _().
 *
 * @usage name = p:nameRaw()
 *    @luatparam Planet p Planet to get the raw name of.
 *    @luatreturn string The raw name of the planet.
 * @luafunc nameRaw
 */
static int planetL_nameRaw( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushstring(L, p->name);
   return 1;
}

/**
 * @brief Gets the planet's radius.
 *
 * @usage radius = p:radius()
 *    @luatparam Planet p Planet to get the radius of.
 *    @luatreturn number The planet's graphics radius.
 * @luafunc radius
 */
static int planetL_radius( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   planet_gfxLoad(p);  /* Ensure graphics measurements are available. */
   lua_pushnumber(L,p->radius);
   return 1;
}

/**
 * @brief Gets the planet's faction.
 *
 * @usage f = p:faction()
 *    @luatparam Planet p Planet to get the faction of.
 *    @luatreturn Faction The planet's faction, or nil if it has no faction.
 * @luafunc faction
 */
static int planetL_faction( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   if (p->presence.faction < 0)
      return 0;
   lua_pushfaction(L, p->presence.faction);
   return 1;
}

/**
 * @brief Gets a planet's colour based on its friendliness or hostility to the player.
 *
 * @usage col = p:colour()
 *
 *    @luatparam Pilot p Planet to get the colour of.
 *    @luatreturn Colour The planet's colour.
 * @luafunc colour
 */
static int planetL_colour( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   const glColour *col = planet_getColour( p );
   lua_pushcolour( L, *col );
   return 1;
}

/**
 * @brief Gets the planet's class.
 *
 * Usually classes are characters for planets and numbers
 * for stations.
 *
 * @usage c = p:class()
 *    @luatparam Planet p Planet to get the class of.
 *    @luatreturn string The class of the planet in a one char identifier.
 * @luafunc class
 */
static int planetL_class(lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushstring(L,p->class);
   return 1;
}

/**
 * @brief Gets the planet's class in long human-readable format already translated.
 *
 * @usage c = p:classLong()
 *    @luatparam Planet p Planet to get the class of.
 *    @luatreturn string The class of the planet in descriptive form such as "Pelagic".
 * @luafunc classLong
 */
static int planetL_classLong(lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushstring(L, planet_getClassName(p->class));
   return 1;
}

/**
 * @brief Checks for planet services.
 *
 * Possible services are:<br />
 *  - "land"<br />
 *  - "inhabited"<br />
 *  - "refuel"<br />
 *  - "bar"<br />
 *  - "missions"<br />
 *  - "commodity"<br />
 *  - "outfits"<br />
 *  - "shipyard"<br />
 *  - "blackmarket"<br />
 *
 * @usage if p:services()["refuel"] then -- Planet has refuel service.
 * @usage if p:services()["shipyard"] then -- Planet has shipyard service.
 *    @luatparam Planet p Planet to get the services of.
 *    @luatreturn table Table containing all the services.
 * @luafunc services
 */
static int planetL_services( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   /* Return result in table */
   lua_newtable(L);
   /* allows syntax like foo = planet.get("foo"); if foo["bar"] then ... end */
   for (int i=1; i<PLANET_SERVICES_MAX; i<<=1) {
      if (planet_hasService(p, i)) {
         char lower[STRMAX_SHORT];
         const char *name = planet_getServiceName(i);
         size_t len = strlen(name) + 1;
         snprintf( lower, MIN(len,sizeof(lower)), "%c%s", tolower(name[0]), &name[1] );

         /* GUI depends on this returning the service name. */
         lua_pushstring(L, _(name));
         lua_setfield(L, -2, lower );
      }
   }
   return 1;
}

/**
 * @brief Checks for planet flags.
 *
 * Possible services are:<br />
 *  - "nomissionspawn"<br />
 *
 * @usage if p:flags()["nomissionspawn"] then -- Planet doesn't spawn missions
 *    @luatparam Planet p Planet to get the services of.
 *    @luatreturn table Table containing all the services.
 * @luafunc services
 */
static int planetL_flags( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_newtable(L);
   if (planet_isFlag( p, PLANET_NOMISNSPAWN )) {
      lua_pushstring(L, "nomissionspawn");
      lua_pushboolean(L, 1);
      lua_settable(L,-3);
   }
   return 1;
}

/**
 * @brief Gets whether or not the player can land on the planet (or bribe it).
 *
 * @usage can_land, can_bribe = p:canLand()
 *    @luatparam Planet p Planet to get land and bribe status of.
 *    @luatreturn boolean The land status of the planet.
 *    @luatreturn boolean The bribability status of the planet.
 * @luafunc canLand
 */
static int planetL_canland( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   planet_updateLand( p );
   lua_pushboolean( L, p->can_land );
   lua_pushboolean( L, p->bribe_price > 0 );
   return 2;
}

/**
 * @brief Lets player land on a planet no matter what. The override lasts until the player jumps or lands.
 *
 * @usage p:landOverride( true ) -- Planet can land on p now.
 *    @luatparam Planet p Planet to forcibly allow the player to land on.
 *    @luatparam[opt=false] boolean b Whether or not the player should be allowed to land, true enables, false disables override.
 * @luafunc landOverride
 */
static int planetL_landOverride( lua_State *L )
{
   NLUA_CHECKRW(L);
   Planet *p = luaL_validplanet(L,1);
   int old = p->land_override;

   p->land_override = !!lua_toboolean(L,2);

   /* If the value has changed, re-run the landing Lua next frame. */
   if (p->land_override != old)
      space_factionChange();

   return 0;
}

/**
 * @brief Gets the land override status for a planet.
 *
 * @usage if p:getLandOverride() then -- Player can definitely land.
 *    @luatparam Planet p Planet to check.
 *    @luatreturn b Whether or not the player is always allowed to land.
 * @luafunc getLandOverride
 */
static int planetL_getLandOverride( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushboolean(L, p->land_override);
   return 1;
}

/**
 * @brief Gets the position of the planet in the system.
 *
 * @usage v = p:pos()
 *    @luatparam Planet p Planet to get the position of.
 *    @luatreturn Vec2 The position of the planet in the system.
 * @luafunc pos
 */
static int planetL_position( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushvector(L, p->pos);
   return 1;
}

/**
 * @brief Gets the texture of the planet in space.
 *
 * @usage gfx = p:gfxSpace()
 *    @luatparam Planet p Planet to get texture of.
 *    @luatreturn Tex The space texture of the planet.
 * @luafunc gfxSpace
 */
static int planetL_gfxSpace( lua_State *L )
{
   Planet *p;
   glTexture *tex;
   p        = luaL_validplanet(L,1);
   if (p->gfx_space == NULL) { /* Not loaded. */
      /* If the planet has no texture, just return nothing. */
      if (p->gfx_spaceName == NULL)
         return 0;
      tex = gl_newImage( p->gfx_spaceName, OPENGL_TEX_MIPMAPS );
   }
   else
      tex = gl_dupTexture( p->gfx_space );
   lua_pushtex( L, tex );
   return 1;
}

/**
 * @brief Gets the texture of the planet in exterior.
 *
 * @usage gfx = p:gfxExterior()
 *    @luatparam Planet p Planet Planet to get texture of.
 *    @luatreturn Tex The exterior texture of the planet.
 * @luafunc gfxExterior
 */
static int planetL_gfxExterior( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);

   /* If no exterior image just return nothing instead of crashing. */
   if (p->gfx_exterior==NULL)
      return 0;

   lua_pushtex( L, gl_newImage( p->gfx_exterior, 0 ) );
   return 1;
}

/**
 * @brief Gets the ships sold at a planet.
 *
 *    @luatparam Planet p Planet to get ships sold at.
 *    @luatreturn {Ship,...} An ordered table containing all the ships sold (empty if none sold).
 * @luafunc shipsSold
 */
static int planetL_shipsSold( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   Ship **s = tech_getShip( p->tech );

   /* Push results in a table. */
   lua_newtable(L);
   for (int i=0; i<array_size(s); i++) {
      lua_pushship(L,s[i]); /* value = LuaShip */
      lua_rawseti(L,-2,i+1); /* store the value in the table */
   }

   array_free(s);
   return 1;
}

/**
 * @brief Gets the outfits sold at a planet.
 *
 *    @luatparam Planet p Planet to get outfits sold at.
 *    @luatreturn {Outfit,...} An ordered table containing all the outfits sold (empty if none sold).
 * @luafunc outfitsSold
 */
static int planetL_outfitsSold( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   Outfit **o = tech_getOutfit( p->tech );

   /* Push results in a table. */
   lua_newtable(L);
   for (int i=0; i<array_size(o); i++) {
      lua_pushoutfit(L,o[i]); /* value = LuaOutfit */
      lua_rawseti(L,-2,i+1); /* store the value in the table */
   }

   array_free(o);
   return 1;
}

/**
 * @brief Gets the commodities sold at a planet.
 *
 *    @luatparam Pilot p Planet to get commodities sold at.
 *    @luatreturn {Commodity,...} An ordered table containing all the commodities sold (empty if none sold).
 * @luafunc commoditiesSold
 */
static int planetL_commoditiesSold( lua_State *L )
{
   Planet *p;
   int i;

   /* Get result and tech. */
   p = luaL_validplanet(L,1);

   /* Push results in a table. */
   lua_newtable(L);
   for (i=0; i<array_size(p->commodities); i++) {
      lua_pushcommodity(L,p->commodities[i]); /* value = LuaCommodity */
      lua_rawseti(L,-2,i+1); /* store the value in the table */
   }

   return 1;
}

/**
 * @brief Checks to see if a planet is a black market.
 *
 * @usage b = p:blackmarket()
 *
 *    @luatparam Planet p Planet to check if it's a black market.
 *    @luatreturn boolean true if the planet is a black market.
 * @luafunc blackmarket
 */
static int planetL_isBlackMarket( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushboolean(L, planet_hasService(p, PLANET_SERVICE_BLACKMARKET));
   return 1;
}

/**
 * @brief Checks to see if a planet is restricted (has complicated land condition).
 *
 * @usage b = p:restricted()
 *
 *    @luatparam Planet p Planet to check if it's restricted.
 *    @luatreturn boolean true if the planet is restricted.
 * @luafunc restricted
 */
static int planetL_isRestricted( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushboolean(L, p->land_func != NULL);
   return 1;
}

/**
 * @brief Checks to see if a planet is known by the player.
 *
 * @usage b = p:known()
 *
 *    @luatparam Planet p Planet to check if the player knows.
 *    @luatreturn boolean true if the player knows the planet.
 * @luafunc known
 */
static int planetL_isKnown( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_pushboolean(L, planet_isKnown(p));
   return 1;
}

/**
 * @brief Sets a planets's known state.
 *
 * @usage p:setKnown( false ) -- Makes planet unknown.
 *    @luatparam Planet p Planet to set known.
 *    @luatparam[opt=false] boolean b Whether or not to set as known.
 * @luafunc setKnown
 */
static int planetL_setKnown( lua_State *L )
{
   int b, changed;
   Planet *p;

   NLUA_CHECKRW(L);

   p = luaL_validplanet(L,1);
   b = lua_toboolean(L, 2);

   changed = (b != (int)planet_isKnown(p));

   if (b)
      planet_setKnown( p );
   else
      planet_rmFlag( p, PLANET_KNOWN );

   /* Update outfits image array. */
   if (changed)
      outfits_updateEquipmentOutfits();

   return 0;
}

/**
 * @brief Records commodity prices at a given time, adding to players stats.
 *
 * @usage p:recordCommodityPriceAtTime( t )
 *    @luatparam Planet p Planet to record prices at
 *    @luatparam ntime_t t Time at which to record prices.
 * @luafunc recordCommodityPriceAtTime
 */
static int planetL_recordCommodityPriceAtTime( lua_State *L )
{
   NLUA_CHECKRW(L);
   Planet *p = luaL_validplanet(L,1);
   ntime_t t = luaL_validtime(L, 2);
   planet_averageSeenPricesAtTime( p, t );
   return 0;
}

/**
 * @brief Gets the planet tags.
 *
 * @usage if planet.cur():tags["fancy"] then -- Has "fancy" tag
 *
 *    @luatparam Planet p Planet to get tags of.
 *    @luatreturn table Table of tags where the name is the key and true is the value.
 * @luafunc tags
 */
static int planetL_tags( lua_State *L )
{
   Planet *p = luaL_validplanet(L,1);
   lua_newtable(L);
   for (int i=0; i<array_size(p->tags); i++) {
      lua_pushstring(L,p->tags[i]);
      lua_pushboolean(L,1);
      lua_rawset(L,-3);
   }
   return 1;
}
