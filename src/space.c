/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file space.c
 *
 * @brief Handles all the space stuff, namely systems and planets.
 */
/** @cond */
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include "physfs.h"

#include "naev.h"
/** @endcond */

#include "space.h"

#include "background.h"
#include "conf.h"
#include "damagetype.h"
#include "dev_uniedit.h"
#include "economy.h"
#include "gui.h"
#include "hook.h"
#include "land.h"
#include "log.h"
#include "map.h"
#include "map_overlay.h"
#include "menu.h"
#include "mission.h"
#include "music.h"
#include "ndata.h"
#include "nebula.h"
#include "nfile.h"
#include "nlua.h"
#include "nlua_pilot.h"
#include "nlua_planet.h"
#include "nluadef.h"
#include "nmath.h"
#include "nstring.h"
#include "ntime.h"
#include "nxml.h"
#include "opengl.h"
#include "pause.h"
#include "pilot.h"
#include "player.h"
#include "queue.h"
#include "rng.h"
#include "sound.h"
#include "spfx.h"
#include "toolkit.h"
#include "weapon.h"

#define XML_ASSET_TAG   "asset" /**< Individual planet xml tag. */
#define XML_SYSTEM_TAG  "ssys" /**< Individual systems xml tag. */

#define PLANET_GFX_EXTERIOR_PATH_W 400 /**< Planet exterior graphic width. */
#define PLANET_GFX_EXTERIOR_PATH_H 400 /**< Planet exterior graphic height. */

/* used to overcome warnings due to 0 values */
#define FLAG_XSET             (1<<0) /**< Set the X position value. */
#define FLAG_YSET             (1<<1) /**< Set the Y position value. */
#define FLAG_INTERFERENCESET  (1<<3) /**< Set the interference value. */
#define FLAG_SERVICESSET      (1<<4) /**< Set the service value. */
#define FLAG_FACTIONSET       (1<<5) /**< Set the faction value. */

#define DEBRIS_BUFFER         1000 /**< Buffer to smooth appearance of debris */

#define ASTEROID_EXPLODE_INTERVAL 5. /**< Interval of asteroids randomly exploding */
#define ASTEROID_EXPLODE_CHANCE   0.1 /**< Chance of asteroid exploding each interval */

/*
 * planet <-> system name stack
 */
static char** planetname_stack = NULL; /**< Planet name stack corresponding to system. */
static char** systemname_stack = NULL; /**< System name stack corresponding to planet. */

/*
 * Arrays.
 */
StarSystem *systems_stack = NULL; /**< Star system stack. */
static Planet *planet_stack = NULL; /**< Planet stack. */
static VirtualAsset *vasset_stack = NULL; /**< Virtual asset stack. */
#ifdef DEBUGGING
static int systemstack_changed = 0; /**< Whether or not the systems_stack was changed after loading. */
static int planetstack_changed = 0; /**< Whether or not the planet_stack was changed after loading. */
#endif /* DEBUGGING */

/*
 * Asteroid types stack.
 */
static AsteroidType *asteroid_types = NULL; /**< Asteroid types stack. */

/*
 * Misc.
 */
static int systems_loading = 1; /**< Systems are loading. */
StarSystem *cur_system = NULL; /**< Current star system. */
glTexture *jumppoint_gfx = NULL; /**< Jump point graphics. */
static glTexture *jumpbuoy_gfx = NULL; /**< Jump buoy graphics. */
static nlua_env landing_env = LUA_NOREF; /**< Landing lua env. */
static int space_fchg = 0; /**< Faction change counter, to avoid unnecessary calls. */
static int space_simulating = 0; /**< Are we simulating space? */
static int space_simulating_effects = 0; /**< Are we doing special effects? */
glTexture **asteroid_gfx = NULL;
static size_t nasterogfx = 0; /**< Nb of asteroid gfx. */
static Planet *space_landQueuePlanet = NULL;

/*
 * Fleet spawning.
 */
int space_spawn = 1; /**< Spawn enabled by default. */

/*
 * Internal Prototypes.
 */
/* planet load */
static int planet_parse( Planet *planet, const xmlNodePtr parent, Commodity **stdList );
static int space_parseAssets( xmlNodePtr parent, StarSystem* sys );
static int asset_parsePresence( xmlNodePtr node, AssetPresence *ap );
/* system load */
static void system_init( StarSystem *sys );
static void asteroid_init( Asteroid *ast, AsteroidAnchor *field );
static void debris_init( Debris *deb );
static int systems_load (void);
static int asteroidTypes_load (void);
static StarSystem* system_parse( StarSystem *system, const xmlNodePtr parent );
static int system_parseJumpPoint( const xmlNodePtr node, StarSystem *sys );
static int system_parseAsteroidField( const xmlNodePtr node, StarSystem *sys );
static int system_parseAsteroidExclusion( const xmlNodePtr node, StarSystem *sys );
static int system_parseJumpPointDiff( const xmlNodePtr node, StarSystem *sys );
static void system_parseJumps( const xmlNodePtr parent );
static void system_parseAsteroids( const xmlNodePtr parent, StarSystem *sys );
/* misc */
static int getPresenceIndex( StarSystem *sys, int faction );
static void system_scheduler( double dt, int init );
static void asteroid_explode ( Asteroid *a, AsteroidAnchor *field, int give_reward );
/* Markers. */
static int space_addMarkerSystem( int sysid, MissionMarkerType type );
static int space_addMarkerPlanet( int pntid, MissionMarkerType type );
static int space_rmMarkerSystem( int sysid, MissionMarkerType type );
static int space_rmMarkerPlanet( int pntid, MissionMarkerType type );
/* Render. */
static void space_renderJumpPoint( const JumpPoint *jp, int i );
static void space_renderPlanet( const Planet *p );
static void space_renderAsteroid( const Asteroid *a );
static void space_renderDebris( const Debris *d, double x, double y );
/*
 * Externed prototypes.
 */
int space_sysSave( xmlTextWriterPtr writer );
int space_sysLoad( xmlNodePtr parent );

/**
 * @brief Gets the (English) name for a service code.
 *
 * @param service One of the \p PLANET_SERVICE_* enum values.
 * @return English name, reversible via \p planet_getService()
 * and presentable via \p _().
 */
const char* planet_getServiceName( int service )
{
   switch (service) {
      case PLANET_SERVICE_LAND:        return N_("Land");
      case PLANET_SERVICE_INHABITED:   return N_("Inhabited");
      case PLANET_SERVICE_REFUEL:      return N_("Refuel");
      case PLANET_SERVICE_BAR:         return N_("Bar");
      case PLANET_SERVICE_MISSIONS:    return N_("Missions");
      case PLANET_SERVICE_COMMODITY:   return N_("Commodity");
      case PLANET_SERVICE_OUTFITS:     return N_("Outfits");
      case PLANET_SERVICE_SHIPYARD:    return N_("Shipyard");
      case PLANET_SERVICE_BLACKMARKET: return N_("Blackmarket");
   }
   return NULL;
}

/**
 * @brief Converts name to planet service flag.
 */
int planet_getService( const char *name )
{
   if (strcasecmp(name,"Land")==0)
      return PLANET_SERVICE_LAND;
   else if (strcasecmp(name,"Inhabited")==0)
      return PLANET_SERVICE_INHABITED;
   else if (strcasecmp(name,"Refuel")==0)
      return PLANET_SERVICE_REFUEL;
   else if (strcasecmp(name,"Bar")==0)
      return PLANET_SERVICE_BAR;
   else if (strcasecmp(name,"Missions")==0)
      return PLANET_SERVICE_MISSIONS;
   else if (strcasecmp(name,"Commodity")==0)
      return PLANET_SERVICE_COMMODITY;
   else if (strcasecmp(name,"Outfits")==0)
      return PLANET_SERVICE_OUTFITS;
   else if (strcasecmp(name,"Shipyard")==0)
      return PLANET_SERVICE_SHIPYARD;
   else if (strcasecmp(name,"Blackmarket")==0)
      return PLANET_SERVICE_BLACKMARKET;
   return -1;
}

/**
 * @brief Gets the long class name for a planet.
 *
 *    @param class Name of the class to process.
 *    @return Long name of the class.
 */
const char* planet_getClassName( const char *class )
{
   if (strcmp(class,"0")==0)
      return _("Civilian Station");
   else if (strcmp(class,"1")==0)
      return _("Military Station");
   else if (strcmp(class,"2")==0)
      return _("Pirate Station");
   else if (strcmp(class,"3")==0)
      return _("Robotic Station");
   else if (strcmp(class,"A")==0)
      return _("Geothermal");
   else if (strcmp(class,"B")==0)
      return _("Geomorteus");
   else if (strcmp(class,"C")==0)
      return _("Geoinactive");
   else if (strcmp(class,"D")==0)
      return _("Asteroid/Moon");
   else if (strcmp(class,"E")==0)
      return _("Geoplastic");
   else if (strcmp(class,"F")==0)
      return _("Geometallic");
   else if (strcmp(class,"G")==0)
      return _("Geocrystaline");
   else if (strcmp(class,"H")==0)
      return _("Desert");
   else if (strcmp(class,"I")==0)
      return _("Gas Supergiant");
   else if (strcmp(class,"J")==0)
      return _("Gas Giant");
   else if (strcmp(class,"K")==0)
      return _("Adaptable");
   else if (strcmp(class,"L")==0)
      return _("Marginal");
   else if (strcmp(class,"M")==0)
      return _("Terrestrial");
   else if (strcmp(class,"N")==0)
      return _("Reducing");
   else if (strcmp(class,"O")==0)
      return _("Pelagic");
   else if (strcmp(class,"P")==0)
      return _("Glaciated");
   else if (strcmp(class,"Q")==0)
      return _("Variable");
   else if (strcmp(class,"R")==0)
      return _("Rogue");
   else if (strcmp(class,"S")==0 || strcmp(class,"T")==0)
      return _("Ultragiants");
   else if (strcmp(class,"X")==0 || strcmp(class,"Y")==0 || strcmp(class,"Z")==0)
      return _("Demon");
   return class;
}

/**
 * @brief Gets the price of a commodity at a planet.
 *
 *    @param p Planet to get price at.
 *    @param c Commodity to get price of.
 */
credits_t planet_commodityPrice( const Planet *p, const Commodity *c )
{
   char *sysname = planet_getSystem( p->name );
   StarSystem *sys = system_get( sysname );
   return economy_getPrice( c, sys, p );
}

/**
 * @brief Gets the price of a commodity at a planet at given time.
 *
 *    @param p Planet to get price at.
 *    @param c Commodity to get price of.
 *    @param t Time to get price at.
 */
credits_t planet_commodityPriceAtTime( const Planet *p, const Commodity *c, ntime_t t )
{
   char *sysname = planet_getSystem( p->name );
   StarSystem *sys = system_get( sysname );
   return economy_getPriceAtTime( c, sys, p, t );
}

/**
 * @brief Adds cost of commodities on planet p to known statistics at time t.
 *
 *    @param p Planet to get price at
 *    @param tupdate Time to get prices at
 */
void planet_averageSeenPricesAtTime( const Planet *p, const ntime_t tupdate )
{
   economy_averageSeenPricesAtTime( p, tupdate );
}

/**
 * @brief Gets the average price of a commodity at a planet that has been seen so far.
 *
 *    @param p Planet to get average price at.
 *    @param c Commodity to get average price of.
 *    @param[out] mean Sample mean, rounded to nearest credit.
 *    @param[out] std Sample standard deviation (via uncorrected population formula).
 */
int planet_averagePlanetPrice( const Planet *p, const Commodity *c, credits_t *mean, double *std)
{
  return economy_getAveragePlanetPrice( c, p, mean, std );
}

/**
 * @brief Changes the planets faction.
 *
 *    @param p Planet to change faction of.
 *    @param faction Faction to change to.
 *    @return 0 on success.
 */
int planet_setFaction( Planet *p, int faction )
{
   p->presence.faction = faction;
   return 0;
}

/**
 * @brief Adds a commodity to a planet.
 *
 *    @param p Planet to add commodity to.
 *    @param c Commodity to add.
 *    @return 0 on success.
 */
int planet_addCommodity( Planet *p, Commodity *c )
{
   array_grow( &p->commodities ) = c;
   array_grow( &p->commodityPrice ).price = c->price;
   return 0;
}

/**
 * @brief Removes a service from a planet.
 *
 *    @param p Planet to remove service from.
 *    @param service Service flag to remove.
 *    @return 0 on success.
 */
int planet_addService( Planet *p, int service )
{
   p->services |= service;

   if (service & PLANET_SERVICE_COMMODITY) {
      /* Only try to add standard commodities if there aren't any. */
      if (p->commodities!=NULL)
         return 0;
      Commodity **stdList = standard_commodities();
      p->commodities = array_create( Commodity* );
      p->commodityPrice = array_create( CommodityPrice );
      for (int i=0; i<array_size(stdList); i++)
         planet_addCommodity( p, stdList[i] );
   }

   return 0;
}

/**
 * @brief Removes a service from a planet.
 *
 *    @param p Planet to remove service from.
 *    @param service Service flag to remove.
 *    @return 0 on success.
 */
int planet_rmService( Planet *p, int service )
{
   p->services &= ~service;
   return 0;
}

/**
 * @brief Checks to make sure if pilot is far enough away to hyperspace.
 *
 *    @param p Pilot to check if he can hyperspace.
 *    @return 1 if he can hyperspace, 0 else.
 */
int space_canHyperspace( const Pilot* p )
{
   double d, r;
   JumpPoint *jp;

   /* Must not have the nojump flag. */
   if (pilot_isFlag(p, PILOT_NOJUMP))
      return 0;

   /* Must have fuel. */
   if (p->fuel < p->fuel_consumption)
      return 0;

   /* Must have hyperspace target. */
   if (p->nav_hyperspace < 0)
      return 0;

   /* Get the jump. */
   jp = &cur_system->jumps[ p->nav_hyperspace ];

   /* Check distance. */
   r = jp->radius * p->stats.jump_distance;
   if (pilot_isFlag( p, PILOT_STEALTH )) /* Stealth gives a jump distance bonus. */
      r *= 3.;
   d = vect_dist2( &p->solid->pos, &jp->pos );
   if (d > pow2(r))
      return 0;
   return 1;
}

/**
 * @brief Tries to get the pilot into hyperspace.
 *
 *    @param p Pilot to try to start hyperspacing.
 *    @return 0 on success.
 */
int space_hyperspace( Pilot* p )
{
   if (pilot_isFlag(p, PILOT_NOJUMP))
      return -2;
   if (p->fuel < p->fuel_consumption)
      return -3;
   if (!space_canHyperspace(p))
      return -1;

   /* pilot is now going to get automatically ready for hyperspace */
   pilot_setFlag(p, PILOT_HYP_PREP);
   return 0;
}

/**
 * @brief Calculates the jump in pos for a pilot.
 *
 *    @param in Star system entering.
 *    @param out Star system exiting.
 *    @param[out] pos Position calculated.
 *    @param[out] vel Velocity calculated.
 *    @param[out] dir Angle calculated.
 *    @param p Pilot that is entering to use stats of (or NULL if not important).
 */
int space_calcJumpInPos( const StarSystem *in, const StarSystem *out, Vector2d *pos, Vector2d *vel, double *dir, const Pilot *p )
{
   JumpPoint *jp;
   double a, d, x, y;
   double ea, ed;

   /* Find the entry system. */
   jp = NULL;
   for (int i=0; i<array_size(in->jumps); i++)
      if (in->jumps[i].target == out)
         jp = &in->jumps[i];

   /* Must have found the jump. */
   if (jp == NULL) {
      WARN(_("Unable to find jump in point for '%s' in '%s': not connected"), out->name, in->name);
      return -1;
   }

   /* Base position target. */
   x = jp->pos.x;
   y = jp->pos.y;

   /* Calculate offset from target position. */
   a = 2.*M_PI - jp->angle;
   d = RNGF()*(HYPERSPACE_ENTER_MAX-HYPERSPACE_ENTER_MIN) + HYPERSPACE_ENTER_MIN;
   if ((p!=NULL) && pilot_isFlag(p, PILOT_STEALTH))
      d *= 1.4; /* Jump in from further out when coming in from stealth. */

   /* Calculate new position. */
   x += d*cos(a);
   y += d*sin(a);

   /* Add some error. */
   ea = 2.*M_PI*RNGF();
   ed = jp->radius/2.;
   if (p != NULL) {
      ed *= p->stats.jump_distance; /* larger variability. */
      if (pilot_isFlag(p, PILOT_STEALTH))
         ed *= 2.;
   }
   x += ed*cos(ea);
   y += ed*sin(ea);

   /* Set new position. */
   vect_cset( pos, x, y );

   /* Set new velocity. */
   a += M_PI;
   vect_cset( vel, HYPERSPACE_VEL*cos(a), HYPERSPACE_VEL*sin(a) );

   /* Set direction. */
   *dir = a;

   return 0;
}

/**
 * @brief Gets the name of all the planets that belong to factions.
 *
 *    @param factions Array (array.h): Factions to check against.
 *    @param landable Whether the search is limited to landable planets.
 *    @return An array (array.h) of faction names.  Individual names are not allocated.
 */
char** space_getFactionPlanet( int *factions, int landable )
{
   char **tmp = array_create( char* );
   for (int i=0; i<array_size(systems_stack); i++) {
      for (int j=0; j<array_size(systems_stack[i].planets); j++) {
         Planet *planet = systems_stack[i].planets[j];
         int f = 0;
         for (int k=0; k<array_size(factions); k++) {
            if (planet->presence.faction == factions[k]) {
               f = 1;
               break;
            }
         }
         if (!f)
            continue;

         /* Check landable. */
         if (landable) {
            planet_updateLand( planet );
            if (!planet->can_land)
               continue;
         }

         /* This is expensive so we probably want to do it last. */
         if (!space_sysReallyReachable( systems_stack[i].name ))
            continue;

         array_push_back( &tmp, planet->name );
         break; /* no need to check all factions */
      }
   }

   return tmp;
}

