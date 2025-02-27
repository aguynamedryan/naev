/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file mission.c
 *
 * @brief Handles missions.
 */
/** @cond */
#include <stdint.h>
#include <stdlib.h>

#include "naev.h"
/** @endcond */

#include "mission.h"

#include "array.h"
#include "cond.h"
#include "faction.h"
#include "gui_osd.h"
#include "hook.h"
#include "land.h"
#include "log.h"
#include "ndata.h"
#include "nlua.h"
#include "nlua_faction.h"
#include "nlua_misn.h"
#include "nlua_ship.h"
#include "nlua_shiplog.h"
#include "nluadef.h"
#include "npc.h"
#include "nstring.h"
#include "nxml.h"
#include "nxml_lua.h"
#include "player.h"
#include "rng.h"
#include "space.h"

#define XML_MISSION_TAG       "mission" /**< XML mission tag. */

/*
 * current player missions
 */
static unsigned int mission_id = 0; /**< Mission ID generator. */
Mission *player_missions[MISSION_MAX]; /**< Player's active missions. */

/*
 * mission stack
 */
static MissionData *mission_stack = NULL; /**< Unmutable after creation */

/*
 * prototypes
 */
/* static */
/* Generation. */
static unsigned int mission_genID (void);
static int mission_init( Mission* mission, MissionData* misn, int genid, int create, unsigned int *id );
static void mission_freeData( MissionData* mission );
/* Matching. */
static int mission_compare( const void* arg1, const void* arg2 );
static int mission_meetReq( int mission, int faction,
      const char* planet, const char* sysname );
static int mission_matchFaction( MissionData* misn, int faction );
static int mission_location( const char *loc );
/* Loading. */
static int missions_cmp( const void *a, const void *b );
static int mission_parseFile( const char* file, MissionData *temp );
static int mission_parseXML( MissionData *temp, const xmlNodePtr parent );
static int missions_parseActive( xmlNodePtr parent );
/* Misc. */
static const char* mission_markerTarget( MissionMarker *m );
static int mission_markerLoad( Mission *misn, xmlNodePtr node );

/**
 * @brief Generates a new id for the mission.
 *
 *    @return New id for the mission.
 */
static unsigned int mission_genID (void)
{
   unsigned int id = ++mission_id; /* default id, not safe if loading */
   /* we save mission ids, so check for collisions with player's missions */
   for (int i=0; i<MISSION_MAX; i++)
      if (id == player_missions[i]->id) /* mission id was loaded from save */
         return mission_genID(); /* recursively try again */
   return id;
}

/**
 * @brief Gets id from mission name.
 *
 *    @param name Name to match.
 *    @return id of the matching mission.
 */
int mission_getID( const char* name )
{
   for (int i=0; i<array_size(mission_stack); i++)
      if (strcmp(name,mission_stack[i].name)==0)
         return i;

   DEBUG(_("Mission '%s' not found in stack"), name);
   return -1;
}

/**
 * @brief Gets a MissionData based on ID.
 *
 *    @param id ID to match.
 *    @return MissonData matching ID.
 */
MissionData* mission_get( int id )
{
   if ((id < 0) || (id >= array_size(mission_stack))) return NULL;
   return &mission_stack[id];
}

/**
 * @brief Gets mission data from a name.
 */
MissionData* mission_getFromName( const char* name )
{
   int id = mission_getID( name );
   if (id < 0)
      return NULL;

   return mission_get( id );
}

/**
 * @brief Initializes a mission.
 *
 *    @param mission Mission to initialize.
 *    @param misn Data to use.
 *    @param genid 1 if should generate id, 0 otherwise.
 *    @param create 1 if should run create function, 0 otherwise.
 *    @param[out] id ID of the newly created mission.
 *    @return 0 on success.
 */
static int mission_init( Mission* mission, MissionData* misn, int genid, int create, unsigned int *id )
{
   /* clear the mission */
   memset( mission, 0, sizeof(Mission) );

   /* Create id if needed. */
   mission->id    = (genid) ? mission_genID() : 0;

   if (id != NULL)
      *id         = mission->id;
   mission->data  = misn;
   if (create) {
      mission->title = strdup(_(misn->name));
      mission->desc  = strdup(_("No description."));
   }

   /* init Lua */
   mission->env = nlua_newEnv(1);

   misn_loadLibs( mission->env ); /* load our custom libraries */

   /* Create the "mem" table for persistence. */
   lua_newtable(naevL);
   nlua_setenv(mission->env, "mem");

   /* load the file */
   if (nlua_dobufenv(mission->env, misn->lua, strlen(misn->lua), misn->sourcefile) != 0) {
      WARN(_("Error loading mission file: %s\n"
          "%s\n"
          "Most likely Lua file has improper syntax, please check"),
           misn->sourcefile, lua_tostring(naevL, -1));
      return -1;
   }

   /* run create function */
   if (create) {
      /* Failed to create. */
      int ret = misn_run( mission, "create");
      if (ret) {
         mission_cleanup(mission);
         return ret;
      }
   }

   return 0;
}

/**
 * @brief Small wrapper for misn_run.
 *
 *    @param mission Mission to accept.
 *    @return -1 on error, 1 on misn.finish() call, 2 if mission got deleted
 *            and 0 normally.
 *
 * @sa misn_run
 */
int mission_accept( Mission* mission )
{
   return misn_run( mission, "accept" );
}