/**
 * @brief Gets the name of a random planet.
 *
 *    @param landable Whether the planet must let the player land normally.
 *    @param services Services the planet must have.
 *    @param filter Filter function for including planets.
 *    @return The name (internal/English) of a random planet.
 */
const char* space_getRndPlanet( int landable, unsigned int services,
      int (*filter)(Planet *p))
{
   char *res = NULL;
   Planet **tmp = array_create( Planet* );

   for (int i=0; i<array_size(systems_stack); i++) {
      for (int j=0; j<array_size(systems_stack[i].planets); j++) {
         Planet *pnt = systems_stack[i].planets[j];

         if (services && planet_hasService(pnt, services) != services)
            continue;

         if (filter != NULL && !filter(pnt))
            continue;

         array_push_back( &tmp, pnt );
      }
   }

   /* Second filter. */
   arrayShuffle( (void**)tmp );
   for (int i=0; i < array_size(tmp); i++) {
      Planet *pnt = tmp[i];

      /* We put expensive calculations here to minimize executions. */
      if (landable) {
         planet_updateLand( pnt );
         if (!pnt->can_land)
            continue;
      }
      if (!space_sysReallyReachable( planet_getSystem(pnt->name) ))
         continue;

      /* We want the name, not the actual planet. */
      res = tmp[i]->name;
      break;
   }
   array_free(tmp);

   return res;
}

/**
 * @brief Gets the closest feature to a position in the system.
 *
 *    @param sys System to get closest feature from a position.
 *    @param[out] pnt ID of closest planet or -1 if a jump point is closer (or none is close).
 *    @param[out] jp ID of closest jump point or -1 if a planet is closer (or none is close).
 *    @param[out] ast ID of closest asteroid or -1 if something else is closer (or none is close).
 *    @param[out] fie ID of the asteroid anchor the asteroid belongs to.
 *    @param x X position to get closest from.
 *    @param y Y position to get closest from.
 */
double system_getClosest( const StarSystem *sys, int *pnt, int *jp, int *ast, int *fie, double x, double y )
{
   double d = HUGE_VAL;

   /* Default output. */
   *pnt = -1;
   *jp  = -1;
   *ast = -1;
   *fie = -1;

   /* Planets. */
   for (int i=0; i<array_size(sys->planets); i++) {
      double td;
      Planet *p  = sys->planets[i];
      if (!planet_isKnown(p))
         continue;
      td = pow2(x-p->pos.x) + pow2(y-p->pos.y);
      if (td < d) {
         *pnt  = i;
         d     = td;
      }
   }

   /* Asteroids. */
   for (int i=0; i<array_size(sys->asteroids); i++) {
      AsteroidAnchor *f = &sys->asteroids[i];
      for (int k=0; k<f->nb; k++) {
         double td;
         Asteroid *as = &f->asteroids[k];

         /* Skip invisible asteroids */
         if (as->appearing == ASTEROID_INVISIBLE)
            continue;

         /* Skip out of range asteroids */
         if (!pilot_inRangeAsteroid( player.p, k, i ))
            continue;

         td = pow2(x-as->pos.x) + pow2(y-as->pos.y);
         if (td < d) {
            *pnt  = -1; /* We must clear planet target as asteroid is closer. */
            *ast  = k;
            *fie  = i;
            d     = td;
         }
      }
   }

   /* Jump points. */
   for (int i=0; i<array_size(sys->jumps); i++) {
      double td;
      JumpPoint *j  = &sys->jumps[i];
      if (!jp_isUsable(j))
         continue;
      td = pow2(x-j->pos.x) + pow2(y-j->pos.y);
      if (td < d) {
         *pnt  = -1; /* We must clear planet target as jump point is closer. */
         *ast  = -1;
         *fie  = -1;
         *jp   = i;
         d     = td;
      }
   }
   return d;
}

/**
 * @brief Gets the feature nearest to directly ahead of a position in the system.
 *
 *    @param sys System to get closest feature from a position.
 *    @param[out] pnt ID of closest planet or -1 if something else is closer (or none is close).
 *    @param[out] jp ID of closest jump point or -1 if something else is closer (or none is close).
 *    @param[out] ast ID of closest asteroid or -1 if something else is closer (or none is close).
 *    @param[out] fie ID of the asteroid anchor the asteroid belongs to.
 *    @param x X position to get closest from.
 *    @param y Y position to get closest from.
 *    @param ang Reference angle.
 *    @return The nearest angle to \p ang which is the direction from (\p x, \p y) to a feature.
 */
double system_getClosestAng( const StarSystem *sys, int *pnt, int *jp, int *ast, int *fie, double x, double y, double ang )
{
   double a;

   /* Default output. */
   *pnt = -1;
   *jp  = -1;
   a    = ang + M_PI;

   /* Planets. */
   for (int i=0; i<array_size(sys->planets); i++) {
      Planet *p = sys->planets[i];
      double ta = atan2( y - p->pos.y, x - p->pos.x);
      if ( ABS(angle_diff(ang, ta)) < ABS(angle_diff(ang, a))) {
         *pnt  = i;
         a     = ta;
      }
   }

   /* Asteroids. */
   for (int i=0; i<array_size(sys->asteroids); i++) {
      AsteroidAnchor *f = &sys->asteroids[i];
      for (int k=0; k<f->nb; k++) {
         double ta;
         Asteroid *as = &f->asteroids[k];

         /* Skip invisible asteroids */
         if (as->appearing == ASTEROID_INVISIBLE)
            continue;

         ta = atan2( y - as->pos.y, x - as->pos.x);
         if ( ABS(angle_diff(ang, ta)) < ABS(angle_diff(ang, a))) {
            *pnt  = -1; /* We must clear planet target as asteroid is closer. */
            *ast  = k;
            *fie  = i;
            a     = ta;
         }
      }
   }

   /* Jump points. */
   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *j = &sys->jumps[i];
      double ta = atan2( y - j->pos.y, x - j->pos.x);
      if ( ABS(angle_diff(ang, ta)) < ABS(angle_diff(ang, a))) {
         *ast  = -1;
         *fie  = -1;
         *pnt  = -1; /* We must clear the rest as jump point is closer. */
         *jp   = i;
         a     = ta;
      }
   }
   return a;
}

/**
 * @brief Sees if a system is reachable.
 *
 *    @return 1 if target system is reachable, 0 if it isn't.
 */
int space_sysReachable( const StarSystem *sys )
{
   if (sys_isKnown(sys))
      return 1; /* it is known */

   /* check to see if it is adjacent to known */
   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *jp = sys->jumps[i].returnJump;
      if (jp && jp_isUsable( jp ))
         return 1;
   }

   return 0;
}

/**
 * @brief Sees if a system can be reached via jumping.
 *
 *    @return 1 if target system is reachable, 0 if it isn't.
 */
int space_sysReallyReachable( char* sysname )
{
   StarSystem** path;

   if (strcmp(sysname,cur_system->name)==0)
      return 1;
   path = map_getJumpPath( cur_system->name, sysname, 1, 1, NULL );
   if (path != NULL) {
      array_free(path);
      return 1;
   }
   return 0;
}

/**
 * @brief Sees if a system is reachable from another system.
 *
 *    @return 1 if target system is reachable, 0 if it isn't.
 */
int space_sysReachableFromSys( const StarSystem *target, const StarSystem *sys )
{
   /* check to see if sys contains a known jump point to target */
   JumpPoint *jp = jump_getTarget( target, sys );
   if (jp == NULL)
      return 0;
   else if (jp_isUsable( jp ))
      return 1;
   return 0;
}

/**
 * @brief Gets an array (array.h) of all star systems.
 */
StarSystem* system_getAll (void)
{
   return systems_stack;
}

/**
 * @brief Checks to see if a system exists case insensitively.
 *
 *    @param sysname Name of the system to match (case insensitive).
 *    @return The actual name of the system of NULL if not found.
 */
const char *system_existsCase( const char* sysname )
{
   for (int i=0; i<array_size(systems_stack); i++)
      if (strcasecmp(sysname, systems_stack[i].name)==0)
         return systems_stack[i].name;
   return NULL;
}

/**
 * @brief Does a fuzzy case matching. Searches translated names but returns internal names.
 */
char **system_searchFuzzyCase( const char* sysname, int *n )
{
   int len;
   char **names;

   /* Overallocate to maximum. */
   names = malloc( sizeof(char*) * array_size(systems_stack) );

   /* Do fuzzy search. */
   len = 0;
   for (int i=0; i<array_size(systems_stack); i++) {
      if (strcasestr( _(systems_stack[i].name), sysname ) != NULL) {
         names[len] = systems_stack[i].name;
         len++;
      }
   }

   /* Free if empty. */
   if (len == 0) {
      free(names);
      names = NULL;
   }

   *n = len;
   return names;
}

/**
 * @brief Comparison function for qsort'ing StarSystem by name.
 */
static int system_cmp( const void *p1, const void *p2 )
{
   const StarSystem *s1, *s2;
   s1 = (const StarSystem*) p1;
   s2 = (const StarSystem*) p2;
   return strcmp(s1->name,s2->name);
}

/**
 * @brief Get the system from its name.
 *
 *    @param sysname Name to match.
 *    @return System matching sysname.
 */
StarSystem* system_get( const char* sysname )
{
   if (sysname == NULL)
      return NULL;

#ifdef DEBUGGING
   if (systemstack_changed) {
      for (int i=0; i<array_size(systems_stack); i++)
         if (strcmp(systems_stack[i].name, sysname)==0)
            return &systems_stack[i];
      WARN(_("System '%s' not found in stack"), sysname);
      return NULL;
   }
#endif /* DEBUGGING */

   const StarSystem s = {.name = (char*)sysname};
   StarSystem *found = bsearch( &s, systems_stack, array_size(systems_stack), sizeof(StarSystem), system_cmp );
   if (found != NULL)
      return found;

   WARN(_("System '%s' not found in stack"), sysname);
   return NULL;
}

/**
 * @brief Get the system by its index.
 *
 *    @param id Index to match.
 *    @return System matching index.
 */
StarSystem* system_getIndex( int id )
{
   return &systems_stack[ id ];
}

/**
 * @brief Gets the index of a star system.
 *
 *    @param sys System to get index of.
 *    @return The index of the system.
 */
int system_index( const StarSystem *sys )
{
   return sys->id;
}

/**
 * @brief Get whether or not a planet has a system (i.e. is on the map).
 *
 *    @param planetname Planet name to match.
 *    @return 1 if the planet has a system, 0 otherwise.
 */
int planet_hasSystem( const char* planetname )
{
   for (int i=0; i<array_size(planetname_stack); i++)
      if (strcmp(planetname_stack[i],planetname)==0)
         return 1;
   return 0;
}

/**
 * @brief Get the name of a system from a planetname.
 *
 *    @param planetname Planet name to match.
 *    @return Name of the system planet belongs to.
 */
char* planet_getSystem( const char* planetname )
{
   for (int i=0; i<array_size(planetname_stack); i++)
      if (strcmp(planetname_stack[i],planetname)==0)
         return systemname_stack[i];
   LOG(_("Planet '%s' is not placed in a system"), planetname);
   return NULL;
}

/**
 * @brief Comparison function for qsort'ing Planet by name.
 */
static int planet_cmp( const void *p1, const void *p2 )
{
   const Planet *pnt1, *pnt2;
   pnt1 = (const Planet*) p1;
   pnt2 = (const Planet*) p2;
   return strcmp(pnt1->name,pnt2->name);
}

/**
 * @brief Gets a planet based on its name.
 *
 *    @param planetname Name to match.
 *    @return Planet matching planetname.
 */
Planet* planet_get( const char* planetname )
{
   if (planetname==NULL) {
      WARN(_("Trying to find NULL planet…"));
      return NULL;
   }

#ifdef DEBUGGING
   if (planetstack_changed) {
      for (int i=0; i<array_size(planet_stack); i++)
         if (strcmp(planet_stack[i].name, planetname)==0)
            return &planet_stack[i];
      WARN(_("Planet '%s' not found in the universe"), planetname);
      return NULL;
   }
#endif /* DEBUGGING */

   const Planet p = {.name = (char*)planetname};
   Planet *found = bsearch( &p, planet_stack, array_size(planet_stack), sizeof(Planet), planet_cmp );
   if (found != NULL)
      return found;

   WARN(_("Planet '%s' not found in the universe"), planetname);
   return NULL;
}

/**
 * @brief Gets planet by index.
 *
 *    @param ind Index of the planet to get.
 *    @return The planet gotten.
 */
Planet* planet_getIndex( int ind )
{
   /* Validity check. */
   if ((ind < 0) || (ind >= array_size(planet_stack))) {
      WARN(_("Planet index '%d' out of range (max %d)"), ind, array_size(planet_stack));
      return NULL;
   }

   return &planet_stack[ ind ];
}

/**
 * @brief Gets the ID of a planet.
 *
 *    @param p Planet to get ID of.
 *    @return The ID of the planet.
 */
int planet_index( const Planet *p )
{
   return p->id;
}

/**
 * @brief Gets an array (array.h) of all planets.
 */
Planet* planet_getAll (void)
{
   return planet_stack;
}

/**
 * @brief Sets a planet's known status, if it's real.
 */
void planet_setKnown( Planet *p )
{
   planet_setFlag(p, PLANET_KNOWN);
}

/**
 * @brief Check to see if a planet exists.
 *
 *    @param planetname Name of the planet to see if it exists.
 *    @return 1 if planet exists.
 */
int planet_exists( const char* planetname )
{
   for (int i=0; i<array_size(planet_stack); i++)
      if (strcmp(planet_stack[i].name,planetname)==0)
         return 1;
   return 0;
}

/**
 * @brief Check to see if a planet exists (case insensitive).
 *
 *    @param planetname Name of the planet to see if it exists.
 *    @return The actual name of the planet or NULL if not found.
 */
const char* planet_existsCase( const char* planetname )
{
   for (int i=0; i<array_size(planet_stack); i++)
      if (strcasecmp(planet_stack[i].name,planetname)==0)
         return planet_stack[i].name;
   return NULL;
}

/**
 * @brief Does a fuzzy case matching. Searches translated names but returns internal names.
 */
char **planet_searchFuzzyCase( const char* planetname, int *n )
{
   int len;
   char **names;

   /* Overallocate to maximum. */
   names = malloc( sizeof(char*) * array_size(planet_stack) );

   /* Do fuzzy search. */
   len = 0;
   for (int i=0; i<array_size(planet_stack); i++) {
      if (strcasestr( _(planet_stack[i].name), planetname ) != NULL) {
         names[len] = planet_stack[i].name;
         len++;
      }
   }

   /* Free if empty. */
   if (len == 0) {
      free(names);
      names = NULL;
   }

   *n = len;
   return names;
}

/**
 * @brief Gets all the virtual assets.
 */
VirtualAsset* virtualasset_getAll (void)
{
   return vasset_stack;
}

/**
 * @brief Comparison function for qsort'ing VirtuaAsset by name.
 */
static int virtualasset_cmp( const void *p1, const void *p2 )
{
   const VirtualAsset *v1, *v2;
   v1 = (const VirtualAsset*) p1;
   v2 = (const VirtualAsset*) p2;
   return strcmp(v1->name,v2->name);
}

/**
 * @brief Gets a virtual asset by matching name.
 */
VirtualAsset* virtualasset_get( const char *name )
{
   const VirtualAsset va = {.name = (char*)name};
   VirtualAsset *found = bsearch( &va, vasset_stack, array_size(vasset_stack), sizeof(VirtualAsset), virtualasset_cmp );
   if (found != NULL)
      return found;
   WARN(_("Virtual Asset '%s' not found in the universe"), name);
   return NULL;
}

/**
 * @brief Gets a jump point based on its target and system.
 *
 *    @param jumpname Name to match.
 *    @param sys System jump is in.
 *    @return Jump point matich jumpname in sys or NULL if not found.
 */
JumpPoint* jump_get( const char* jumpname, const StarSystem* sys )
{
   if (jumpname==NULL) {
      WARN(_("Trying to find NULL jump point..."));
      return NULL;
   }

   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *jp = &sys->jumps[i];
      if (strcmp(jp->target->name,jumpname)==0)
         return jp;
   }

   WARN(_("Jump point '%s' not found in %s"), jumpname, sys->name);
   return NULL;
}

/**
 * @brief Less safe version of jump_get that works with pointers.
 *
 *    @param target Target system jump leads to.
 *    @param sys System to look in.
 *    @return Jump point in sys to target or NULL if not found.
 */
JumpPoint* jump_getTarget( const StarSystem* target, const StarSystem* sys )
{
   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *jp = &sys->jumps[i];
      if (jp->target == target)
         return jp;
   }
   WARN(_("Jump point to '%s' not found in %s"), target->name, sys->name);
   return NULL;
}

/**
 * @brief Gets the jump point symbol.
 */
const char *jump_getSymbol( const JumpPoint *jp )
{
   if (jp_isFlag(jp, JP_HIDDEN))
      return "* ";
   return "";
}

/**
 * @brief Controls fleet spawning.
 *
 *    @param dt Current delta tick.
 *    @param init Should be 1 to initialize the scheduler.
 */
static void system_scheduler( double dt, int init )
{
   /* Go through all the factions and reduce the timer. */
   for (int i=0; i < array_size(cur_system->presence); i++) {
      int n;
      nlua_env env;
      SystemPresence *p = &cur_system->presence[i];
      if (p->value <= 0.)
         continue;

      env = faction_getScheduler( p->faction );

      /* Must have a valid scheduler. */
      if (env==LUA_NOREF)
         continue;

      /* Spawning is disabled for this faction. */
      if (p->disabled)
         continue;

      /* Run the appropriate function. */
      if (init) {
         nlua_getenv( env, "create" ); /* f */
         if (lua_isnil(naevL,-1)) {
            WARN(_("Lua Spawn script for faction '%s' missing obligatory entry point 'create'."),
                  faction_name( p->faction ) );
            lua_pop(naevL,1);
            continue;
         }
         n = 0;
      }
      else {
         /* Decrement dt, only continue  */
         p->timer -= dt;
         if (p->timer >= 0.)
            continue;

         nlua_getenv( env, "spawn" ); /* f */
         if (lua_isnil(naevL,-1)) {
            WARN(_("Lua Spawn script for faction '%s' missing obligatory entry point 'spawn'."),
                  faction_name( p->faction ) );
            lua_pop(naevL,1);
            continue;
         }
         lua_pushnumber( naevL, p->curUsed ); /* f, presence */
         n = 1;
      }
      lua_pushnumber( naevL, p->value ); /* f, [arg,], max */

      /* Actually run the function. */
      if (nlua_pcall(env, n+1, 2)) { /* error has occurred */
         WARN(_("Lua Spawn script for faction '%s' : %s"),
               faction_name( p->faction ), lua_tostring(naevL,-1));
         lua_pop(naevL,1);
         continue;
      }

      /* Output is handled the same way. */
      if (!lua_isnumber(naevL,-2)) {
         WARN(_("Lua spawn script for faction '%s' failed to return timer value."),
               faction_name( p->faction ) );
         lua_pop(naevL,2);
         continue;
      }
      p->timer    += lua_tonumber(naevL,-2);
      /* Handle table if it exists. */
      if (lua_istable(naevL,-1)) {
         lua_pushnil(naevL); /* tk, k */
         while (lua_next(naevL,-2) != 0) { /* tk, k, v */
            Pilot *pilot;

            /* Must be table. */
            if (!lua_istable(naevL,-1)) {
               WARN(_("Lua spawn script for faction '%s' returns invalid data (not a table)."),
                     faction_name( p->faction ) );
               lua_pop(naevL,2); /* tk, k */
               continue;
            }

            lua_getfield( naevL, -1, "pilot" ); /* tk, k, v, p */
            if (!lua_ispilot(naevL,-1)) {
               WARN(_("Lua spawn script for faction '%s' returns invalid data (not a pilot)."),
                     faction_name( p->faction ) );
               lua_pop(naevL,2); /* tk, k */
               continue;
            }
            pilot = pilot_get( lua_topilot(naevL,-1) );
            if (pilot == NULL) {
               lua_pop(naevL,2); /* tk, k */
               continue;
            }
            lua_pop(naevL,1); /* tk, k, v */
            lua_getfield( naevL, -1, "presence" ); /* tk, k, v, p */
            if (!lua_isnumber(naevL,-1)) {
               WARN(_("Lua spawn script for faction '%s' returns invalid data (not a number)."),
                     faction_name( p->faction ) );
               lua_pop(naevL,2); /* tk, k */
               continue;
            }
            pilot->presence = lua_tonumber(naevL,-1);
            if (pilot->faction != p->faction) {
               int pi;
               WARN( _("Lua spawn script for faction '%s' actually spawned a '%s' pilot."),
                     faction_name( p->faction ),
                     faction_name( pilot->faction ) );
               pi = getPresenceIndex( cur_system, pilot->faction );
               p = &cur_system->presence[pi];
            }
            p->curUsed     += pilot->presence;
            lua_pop(naevL,2); /* tk, k */
         }
      }
      lua_pop(naevL,2);
   }
}

/**
 * @brief Mark when a faction changes.
 */
void space_factionChange (void)
{
   space_fchg = 1;
}

/**
 * @brief Handles landing if necessary.
 */
void space_checkLand (void)
{
   if (space_landQueuePlanet != NULL) {
      land( space_landQueuePlanet, 0 );
      space_landQueuePlanet = NULL;
   }
}

/**
 * @brief Controls fleet spawning.
 *
 *    @param dt Current delta tick.
 */
void space_update( const double dt )
{
   /* Needs a current system. */
   if (cur_system == NULL)
      return;

   /* If spawning is enabled, call the scheduler. */
   if (space_spawn)
      system_scheduler( dt, 0 );

   /*
    * Nebula.
    */
   nebu_update( dt );
   if (cur_system->nebu_volatility > 0.) {
      Pilot *const* pilot_stack;
      Damage dmg;
      dmg.type          = dtype_get("nebula");
      dmg.damage        = cur_system->nebu_volatility * dt;
      dmg.penetration   = 1.; /* Full penetration. */
      dmg.disable       = 0.;

      /* Damage pilots in volatile systems. */
      pilot_stack = pilot_getAll();
      for (int i=0; i<array_size(pilot_stack); i++)
         pilot_hit( pilot_stack[i], NULL, 0, &dmg, 0 );
   }

   /* Faction updates. */
   if (space_fchg) {
      for (int i=0; i<array_size(cur_system->planets); i++)
         planet_updateLand( cur_system->planets[i] );

      /* Verify land authorization is still valid. */
      if (player_isFlag(PLAYER_LANDACK))
         player_checkLandAck();

      gui_updateFaction();
      space_fchg = 0;
   }

   if (!space_simulating) {
      int found_something = 0;
      /* Planet updates */
      for (int i=0; i<array_size(cur_system->planets); i++) {
         if ((!planet_isKnown( cur_system->planets[i] )) && ( pilot_inRangePlanet( player.p, i ))) {
            HookParam hparam[3];

            planet_setKnown( cur_system->planets[i] );
            player_message( _("You discovered #%c%s#0."),
                  planet_getColourChar( cur_system->planets[i] ),
                  _(cur_system->planets[i]->name) );
            hparam[0].type  = HOOK_PARAM_STRING;
            hparam[0].u.str = "asset";
            hparam[1].type  = HOOK_PARAM_ASSET;
            hparam[1].u.la  = cur_system->planets[i]->id;
            hparam[2].type  = HOOK_PARAM_SENTINEL;
            hooks_runParam( "discover", hparam );
            found_something = 1;
            cur_system->planets[i]->map_alpha = 0.;
         }
      }

      /* Jump point updates */
      for (int i=0; i<array_size(cur_system->jumps); i++) {
         HookParam hparam[3];
         JumpPoint *jp = &cur_system->jumps[i];

         if (jp_isKnown(jp))
            continue;
         if (jp_isFlag(jp,JP_EXITONLY))
            continue;
         if (!(pilot_inRangeJump( player.p, i )))
            continue;

         jp_setFlag( jp, JP_KNOWN );
         player_message( _("You discovered a Jump Point.") );
         hparam[0].type  = HOOK_PARAM_STRING;
         hparam[0].u.str = "jump";
         hparam[1].type  = HOOK_PARAM_JUMP;
         hparam[1].u.lj.srcid = cur_system->id;
         hparam[1].u.lj.destid = jp->target->id;
         hparam[2].type  = HOOK_PARAM_SENTINEL;
         hooks_runParam( "discover", hparam );
         found_something = 1;
         jp->map_alpha = 0.;
      }

      if (found_something)
         ovr_refresh();
   }

   /* Update the gatherable objects. */
   gatherable_update(dt);

   /* Asteroids/Debris update */
   for (int i=0; i<array_size(cur_system->asteroids); i++) {
      double x, y;
      Pilot *pplayer;
      AsteroidAnchor *ast = &cur_system->asteroids[i];

      for (int j=0; j<ast->nb; j++) {
         Asteroid *a = &ast->asteroids[j];

         /* Skip invisible asteroids */
         if (a->appearing == ASTEROID_INVISIBLE)
            continue;

         a->pos.x += a->vel.x * dt;
         a->pos.y += a->vel.y * dt;

         if (a->appearing == ASTEROID_VISIBLE) {
            /* Random explosions */
            a->timer += dt;
            if (a->timer >= ASTEROID_EXPLODE_INTERVAL) {
               a->timer = 0.;
               if ((RNGF() < ASTEROID_EXPLODE_CHANCE) ||
                     (space_isInField(&a->pos) < 0)) {
                  asteroid_explode( a, ast, 0 );
               }
            }
         }
         else if (a->appearing == ASTEROID_GROWING) {
            /* Grow */
            a->timer += dt;
            if (a->timer >= 2.) {
               a->timer = 0.;
               a->appearing = ASTEROID_VISIBLE;
            }
         }
         else if (a->appearing == ASTEROID_SHRINKING) {
            /* Shrink */
            a->timer += dt;
            if (a->timer >= 2.) {
               /* Remove the asteroid target to any pilot. */
               pilot_untargetAsteroid( a->parent, a->id );
               /* reinit any disappeared asteroid */
               asteroid_init( a, ast );
            }
         }
         else if (a->appearing == ASTEROID_EXPLODING) {
            /* Exploding asteroid */
            a->timer += dt;
            if (a->timer >= 0.5) {
               /* Make it explode */
               asteroid_explode( a, ast, 1 );
            }
         }
      }

      x = 0.;
      y = 0.;
      pplayer = pilot_get( PLAYER_ID );
      if (pplayer != NULL) {
         Solid *psolid  = pplayer->solid;
         x = psolid->vel.x;
         y = psolid->vel.y;
      }

      if (!space_simulating) {
         for (int j=0; j<ast->ndebris; j++) {
            Debris *d = &ast->debris[j];

            d->pos.x += (d->vel.x-x) * dt;
            d->pos.y += (d->vel.y-y) * dt;

            /* Check boundaries */
            if (d->pos.x > SCREEN_W + DEBRIS_BUFFER)
               d->pos.x -= SCREEN_W + 2*DEBRIS_BUFFER;
            else if (d->pos.y > SCREEN_H + DEBRIS_BUFFER)
               d->pos.y -= SCREEN_H + 2*DEBRIS_BUFFER;
            else if (d->pos.x < -DEBRIS_BUFFER)
               d->pos.x += SCREEN_W + 2*DEBRIS_BUFFER;
            else if (d->pos.y < -DEBRIS_BUFFER)
               d->pos.y += SCREEN_H + 2*DEBRIS_BUFFER;
         }
      }
   }
}

/**
 * @brief returns whether we're just simulating.
 */
int space_isSimulation( void )
{
   return space_simulating;
}

/**
 * @brief returns whether or not we're simulating with effects.
 */
int space_isSimulationEffects (void)
{
   return space_simulating_effects;
}

/**
 * @brief Initializes the system.
 *
 *    @param sysname Name of the system to initialize.
 *    @param do_simulate Whether or not perform the initial simulation.
 */
void space_init( const char* sysname, int do_simulate )
{
   char *nt;
   int n, s;
   const double fps_min_simulation = fps_min * 2.;
   StarSystem *oldsys = cur_system;

   /* cleanup some stuff */
   player_clear(); /* clears targets */
   ovr_mrkClear(); /* Clear markers when jumping. */
   pilots_clean(1); /* destroy non-persistent pilots */
   weapon_clear(); /* get rid of all the weapons */
   spfx_clear(); /* get rid of the explosions */
   gatherable_free(); /* get rid of gatherable stuff. */
   background_clear(); /* Get rid of the background. */
   factions_clearDynamic(); /* get rid of dynamic factions. */
   space_spawn = 1; /* spawn is enabled by default. */
   if (player.p != NULL) {
      pilot_lockClear( player.p );
      pilot_clearTimers( player.p ); /* Clear timers. */
   }

   if ((sysname==NULL) && (cur_system==NULL))
      ERR(_("Cannot reinit system if there is no system previously loaded"));
   else if (sysname!=NULL) {
      cur_system = system_get( sysname );

      nt = ntime_pretty(0, 2);
      player_message(_("#oEntering System %s on %s."), _(sysname), nt);
      if (cur_system->nebu_volatility > 0.)
         player_message(_("#rWARNING - Volatile nebula detected in %s! Taking %.1f MW damage!"), _(sysname), cur_system->nebu_volatility);
      free(nt);

      /* Handle background */
      if (cur_system->nebu_density > 0.) {
         /* Background is Nebula */
         nebu_prep( cur_system->nebu_density, cur_system->nebu_volatility, cur_system->nebu_hue );

         /* Set up sound. */
         sound_env( SOUND_ENV_NEBULA, cur_system->nebu_density );
      }
      else {
         /* Background is starry */
         background_initStars( cur_system->stars );

         /* Set up sound. */
         sound_env( SOUND_ENV_NORMAL, 0. );
      }
   }

   /* Update after setting cur_system. */
   if ((oldsys != NULL && oldsys->stats != NULL) || cur_system->stats != NULL) {
      Pilot *const* pilot_stack = pilot_getAll();
      for (int i=0; i<array_size(pilot_stack); i++)
         pilot_calcStats( pilot_stack[i] );
   }

   /* Set up planets. */
   for (int i=0; i<array_size(cur_system->planets); i++) {
      Planet *pnt = cur_system->planets[i];
      pnt->bribed = 0;
      pnt->land_override = 0;
      planet_updateLand( pnt );
   }

   /* Set up asteroids. */
   for (int i=0; i<array_size(cur_system->asteroids); i++) {
      AsteroidAnchor *ast = &cur_system->asteroids[i];
      ast->id = i;

      /* Add the asteroids to the anchor */
      ast->asteroids = realloc( ast->asteroids, (ast->nb) * sizeof(Asteroid) );
      for (int j=0; j<ast->nb; j++) {
         Asteroid *a = &ast->asteroids[j];
         a->id = j;
         a->appearing = ASTEROID_INIT;
         asteroid_init(a, ast);
      }
      /* Add the debris to the anchor */
      ast->debris = realloc( ast->debris, (ast->ndebris) * sizeof(Debris) );
      for (int j=0; j<ast->ndebris; j++) {
         Debris *d = &ast->debris[j];
         debris_init(d);
      }
   }

   /* See if we should get a new music song. */
   if (player.p != NULL)
      music_choose(NULL);

   /* Reset player enemies. */
   player.enemies = 0;
   player.disabled_enemies = 0;

   /* Reset new trails. */
   pilots_newSystem();

   /* Reset any schedules and used presence. */
   for (int i=0; i<array_size(cur_system->presence); i++) {
      cur_system->presence[i].curUsed  = 0;
      cur_system->presence[i].timer    = 0.;
      cur_system->presence[i].disabled = 0;
   }

   /* Load graphics. */
   space_gfxLoad( cur_system );

   /* Call the scheduler. */
   system_scheduler( 0., 1 );

   /* we now know this system */
   sys_setFlag(cur_system,SYSTEM_KNOWN);

   /* Simulate system. */
   space_simulating = 1;
   space_simulating_effects = 0;
   if (player.p != NULL)
      pilot_setFlag( player.p, PILOT_HIDE );
   player_messageToggle( 0 );
   if (do_simulate) {
      s = sound_disabled;
      sound_disabled = 1;
      ntime_allowUpdate( 0 );
      n = SYSTEM_SIMULATE_TIME_PRE / fps_min_simulation;
      for (int i=0; i<n; i++)
         update_routine( fps_min_simulation, 1 );
      space_simulating_effects = 1;
      n = SYSTEM_SIMULATE_TIME_POST / fps_min_simulation;
      for (int i=0; i<n; i++)
         update_routine( fps_min_simulation, 1 );
      ntime_allowUpdate( 1 );
      sound_disabled = s;
   }
   player_messageToggle( 1 );
   if (player.p != NULL)
      pilot_rmFlag( player.p, PILOT_HIDE );
   space_simulating = 0;

   /* Refresh overlay if necessary (player kept it open). */
   ovr_refresh();

   /* Update gui. */
   gui_setSystem();

   /* Start background. */
   background_load( cur_system->background );
}

/**
 * @brief Initializes an asteroid.
 *    @param ast Asteroid to initialize.
 *    @param field Asteroid field the asteroid belongs to.
 */
void asteroid_init( Asteroid *ast, AsteroidAnchor *field )
{
   int i;
   double mod, theta;
   AsteroidType *at;
   int attempts = 0;

   ast->parent = field->id;
   ast->scanned = 0;

   /* randomly init the type of asteroid */
   i = RNG(0,field->ntype-1);
   ast->type = field->type[i];
   /* randomly init the gfx ID */
   at = &asteroid_types[ast->type];
   ast->gfxID = RNG(0, array_size(at->gfxs)-1);
   ast->armour = at->armour;

   do {
      double angle = RNGF() * 2 * M_PI;
      double radius = RNGF() * field->radius;
      ast->pos.x = radius * cos(angle) + field->pos.x;
      ast->pos.y = radius * sin(angle) + field->pos.y;

      /* If this is the first time and it's spawned outside the field,
       * we get rid of it so that density remains roughly consistent. */
      if ((ast->appearing == ASTEROID_INIT) &&
            (space_isInField(&ast->pos) < 0)) {
         ast->appearing = ASTEROID_INVISIBLE;
         return;
      }

      attempts++;
   } while ( (space_isInField(&ast->pos) < 0) && (attempts < 1000) );

   /* And a random velocity */
   theta = RNGF()*2.*M_PI;
   mod = RNGF() * 20;
   vect_pset( &ast->vel, mod, theta );

   /* Grow effect stuff */
   ast->appearing = ASTEROID_GROWING;
   ast->timer = 0.;
}

/**
 * @brief Initializes a debris.
 *    @param deb Debris to initialize.
 */