/**
 * @brief Checks to see if mission is already running.
 *
 *    @param misn Mission to check if is already running.
 *    @return 1 if already running, 0 if isn't.
 */
int mission_alreadyRunning( const MissionData* misn )
{
   for (int i=0; i<MISSION_MAX; i++)
      if (player_missions[i]->data == misn)
         return 1;
   return 0;
}

/**
 * @brief Checks to see if a mission meets the requirements.
 *
 *    @param mission ID of the mission to check.
 *    @param faction Faction of the current planet.
 *    @param planet Name of the current planet.
 *    @param sysname Name of the current system.
 *    @return 1 if requirements are met, 0 if they aren't.
 */
static int mission_meetReq( int mission, int faction,
      const char* planet, const char* sysname )
{
   MissionData* misn = mission_get( mission );
   if (misn == NULL) /* In case it doesn't exist */
      return 0;

   /* If planet, must match planet. */
   if ((misn->avail.planet != NULL) && (strcmp(misn->avail.planet,planet)!=0))
      return 0;

   /* If system, must match system. */
   if ((misn->avail.system != NULL) && (strcmp(misn->avail.system,sysname)!=0))
      return 0;

   /* Match faction. */
   if ((faction >= 0) && !mission_matchFaction(misn,faction))
      return 0;

   /* Must not be already done or running if unique. */
   if (mis_isFlag(misn,MISSION_UNIQUE) &&
         (player_missionAlreadyDone(mission) ||
          mission_alreadyRunning(misn)))
      return 0;

   /* Must meet Lua condition. */
   if (misn->avail.cond != NULL) {
      int c = cond_check(misn->avail.cond);
      if (c < 0) {
         WARN(_("Conditional for mission '%s' failed to run"), misn->name);
         return 0;
      }
      else if (!c)
         return 0;
   }

   /* Must meet previous mission requirements. */
   if ((misn->avail.done != NULL) &&
         (player_missionAlreadyDone( mission_getID(misn->avail.done) ) == 0))
      return 0;

  return 1;
}

/**
 * @brief Runs missions matching location, all Lua side and one-shot.
 *
 *    @param loc Location to match.
 *    @param faction Faction of the planet.
 *    @param planet Name of the current planet.
 *    @param sysname Name of the current system.
 */
void missions_run( int loc, int faction, const char* planet, const char* sysname )
{
   for (int i=0; i<array_size(mission_stack); i++) {
      Mission mission;
      double chance;
      MissionData *misn = &mission_stack[i];

      if (misn->avail.loc != loc)
         continue;

      if (!mission_meetReq(i, faction, planet, sysname))
         continue;

      chance = (double)(misn->avail.chance % 100)/100.;
      if (chance == 0.) /* We want to consider 100 -> 100% not 0% */
         chance = 1.;

      if (RNGF() < chance) {
         mission_init( &mission, misn, 1, 1, NULL );
         mission_cleanup(&mission); /* it better clean up for itself or we do it */
      }
   }
}

/**
 * @brief Starts a mission.
 *
 *  Mission must still call misn.accept() to actually get added to the player's
 * active missions.
 *
 *    @param name Name of the mission to start.
 *    @param[out] id ID of the newly created mission.
 *    @return 0 on success, >0 on forced exit (misn.finish), <0 on error.
 */
int mission_start( const char *name, unsigned int *id )
{
   Mission mission;
   MissionData *mdat;
   int ret;

   /* Try to get the mission. */
   mdat = mission_get( mission_getID(name) );
   if (mdat == NULL)
      return -1;

   /* Try to run the mission. */
   ret = mission_init( &mission, mdat, 1, 1, id );
   /* Add to mission giver if necessary. */
   if (landed && (ret==0) && (mdat->avail.loc==MIS_AVAIL_BAR))
      npc_patchMission( &mission );
   else
      mission_cleanup( &mission ); /* Clean up in case not accepted. */

   return ret;
}

/**
 * @brief Gets the name of the mission marker target.
 */
static const char* mission_markerTarget( MissionMarker *m )
{
   switch (m->type) {
      case SYSMARKER_COMPUTER:
      case SYSMARKER_LOW:
      case SYSMARKER_HIGH:
      case SYSMARKER_PLOT:
         return system_getIndex( m->objid )->name;
      case PNTMARKER_COMPUTER:
      case PNTMARKER_LOW:
      case PNTMARKER_HIGH:
      case PNTMARKER_PLOT:
         return planet_getIndex( m->objid )->name;
      default:
         WARN(_("Unknown marker type."));
         return NULL;
   }
}

MissionMarkerType mission_markerTypePlanetToSystem( MissionMarkerType t )
{
   switch (t) {
      case SYSMARKER_COMPUTER:
      case SYSMARKER_LOW:
      case SYSMARKER_HIGH:
      case SYSMARKER_PLOT:
         return t;
      case PNTMARKER_COMPUTER:
         return SYSMARKER_COMPUTER;
      case PNTMARKER_LOW:
         return SYSMARKER_LOW;
      case PNTMARKER_HIGH:
         return SYSMARKER_HIGH;
      case PNTMARKER_PLOT:
         return SYSMARKER_PLOT;
      default:
         WARN(_("Unknown marker type."));
         return -1;
   }
}