void debris_init( Debris *deb )
{
   double theta, mod;

   /* Position */
   deb->pos.x = (double)RNG(-DEBRIS_BUFFER, SCREEN_W + DEBRIS_BUFFER);
   deb->pos.y = (double)RNG(-DEBRIS_BUFFER, SCREEN_H + DEBRIS_BUFFER);

   /* And a random velocity */
   theta = RNGF()*2.*M_PI;
   mod = RNGF() * 20.;
   vect_pset( &deb->vel, mod, theta );

   /* Randomly init the gfx ID */
   deb->gfxID = RNG(0,(int)nasterogfx-1);

   /* Random height vs player. */
   deb->height = 0.8 + RNGF()*0.4;
}

/**
 * @brief Creates a new planet.
 */
Planet *planet_new (void)
{
   Planet *p, *old_stack;
   int realloced;

#if DEBUGGING
   if (!systems_loading)
      planetstack_changed = 1;
#else /* DEBUGGING */
   if (!systems_loading)
      WARN(_("Creating new planet in non-debugging mode. Things are probably going to break horribly."));
#endif /* DEBUGGING */

   /* Grow and initialize memory. */
   old_stack   = planet_stack;
   p           = &array_grow( &planet_stack );
   realloced   = (old_stack!=planet_stack);
   memset( p, 0, sizeof(Planet) );
   p->id       = array_size(planet_stack)-1;
   p->presence.faction = -1;

   /* Reconstruct the jumps. */
   if (!systems_loading && realloced)
      systems_reconstructPlanets();

   return p;
}

/**
 * @brief Loads all the planets in the game.
 *
 *    @return 0 on success.
 */
static int planets_load (void)
{
   size_t bufsize;
   char *buf, **planet_files;
   Commodity **stdList;

   /* Load landing stuff. */
   landing_env = nlua_newEnv(0);
   nlua_loadStandard(landing_env);
   buf         = ndata_read( LANDING_DATA_PATH, &bufsize );
   if (nlua_dobufenv(landing_env, buf, bufsize, LANDING_DATA_PATH) != 0) {
      WARN( _("Failed to load landing file: %s\n"
            "%s\n"
            "Most likely Lua file has improper syntax, please check"),
            LANDING_DATA_PATH, lua_tostring(naevL,-1));
   }
   free(buf);

   /* Initialize stack if needed. */
   if (planet_stack == NULL)
      planet_stack = array_create_size(Planet, 256);

   /* Extract the list of standard commodities. */
   stdList = standard_commodities();

   /* Load XML stuff. */
   planet_files = PHYSFS_enumerateFiles( PLANET_DATA_PATH );
   for (size_t i=0; planet_files[i]!=NULL; i++) {
      xmlNodePtr node;
      xmlDocPtr doc;
      char *file;

      if (!ndata_matchExt( planet_files[i], "xml" ))
         continue;

      asprintf( &file, "%s%s", PLANET_DATA_PATH, planet_files[i]);
      doc = xml_parsePhysFS( file );
      if (doc == NULL) {
         free(file);
         continue;
      }

      node = doc->xmlChildrenNode; /* first planet node */
      if (node == NULL) {
         WARN(_("Malformed %s file: does not contain elements"),file);
         free(file);
         xmlFreeDoc(doc);
         continue;
      }

      if (xml_isNode(node,XML_ASSET_TAG)) {
         Planet *p = planet_new();
         planet_parse( p, node, stdList );
      }

      /* Clean up. */
      free(file);
      xmlFreeDoc(doc);
   }
   qsort( planet_stack, array_size(planet_stack), sizeof(Planet), planet_cmp );
   for (int j=0; j<array_size(planet_stack); j++)
      planet_stack[j].id = j;

   /* Clean up. */
   PHYSFS_freeList( planet_files );
   array_free(stdList);

   return 0;
}

/**
 * @brief Loads all the virtual assets.
 *
 *    @return 0 on success.
 */
static int virtualassets_load (void)
{
   char **asset_files;

   /* Initialize stack if needed. */
   if (vasset_stack == NULL)
      vasset_stack = array_create_size(VirtualAsset, 64);

   /* Load XML stuff. */
   asset_files = PHYSFS_enumerateFiles( VIRTUALASSET_DATA_PATH );
   for (size_t i=0; asset_files[i]!=NULL; i++) {
      xmlDocPtr doc;
      xmlNodePtr node;
      char *file;

      if (!ndata_matchExt( asset_files[i], "xml" ))
         continue;

      asprintf( &file, "%s%s", VIRTUALASSET_DATA_PATH, asset_files[i]);
      doc = xml_parsePhysFS( file );
      if (doc == NULL) {
         free(file);
         continue;
      }

      node = doc->xmlChildrenNode; /* first asset node */
      if (node == NULL) {
         WARN(_("Malformed %s file: does not contain elements"),file);
         free(file);
         xmlFreeDoc(doc);
         continue;
      }

      if (xml_isNode(node,XML_ASSET_TAG)) {
         xmlNodePtr cur;
         VirtualAsset va;
         memset( &va, 0, sizeof(va) );
         xmlr_attr_strd( node, "name", va.name );
         va.presences = array_create( AssetPresence );

         cur = node->children;
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur,"presence")) {
               AssetPresence ap;
               asset_parsePresence( cur, &ap );
               array_push_back( &va.presences, ap );
               continue;
            }

            WARN(_("Unknown node '%s' in virtual asset '%s'"),cur->name,va.name);
         } while (xml_nextNode(cur));

         array_push_back( &vasset_stack, va );
      }

      /* Clean up. */
      free(file);
      xmlFreeDoc(doc);
   }
   qsort( vasset_stack, array_size(vasset_stack), sizeof(VirtualAsset), virtualasset_cmp );

   /* Clean up. */
   PHYSFS_freeList( asset_files );

   return 0;
}

/**
 * @brief Gets the planet colour char.
 */
char planet_getColourChar( const Planet *p )
{
   if (!planet_hasService( p, PLANET_SERVICE_INHABITED ))
      return 'I';

   if (p->can_land || p->bribed) {
      if (areAllies(FACTION_PLAYER,p->presence.faction))
         return 'F';
      return 'N';
   }

   if (areEnemies(FACTION_PLAYER,p->presence.faction))
      return 'H';
   return 'R';
}

/**
 * @brief Gets the planet symbol.
 */
const char *planet_getSymbol( const Planet *p )
{
   if (!planet_hasService( p, PLANET_SERVICE_INHABITED )) {
      if (planet_hasService( p, PLANET_SERVICE_LAND ))
         return "= ";
      return "";
   }

   if (p->can_land || p->bribed) {
      if (areAllies(FACTION_PLAYER,p->presence.faction))
         return "+ ";
      return "~ ";
   }

   if (areEnemies(FACTION_PLAYER,p->presence.faction))
      return "!! ";
   return "* ";
}

/**
 * @brief Gets the planet colour.
 */
const glColour* planet_getColour( const Planet *p )
{
   if (!planet_hasService( p, PLANET_SERVICE_INHABITED ))
      return &cInert;

   if (p->can_land || p->bribed) {
      if (areAllies(FACTION_PLAYER,p->presence.faction))
         return &cFriend;
      return &cNeutral;
   }

   if (areEnemies(FACTION_PLAYER,p->presence.faction))
      return &cHostile;
   return &cRestricted;
}

/**
 * @brief Updates the land possibilities of a planet.
 *
 *    @param p Planet to update land possibilities of.
 */
void planet_updateLand( Planet *p )
{
   char *str;

   /* Must be inhabited. */
   if (!planet_hasService( p, PLANET_SERVICE_INHABITED ) ||
         (player.p == NULL))
      return;

   /* Clean up old stuff. */
   free( p->land_msg );
   free( p->bribe_msg );
   free( p->bribe_ack_msg );
   p->can_land    = 0;
   p->land_msg    = NULL;
   p->bribe_msg   = NULL;
   p->bribe_ack_msg = NULL;
   p->bribe_price = 0;

   /* Set up function. */
   if (p->land_func == NULL)
      str = "land";
   else
      str = p->land_func;
   nlua_getenv( landing_env, str );
   lua_pushplanet( naevL, p->id );
   if (nlua_pcall(landing_env, 1, 5)) { /* error has occurred */
      WARN(_("Landing: '%s' : %s"), str, lua_tostring(naevL,-1));
      lua_pop(naevL,1);
      return;
   }

   /* Parse parameters. */
   p->can_land = lua_toboolean(naevL,-5);
   if (lua_isstring(naevL,-4))
      p->land_msg = strdup( lua_tostring(naevL,-4) );
   else {
      WARN( _("%s: %s (%s) -> return parameter 2 is not a string!"), LANDING_DATA_PATH, str, p->name );
      p->land_msg = strdup( _("Invalid land message") );
   }
   /* Parse bribing. */
   if (!p->can_land && lua_isnumber(naevL,-3)) {
      p->bribe_price = lua_tonumber(naevL,-3);
      /* We need the bribe message. */
      if (lua_isstring(naevL,-2))
         p->bribe_msg = strdup( lua_tostring(naevL,-2) );
      else {
         WARN( _("%s: %s (%s) -> return parameter 4 is not a string!"), LANDING_DATA_PATH, str, p->name );
         p->bribe_msg = strdup( _("Invalid bribe message") );
      }
      /* We also need the bribe ACK message. */
      if (lua_isstring(naevL,-1))
         p->bribe_ack_msg = strdup( lua_tostring(naevL,-1) );
      else {
         WARN( _("%s: %s (%s) -> return parameter 5 is not a string!"), LANDING_DATA_PATH, str, p->name );
         p->bribe_ack_msg = strdup( _("Invalid bribe ack message") );
      }
   }
   else if (lua_isstring(naevL,-3))
      p->bribe_msg = strdup( lua_tostring(naevL,-3) );
   else if (!lua_isnil(naevL,-3))
      WARN( _("%s: %s (%s) -> return parameter 3 is not a number or string or nil!"), LANDING_DATA_PATH, str, p->name );

   lua_pop(naevL,5);

   /* Unset bribe status if bribing is no longer possible. */
   if (p->bribed && p->bribe_ack_msg == NULL)
      p->bribed = 0;
}

/**
 * @brief Loads a planet's graphics (and radius).
 */
void planet_gfxLoad( Planet *planet )
{
   if (planet->gfx_space == NULL) {
      planet->gfx_space = gl_newImage( planet->gfx_spaceName, OPENGL_TEX_MIPMAPS );
      planet->radius = (planet->gfx_space->w + planet->gfx_space->h)/4.;
   }
}

/**
 * @brief Loads all the graphics for a star system.
 *
 *    @param sys System to load graphics for.
 */
void space_gfxLoad( StarSystem *sys )
{
   for (int i=0; i<array_size(sys->planets); i++)
      planet_gfxLoad( sys->planets[i] );
}

/**
 * @brief Unloads all the graphics for a star system.
 *
 *    @param sys System to unload graphics for.
 */
void space_gfxUnload( StarSystem *sys )
{
   for (int i=0; i<array_size(sys->planets); i++) {
      Planet *planet = sys->planets[i];
      gl_freeTexture( planet->gfx_space );
      planet->gfx_space = NULL;
   }
}

/**
 * @brief Parsess an asset presence from xml.
 *
 *    @param node Node to process.
 *    @param[out] ap Asset presence to save to.
 */
static int asset_parsePresence( xmlNodePtr node, AssetPresence *ap )
{
   xmlNodePtr cur = node->children;
   memset( ap, 0, sizeof(AssetPresence) );
   ap->faction = -1;
   do {
      xml_onlyNodes(cur);
      xmlr_float(cur, "base", ap->base);
      xmlr_float(cur, "bonus", ap->bonus);
      xmlr_int(cur, "range", ap->range);
      if (xml_isNode(cur,"faction")) {
         ap->faction = faction_get( xml_get(cur) );
         continue;
      }
   } while (xml_nextNode(cur));
   return 0;
}

/**
 * @brief Parses a planet from an xml node.
 *
 *    @param planet Planet to fill up.
 *    @param parent Node that contains planet data.
 *    @param[in] stdList The array of standard commodities.
 *    @return 0 on success.
 */
static int planet_parse( Planet *planet, const xmlNodePtr parent, Commodity **stdList )
{
   xmlNodePtr node;
   unsigned int flags;
   Commodity **comms;

   /* Clear up memory for safe defaults. */
   flags          = 0;
   planet->hide   = 0.01;
   comms          = array_create( Commodity* );

   /* Get the name. */
   xmlr_attr_strd( parent, "name", planet->name );

   node = parent->xmlChildrenNode;
   do {
      /* Only handle nodes. */
      xml_onlyNodes(node);

      if (xml_isNode(node,"GFX")) {
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur,"space")) { /* load space gfx */
               char str[PATH_MAX];
               snprintf( str, sizeof(str), PLANET_GFX_SPACE_PATH"%s", xml_get(cur));
               planet->gfx_spaceName = strdup(str);
               planet->gfx_spacePath = xml_getStrd(cur);
               planet->radius = -1.;
               continue;
            }
            if (xml_isNode(cur,"exterior")) { /* load land gfx */
               char str[PATH_MAX];
               snprintf( str, sizeof(str), PLANET_GFX_EXTERIOR_PATH"%s", xml_get(cur));
               planet->gfx_exterior = strdup(str);
               planet->gfx_exteriorPath = xml_getStrd(cur);
               continue;
            }
            WARN(_("Unknown node '%s' in planet '%s'"),node->name,planet->name);
         } while (xml_nextNode(cur));
         continue;
      }
      else if (xml_isNode(node,"pos")) {
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur,"x")) {
               flags |= FLAG_XSET;
               planet->pos.x = xml_getFloat(cur);
               continue;
            }
            if (xml_isNode(cur,"y")) {
               flags |= FLAG_YSET;
               planet->pos.y = xml_getFloat(cur);
               continue;
            }
            WARN(_("Unknown node '%s' in planet '%s'"),node->name,planet->name);
         } while (xml_nextNode(cur));
         continue;
      }
      else if (xml_isNode(node, "presence")) {
         asset_parsePresence( node, &planet->presence );
         if (planet->presence.faction>=0)
            flags += FLAG_FACTIONSET;
         continue;
      }
      else if (xml_isNode(node,"general")) {
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            /* Direct reads. */
            xmlr_strd(cur, "class", planet->class);
            xmlr_strd(cur, "bar", planet->bar_description);
            xmlr_strd(cur, "description", planet->description );
            xmlr_float(cur, "population", planet->population );
            xmlr_float(cur, "hide", planet->hide );

            if (xml_isNode(cur, "services")) {
               xmlNodePtr ccur = cur->children;
               flags |= FLAG_SERVICESSET;
               planet->services = 0;
               do {
                  xml_onlyNodes(ccur);

                  if (xml_isNode(ccur, "land")) {
                     char *tmp = xml_get(ccur);
                     planet->services |= PLANET_SERVICE_LAND;
                     if (tmp != NULL) {
                        planet->land_func = strdup(tmp);
#ifdef DEBUGGING
                        if (landing_env != LUA_NOREF) {
                           nlua_getenv( landing_env, tmp );
                           if (lua_isnil(naevL,-1))
                              WARN(_("Planet '%s' has landing function '%s' which is not found in '%s'."),
                                    planet->name, tmp, LANDING_DATA_PATH);
                           lua_pop(naevL,1);
                        }
#endif /* DEBUGGING */
                     }
                  }
                  else if (xml_isNode(ccur, "refuel"))
                     planet->services |= PLANET_SERVICE_REFUEL | PLANET_SERVICE_INHABITED;
                  else if (xml_isNode(ccur, "bar"))
                     planet->services |= PLANET_SERVICE_BAR | PLANET_SERVICE_INHABITED;
                  else if (xml_isNode(ccur, "missions"))
                     planet->services |= PLANET_SERVICE_MISSIONS | PLANET_SERVICE_INHABITED;
                  else if (xml_isNode(ccur, "commodity"))
                     planet->services |= PLANET_SERVICE_COMMODITY | PLANET_SERVICE_INHABITED;
                  else if (xml_isNode(ccur, "outfits"))
                     planet->services |= PLANET_SERVICE_OUTFITS | PLANET_SERVICE_INHABITED;
                  else if (xml_isNode(ccur, "shipyard"))
                     planet->services |= PLANET_SERVICE_SHIPYARD | PLANET_SERVICE_INHABITED;
                  else if (xml_isNode(ccur, "nomissionspawn"))
                     planet->flags |= PLANET_NOMISNSPAWN;
                  else if (xml_isNode(ccur, "uninhabited"))
                     planet->flags |= PLANET_UNINHABITED;
                  else if (xml_isNode(ccur, "blackmarket"))
                     planet->services |= PLANET_SERVICE_BLACKMARKET;
                  else
                     WARN(_("Planet '%s' has unknown services tag '%s'"), planet->name, ccur->name);
               } while (xml_nextNode(ccur));
            }

            else if (xml_isNode(cur, "commodities")) {
               xmlNodePtr ccur = cur->children;
               do {
                  if (xml_isNode(ccur,"commodity")) {
                     /* If the commodity is standard, don't re-add it. */
                     Commodity *com = commodity_get( xml_get(ccur) );
                     if (com->standard == 1)
                        continue;

                     array_push_back( &comms, com );
                  }
               } while (xml_nextNode(ccur));
            }
            else if (xml_isNode(cur, "blackmarket")) {
               planet_addService(planet, PLANET_SERVICE_BLACKMARKET);
               continue;
            }
         } while (xml_nextNode(cur));
         continue;
      }
      else if (xml_isNode(node, "tech")) {
         planet->tech = tech_groupCreateXML( node );
         continue;
      }
      else if (xml_isNode(node, "tags")) {
         xmlNodePtr cur = node->children;
         planet->tags = array_create( char* );
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur, "tag")) {
               char *tmp = xml_get(cur);
               if (tmp != NULL)
                  array_push_back( &planet->tags, strdup(tmp) );
            }
         } while (xml_nextNode(cur));
         continue;
      }
      WARN(_("Unknown node '%s' in planet '%s'"),node->name,planet->name);
   } while (xml_nextNode(node));

   /* Allow forcing to be uninhabited. */
   if (planet_isFlag(planet, PLANET_UNINHABITED))
      planet->services &= ~PLANET_SERVICE_INHABITED;

/*
 * Verification
 */
#define MELEMENT(o,s)   if (o) WARN(_("Planet '%s' missing '%s' element"), planet->name, s)
   /* Issue warnings on missing items only it the asset is real. */
   MELEMENT(planet->gfx_spaceName==NULL,"GFX space");
   MELEMENT( planet_hasService(planet,PLANET_SERVICE_LAND) &&
         planet->gfx_exterior==NULL,"GFX exterior");
   MELEMENT( planet_hasService(planet,PLANET_SERVICE_INHABITED) &&
         (planet->population==0), "population");
   MELEMENT((flags&FLAG_XSET)==0,"x");
   MELEMENT((flags&FLAG_YSET)==0,"y");
   MELEMENT(planet->class==NULL,"class");
   MELEMENT( planet_hasService(planet,PLANET_SERVICE_LAND) &&
         planet->description==NULL,"description");
   MELEMENT( planet_hasService(planet,PLANET_SERVICE_BAR) &&
         planet->bar_description==NULL,"bar");
   MELEMENT( planet_hasService(planet,PLANET_SERVICE_INHABITED) &&
         (flags&FLAG_FACTIONSET)==0,"faction");
   MELEMENT((flags&FLAG_SERVICESSET)==0,"services");
   MELEMENT( (planet_hasService(planet,PLANET_SERVICE_OUTFITS) ||
            planet_hasService(planet,PLANET_SERVICE_SHIPYARD)) &&
         (planet->tech==NULL), "tech" );
   /*MELEMENT( planet_hasService(planet,PLANET_SERVICE_COMMODITY) &&
         (array_size(planet->commodities)==0),"commodity" );*/
   /*MELEMENT( (flags&FLAG_FACTIONSET) && (planet->presenceAmount == 0.),
         "presence" );*/
#undef MELEMENT

   /* Build commodities list */
   if (planet_hasService(planet, PLANET_SERVICE_COMMODITY)) {
      planet->commodityPrice = array_create( CommodityPrice );
      planet->commodities = array_create( Commodity* );

      /* First, store all the standard commodities and prices. */
      if (array_size(stdList) > 0) {
         for (int i=0; i<array_size(stdList); i++)
            planet_addCommodity( planet, stdList[i] );
      }

      /* Now add extra commodities */
      for (int i=0; i<array_size(comms); i++)
         planet_addCommodity( planet, comms[i] );

      /* Shrink to minimum size. */
      array_shrink( &planet->commodities );
      array_shrink( &planet->commodityPrice );
   }
   /* Free temporary comms list. */
   array_free(comms);

   return 0;
}

/**
 * @brief Adds a planet to a star system.
 *
 *    @param sys Star System to add planet to. (Assumed to belong to systems_stack.)
 *    @param planetname Name of the planet to add.
 *    @return 0 on success.
 */
int system_addPlanet( StarSystem *sys, const char *planetname )
{
   Planet *planet;

   if (sys == NULL)
      return -1;

   planet = planet_get( planetname );
   if (planet == NULL)
      return -1;
   array_push_back( &sys->planets, planet );
   array_push_back( &sys->planetsid, planet->id );

   /* add planet <-> star system to name stack */
   array_push_back( &planetname_stack, planet->name );
   array_push_back( &systemname_stack, sys->name );

   economy_addQueuedUpdate();
   /* This is required to clear the player statistics for this planet */
   economy_clearSinglePlanet(planet);

   /* Reload graphics if necessary. */
   if (cur_system != NULL)
      space_gfxLoad( cur_system );

   return 0;
}

/**
 * @brief Removes a planet from a star system.
 *
 *    @param sys Star System to remove planet from.
 *    @param planetname Name of the planet to remove.
 *    @return 0 on success.
 */
int system_rmPlanet( StarSystem *sys, const char *planetname )
{
   int i, found;
   Planet *planet;

   if (sys == NULL) {
      WARN(_("Unable to remove planet '%s' from NULL system."), planetname);
      return -1;
   }

   /* Try to find planet. */
   planet = planet_get( planetname );
   for (i=0; i<array_size(sys->planets); i++)
      if (sys->planets[i] == planet)
         break;

   /* Planet not found. */
   if (i>=array_size(sys->planets)) {
      WARN(_("Planet '%s' not found in system '%s' for removal."), planetname, sys->name);
      return -1;
   }

   /* Remove planet from system. */
   array_erase( &sys->planets, &sys->planets[i], &sys->planets[i+1] );
   array_erase( &sys->planetsid, &sys->planetsid[i], &sys->planetsid[i+1] );

   /* Remove the presence. */
   space_reconstructPresences(); /* TODO defer this if removing multiple planets at once. */

   /* Remove from the name stack thingy. */
   found = 0;
   for (i=0; i<array_size(planetname_stack); i++)
      if (strcmp(planetname, planetname_stack[i])==0) {
         array_erase( &planetname_stack, &planetname_stack[i], &planetname_stack[i+1] );
         array_erase( &systemname_stack, &systemname_stack[i], &systemname_stack[i+1] );
         found = 1;
         break;
      }
   if (found == 0)
      WARN(_("Unable to find planet '%s' and system '%s' in planet<->system stack."),
            planetname, sys->name );

   system_setFaction(sys);

   economy_addQueuedUpdate();

   return 0;
}

/**
 * @brief Adds a virtual asset to a system.
 *
 *    @param sys System to add virtual asset to.
 *    @param assetname Name of the virtual asset being added.
 */
int system_addVirtualAsset( StarSystem *sys, const char *assetname )
{
   VirtualAsset *va;

   if (sys == NULL)
      return -1;

   va = virtualasset_get( assetname );
   if (va == NULL)
      return -1;
   array_push_back( &sys->assets_virtual, va );

   /* Economy is affected by presence. */
   economy_addQueuedUpdate();

   return 0;
}

/**
 * @brief Removes a virtual asset from a system.
 *
 *    @param sys System to remove virtual asset from.
 *    @param assetname Name of the virtual asset being removed.
 */
int system_rmVirtualAsset( StarSystem *sys, const char *assetname )
{
   int i;

   if (sys == NULL) {
      WARN(_("Unable to remove virtual asset '%s' from NULL system."), assetname);
      return -1;
   }

   /* Try to find virtual asset. */
   for (i=0; i<array_size(sys->assets_virtual); i++)
      if (strcmp(sys->assets_virtual[i]->name, assetname)==0)
         break;

   /* Virtual asset not found. */
   if (i>=array_size(sys->assets_virtual)) {
      WARN(_("Virtual asset '%s' not found in system '%s' for removal."), assetname, sys->name);
      return -1;
   }

   /* Remove virtual asset. */
   array_erase( &sys->assets_virtual, &sys->assets_virtual[i], &sys->assets_virtual[i+1] );

   /* Remove the presence. */
   space_reconstructPresences(); /* TODO defer this if removing multiple assets at once. */
   system_setFaction(sys);

   economy_addQueuedUpdate();

   return 0;
}

/**
 * @brief Adds a jump point to a star system from a diff.
 *
 * Note that economy_execQueued should always be run after this.
 *
 *    @param sys Star System to add jump point to.
 *    @param node Parent node containing jump point information.
 *    @return 0 on success.
 */
int system_addJumpDiff( StarSystem *sys, xmlNodePtr node )
{
   if (system_parseJumpPointDiff(node, sys) <= -1)
      return 0;
   systems_reconstructJumps();
   economy_addQueuedUpdate();

   return 1;
}

/**
 * @brief Adds a jump point to a star system.
 *
 * Note that economy_execQueued should always be run after this.
 *
 *    @param sys Star System to add jump point to.
 *    @param node Parent node containing jump point information.
 *    @return 0 on success.
 */
int system_addJump( StarSystem *sys, xmlNodePtr node )
{
   if (system_parseJumpPoint(node, sys) <= -1)
      return 0;
   systems_reconstructJumps();
   economy_refresh();

   return 1;
}

/**
 * @brief Removes a jump point from a star system.
 *
 * Note that economy_execQueued should always be run after this.
 *
 *    @param sys Star System to remove jump point from.
 *    @param jumpname Name of the jump point to remove.
 *    @return 0 on success.
 */
int system_rmJump( StarSystem *sys, const char *jumpname )
{
   int i;
   JumpPoint *jump;

   if (sys == NULL) {
      WARN(_("Unable to remove jump point '%s' from NULL system."), jumpname);
      return -1;
   }

   /* Try to find planet. */
   jump = jump_get( jumpname, sys );
   for (i=0; i<array_size(sys->jumps); i++)
      if (&sys->jumps[i] == jump)
         break;

   /* Planet not found. */
   if (i>=array_size(sys->jumps)) {
      WARN(_("Jump point '%s' not found in system '%s' for removal."), jumpname, sys->name);
      return -1;
   }

   /* Remove jump from system. */
   array_erase( &sys->jumps, &sys->jumps[i], &sys->jumps[i+1] );

   /* Refresh presence */
   system_setFaction(sys);

   economy_addQueuedUpdate();

   return 0;
}

/**
 * @brief Initializes a new star system with null memory.
 */
static void system_init( StarSystem *sys )
{
   memset( sys, 0, sizeof(StarSystem) );
   sys->planets   = array_create( Planet* );
   sys->assets_virtual = array_create( VirtualAsset* );
   sys->planetsid = array_create( int );
   sys->jumps     = array_create( JumpPoint );
   sys->asteroids = array_create( AsteroidAnchor );
   sys->astexclude= array_create( AsteroidExclusion );
   sys->faction   = -1;
}

/**
 * @brief Creates a new star system.
 */
StarSystem *system_new (void)
{
   StarSystem *sys;
   int id;

#if DEBUGGING
   if (!systems_loading)
      systemstack_changed = 1;
#else /* DEBUGGING */
   if (!systems_loading)
      WARN(_("Creating new system in non-debugging mode. Things are probably going to break horribly."));
#endif /* DEBUGGING */

   /* Protect current system in case of realloc. */
   id = -1;
   if (cur_system != NULL)
      id = system_index( cur_system );

   /* Grow array. */
   sys = &array_grow( &systems_stack );

   /* Reset cur_system. */
   if (id >= 0)
      cur_system = system_getIndex( id );

   /* Initialize system and id. */
   system_init( sys );
   sys->id = array_size(systems_stack)-1;

   /* Reconstruct the jumps, only truely necessary if the systems realloced. */
   if (!systems_loading)
      systems_reconstructJumps();

   return sys;
}

/**
 * @brief Reconstructs the jumps for a single system.
 */
void system_reconstructJumps (StarSystem *sys)
{
   for (int j=0; j<array_size(sys->jumps); j++) {
      double dx, dy, a;
      JumpPoint *jp  = &sys->jumps[j];
      jp->from       = sys;
      jp->target     = system_getIndex( jp->targetid );
      jp->returnJump = jump_getTarget( sys, jp->target );

      /* Get heading. */
      dx = jp->target->pos.x - sys->pos.x;
      dy = jp->target->pos.y - sys->pos.y;
      a = atan2( dy, dx );
      if (a < 0.)
         a += 2.*M_PI;

      /* Update position if needed.. */
      if (jp->flags & JP_AUTOPOS)
         vect_pset( &jp->pos, sys->radius, a );

      /* Update jump specific data. */
      gl_getSpriteFromDir( &jp->sx, &jp->sy, jumppoint_gfx, a );
      jp->angle = 2.*M_PI-a;
      jp->cosa  = cos(jp->angle);
      jp->sina  = sin(jp->angle);
   }
}

/**
 * @brief Reconstructs the jumps.
 */
void systems_reconstructJumps (void)
{
   /* So we need to calculate the shortest jump. */
   for (int i=0; i<array_size(systems_stack); i++) {
      StarSystem *sys = &systems_stack[i];
      system_reconstructJumps(sys);
   }
}

/**
 * @brief Updates the system planet pointers.
 */
void systems_reconstructPlanets (void)
{
   for (int i=0; i<array_size(systems_stack); i++) {
      StarSystem *sys = &systems_stack[i];
      for (int j=0; j<array_size(sys->planetsid); j++)
         sys->planets[j] = &planet_stack[ sys->planetsid[j] ];
   }
}

/**
 * @brief Creates a system from an XML node.
 *
 *    @param sys System to set up.
 *    @param parent XML node to get system from.
 *    @return System matching parent data.
 */
static StarSystem* system_parse( StarSystem *sys, const xmlNodePtr parent )
{
   xmlNodePtr node;
   uint32_t flags;

   /* Clear memory for safe defaults. */
   flags          = 0;
   sys->presence  = array_create( SystemPresence );
   sys->ownerpresence = 0.;
   sys->nebu_hue  = NEBULA_DEFAULT_HUE;

   xmlr_attr_strd( parent, "name", sys->name );

   node  = parent->xmlChildrenNode;
   do { /* load all the data */
      /* Only handle nodes. */
      xml_onlyNodes(node);

      if (xml_isNode(node,"pos")) {
         xmlNodePtr cur = node->children;
         do {
            if (xml_isNode(cur,"x")) {
               flags |= FLAG_XSET;
               sys->pos.x = xml_getFloat(cur);
            }
            else if (xml_isNode(cur,"y")) {
               flags |= FLAG_YSET;
               sys->pos.y = xml_getFloat(cur);
            }
         } while (xml_nextNode(cur));
         continue;
      }
      else if (xml_isNode(node,"general")) {
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            xmlr_strd( cur, "background", sys->background );
            xmlr_strd( cur, "features", sys->features );
            xmlr_int( cur, "stars", sys->stars );
            xmlr_float( cur, "radius", sys->radius );
            if (xml_isNode(cur,"interference")) {
               flags |= FLAG_INTERFERENCESET;
               sys->interference = xml_getFloat(cur);
               continue;
            }
            if (xml_isNode(cur,"nebula")) {
               xmlr_attr_float( cur, "volatility", sys->nebu_volatility );
               xmlr_attr_float_def( cur, "hue", sys->nebu_hue, NEBULA_DEFAULT_HUE );
               sys->nebu_density = xml_getFloat(cur);
               continue;
            }
            if (xml_isNode(cur,"nolanes")) {
               sys_setFlag( sys, SYSTEM_NOLANES );
               continue;
            }
            DEBUG(_("Unknown node '%s' in star system '%s'"),node->name,sys->name);
         } while (xml_nextNode(cur));
         continue;
      }
      /* Loads all the assets. */
      else if (xml_isNode(node,"assets")) {
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur,"asset")) {
               system_addPlanet( sys, xml_get(cur) );
               continue;
            }
            if (xml_isNode(cur,"asset_virtual")) {
               system_addVirtualAsset( sys, xml_get(cur) );
               continue;
            }
            DEBUG(_("Unknown node '%s' in star system '%s'"),node->name,sys->name);
         } while (xml_nextNode(cur));
         continue;
      }

      if (xml_isNode(node, "stats")) {
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            ShipStatList *ll = ss_listFromXML( cur );
            if (ll != NULL) {
               ll->next   = sys->stats;
               sys->stats = ll;
               continue;
            }
            WARN(_("System '%s' has unknown stat '%s'."), sys->name, cur->name);
         } while (xml_nextNode(cur));
         continue;
      }

      if (xml_isNode(node, "tags")) {
         xmlNodePtr cur = node->children;
         sys->tags = array_create( char* );
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur, "tag")) {
               char *tmp = xml_get(cur);
               if (tmp != NULL)
                  array_push_back( &sys->tags, strdup(tmp) );
            }
         } while (xml_nextNode(cur));
         continue;
      }

      /* Avoid warnings. */
      if (xml_isNode(node,"jumps") || xml_isNode(node,"asteroids"))
         continue;

      DEBUG(_("Unknown node '%s' in star system '%s'"),node->name,sys->name);
   } while (xml_nextNode(node));

   array_shrink( &sys->planets );
   array_shrink( &sys->planetsid );

   /* Convert hue from 0 to 359 value to 0 to 1 value. */
   sys->nebu_hue /= 360.;

#define MELEMENT(o,s)      if (o) WARN(_("Star System '%s' missing '%s' element"), sys->name, s)
   if (sys->name == NULL) WARN(_("Star System '%s' missing 'name' tag"), sys->name);
   MELEMENT((flags&FLAG_XSET)==0,"x");
   MELEMENT((flags&FLAG_YSET)==0,"y");
   MELEMENT(sys->stars==0,"stars");
   MELEMENT(sys->radius==0.,"radius");
   MELEMENT((flags&FLAG_INTERFERENCESET)==0,"inteference");
#undef MELEMENT

   return 0;
}

/**
 * @brief Compares two system presences.
 */
static int sys_cmpSysFaction( const void *a, const void *b )
{
   SystemPresence *spa, *spb;

   spa = (SystemPresence*) a;
   spb = (SystemPresence*) b;

   /* Compare value. */
   if (spa->value < spb->value)
      return +1;
   else if (spa->value > spb->value)
      return -1;

   /* Compare faction id. */
   if (spa->faction < spb->faction)
      return +1;
   else if (spa->faction > spb->faction)
      return -1;

   return 0;
}

/**
 * @brief Sets the system faction based on the planets it has.
 *
 *    @param sys System to set the faction of.
 *    @return Faction that controls the system.
 */