MissionMarkerType mission_markerTypeSystemToPlanet( MissionMarkerType t )
{
   switch (t) {
      case SYSMARKER_COMPUTER:
         return PNTMARKER_COMPUTER;
      case SYSMARKER_LOW:
         return PNTMARKER_LOW;
      case SYSMARKER_HIGH:
         return PNTMARKER_HIGH;
      case SYSMARKER_PLOT:
         return PNTMARKER_PLOT;
      case PNTMARKER_COMPUTER:
      case PNTMARKER_LOW:
      case PNTMARKER_HIGH:
      case PNTMARKER_PLOT:
         return t;
      default:
         WARN(_("Unknown marker type."));
         return -1;
   }
}

/**
 * @brief Loads a mission marker from xml.
 */
static int mission_markerLoad( Mission *misn, xmlNodePtr node )
{
   int id;
   MissionMarkerType type;
   StarSystem *ssys;
   Planet *pnt;

   xmlr_attr_int_def( node, "id", id, -1 );
   xmlr_attr_int_def( node, "type", type, -1 );

   switch (type) {
      case SYSMARKER_COMPUTER:
      case SYSMARKER_LOW:
      case SYSMARKER_HIGH:
      case SYSMARKER_PLOT:
         ssys = system_get( xml_get( node ));
         if (ssys == NULL) {
            WARN( _("Mission Marker to system '%s' does not exist"), xml_get( node ) );
            return -1;
         }
         return mission_addMarker( misn, id, system_index(ssys), type );
      case PNTMARKER_COMPUTER:
      case PNTMARKER_LOW:
      case PNTMARKER_HIGH:
      case PNTMARKER_PLOT:
         pnt = planet_get( xml_get( node ));
         if (pnt == NULL) {
            WARN( _("Mission Marker to planet '%s' does not exist"), xml_get( node ) );
            return -1;
         }
         return mission_addMarker( misn, id, planet_index(pnt), type );
      default:
         WARN(_("Unknown marker type."));
         return -1;
   }
}

/**
 * @brief Adds a system marker to a mission.
 */
int mission_addMarker( Mission *misn, int id, int objid, MissionMarkerType type )
{
   MissionMarker *marker;

   /* Create array. */
   if (misn->markers == NULL)
      misn->markers = array_create( MissionMarker );

   /* Avoid ID collisions. */
   if (id < 0) {
      int m = -1;
      for (int i=0; i<array_size(misn->markers); i++)
         if (misn->markers[i].id > m)
            m = misn->markers[i].id;
      id = m+1;
   }

   /* Create the marker. */
   marker      = &array_grow( &misn->markers );
   marker->id  = id;
   marker->objid = objid;
   marker->type = type;

   return marker->id;
}

/**
 * @brief Marks all active systems that need marking.
 */
void mission_sysMark (void)
{
   /* Clear markers. */
   space_clearMarkers();
   for (int i=0; i<MISSION_MAX; i++) {
      /* Must be a valid player mission. */
      if (player_missions[i]->id == 0)
         continue;

      for (int j=0; j<array_size(player_missions[i]->markers); j++) {
         MissionMarker *m = &player_missions[i]->markers[j];

         /* Add the individual markers. */
         space_addMarker( m->objid, m->type );
      }
   }
}

/**
 * @brief Marks the system of the computer mission to reflect where it will head to.
 *
 * Does not modify other markers.
 *
 *    @param misn Mission to mark.
 */
const StarSystem* mission_sysComputerMark( const Mission* misn )
{
   StarSystem *firstsys = NULL;

   /* Clear markers. */
   space_clearComputerMarkers();

   /* Set all the markers. */
   for (int i=0; i<array_size(misn->markers); i++) {
      StarSystem *sys;
      Planet *pnt;
      const char *sysname;
      MissionMarker *m = &misn->markers[i];

      switch (m->type) {
         case SYSMARKER_COMPUTER:
         case SYSMARKER_LOW:
         case SYSMARKER_HIGH:
         case SYSMARKER_PLOT:
            sys = system_getIndex( m->objid );
            break;
         case PNTMARKER_COMPUTER:
         case PNTMARKER_LOW:
         case PNTMARKER_HIGH:
         case PNTMARKER_PLOT:
            pnt = planet_getIndex( m->objid );
            sysname = planet_getSystem( pnt->name );
            if (sysname==NULL) {
               WARN(_("Marked planet '%s' is not in any system!"), pnt->name);
               continue;
            }
            sys = system_get( sysname );
            break;
         default:
            WARN(_("Unknown marker type."));
            continue;
      }

      if (sys != NULL)
         sys_setFlag( sys, SYSTEM_CMARKED );

      if (firstsys==NULL)
         firstsys = sys;
   }
   return firstsys;
}

/**
 * @brief Gets the first system that has been marked by a mission.
 *
 *    @param misn Mission to get marked system of.
 *    @return First marked system.
 */
const StarSystem* mission_getSystemMarker( const Mission* misn )
{
   /* Set all the markers. */
   for (int i=0; i<array_size(misn->markers); i++) {
      StarSystem *sys;
      Planet *pnt;
      const char *sysname;
      MissionMarker *m = &misn->markers[i];;

      switch (m->type) {
         case SYSMARKER_COMPUTER:
         case SYSMARKER_LOW:
         case SYSMARKER_HIGH:
         case SYSMARKER_PLOT:
            sys = system_getIndex( m->objid );
            break;
         case PNTMARKER_COMPUTER:
         case PNTMARKER_LOW:
         case PNTMARKER_HIGH:
         case PNTMARKER_PLOT:
            pnt = planet_getIndex( m->objid );
            sysname = planet_getSystem( pnt->name );
            if (sysname==NULL) {
               WARN(_("Marked planet '%s' is not in any system!"), pnt->name);
               continue;
            }
            sys = system_get( sysname );
            break;
         default:
            WARN(_("Unknown marker type."));
            continue;
      }

      return sys;
   }
   return NULL;
}