void system_setFaction( StarSystem *sys )
{
   /* Sort presences in descending order. */
   if (array_size(sys->presence) != 0)
      qsort( sys->presence, array_size(sys->presence), sizeof(SystemPresence), sys_cmpSysFaction );

   sys->faction = -1;
   for (int i=0; i<array_size(sys->presence); i++) {
      for (int j=0; j<array_size(sys->planets); j++) { /** @todo Handle multiple different factions. */
         Planet *pnt = sys->planets[j];

         if (pnt->presence.faction != sys->presence[i].faction)
            continue;

         sys->faction = pnt->presence.faction;
         return;
      }
   }
}

/**
 * @brief Parses a single jump point for a system, from unidiff.
 *
 *    @param node Parent node containing jump point information.
 *    @param sys System to which the jump point belongs.
 *    @return 0 on success.
 */
static int system_parseJumpPointDiff( const xmlNodePtr node, StarSystem *sys )
{
   JumpPoint *j;
   char *buf;
   double x, y;
   StarSystem *target;

   /* Get target. */
   xmlr_attr_strd( node, "target", buf );
   if (buf == NULL) {
      WARN(_("JumpPoint node for system '%s' has no target attribute."), sys->name);
      return -1;
   }
   target = system_get(buf);
   if (target == NULL) {
      WARN(_("JumpPoint node for system '%s' has invalid target '%s'."), sys->name, buf );
      free(buf);
      return -1;
   }
   free(buf);

#ifdef DEBUGGING
   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *jp = &sys->jumps[i];
      if (jp->targetid != target->id)
         continue;

      WARN(_("Star System '%s' has duplicate jump point to '%s'."),
            sys->name, target->name );
      break;
   }
#endif /* DEBUGGING */

   /* Allocate more space. */
   j = &array_grow( &sys->jumps );
   memset( j, 0, sizeof(JumpPoint) );

   /* Handle jump point position. We want both x and y, or we autoposition the jump point. */
   xmlr_attr_float_def( node, "x", x, HUGE_VAL );
   xmlr_attr_float_def( node, "y", y, HUGE_VAL );

   /* Handle jump point type. */
   xmlr_attr_strd( node, "type", buf );
   if (buf == NULL);
   else if (strcmp(buf, "hidden") == 0)
      jp_setFlag(j,JP_HIDDEN);
   else if (strcmp(buf, "exitonly") == 0)
      jp_setFlag(j,JP_EXITONLY);
   free( buf );

   xmlr_attr_float_def( node, "hide", j->hide, HIDE_DEFAULT_JUMP);

   /* Set some stuff. */
   j->target = target;
   j->targetid = j->target->id;
   j->radius = 200.;

   if (x < HUGE_VAL && y < HUGE_VAL)
      vect_cset( &j->pos, x, y );
   else
      jp_setFlag(j,JP_AUTOPOS);

   return 0;
}

/**
 * @brief Parses a single jump point for a system.
 *
 *    @param node Parent node containing jump point information.
 *    @param sys System to which the jump point belongs.
 *    @return 0 on success.
 */
static int system_parseJumpPoint( const xmlNodePtr node, StarSystem *sys )
{
   JumpPoint *j;
   char *buf;
   xmlNodePtr cur;
   double x, y;
   StarSystem *target;
   int pos;

   /* Get target. */
   xmlr_attr_strd( node, "target", buf );
   if (buf == NULL) {
      WARN(_("JumpPoint node for system '%s' has no target attribute."), sys->name);
      return -1;
   }
   target = system_get(buf);
   if (target == NULL) {
      WARN(_("JumpPoint node for system '%s' has invalid target '%s'."), sys->name, buf );
      free(buf);
      return -1;
   }
   free(buf);

#ifdef DEBUGGING
   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *jp = &sys->jumps[i];
      if (jp->targetid != target->id)
         continue;

      WARN(_("Star System '%s' has duplicate jump point to '%s'."),
            sys->name, target->name );
      break;
   }
#endif /* DEBUGGING */

   /* Allocate more space. */
   j = &array_grow( &sys->jumps );
   memset( j, 0, sizeof(JumpPoint) );

   /* Set some stuff. */
   j->from = sys;
   j->target = target;
   j->targetid = j->target->id;
   j->radius = 200.;

   pos = 0;

   /* Parse data. */
   cur = node->xmlChildrenNode;
   do {
      xmlr_float( cur, "radius", j->radius );

      /* Handle position. */
      if (xml_isNode(cur,"pos")) {
         pos = 1;
         xmlr_attr_float( cur, "x", x );
         xmlr_attr_float( cur, "y", y );

         /* Set position. */
         vect_cset( &j->pos, x, y );
      }
      else if (xml_isNode(cur,"autopos"))
         jp_setFlag(j,JP_AUTOPOS);
      else if (xml_isNode(cur,"hidden"))
         jp_setFlag(j,JP_HIDDEN);
      else if (xml_isNode(cur,"exitonly"))
         jp_setFlag(j,JP_EXITONLY);
      else if (xml_isNode(cur,"hide")) {
         xmlr_float( cur,"hide", j->hide );
      }
   } while (xml_nextNode(cur));

   if (!jp_isFlag(j,JP_AUTOPOS) && !pos)
      WARN(_("JumpPoint in system '%s' is missing pos element but does not have autopos flag."), sys->name);

   return 0;
}

/**
 * @brief Loads the jumps into a system.
 *
 *    @param parent System parent node.
 */
static void system_parseJumps( const xmlNodePtr parent )
{
   StarSystem *sys;
   char* name;
   xmlNodePtr cur, node;

   xmlr_attr_strd( parent, "name", name );
   sys = NULL;
   for (int i=0; i<array_size(systems_stack); i++) {
      if (strcmp( systems_stack[i].name, name)==0) {
         sys = &systems_stack[i];
         break;
      }
   }
   if (sys == NULL) {
      WARN(_("System '%s' was not found in the stack for some reason"),name);
      return;
   }
   free(name); /* no more need for it */

   node  = parent->xmlChildrenNode;

   do { /* load all the data */
      if (xml_isNode(node,"jumps")) {
         cur = node->children;
         do {
            if (xml_isNode(cur,"jump"))
               system_parseJumpPoint( cur, sys );
         } while (xml_nextNode(cur));
      }
   } while (xml_nextNode(node));

   array_shrink( &sys->jumps );
}

/**
 * @brief Parses a single asteroid field for a system.
 *
 *    @param node Parent node containing asteroid field information.
 *    @param sys System.
 *    @return 0 on success.
 */
static int system_parseAsteroidField( const xmlNodePtr node, StarSystem *sys )
{
   AsteroidAnchor *a;
   xmlNodePtr cur;
   double x, y;
   char *name;
   int pos;

   /* Allocate more space. */
   a = &array_grow( &sys->asteroids );
   memset( a, 0, sizeof(AsteroidAnchor) );

   /* Initialize stuff. */
   pos         = 1;
   a->density  = .2;
   a->area     = 0.;
   a->ntype    = 0;
   a->type     = NULL;
   a->radius   = 0.;
   vect_cset( &a->pos, 0., 0. );

   /* Parse label if available. */
   xmlr_attr_strd( node, "label", a->label );

   /* Parse data. */
   cur = node->xmlChildrenNode;
   do {
      xmlr_float( cur,"density", a->density );

      /* Handle types of asteroids. */
      if (xml_isNode(cur,"type")) {
         a->ntype++;
         if (a->type==NULL)
            a->type = malloc( sizeof(int) );
         else
            a->type = realloc( a->type, (a->ntype)*sizeof(int) );

         name = xml_get(cur);
         /* Find the ID */
         for (int i=0; i<array_size(asteroid_types); i++) {
            if ( (strcmp(asteroid_types[i].ID,name)==0) )
               a->type[a->ntype-1] = i;
         }
      }

      xmlr_float( cur, "radius", a->radius );

      /* Handle position. */
      if (xml_isNode(cur,"pos")) {
         pos = 1;
         xmlr_attr_float( cur, "x", x );
         xmlr_attr_float( cur, "y", y );

         /* Set position. */
         vect_cset( &a->pos, x, y );
      }

   } while (xml_nextNode(cur));

   if (!pos)
      WARN(_("Asteroid field in %s has no position."), sys->name);

   if (a->radius == 0.)
      WARN(_("Asteroid field in %s has no radius."), sys->name);

   /* By default, take the first in the list. */
   if (a->type == NULL) {
       a->type = malloc( sizeof(int) );
       a->ntype = 1;
       a->type[0] = 0;
   }

   /* Calculate area */
   a->area = M_PI * a->radius * a->radius;

   /* Compute number of asteroids */
   a->nb      = floor( ABS(a->area) / ASTEROID_REF_AREA * a->density );
   a->ndebris = floor( 100.*a->density );

   return 0;
}

/**
 * @brief Parses a single asteroid exclusion zone for a system.
 *
 *    @param node Parent node containing asteroid exclusion information.
 *    @param sys System.
 *    @return 0 on success.
 */
static int system_parseAsteroidExclusion( const xmlNodePtr node, StarSystem *sys )
{
   AsteroidExclusion *a;
   xmlNodePtr cur;
   double x, y;
   int pos;

   /* Allocate more space. */
   a = &array_grow( &sys->astexclude );
   memset( a, 0, sizeof(*a) );

   /* Initialize stuff. */
   pos         = 0;
   a->radius   = 0.;
   vect_cset( &a->pos, 0., 0. );

   /* Parse data. */
   cur = node->xmlChildrenNode;
   do {
      xmlr_float( cur, "radius", a->radius );

      /* Handle position. */
      if (xml_isNode(cur,"pos")) {
         pos = 1;
         xmlr_attr_float( cur, "x", x );
         xmlr_attr_float( cur, "y", y );

         /* Set position. */
         vect_cset( &a->pos, x, y );
      }

   } while (xml_nextNode(cur));

   if (!pos)
      WARN(_("Asteroid exclusion in %s has no position."), sys->name);

   if (a->radius == 0.)
      WARN(_("Asteroid exclusion in %s has no radius."), sys->name);

   return 0;
}

/**
 * @brief Loads the asteroid anchor into a system.
 *
 *    @param parent System parent node.
 *    @param sys System.
 */
static void system_parseAsteroids( const xmlNodePtr parent, StarSystem *sys )
{
   xmlNodePtr cur, node;

   node  = parent->xmlChildrenNode;

   do { /* load all the data */
      if (xml_isNode(node,"asteroids")) {
         cur = node->children;
         do {
            if (xml_isNode(cur,"asteroid"))
               system_parseAsteroidField( cur, sys );
            else if (xml_isNode(cur,"exclusion"))
               system_parseAsteroidExclusion( cur, sys );
         } while (xml_nextNode(cur));
      }
   } while (xml_nextNode(node));

   array_shrink( &sys->asteroids );
   array_shrink( &sys->astexclude );
}

/**
 * @brief Loads the entire universe into ram - pretty big feat eh?
 *
 *    @return 0 on success.
 */
int space_load (void)
{
   int ret;
   char **asteroid_files, file[PATH_MAX];

   /* Loading. */
   systems_loading = 1;

   /* Create some arrays. */
   planetname_stack = array_create( char* );
   systemname_stack = array_create( char* );

   /* Load jump point graphic - must be before systems_load(). */
   jumppoint_gfx = gl_newSprite(  PLANET_GFX_SPACE_PATH"jumppoint.webp", 4, 4, OPENGL_TEX_MIPMAPS );
   jumpbuoy_gfx = gl_newImage(  PLANET_GFX_SPACE_PATH"jumpbuoy.webp", 0 );

   /* Load planets. */
   ret = planets_load();
   if (ret < 0)
      return ret;

   /* Load virtual assets. */
   ret = virtualassets_load();
   if (ret < 0)
      return ret;

   /* Load asteroid types. */
   ret = asteroidTypes_load();
   if (ret < 0)
      return ret;

   /* Load systems. */
   ret = systems_load();
   if (ret < 0)
      return ret;

   /* Load asteroid graphics. */
   asteroid_files = PHYSFS_enumerateFiles( PLANET_GFX_SPACE_PATH"asteroid/" );
   for (nasterogfx=0; asteroid_files[nasterogfx]!=NULL; nasterogfx++) {}
   asteroid_gfx = malloc( sizeof(glTexture*) * nasterogfx );

   for (size_t i=0; asteroid_files[i]!=NULL; i++) {
      snprintf( file, sizeof(file), "%s%s", PLANET_GFX_SPACE_PATH"asteroid/", asteroid_files[i] );
      asteroid_gfx[i] = gl_newImage( file, OPENGL_TEX_MIPMAPS );
   }

   /* Done loading. */
   systems_loading = 0;

   /* Apply all the presences. */
   for (int i=0; i<array_size(systems_stack); i++)
      system_addAllPlanetsPresence(&systems_stack[i]);

   /* Determine dominant faction. */
   for (int i=0; i<array_size(systems_stack); i++)
      system_setFaction( &systems_stack[i] );

   /* Reconstruction. */
   systems_reconstructJumps();
   systems_reconstructPlanets();

   /* Fine tuning. */
   for (int i=0; i<array_size(systems_stack); i++) {
      StarSystem *sys = &systems_stack[i];

      /* Save jump indexes. */
      for (int j=0; j<array_size(sys->jumps); j++)
         sys->jumps[j].targetid = sys->jumps[j].target->id;
      sys->ownerpresence = system_getPresence( sys, sys->faction );
   }

   /* Calculate commodity prices (sinusoidal model). */
   economy_initialiseCommodityPrices();

   PHYSFS_freeList( asteroid_files );

   return 0;
}

/**
 * @brief Loads the asteroids types.
 *
 *    @return 0 on success.
 */
static int asteroidTypes_load (void)
{
   int namdef, qttdef;
   AsteroidType *at;
   char *str;
   xmlNodePtr node, cur, child;
   xmlDocPtr doc;

   /* Load the data. */
   doc = xml_parsePhysFS( ASTERO_DATA_PATH );
   if (doc == NULL)
      return -1;

   /* Get the root node. */
   node = doc->xmlChildrenNode;
   if (!xml_isNode(node,"Asteroid_types")) {
      WARN( _("Malformed '%s' file: missing root element 'Asteroid_types'"), ASTERO_DATA_PATH);
      return -1;
   }

   /* Get the first node. */
   node = node->xmlChildrenNode; /* first event node */
   if (node == NULL) {
      WARN( _("Malformed '%s' file: does not contain elements"), ASTERO_DATA_PATH);
      return -1;
   }

   asteroid_types = array_create( AsteroidType );
   do {
      if (xml_isNode(node,"asteroid")) {
         /* Load it. */
         at = &array_grow( &asteroid_types );
         at->gfxs = array_create( glTexture* );
         at->material = array_create( Commodity* );
         at->quantity = array_create( int );
         at->armour = 0.;

         cur = node->children;
         do {
            if (xml_isNode(cur,"gfx"))
               array_push_back( &at->gfxs, xml_parseTexture( cur, PLANET_GFX_SPACE_PATH"asteroid/%s", 1, 1,  OPENGL_TEX_MAPTRANS | OPENGL_TEX_MIPMAPS ) );

            else if (xml_isNode(cur,"id"))
               at->ID = xml_getStrd(cur);

            else if (xml_isNode(cur,"armour"))
               at->armour = xml_getFloat(cur);

            else if (xml_isNode(cur,"commodity")) {
               /* Check that name and quantity are defined. */
               namdef = 0; qttdef = 0;

               child = cur->children;
               do {
                  if (xml_isNode(child,"name")) {
                     str = xml_get(child);
                     array_push_back( &at->material, commodity_get( str ) );
                     namdef = 1;
                  }
                  else if (xml_isNode(child,"quantity")) {
                     array_push_back( &at->quantity, xml_getInt(child) );
                     qttdef = 1;
                  }
               } while (xml_nextNode(child));

               if (namdef == 0 || qttdef == 0)
                  ERR(_("Asteroid type's commodity lacks name or quantity."));
            }
         } while (xml_nextNode(cur));

         if (array_size(at->gfxs)==0)
            WARN(_("Asteroid type has no gfx associated."));
      }
   } while (xml_nextNode(node));

   /* Shrink to minimum. */
   array_shrink( &asteroid_types );

   /* Clean up. */
   xmlFreeDoc(doc);

   return 0;
}

/**
 * @brief Loads the entire systems, needs to be called after planets_load.
 *
 * Does multiple passes to load:
 *
 *  - First loads the star systems.
 *  - Next sets the jump routes.
 *
 *    @return 0 on success.
 */
static int systems_load (void)
{
   char **system_files, *file;
   xmlNodePtr node;
   xmlDocPtr doc;
   StarSystem *sys;

   /* Allocate if needed. */
   if (systems_stack == NULL)
      systems_stack = array_create( StarSystem );

   system_files = PHYSFS_enumerateFiles( SYSTEM_DATA_PATH );

   /*
    * First pass - loads all the star systems_stack.
    */
   for (size_t i=0; system_files[i]!=NULL; i++) {
      if (!ndata_matchExt( system_files[i], "xml" ))
         continue;

      asprintf( &file, "%s%s", SYSTEM_DATA_PATH, system_files[i] );
      /* Load the file. */
      doc = xml_parsePhysFS( file );
      if (doc == NULL)
         continue;

      node = doc->xmlChildrenNode; /* first planet node */
      if (node == NULL) {
         WARN(_("Malformed %s file: does not contain elements"),file);
         xmlFreeDoc(doc);
         continue;
      }

      sys = system_new();
      system_parse( sys, node );
      system_parseAsteroids(node, sys); /* load the asteroids anchors */

      /* Clean up. */
      xmlFreeDoc(doc);
      free( file );
   }
   qsort( systems_stack, array_size(systems_stack), sizeof(StarSystem), system_cmp );
   for (int j=0; j<array_size(systems_stack); j++)
      systems_stack[j].id = j;

   /*
    * Second pass - loads all the jump routes.
    */
   for (size_t i=0; system_files[i]!=NULL; i++) {
      asprintf( &file, "%s%s", SYSTEM_DATA_PATH, system_files[i] );
      /* Load the file. */
      doc = xml_parsePhysFS( file );
      free( file );
      file = NULL;
      if (doc == NULL)
         continue;

      node = doc->xmlChildrenNode; /* first planet node */
      if (node == NULL) {
         xmlFreeDoc(doc);
         continue;
      }

      system_parseJumps(node); /* will automatically load the jumps into the system */

      /* Clean up. */
      xmlFreeDoc(doc);
   }

   DEBUG( n_( "Loaded %d Star System", "Loaded %d Star Systems", array_size(systems_stack) ), array_size(systems_stack) );
   DEBUG( n_( "       with %d Planet", "       with %d Planets", array_size(planet_stack) ), array_size(planet_stack) );

   /* Clean up. */
   PHYSFS_freeList( system_files );

   return 0;
}