/**
 * @brief Links cargo to the mission for posterior cleanup.
 *
 *    @param misn Mission to link cargo to.
 *    @param cargo_id ID of cargo to link.
 *    @return 0 on success.
 */
int mission_linkCargo( Mission* misn, unsigned int cargo_id )
{
   if (misn->cargo == NULL)
      misn->cargo = array_create( unsigned int );
   array_push_back( &misn->cargo, cargo_id );

   return 0;
}

/**
 * @brief Unlinks cargo from the mission, removes it from the player.
 *
 *    @param misn Mission to unlink cargo from.
 *    @param cargo_id ID of cargo to unlink.
 *    @return returns 0 on success.
 */
int mission_unlinkCargo( Mission* misn, unsigned int cargo_id )
{
   int i;
   for (i=0; i<array_size(misn->cargo); i++)
      if (misn->cargo[i] == cargo_id)
         break;

   if (i>=array_size(misn->cargo)) { /* not found */
      DEBUG(_("Mission '%s' attempting to unlink nonexistent cargo %d."),
            misn->title, cargo_id);
      return 1;
   }

   /* shrink cargo size. */
   array_erase( &misn->cargo, &misn->cargo[i], &misn->cargo[i+1] );

   return 0;
}

/**
 * @brief Cleans up a mission.
 *
 *    @param misn Mission to clean up.
 */
void mission_cleanup( Mission* misn )
{
   /* Hooks and missions. */
   if (misn->id != 0) {
      hook_rmMisnParent( misn->id ); /* remove existing hooks */
      npc_rm_parentMission( misn->id ); /* remove existing npc */
   }

   /* Cargo. */
   for (int i=0; i<array_size(misn->cargo); i++) { /* must unlink all the cargo */
      if (player.p != NULL) { /* Only remove if player exists. */
         int ret = pilot_rmMissionCargo( player.p, misn->cargo[i], 0 );
         if (ret)
            WARN(_("Failed to remove mission cargo '%d' for mission '%s'."), misn->cargo[i], misn->title);
      }
   }
   array_free(misn->cargo);
   if (misn->osd > 0)
      osd_destroy(misn->osd);
   /*
    * XXX With the way the mission code works, this function is called on a
    * Mission struct of all zeros. Looking at the implementation, luaL_ref()
    * never returns 0, but this is probably undefined behavior.
    */
   if (misn->env != LUA_NOREF && misn->env != 0)
      nlua_freeEnv(misn->env);

   /* Data. */
   free(misn->title);
   free(misn->desc);
   free(misn->reward);
   gl_freeTexture(misn->portrait);
   free(misn->npc);
   free(misn->npc_desc);

   /* Markers. */
   array_free( misn->markers );

   /* Claims. */
   if (misn->claims != NULL)
      claim_destroy( misn->claims );

   /* Clear the memory. */
   memset( misn, 0, sizeof(Mission) );
}

/**
 * @brief Puts the specified mission at the end of the player_missions array.
 *
 *    @param pos Mission's position within player_missions
 */
void mission_shift( int pos )
{
   Mission *misn;

   if (pos >= (MISSION_MAX-1))
      return;

   /* Store specified mission. */
   misn = player_missions[pos];

   /* Move other missions down. */
   memmove( &player_missions[pos], &player_missions[pos+1],
      sizeof(Mission*) * (MISSION_MAX - pos - 1) );

   /* Put the specified mission at the end of the array. */
   player_missions[MISSION_MAX - 1] = misn;
}

/**
 * @brief Frees MissionData.
 *
 *    @param mission MissionData to free.
 */
static void mission_freeData( MissionData* mission )
{
   free(mission->name);
   free(mission->lua);
   free(mission->sourcefile);
   free(mission->avail.planet);
   free(mission->avail.system);
   array_free(mission->avail.factions);
   free(mission->avail.cond);
   free(mission->avail.done);

   /* Clear the memory. */
#ifdef DEBUGGING
   memset( mission, 0, sizeof(MissionData) );
#endif /* DEBUGGING */
}

/**
 * @brief Checks to see if a mission matches the faction requirements.
 *
 *    @param misn Mission to check.
 *    @param faction Faction to check against.
 *    @return 1 if it meets the faction requirement, 0 if it doesn't.
 */
static int mission_matchFaction( MissionData* misn, int faction )
{
   /* No faction always accepted. */
   if (array_size(misn->avail.factions) == 0)
      return 1;

   /* Check factions. */
   for (int i=0; i<array_size(misn->avail.factions); i++)
      if (faction == misn->avail.factions[i])
         return 1;

   return 0;
}

/**
 * @brief Activates mission claims.
 */
void missions_activateClaims (void)
{
   for (int i=0; i<MISSION_MAX; i++)
      if (player_missions[i]->claims != NULL)
         claim_activate( player_missions[i]->claims );
}

/**
 * @brief Compares to missions to see which has more priority.
 */
static int mission_compare( const void* arg1, const void* arg2 )
{
   Mission *m1, *m2;

   /* Get arguments. */
   m1 = (Mission*) arg1;
   m2 = (Mission*) arg2;

   /* Check priority - lower is more important. */
   if (m1->data->avail.priority < m2->data->avail.priority)
      return -1;
   else if (m1->data->avail.priority > m2->data->avail.priority)
      return +1;

   /* Compare NPC. */
   if ((m1->npc != NULL) && (m2->npc != NULL))
      return strcmp( m1->npc, m2->npc );

   /* Compare title. */
   if ((m1->title != NULL) && (m2->title != NULL))
      return strcmp( m1->title, m2->title );

   /* Tied. */
   return strcmp(m1->data->name, m2->data->name);
}

/**
 * @brief Generates a mission list. This runs create() so won't work with all
 *        missions.
 *
 *    @param[out] n Missions created.
 *    @param faction Faction of the planet.
 *    @param planet Name of the planet.
 *    @param sysname Name of the current system.
 *    @param loc Location
 *    @return The stack of Missions created with n members.
 */
Mission* missions_genList( int *n, int faction,
      const char* planet, const char* sysname, int loc )
{
   int m, alloced;
   int rep;
   Mission* tmp;

   /* Find available missions. */
   tmp      = NULL;
   m        = 0;
   alloced  = 0;
   for (int i=0; i<array_size(mission_stack); i++) {
      MissionData *misn = &mission_stack[i];
      if (misn->avail.loc == loc) {
         double chance;

         /* Must meet requirements. */
         if (!mission_meetReq(i, faction, planet, sysname))
            continue;

         /* Must hit chance. */
         chance = (double)(misn->avail.chance % 100)/100.;
         if (chance == 0.) /* We want to consider 100 -> 100% not 0% */
            chance = 1.;
         rep = MAX(1, misn->avail.chance / 100);

         for (int j=0; j<rep; j++) /* random chance of rep appearances */
            if (RNGF() < chance) {
               m++;
               /* Extra allocation. */
               if (m > alloced) {
                  if (alloced == 0)
                     alloced = 32;
                  else
                     alloced *= 2;
                  tmp      = realloc( tmp, sizeof(Mission) * alloced );
               }
               /* Initialize the mission. */
               if (mission_init( &tmp[m-1], misn, 1, 1, NULL ))
                  m--;
            }
      }
   }

   /* Sort. */
   if (tmp != NULL) {
      qsort( tmp, m, sizeof(Mission), mission_compare );
      (*n) = m;
   }
   else
      (*n) = 0;

   return tmp;
}

/**
 * @brief Gets location based on a human readable string.
 *
 *    @param loc String to get the location of.
 *    @return Location matching loc.
 */
static int mission_location( const char *loc )
{
   if (loc != NULL) {
      if (strcmp( loc, "None" ) == 0)
         return MIS_AVAIL_NONE;
      else if (strcmp( loc, "Computer" ) == 0)
         return MIS_AVAIL_COMPUTER;
      else if (strcmp( loc, "Bar" ) == 0)
         return MIS_AVAIL_BAR;
      else if (strcmp( loc, "Outfit" ) == 0)
         return MIS_AVAIL_OUTFIT;
      else if (strcmp( loc, "Shipyard" ) == 0)
         return MIS_AVAIL_SHIPYARD;
      else if (strcmp( loc, "Land" ) == 0)
         return MIS_AVAIL_LAND;
      else if (strcmp( loc, "Commodity" ) == 0)
         return MIS_AVAIL_COMMODITY;
      else if (strcmp( loc, "Space" ) == 0)
         return MIS_AVAIL_SPACE;
   }
   return -1;
}

/**
 * @brief Parses a node of a mission.
 *
 *    @param temp Data to load into.
 *    @param parent Node containing the mission.
 *    @return 0 on success.
 */
static int mission_parseXML( MissionData *temp, const xmlNodePtr parent )
{
   xmlNodePtr node;

   /* Clear memory. */
   memset( temp, 0, sizeof(MissionData) );

   /* Defaults. */
   temp->avail.priority = 5;

   /* get the name */
   xmlr_attr_strd(parent,"name",temp->name);
   if (temp->name == NULL)
      WARN( _("Mission in %s has invalid or no name"), MISSION_DATA_PATH );

   node = parent->xmlChildrenNode;

   do { /* load all the data */
      /* Only handle nodes. */
      xml_onlyNodes(node);

      if (xml_isNode(node,"flags")) { /* set the various flags */
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur,"unique")) {
               mis_setFlag(temp,MISSION_UNIQUE);
               continue;
            }
            WARN(_("Mission '%s' has unknown flag node '%s'."), temp->name, cur->name);
         } while (xml_nextNode(cur));
         continue;
      }
      else if (xml_isNode(node,"avail")) { /* mission availability */
         xmlNodePtr cur = node->children;
         do {
            xml_onlyNodes(cur);
            if (xml_isNode(cur,"location")) {
               temp->avail.loc = mission_location( xml_get(cur) );
               continue;
            }
            xmlr_int(cur,"chance",temp->avail.chance);
            xmlr_strd(cur,"planet",temp->avail.planet);
            xmlr_strd(cur,"system",temp->avail.system);
            if (xml_isNode(cur,"faction")) {
               if (temp->avail.factions == NULL)
                  temp->avail.factions = array_create( int );
               array_push_back( &temp->avail.factions, faction_get( xml_get(cur) ) );
               continue;
            }
            xmlr_strd(cur,"cond",temp->avail.cond);
            xmlr_strd(cur,"done",temp->avail.done);
            xmlr_int(cur,"priority",temp->avail.priority);
            WARN(_("Mission '%s' has unknown avail node '%s'."), temp->name, cur->name);
         } while (xml_nextNode(cur));
         continue;
      }
      else if (xml_isNode(node,"notes")) continue; /* Notes for the python mission mapping script */

      DEBUG(_("Unknown node '%s' in mission '%s'"),node->name,temp->name);
   } while (xml_nextNode(node));