/**
 * @brief Renders the system.
 *
 *    @param dt Current delta tick.
 */
void space_render( const double dt )
{
   if (cur_system == NULL)
      return;

   if (cur_system->nebu_density > 0.)
      nebu_render(dt);
   else
      background_render(dt);
}

/**
 * @brief Renders the system overlay.
 *
 *    @param dt Current delta tick.
 */
void space_renderOverlay( const double dt )
{
   Pilot *pplayer;
   Solid *psolid;

   if (cur_system == NULL)
      return;

   /* Render the debris. */
   pplayer = pilot_get( PLAYER_ID );
   if (pplayer != NULL) {
      psolid  = pplayer->solid;
      for (int i=0; i < array_size(cur_system->asteroids); i++) {
         double x, y;
         AsteroidAnchor *ast = &cur_system->asteroids[i];
         x = psolid->pos.x - SCREEN_W/2;
         y = psolid->pos.y - SCREEN_H/2;
         for (int j=0; j < ast->ndebris; j++) {
           if (ast->debris[j].height > 1.)
              space_renderDebris( &ast->debris[j], x, y );
         }
      }
   }

   /* Render overlay if necessary. */
   background_renderOverlay( dt );

   if ((cur_system->nebu_density > 0.) &&
         !menu_isOpen( MENU_MAIN ))
      nebu_renderOverlay(dt);
}

/**
 * @brief Renders the current systemsplanets.
 */
void planets_render (void)
{
   Pilot *pplayer;
   Solid *psolid;

   /* Must be a system. */
   if (cur_system==NULL)
      return;

   /* Render the jumps. */
   for (int i=0; i < array_size(cur_system->jumps); i++)
      space_renderJumpPoint( &cur_system->jumps[i], i );

   /* Render the planets. */
   for (int i=0; i < array_size(cur_system->planets); i++)
      space_renderPlanet( cur_system->planets[i] );

   /* Get the player in order to compute the offset for debris. */
   pplayer = pilot_get( PLAYER_ID );
   if (pplayer != NULL)
      psolid  = pplayer->solid;

   /* Render the asteroids & debris. */
   for (int i=0; i < array_size(cur_system->asteroids); i++) {
      AsteroidAnchor *ast = &cur_system->asteroids[i];
      for (int j=0; j < ast->nb; j++)
        space_renderAsteroid( &ast->asteroids[j] );

      if (pplayer != NULL) {
         double x = psolid->pos.x - SCREEN_W/2;
         double y = psolid->pos.y - SCREEN_H/2;
         for (int j=0; j < ast->ndebris; j++) {
           if (ast->debris[j].height < 1.)
              space_renderDebris( &ast->debris[j], x, y );
         }
      }
   }

   /* Render gatherable stuff. */
   gatherable_render();

}

/**
 * @brief Renders a jump point.
 */
static void space_renderJumpPoint( const JumpPoint *jp, int i )
{
   const glColour *c;

   if (!jp_isUsable(jp))
      return;

   if ((player.p != NULL) && (i==player.p->nav_hyperspace) &&
         (pilot_isFlag(player.p, PILOT_HYPERSPACE) || space_canHyperspace(player.p)))
      c = &cGreen;
   else if (jp_isFlag(jp, JP_HIDDEN))
      c = &cRed;
   else
      c = NULL;

   gl_renderSprite( jumppoint_gfx, jp->pos.x, jp->pos.y, jp->sx, jp->sy, c );

   /* Draw buoys next to "highway" jump points. */
   if (jp->hide == 0.) {
      gl_renderSprite( jumpbuoy_gfx, jp->pos.x + 200 * jp->sina, jp->pos.y + 200 * jp->cosa, 0, 0, NULL ); /* Left */
      gl_renderSprite( jumpbuoy_gfx, jp->pos.x + -200 * jp->sina, jp->pos.y + -200 * jp->cosa, 0, 0, NULL ); /* Right */
   }
}

/**
 * @brief Renders a planet.
 */
static void space_renderPlanet( const Planet *p )
{
   gl_renderSprite( p->gfx_space, p->pos.x, p->pos.y, 0, 0, NULL );
}

/**
 * @brief Renders an asteroid.
 */
static void space_renderAsteroid( const Asteroid *a )
{
   double scale, nx, ny;
   AsteroidType *at;
   char c[20];

   /* Skip invisible asteroids */
   if (a->appearing == ASTEROID_INVISIBLE)
      return;

   /* Check if needs scaling. */
   if (a->appearing == ASTEROID_GROWING)
      scale = CLAMP( 0., 1., a->timer / 2. );
   else if (a->appearing == ASTEROID_SHRINKING)
      scale = CLAMP( 0., 1., 1. - a->timer / 2. );
   else
      scale = 1.;

   at = &asteroid_types[a->type];

   gl_renderSpriteInterpolateScale( at->gfxs[a->gfxID], at->gfxs[a->gfxID], 1,
                                  a->pos.x, a->pos.y, scale, scale, 0, 0, NULL );

   /* Add the commodities if scanned. */
   if (!a->scanned) return;
   gl_gameToScreenCoords( &nx, &ny, a->pos.x, a->pos.y );
   for (int i=0; i<array_size(at->material); i++) {
      Commodity *com = at->material[i];
      gl_renderSprite( com->gfx_space, a->pos.x, a->pos.y-10.*i, 0, 0, NULL );
      snprintf(c, sizeof(c), "x%i", at->quantity[i]);
      gl_printRaw( &gl_smallFont, nx+10, ny-5-10.*i, &cFontWhite, -1., c );
   }
}

/**
 * @brief Renders a debris.
 */
static void space_renderDebris( const Debris *d, double x, double y )
{
   double scale;
   Vector2d *testVect;

   scale = 0.5;

   testVect = malloc(sizeof(Vector2d));
   testVect->x = d->pos.x + x;
   testVect->y = d->pos.y + y;

   if (space_isInField( testVect ) == 0)
      gl_renderSpriteInterpolateScale( asteroid_gfx[d->gfxID], asteroid_gfx[d->gfxID], 1,
                                     testVect->x, testVect->y, scale, scale, 0, 0, &cInert );
   free(testVect);
}

/**
 * @brief Cleans up the system.
 */
void space_exit (void)
{
   /* Free standalone graphic textures */
   gl_freeTexture(jumppoint_gfx);
   jumppoint_gfx = NULL;
   gl_freeTexture(jumpbuoy_gfx);
   jumpbuoy_gfx = NULL;

   /* Free asteroid graphics. */
   for (int i=0; i<(int)nasterogfx; i++)
      gl_freeTexture(asteroid_gfx[i]);
   free(asteroid_gfx);

   /* Free the names. */
   array_free(planetname_stack);
   array_free(systemname_stack);

   /* Free the planets. */
   for (int i=0; i < array_size(planet_stack); i++) {
      Planet *pnt = &planet_stack[i];

      free(pnt->name);
      free(pnt->class);
      free(pnt->description);
      free(pnt->bar_description);
      for (int j=0; j<array_size(pnt->tags); j++)
         free( pnt->tags[j] );
      array_free(pnt->tags);

      /* graphics */
      if (pnt->gfx_spaceName != NULL) {
         gl_freeTexture( pnt->gfx_space );
         free(pnt->gfx_spaceName);
         free(pnt->gfx_spacePath);
      }
      if (pnt->gfx_exterior != NULL) {
         free(pnt->gfx_exterior);
         free(pnt->gfx_exteriorPath);
      }

      /* Landing. */
      free(pnt->land_func);
      free(pnt->land_msg);
      free(pnt->bribe_msg);
      free(pnt->bribe_ack_msg);

      /* tech */
      if (pnt->tech != NULL)
         tech_groupDestroy( pnt->tech );

      /* commodities */
      array_free(pnt->commodities);
      array_free(pnt->commodityPrice);
   }
   array_free(planet_stack);

   for (int i=0; i<array_size(vasset_stack); i++) {
      VirtualAsset *va = &vasset_stack[i];
      free( va->name );
      array_free( va->presences );
   }
   array_free( vasset_stack );

   /* Free the systems. */
   for (int i=0; i < array_size(systems_stack); i++) {
      StarSystem *sys = &systems_stack[i];

      free(sys->name);
      free(sys->background);
      free(sys->features);
      array_free(sys->jumps);
      array_free(sys->presence);
      array_free(sys->planets);
      array_free(sys->planetsid);
      array_free(sys->assets_virtual);

      for (int j=0; j<array_size(sys->tags); j++)
         free( sys->tags[j] );
      array_free(sys->tags);

      /* Free the asteroids. */
      for (int j=0; j < array_size(sys->asteroids); j++) {
         AsteroidAnchor *ast = &sys->asteroids[j];
         free(ast->label);
         free(ast->asteroids);
         free(ast->debris);
         free(ast->type);
      }
      array_free(sys->asteroids);
      array_free(sys->astexclude);

      ss_free( sys->stats );
   }
   array_free(systems_stack);
   systems_stack = NULL;

   /* Free the asteroid types. */
   for (int i=0; i < array_size(asteroid_types); i++) {
      AsteroidType *at = &asteroid_types[i];
      free(at->ID);
      array_free(at->material);
      array_free(at->quantity);
      for (int j=0; j<array_size(at->gfxs); j++)
         gl_freeTexture(at->gfxs[j]);
      array_free(at->gfxs);
   }
   array_free(asteroid_types);
   asteroid_types = NULL;

   /* Free the gatherable stack. */
   gatherable_free();

   /* Free landing lua. */
   if (landing_env != LUA_NOREF)
      nlua_freeEnv( landing_env );
   landing_env = LUA_NOREF;
}

/**
 * @brief Clears all system knowledge.
 */
void space_clearKnown (void)
{
   for (int i=0; i<array_size(systems_stack); i++) {
      StarSystem *sys = &systems_stack[i];
      sys_rmFlag(sys,SYSTEM_KNOWN);
      sys_rmFlag(sys,SYSTEM_HIDDEN);
      for (int j=0; j<array_size(sys->jumps); j++)
         jp_rmFlag(&sys->jumps[j],JP_KNOWN);
   }
   for (int j=0; j<array_size(planet_stack); j++)
      planet_rmFlag(&planet_stack[j],PLANET_KNOWN);
}

/**
 * @brief Clears all system markers.
 */
void space_clearMarkers (void)
{
   for (int i=0; i<array_size(systems_stack); i++) {
      StarSystem *sys = &systems_stack[i];
      sys_rmFlag( sys, SYSTEM_MARKED );
      sys->markers_computer = 0;
      sys->markers_plot  = 0;
      sys->markers_high  = 0;
      sys->markers_low   = 0;
   }
   for (int i=0; i<array_size(planet_stack); i++) {
      Planet *pnt = &planet_stack[i];
      planet_rmFlag( pnt, PLANET_MARKED );
      pnt->markers = 0;
   }
}

/**
 * @brief Clears all the system computer markers.
 */
void space_clearComputerMarkers (void)
{
   for (int i=0; i<array_size(systems_stack); i++)
      sys_rmFlag(&systems_stack[i],SYSTEM_CMARKED);
}

static int space_addMarkerSystem( int sysid, MissionMarkerType type )
{
   StarSystem *ssys;
   int *markers;

   /* Get the system. */
   ssys = system_getIndex(sysid);
   if (ssys == NULL)
      return -1;

   /* Get the marker. */
   switch (type) {
      case SYSMARKER_COMPUTER:
         markers = &ssys->markers_computer;
         break;
      case SYSMARKER_LOW:
         markers = &ssys->markers_low;
         break;
      case SYSMARKER_HIGH:
         markers = &ssys->markers_high;
         break;
      case SYSMARKER_PLOT:
         markers = &ssys->markers_plot;
         break;
      default:
         WARN(_("Unknown marker type."));
         return -1;
   }

   /* Decrement markers. */
   (*markers)++;
   sys_setFlag(ssys, SYSTEM_MARKED);

   return 0;
}

static int space_addMarkerPlanet( int pntid, MissionMarkerType type )
{
   const char *sys;
   MissionMarkerType stype;
   Planet *pnt = planet_getIndex( pntid );
   if (pnt==NULL)
      return -1;

   /* Mark planet. */
   pnt->markers++;
   planet_setFlag( pnt, PLANET_MARKED );

   /* Now try to mark system. */
   sys = planet_getSystem( pnt->name );
   if (sys == NULL) {
      WARN(_("Marking planet '%s' that is not in any system!"), pnt->name);
      return 0;
   }
   stype = mission_markerTypePlanetToSystem( type );
   return space_addMarkerSystem( system_index( system_get(sys) ), stype );
}

/**
 * @brief Adds a marker to a system.
 *
 *    @param objid ID of the object to add marker to.
 *    @param type Type of the marker to add.
 *    @return 0 on success.
 */
int space_addMarker( int objid, MissionMarkerType type )
{
   switch (type) {
      case SYSMARKER_COMPUTER:
      case SYSMARKER_LOW:
      case SYSMARKER_HIGH:
      case SYSMARKER_PLOT:
         return space_addMarkerSystem( objid, type );
      case PNTMARKER_COMPUTER:
      case PNTMARKER_LOW:
      case PNTMARKER_HIGH:
      case PNTMARKER_PLOT:
         return space_addMarkerPlanet( objid, type );
      default:
         WARN(_("Unknown marker type."));
         return -1;
   }
   return 0;
}

static int space_rmMarkerSystem( int sys, MissionMarkerType type )
{
   StarSystem *ssys;
   int *markers;

   /* Get the system. */
   ssys = system_getIndex(sys);
   if (ssys == NULL)
      return -1;

   /* Get the marker. */
   switch (type) {
      case SYSMARKER_COMPUTER:
         markers = &ssys->markers_computer;
         break;
      case SYSMARKER_LOW:
         markers = &ssys->markers_low;
         break;
      case SYSMARKER_HIGH:
         markers = &ssys->markers_high;
         break;
      case SYSMARKER_PLOT:
         markers = &ssys->markers_plot;
         break;
      default:
         WARN(_("Unknown marker type."));
         return -1;
   }

   /* Decrement markers. */
   (*markers)--;
   if (*markers <= 0) {
      sys_rmFlag(ssys, SYSTEM_MARKED);
      (*markers) = 0;
   }

   return 0;
}

static int space_rmMarkerPlanet( int pntid, MissionMarkerType type )
{
   (void) type;
   const char *sys;
   MissionMarkerType stype;
   Planet *pnt = planet_getIndex( pntid );

   /* Remove planet marker. */
   pnt->markers--;
   if (pnt->markers <= 0)
      planet_rmFlag( pnt, PLANET_MARKED );

   /* Now try to remove system. */
   sys = planet_getSystem( pnt->name );
   if (sys == NULL)
      return 0;
   stype = mission_markerTypePlanetToSystem( type );
   return space_rmMarkerSystem( system_index( system_get(sys) ), stype );
}

/**
 * @brief Removes a marker from a system.
 *
 *    @param objid ID of the object to remove marker from.
 *    @param type Type of the marker to remove.
 *    @return 0 on success.
 */
int space_rmMarker( int objid, MissionMarkerType type )
{
   switch (type) {
      case SYSMARKER_COMPUTER:
      case SYSMARKER_LOW:
      case SYSMARKER_HIGH:
      case SYSMARKER_PLOT:
         return space_rmMarkerSystem( objid, type );
      case PNTMARKER_COMPUTER:
      case PNTMARKER_LOW:
      case PNTMARKER_HIGH:
      case PNTMARKER_PLOT:
         return space_rmMarkerPlanet( objid, type );
      default:
         WARN(_("Unknown marker type."));
         return -1;
   }
}

/**
 * @brief Saves what is needed to be saved for space.
 *
 *    @param writer XML writer to use.
 *    @return 0 on success.
 */
int space_sysSave( xmlTextWriterPtr writer )
{
   xmlw_startElem(writer,"space");

   for (int i=0; i<array_size(systems_stack); i++) {
      StarSystem *sys;

      if (!sys_isKnown(&systems_stack[i]))
         continue; /* not known */

      xmlw_startElem(writer,"known");

      xmlw_attr(writer,"sys","%s",systems_stack[i].name);

      sys = &systems_stack[i];

      for (int j=0; j<array_size(sys->planets); j++) {
         if (!planet_isKnown(sys->planets[j]))
            continue; /* not known */
         xmlw_elem(writer, "planet", "%s", sys->planets[j]->name);
      }

      for (int j=0; j<array_size(sys->jumps); j++) {
         if (!jp_isKnown(&sys->jumps[j]))
            continue; /* not known */
         xmlw_elem(writer,"jump","%s",(&sys->jumps[j])->target->name);
      }

      xmlw_endElem(writer);
   }

   xmlw_endElem(writer); /* "space" */

   return 0;
}

/**
 * @brief Loads player's space properties from an XML node.
 *
 *    @param parent Parent node for space.
 *    @return 0 on success.
 */