#define MELEMENT(o,s) \
   if (o) WARN( _("Mission '%s' missing/invalid '%s' element"), temp->name, s)
   MELEMENT(temp->avail.loc==-1,"location");
   MELEMENT((temp->avail.loc!=MIS_AVAIL_NONE) && (temp->avail.chance==0),"chance");
#undef MELEMENT

   return 0;
}

/**
 * @brief Ordering function for missions.
 */
static int missions_cmp( const void *a, const void *b )
{
   const MissionData *ma, *mb;
   ma = (const MissionData*) a;
   mb = (const MissionData*) b;
   if (ma->avail.priority < mb->avail.priority)
      return -1;
   else if (ma->avail.priority > mb->avail.priority)
      return +1;
   return strcmp( ma->name, mb->name );
}

/**
 * @brief Loads all the mission data.
 *
 *    @return 0 on success.
 */
int missions_load (void)
{
   char **mission_files;

   /* Allocate player missions. */
   for (int i=0; i<MISSION_MAX; i++)
      player_missions[i] = calloc(1, sizeof(Mission));

   /* Run over missions. */
   mission_files = ndata_listRecursive( MISSION_DATA_PATH );
   mission_stack = array_create_size( MissionData, array_size( mission_files ) );
   for (int i=0; i < array_size( mission_files ); i++) {
      mission_parseFile( mission_files[i], NULL );
      free( mission_files[i] );
   }
   array_free( mission_files );
   array_shrink(&mission_stack);

   /* Sort based on priority so higher priority missions can establish claims first. */
   qsort( mission_stack, array_size(mission_stack), sizeof(MissionData), missions_cmp );

   DEBUG( n_("Loaded %d Mission", "Loaded %d Missions", array_size(mission_stack) ), array_size(mission_stack) );

   return 0;
}

/**
 * @brief Parses a single mission.
 *
 *    @param file Source file path.
 *    @param temp Data to load into, or NULL for initial load.
 */
static int mission_parseFile( const char* file, MissionData *temp )
{
   xmlDocPtr doc;
   xmlNodePtr node;
   size_t bufsize;
   char *filebuf;
   const char *pos, *start_pos;

   /* Load string. */
   filebuf = ndata_read( file, &bufsize );
   if (filebuf == NULL) {
      WARN(_("Unable to read data from '%s'"), file);
      return -1;
   }

   /* Skip if no XML. */
   pos = strnstr( filebuf, "</mission>", bufsize );
   if (pos==NULL) {
      pos = strnstr( filebuf, "function create", bufsize );
      if ((pos != NULL) && !strncmp(pos,"--common",bufsize))
         WARN(_("Mission '%s' has create function but no XML header!"), file);
      free(filebuf);
      return 0;
   }

   /* Separate XML header and Lua. */
   start_pos = strnstr( filebuf, "<?xml ", bufsize );
   pos = strnstr( filebuf, "--]]", bufsize );
   if (pos == NULL || start_pos == NULL) {
      WARN(_("Mission file '%s' has missing XML header!"), file);
      return -1;
   }

   /* Parse the header. */
   doc = xmlParseMemory( start_pos, pos-start_pos);
   if (doc == NULL) {
      WARN(_("Unable to parse document XML header for Mission '%s'"), file);
      return -1;
   }

   node = doc->xmlChildrenNode;
   if (!xml_isNode(node,XML_MISSION_TAG)) {
      ERR( _("Malformed XML header for '%s' mission: missing root element '%s'"), file, XML_MISSION_TAG );
      return -1;
   }

   if (temp == NULL)
      temp = &array_grow(&mission_stack);
   mission_parseXML( temp, node );
   temp->lua = strdup(filebuf);
   temp->sourcefile = strdup(file);

#ifdef DEBUGGING
   /* Check to see if syntax is valid. */
   int ret = luaL_loadbuffer(naevL, temp->lua, strlen(temp->lua), temp->name );
   if (ret == LUA_ERRSYNTAX) {
      WARN(_("Mission Lua '%s' syntax error: %s"),
            file, lua_tostring(naevL,-1) );
   } else {
      lua_pop(naevL, 1);
   }
#endif /* DEBUGGING */

   /* Clean up. */
   xmlFreeDoc(doc);
   free(filebuf);

   return 0;
}

/**
 * @brief Frees all the mission data.
 */
void missions_free (void)
{
   /* Free all the player missions. */
   missions_cleanup();

   /* Free the mission data. */
   for (int i=0; i<array_size(mission_stack); i++)
      mission_freeData( &mission_stack[i] );
   array_free( mission_stack );
   mission_stack = NULL;

   /* Free the player mission stack. */
   for (int i=0; i<MISSION_MAX; i++)
      free(player_missions[i]);
}

/**
 * @brief Cleans up all the player's active missions.
 */
void missions_cleanup (void)
{
   for (int i=0; i<MISSION_MAX; i++)
      mission_cleanup( player_missions[i] );
}

/**
 * @brief Saves the player's active missions.
 *
 *    @param writer XML Write to use to save missions.
 *    @return 0 on success.
 */
int missions_saveActive( xmlTextWriterPtr writer )
{
   /* We also save specially created cargos here. Since it can only be mission
    * cargo and can only be placed on the player's main ship, we don't have to
    * worry about it being on other ships. */
   xmlw_startElem(writer,"mission_cargo");
   for (int i=0; i<array_size(player.p->commodities); i++) {
      const Commodity *c = player.p->commodities[i].commodity;
      if (!c->istemp)
         continue;
      xmlw_startElem(writer,"cargo");
      missions_saveTempCommodity( writer, c );
      xmlw_endElem(writer); /* "cargo" */
   }
   xmlw_endElem(writer); /* "missions_cargo */

   xmlw_startElem(writer,"missions");
   for (int i=0; i<MISSION_MAX; i++) {
      if (player_missions[i]->id != 0) {
         xmlw_startElem(writer,"mission");

         /* data and id are attributes because they must be loaded first */
         xmlw_attr(writer,"data","%s",player_missions[i]->data->name);
         xmlw_attr(writer,"id","%u",player_missions[i]->id);

         xmlw_elem(writer,"title","%s",player_missions[i]->title);
         xmlw_elem(writer,"desc","%s",player_missions[i]->desc);
         xmlw_elem(writer,"reward","%s",player_missions[i]->reward);

         /* Markers. */
         xmlw_startElem( writer, "markers" );
         for (int j=0; j<array_size( player_missions[i]->markers ); j++) {
            MissionMarker *m = &player_missions[i]->markers[j];
            xmlw_startElem(writer,"marker");
            xmlw_attr(writer,"id","%d",m->id);
            xmlw_attr(writer,"type","%d",m->type);
            xmlw_str(writer,"%s", mission_markerTarget( m ));
            xmlw_endElem(writer); /* "marker" */
         }
         xmlw_endElem( writer ); /* "markers" */

         /* Cargo */
         xmlw_startElem(writer,"cargos");
         for (int j=0; j<array_size(player_missions[i]->cargo); j++)
            xmlw_elem(writer,"cargo","%u", player_missions[i]->cargo[j]);
         xmlw_endElem(writer); /* "cargos" */

         /* OSD. */
         if (player_missions[i]->osd > 0) {
            char **items = osd_getItems(player_missions[i]->osd);

            xmlw_startElem(writer,"osd");

            /* Save attributes. */
            xmlw_attr(writer,"title","%s",osd_getTitle(player_missions[i]->osd));
            xmlw_attr(writer,"nitems","%d",array_size(items));
            xmlw_attr(writer,"active","%d",osd_getActive(player_missions[i]->osd));

            /* Save messages. */
            for (int j=0; j<array_size(items); j++)
               xmlw_elem(writer,"msg","%s",items[j]);

            xmlw_endElem(writer); /* "osd" */
         }

         /* Claims. */
         xmlw_startElem(writer,"claims");
         claim_xmlSave( writer, player_missions[i]->claims );
         xmlw_endElem(writer); /* "claims" */

         /* Write Lua magic */
         xmlw_startElem(writer,"lua");
         nxml_persistLua( player_missions[i]->env, writer );
         xmlw_endElem(writer); /* "lua" */

         xmlw_endElem(writer); /* "mission" */
      }
   }
   xmlw_endElem(writer); /* "missions" */

   return 0;
}

/**
 * @brief Saves a temporary commodity's defintion into the current node.
 *
 *    @param writer XML Write to use to save missions.
 *    @param c Commodity to save.
 *    @return 0 on success.
 */
int missions_saveTempCommodity( xmlTextWriterPtr writer, const Commodity *c )
{
   xmlw_attr( writer, "name", "%s", c->name );
   xmlw_attr( writer, "description", "%s", c->description );
   for (int j=0; j < array_size( c->illegalto ); j++)
      xmlw_elem( writer, "illegalto", "%s", faction_name( c->illegalto[ j ] ) );
   return 0;
}

/**
 * @brief Loads the player's special mission commodities.
 *
 *    @param parent Node containing the player's special mission cargo.
 *    @return 0 on success.
 */
int missions_loadCommodity( xmlNodePtr parent )
{
   xmlNodePtr node;

   /* We have to ensure the mission_cargo stuff is loaded first. */
   node = parent->xmlChildrenNode;
   do {
      xml_onlyNodes(node);

      if (xml_isNode(node,"mission_cargo")) {
         xmlNodePtr cur = node->xmlChildrenNode;
         do {
            xml_onlyNodes(cur);
            if (xml_isNode( cur, "cargo" ))
               (void)missions_loadTempCommodity( cur );
         } while (xml_nextNode( cur ));
         continue;
      }
   } while (xml_nextNode( node ));

   return 0;
}

/**
 * @brief Loads a temporary commodity.
 *
 *    @param cur Node defining the commodity.
 *    @return The temporary commodity, or NULL on failure.
 */