int space_sysLoad( xmlNodePtr parent )
{
   xmlNodePtr node, cur;
   StarSystem *sys;
   char *str;

   space_clearKnown();

   node = parent->xmlChildrenNode;
   do {
      if (xml_isNode(node,"space")) {
         cur = node->xmlChildrenNode;

         do {
            if (xml_isNode(cur,"known")) {
               xmlr_attr_strd(cur,"sys",str);
               if (str != NULL) { /* check for 5.0 saves */
                  sys = system_get(str);
                  free(str);
               }
               else /* load from 5.0 saves */
                  sys = system_get(xml_get(cur));
               if (sys != NULL) { /* Must exist */
                  sys_setFlag(sys,SYSTEM_KNOWN);
                  space_parseAssets(cur, sys);
               }
            }
         } while (xml_nextNode(cur));
      }
   } while (xml_nextNode(node));

   return 0;
}

/**
 * @brief Parses assets in a system.
 *
 *    @param parent Node of the system.
 *    @param sys System to populate.
 *    @return 0 on success.
 */
static int space_parseAssets( xmlNodePtr parent, StarSystem* sys )
{
   xmlNodePtr node = parent->xmlChildrenNode;
   do {
      if (xml_isNode(node,"planet")) {
         Planet *planet = planet_get(xml_get(node));
         if (planet != NULL) /* Must exist */
            planet_setKnown(planet);
      }
      else if (xml_isNode(node,"jump")) {
         JumpPoint *jp = jump_get(xml_get(node), sys);
         if (jp != NULL) /* Must exist */
            jp_setFlag(jp,JP_KNOWN);
      }
   } while (xml_nextNode(node));

   return 0;
}

/**
 * @brief Gets the index of the presence element for a faction.
 *          Creates one if it doesn't exist.
 *
 *    @param sys Pointer to the system to check.
 *    @param faction The index of the faction to search for.
 *    @return The index of the presence array for faction.
 */
static int getPresenceIndex( StarSystem *sys, int faction )
{
   int n;

   /* Check for NULL and display a warning. */
   if (sys == NULL) {
      WARN("sys == NULL");
      return 0;
   }

   /* Go through the array (if created), looking for the faction. */
   for (int i=0; i < array_size(sys->presence); i++)
      if (sys->presence[i].faction == faction)
         return i;

   /* Grow the array. */
   n = array_size(sys->presence);
   memset(&array_grow(&sys->presence), 0, sizeof(SystemPresence));
   sys->presence[n].faction = faction;

   return n;
}

/**
 * @brief Adds (or removes) some presence to a system.
 *
 *    @param sys Pointer to the system to add to or remove from.
 *    @param ap Asset presence to add.
 */
void system_presenceAddAsset( StarSystem *sys, const AssetPresence *ap )
{
   int i, x, curSpill;
   Queue q, qn;
   StarSystem *cur;
   double spillfactor;
   int faction = ap->faction;
   double base = ap->base;
   double bonus = ap->bonus;
   double range = ap->range;
   int usehidden = faction_usesHiddenJumps( faction );
   const FactionGenerator *fgens;

   /* Check for NULL and display a warning. */
   if (sys == NULL) {
      WARN("sys == NULL");
      return;
   }

   /* Check that we have a valid faction. */
   if (faction_isFaction(faction) == 0)
      return;

   /* Check that we're actually adding any. */
   if ((base == 0.) && (bonus == 0.))
      return;

   /* Get secondary if applicable. */
   fgens = faction_generators( faction );

   /* Add the presence to the current system. */
   i = getPresenceIndex(sys, faction);
   sys->presence[i].base   = MAX( sys->presence[i].base, base );
   sys->presence[i].bonus += bonus;
   sys->presence[i].value  = sys->presence[i].base + sys->presence[i].bonus;
   for (i=0; i<array_size(fgens); i++) {
      x = getPresenceIndex(sys, fgens[i].id);
      sys->presence[x].base   = MAX( sys->presence[x].base, MAX(0., base*fgens[i].weight) );
      sys->presence[x].bonus += MAX(0., bonus*fgens[i].weight);
      sys->presence[x].value  = sys->presence[x].base + sys->presence[x].bonus;
   }

   /* If there's no range, we're done here. */
   if (range < 1)
      return;

   /* Add the spill. */
   sys->spilled   = 1;
   curSpill       = 0;
   q              = q_create();
   qn             = q_create();

   /* Create the initial queue consisting of sys adjacencies. */
   for (i=0; i < array_size(sys->jumps); i++) {
      if (sys->jumps[i].target->spilled == 0 && (usehidden || !jp_isFlag( &sys->jumps[i], JP_HIDDEN )) && !jp_isFlag( &sys->jumps[i], JP_EXITONLY )) {
         q_enqueue( q, sys->jumps[i].target );
         sys->jumps[i].target->spilled = 1;
      }
   }

   /* If it's empty, something's wrong. */
   if (q_isEmpty(q)) {
      /* Means system isn't connected. */
      /*WARN(_("q is empty after getting adjacencies of %s."), sys->name);*/
      q_destroy(q);
      q_destroy(qn);
      goto sys_cleanup;
      return;
   }

   while (curSpill < range) {
      /* Pull one off the current range queue. */
      cur = q_dequeue(q);

      /* Ran out of candidates before running out of spill range! */
      if (cur == NULL)
         break;

      /* Enqueue all its adjacencies to the next range queue. */
      for (i=0; i<array_size(cur->jumps); i++) {
         if (cur->jumps[i].target->spilled == 0 && (usehidden || !jp_isFlag( &cur->jumps[i], JP_HIDDEN )) && !jp_isFlag( &cur->jumps[i], JP_EXITONLY )) {
            q_enqueue( qn, cur->jumps[i].target );
            cur->jumps[i].target->spilled = 1;
         }
      }

      /* Spill some presence. */
      x = getPresenceIndex(cur, faction);
      spillfactor = 1. / (2. + (double)curSpill);
      cur->presence[x].base   = MAX( cur->presence[x].base, base * spillfactor );
      cur->presence[x].bonus += bonus * spillfactor;
      cur->presence[x].value  = cur->presence[x].base + cur->presence[x].bonus;

      for (i=0; i<array_size(fgens); i++) {
         x = getPresenceIndex(cur, fgens[i].id);
         cur->presence[x].base   = MAX( cur->presence[x].base, MAX(0., base*spillfactor*fgens[i].weight) );
         cur->presence[x].bonus += MAX(0., bonus*spillfactor*fgens[i].weight );
         cur->presence[x].value  = cur->presence[x].base + cur->presence[x].bonus;
      }

      /* Check to see if we've finished this range and grab the next queue. */
      if (q_isEmpty(q)) {
         curSpill++;
         q_destroy(q);
         q  = qn;
         qn = q_create();
      }
   }

   /* Destroy the queues. */
   q_destroy(q);
   q_destroy(qn);

sys_cleanup:
   /* Clean up our mess. */
   for (i=0; i < array_size(systems_stack); i++)
      systems_stack[i].spilled = 0;
   return;
}

/**
 * @brief Get the presence of a faction in a system.
 *
 *    @param sys Pointer to the system to process.
 *    @param faction The faction to get the presence for.
 *    @return The amount of presence the faction has in the system.
 */
double system_getPresence( const StarSystem *sys, int faction )
{
   /* Check for NULL and display a warning. */
#if DEBUGGING
   if (sys == NULL) {
      WARN("sys == NULL");
      return 0;
   }
#endif /* DEBUGGING */

   /* Go through the array, looking for the faction. */
   for (int i=0; i < array_size(sys->presence); i++) {
      if (sys->presence[i].faction == faction)
         return MAX(sys->presence[i].value, 0);
   }

   /* If it's not in there, it's zero. */
   return 0.;
}

/**
 * @brief Get the presence of a faction in a system.
 *
 *    @param sys Pointer to the system to process.
 *    @param faction The faction to get the presence for.
 *    @param[out] base Base value of the presence.
 *    @param[out] bonus Bonus value of the presence.
 *    @return The amount of presence the faction has in the system.
 */
double system_getPresenceFull( const StarSystem *sys, int faction, double *base, double *bonus )
{
   /* Check for NULL and display a warning. */
#if DEBUGGING
   if (sys == NULL) {
      WARN("sys == NULL");
      return 0;
   }
#endif /* DEBUGGING */

   /* Go through the array, looking for the faction. */
   for (int i=0; i < array_size(sys->presence); i++) {
      if (sys->presence[i].faction == faction) {
         *base = sys->presence[i].base;
         *bonus = sys->presence[i].bonus;
         return MAX(sys->presence[i].value, 0);
      }
   }

   /* If it's not in there, it's zero. */
   *base = 0.;
   *bonus = 0.;
   return 0.;
}

/**
 * @brief Go through all the assets and call system_addPresence().
 *
 *    @param sys Pointer to the system to process.
 */
void system_addAllPlanetsPresence( StarSystem *sys )
{
   /* Check for NULL and display a warning. */
#if DEBUGGING
   if (sys == NULL) {
      WARN("sys == NULL");
      return;
   }
#endif /* DEBUGGING */

   /* Real planets. */
   for (int i=0; i<array_size(sys->planets); i++)
      system_presenceAddAsset(sys, &sys->planets[i]->presence );

   /* Virtual assets. */
   for (int i=0; i<array_size(sys->assets_virtual); i++)
      for (int j=0; j<array_size(sys->assets_virtual[i]->presences); j++)
         system_presenceAddAsset(sys, &sys->assets_virtual[i]->presences[j] );
}

/**
 * @brief Reset the presence of all systems.
 */
void space_reconstructPresences( void )
{
   /* Reset the presence in each system. */
   for (int i=0; i<array_size(systems_stack); i++) {
      array_free(systems_stack[i].presence);
      systems_stack[i].presence  = array_create( SystemPresence );
      systems_stack[i].ownerpresence = 0.;
   }

   /* Re-add presence to each system. */
   for (int i=0; i<array_size(systems_stack); i++)
      system_addAllPlanetsPresence(&systems_stack[i]);

   /* Determine dominant faction. */
   for (int i=0; i<array_size(systems_stack); i++) {
      system_setFaction( &systems_stack[i] );
      systems_stack[i].ownerpresence = system_getPresence( &systems_stack[i], systems_stack[i].faction );
   }

   /* Have to redo the scheduler because everything changed. */
   /* TODO this actually ignores existing presence and will temporarily increase system presence more than normal... */
   if (cur_system != NULL)
      system_scheduler( 0., 1 );
}

/**
 * @brief See if the position is in an asteroid field.
 *
 *    @param p pointer to the position.
 *    @return -1 If false; index of the field otherwise.
 */
int space_isInField( const Vector2d *p )
{
   /* Always return -1 if in an exclusion zone */
   for (int i=0; i < array_size(cur_system->astexclude); i++) {
      AsteroidExclusion *e = &cur_system->astexclude[i];
      if (vect_dist2( p, &e->pos ) <= pow2(e->radius))
         return -1;
   }

   /* Check if in asteroid field */
   for (int i=0; i < array_size(cur_system->asteroids); i++) {
      AsteroidAnchor *a = &cur_system->asteroids[i];
      if (vect_dist2( p, &a->pos ) <= pow2(a->radius))
         return i;
   }

   return -1;
}

/**
 * @brief Returns the asteroid type corresponding to an ID
 *
 *    @param ID ID of the type.
 *    @return AsteroidType object.
 */
const AsteroidType *space_getType ( int ID )
{
   return &asteroid_types[ ID ];
}

/**
 * @brief Hits an asteroid.
 *
 *    @param a hit asteroid
 *    @param dmg Damage being done
 */
void asteroid_hit( Asteroid *a, const Damage *dmg )
{
   double darmour;
   dtype_calcDamage( NULL, &darmour, 1, NULL, dmg, NULL );

   a->armour -= darmour;
   if (a->armour <= 0) {
      a->appearing = ASTEROID_EXPLODING;
      a->timer = 0.;
   }
}

/**
 * @brief Makes an asteroid explode.
 *
 *    @param a asteroid to make explode
 *    @param field Asteroid field the asteroid belongs to.
 *    @param give_reward Whether a pilot blew the asteroid up and should be rewarded.
 */
static void asteroid_explode ( Asteroid *a, AsteroidAnchor *field, int give_reward )
{
   Damage dmg;
   char buf[16];

   /* Manage the explosion */
   dmg.type          = dtype_get("explosion_splash");
   dmg.damage        = 100.;
   dmg.penetration   = 1.; /* Full penetration. */
   dmg.disable       = 0.;
   expl_explode( a->pos.x, a->pos.y, a->vel.x, a->vel.y,
                 50., &dmg, NULL, EXPL_MODE_SHIP );

   /* Play random explosion sound. */
   snprintf(buf, sizeof(buf), "explosion%d", RNG(0,2));
   sound_playPos( sound_get(buf), a->pos.x, a->pos.y, a->vel.x, a->vel.y );

   if (give_reward) {
      /* Release commodity. */
      AsteroidType *at = &asteroid_types[a->type];

      for (int i=0; i < array_size(at->material); i++) {
         int nb = RNG(0,at->quantity[i]);
         Commodity *com = at->material[i];
         for (int j=0; j < nb; j++) {
            Vector2d pos, vel;
            pos = a->pos;
            vel = a->vel;
            pos.x += (RNGF()*30.-15.);
            pos.y += (RNGF()*30.-15.);
            vel.x += (RNGF()*20.-10.);
            vel.y += (RNGF()*20.-10.);
            gatherable_init( com, pos, vel, -1., RNG(1,5) );
         }
      }
   }

   /* Remove the asteroid target to any pilot. */
   pilot_untargetAsteroid( a->parent, a->id );

   /* Make it respawn elsewhere */
   asteroid_init( a, field );
}

/**
 * @brief See if the system has a planet or station.
 *
 *    @param sys Pointer to the system to process.
 *    @return 0 If empty; otherwise 1.
 */
int system_hasPlanet( const StarSystem *sys )
{
   /* Check for NULL and display a warning. */
   if (sys == NULL) {
      WARN("sys == NULL");
      return 0;
   }

   /* Go through all the assets and look for a real one. */
   for (int i=0; i < array_size(sys->planets); i++)
      return 1;

   return 0;
}

/**
 * @brief Removes active presence.
 */
void system_rmCurrentPresence( StarSystem *sys, int faction, double amount )
{
   int id;
   nlua_env env;
   SystemPresence *presence;

   /* Remove the presence. */
   id = getPresenceIndex( cur_system, faction );
   sys->presence[id].curUsed -= amount;

   /* Safety. */
   presence = &sys->presence[id];
   presence->curUsed = MAX( 0, sys->presence[id].curUsed );

   /* Run lower hook. */
   env = faction_getScheduler( faction );

   /* Run decrease function if applicable. */
   nlua_getenv( env, "decrease" ); /* f */
   if (lua_isnil(naevL,-1)) {
      lua_pop(naevL,1);
      return;
   }
   lua_pushnumber( naevL, presence->curUsed ); /* f, cur */
   lua_pushnumber( naevL, presence->value );   /* f, cur, max */
   lua_pushnumber( naevL, presence->timer );   /* f, cur, max, timer */

   /* Actually run the function. */
   if (nlua_pcall(env, 3, 1)) { /* error has occurred */
      WARN(_("Lua decrease script for faction '%s' : %s"),
            faction_name( faction ), lua_tostring(naevL,-1));
      lua_pop(naevL,1);
      return;
   }

   /* Output is handled the same way. */
   if (!lua_isnumber(naevL,-1)) {
      WARN(_("Lua spawn script for faction '%s' failed to return timer value."),
            faction_name( presence->faction ) );
      lua_pop(naevL,1);
      return;
   }
   presence->timer = lua_tonumber(naevL,-1);
   lua_pop(naevL,1);
}

/**
 * @brief Cues a planet to be landed on. This is not done immediately, but when
 * the engine thinks it is ok to do.
 *
 *    @param pnt Planet to land on.
 */
void space_queueLand( Planet *pnt )
{
   space_landQueuePlanet = pnt;
}

/**
 * @brief Gets the population in an approximated string. Note this function changes the string value each call, so be careful!
 *
 *    @param population Population to get string of.
 *    @return String corresponding to the population.
 */
const char *space_populationStr( uint64_t population )
{
   static char pop[STRMAX_SHORT];
   double p = (double)population;

   /* Out of respect for the first version of this, do something fancy and human-oriented.
    * However, specifying a thousand/million/billion system failed in a few ways: needing 2x as many cases as
    * intended to avoid silliness (1.0e10 -> 10000 million), and not being gettext-translatable to other number
    * systems like the Japanese one. */

   if (p < 1.0e3)
      snprintf( pop, sizeof(pop), "%.0f", p );
   else {
      char scratch[STRMAX_SHORT];
      const char *digits[] = {"\xe2\x81\xb0", "\xc2\xb9", "\xc2\xb2", "\xc2\xb3", "\xe2\x81\xb4", "\xe2\x81\xb5", "\xe2\x81\xb6", "\xe2\x81\xb7", "\xe2\x81\xb8", "\xe2\x81\xb9"};
      int state = 0,  COEF = 0, E = 1, EXP = 4;
      size_t l = 0;
      snprintf( scratch, sizeof(scratch), "%.1e", p );
      for (const char *c = scratch; *c; c++) {
         if (state == COEF && *c != 'e')
            l += scnprintf( &pop[l], sizeof(pop)-l, "%c", *c );
         else if (state == COEF ) {
            l += scnprintf( &pop[l], sizeof(pop)-l, "%s", "\xc2\xb7" "10" );
            state = E;
         }
         else if (state == E && (*c == '+' || *c == '0'))
            state = E;
         else {
            state = EXP;
            l += scnprintf( &pop[l], sizeof(pop)-l, "%s", digits[*c-'0'] );
         }
      }
   }

   return pop;
}