Commodity *missions_loadTempCommodity( xmlNodePtr cur )
{
   xmlNodePtr ccur;
   char *     name, *desc;
   Commodity *c;

   xmlr_attr_strd( cur, "name", name );
   if ( name == NULL ) {
      WARN( _( "Mission cargo without name!" ) );
      return NULL;
   }

   c = commodity_getW( name );
   if ( c != NULL ) {
      free( name );
      return c;
   }

   xmlr_attr_strd( cur, "description", desc );
   if ( desc == NULL ) {
      WARN( _( "Mission temporary cargo '%s' missing description!" ), name );
      free( name );
      return NULL;
   }

   c = commodity_newTemp( name, desc );

   ccur = cur->xmlChildrenNode;
   do {
      xml_onlyNodes( ccur );
      if ( xml_isNode( ccur, "illegalto" ) ) {
         int f = faction_get( xml_get( ccur ) );
         commodity_tempIllegalto( c, f );
      }
   } while ( xml_nextNode( ccur ) );

   free( name );
   free( desc );
   return c;
}

/**
 * @brief Loads the player's active missions from a save.
 *
 *    @param parent Node containing the player's active missions.
 *    @return 0 on success.
 */
int missions_loadActive( xmlNodePtr parent )
{
   xmlNodePtr node;

   /* cleanup old missions */
   missions_cleanup();

   /* After load the normal missions. */
   node = parent->xmlChildrenNode;
   do {
      xml_onlyNodes(node);
      if (xml_isNode(node,"missions")) {
         if (missions_parseActive( node ) < 0)
            return -1;
         continue;
      }
   } while (xml_nextNode(node));

   return 0;
}

/**
 * @brief Parses the actual individual mission nodes.
 *
 *    @param parent Parent node to parse.
 *    @return 0 on success.
 */
static int missions_parseActive( xmlNodePtr parent )
{
   Mission *misn;
   MissionData *data;
   int m, i;
   char *buf;
   char *title;
   const char **items;
   int nitems, active;

   xmlNodePtr node, cur, nest;

   m = 0; /* start with mission 0 */
   node = parent->xmlChildrenNode;
   do {
      if (xml_isNode(node, "mission")) {
         misn = player_missions[m];

         /* process the attributes to create the mission */
         xmlr_attr_strd(node, "data", buf);
         data = mission_get(mission_getID(buf));
         if (data == NULL) {
            WARN(_("Mission '%s' from saved game not found in game - ignoring."), buf);
            free(buf);
            continue;
         }
         else {
            if (mission_init( misn, data, 0, 0, NULL )) {
               WARN(_("Mission '%s' from saved game failed to load properly - ignoring."), buf);
               free(buf);
               continue;
            }
            misn->accepted = 1;
         }
         free(buf);

         /* this will orphan an identifier */
         xmlr_attr_int(node, "id", misn->id);

         cur = node->xmlChildrenNode;
         do {
            xmlr_strd(cur,"title",misn->title);
            xmlr_strd(cur,"desc",misn->desc);
            xmlr_strd(cur,"reward",misn->reward);

            /* Get the markers. */
            if (xml_isNode(cur,"markers")) {
               nest = cur->xmlChildrenNode;
               do {
                  if (xml_isNode(nest,"marker"))
                     mission_markerLoad( misn, nest );
               } while (xml_nextNode(nest));
            }

            /* Cargo. */
            if (xml_isNode(cur,"cargos")) {
               nest = cur->xmlChildrenNode;
               do {
                  if (xml_isNode(nest,"cargo"))
                     mission_linkCargo( misn, xml_getLong(nest) );
               } while (xml_nextNode(nest));
            }

            /* OSD. */
            if (xml_isNode(cur,"osd")) {
               xmlr_attr_int_def( cur, "nitems", nitems, -1 );
               if (nitems == -1)
                  continue;
               xmlr_attr_strd(cur,"title",title);
               items = malloc( nitems * sizeof(char*) );
               i = 0;
               nest = cur->xmlChildrenNode;
               do {
                  if (xml_isNode(nest,"msg")) {
                     if (i > nitems) {
                        WARN(_("Inconsistency with 'nitems' in save file."));
                        break;
                     }
                     items[i] = xml_get(nest);
                     i++;
                  }
               } while (xml_nextNode(nest));

               /* Create the OSD. */
               misn->osd = osd_create( title, nitems, items, data->avail.priority );
               free(items);
               free(title);

               /* Set active. */
               xmlr_attr_int_def( cur, "active", active, -1 );
               if (active != -1)
                  osd_active( misn->osd, active );
            }

            /* Claims. */
            if (xml_isNode(cur,"claims"))
               misn->claims = claim_xmlLoad( cur );

            if (xml_isNode(cur,"lua"))
               /* start the unpersist routine */
               nxml_unpersistLua( misn->env, cur );

         } while (xml_nextNode(cur));

         m++; /* next mission */
         if (m >= MISSION_MAX) break; /* full of missions, must be an error */
      }
   } while (xml_nextNode(node));

   return 0;
}

int mission_reload( const char *name )
{
   int res;
   MissionData save, *temp = mission_getFromName( name );
   if (temp == NULL)
      return -1;
   save = *temp;
   res = mission_parseFile( save.sourcefile, temp );
   if (res == 0)
      mission_freeData( &save );
   else
      *temp = save;
   return res;
}
