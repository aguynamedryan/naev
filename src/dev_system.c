/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file dev_system.c
 *
 * @brief Handles development of star system stuff.
 */
/** @cond */
#include <stdlib.h> /* qsort */

#include "naev.h"
/** @endcond */

#include "dev_system.h"

#include "array.h"
#include "conf.h"
#include "dev_uniedit.h"
#include "nstring.h"
#include "nxml.h"
#include "physics.h"
#include "space.h"
#include "nebula.h"

/*
 * Prototypes.
 */
static int dsys_compPlanet( const void *planet1, const void *planet2 );
static int dsys_compJump( const void *jmp1, const void *jmp2 );

/**
 * @brief Compare function for planet qsort.
 *
 *    @param planet1 Planet 1 to sort.
 *    @param planet2 Planet 2 to sort.
 *    @return Order to sort.
 */
static int dsys_compPlanet( const void *planet1, const void *planet2 )
{
   const Planet *p1, *p2;

   p1 = * (const Planet**) planet1;
   p2 = * (const Planet**) planet2;

   return strcmp( p1->name, p2->name );
}

/**
 * @brief Compare function for asset qsort.
 *
 *    @param asset1 Virtual asset 1 to sort.
 *    @param asset2 Virtual asset 2 to sort.
 *    @return Order to sort.
 */
static int dsys_compVirtualAsset( const void *asset1, const void *asset2 )
{
   const VirtualAsset *va1, *va2;

   va1 = *(const VirtualAsset**) asset1;
   va2 = *(const VirtualAsset**) asset2;

   return strcmp( va1->name, va2->name );
}

/**
 * @brief Function for qsorting jumppoints.
 *
 *    @param jmp1 Jump Point 1 to sort.
 *    @param jmp2 Jump Point 2 to sort.
 *    @return Order to sort.
 */
static int dsys_compJump( const void *jmp1, const void *jmp2 )
{
   const JumpPoint *jp1, *jp2;

   jp1 = * (const JumpPoint**) jmp1;
   jp2 = * (const JumpPoint**) jmp2;

   return strcmp( jp1->target->name, jp2->target->name );
}

/**
 * @brief Saves a star system.
 *
 *    @param sys Star system to save.
 *    @return 0 on success.
 */
int dsys_saveSystem( StarSystem *sys )
{
   xmlDocPtr doc;
   xmlTextWriterPtr writer;
   const Planet **sorted_planets;
   const VirtualAsset **sorted_assets;
   const JumpPoint **sorted_jumps;
   char *file, *cleanName;

   /* Reconstruct jumps so jump pos are updated. */
   system_reconstructJumps(sys);

   /* Create the writer. */
   writer = xmlNewTextWriterDoc(&doc, 0);
   if (writer == NULL) {
      WARN(_("testXmlwriterDoc: Error creating the xml writer"));
      return -1;
   }

   /* Set the writer parameters. */
   xmlw_setParams( writer );

   /* Start writer. */
   xmlw_start(writer);
   xmlw_startElem( writer, "ssys" );

   /* Attributes. */
   xmlw_attr( writer, "name", "%s", sys->name );

   /* General. */
   xmlw_startElem( writer, "general" );
   if (sys->background != NULL)
      xmlw_elem( writer, "background", "%s", sys->background );
   if (sys->features != NULL)
      xmlw_elem( writer, "features", "%s", sys->features );
   xmlw_elem( writer, "radius", "%f", sys->radius );
   xmlw_elem( writer, "stars", "%d", sys->stars );
   xmlw_elem( writer, "interference", "%f", sys->interference );
   xmlw_startElem( writer, "nebula" );
   xmlw_attr( writer, "volatility", "%f", sys->nebu_volatility );
   if (fabs(sys->nebu_hue*360.0 - NEBULA_DEFAULT_HUE) > 1e-5)
      xmlw_attr( writer, "hue", "%f", sys->nebu_hue*360.0 );
   xmlw_str( writer, "%f", sys->nebu_density );
   xmlw_endElem( writer ); /* "nebula" */
   xmlw_endElem( writer ); /* "general" */

   /* Position. */
   xmlw_startElem( writer, "pos" );
   xmlw_elem( writer, "x", "%f", sys->pos.x );
   xmlw_elem( writer, "y", "%f", sys->pos.y );
   xmlw_endElem( writer ); /* "pos" */

   /* Sort planets. */
   sorted_planets = malloc( sizeof(Planet*) * array_size(sys->planets) );
   memcpy( sorted_planets, sys->planets, sizeof(Planet*) * array_size(sys->planets) );
   qsort( sorted_planets, array_size(sys->planets), sizeof(Planet*), dsys_compPlanet );

   /* Sort virtual assets. */
   sorted_assets = malloc( sizeof(VirtualAsset*) * array_size(sys->assets_virtual) );
   memcpy( sorted_assets, sys->assets_virtual, sizeof(VirtualAsset*) * array_size(sys->assets_virtual) );
   qsort( sorted_assets, array_size(sys->assets_virtual), sizeof(VirtualAsset*), dsys_compVirtualAsset );

   /* Write assets and clean up. */
   xmlw_startElem( writer, "assets" );
   for (int i=0; i<array_size(sys->planets); i++)
      xmlw_elem( writer, "asset", "%s", sorted_planets[i]->name );
   for (int i=0; i<array_size(sys->assets_virtual); i++)
      xmlw_elem( writer, "asset_virtual", "%s", sorted_assets[i]->name );
   xmlw_endElem( writer ); /* "assets" */
   free(sorted_planets);
   free(sorted_assets);

   /* Jumps. */
   sorted_jumps = malloc( sizeof(JumpPoint*) * array_size(sys->jumps) );
   for (int i=0; i<array_size(sys->jumps); i++)
      sorted_jumps[i] = &sys->jumps[i];
   qsort( sorted_jumps, array_size(sys->jumps), sizeof(JumpPoint*), dsys_compJump );
   xmlw_startElem( writer, "jumps" );
   for (int i=0; i<array_size(sys->jumps); i++) {
      const JumpPoint *jp = sorted_jumps[i];
      xmlw_startElem( writer, "jump" );
      xmlw_attr( writer, "target", "%s", jp->target->name );
      /* Position. */
      if (!jp_isFlag( jp, JP_AUTOPOS )) {
         xmlw_startElem( writer, "pos" );
         xmlw_attr( writer, "x", "%f", jp->pos.x );
         xmlw_attr( writer, "y", "%f", jp->pos.y );
         xmlw_endElem( writer ); /* "pos" */
      }
      else
         xmlw_elemEmpty( writer, "autopos" );
      /* Radius and misc properties. */
      if (jp->radius != 200.)
         xmlw_elem( writer, "radius", "%f", jp->radius );
      /* More flags. */
      if (jp_isFlag( jp, JP_HIDDEN ))
         xmlw_elemEmpty( writer, "hidden" );
      if (jp_isFlag( jp, JP_EXITONLY ))
         xmlw_elemEmpty( writer, "exitonly" );
      xmlw_elem( writer, "hide", "%f", jp->hide );
      xmlw_endElem( writer ); /* "jump" */
   }
   xmlw_endElem( writer ); /* "jumps" */
   free(sorted_jumps);

   /* Asteroids. */
   if (array_size(sys->asteroids) > 0 || array_size(sys->astexclude) > 0) {
      xmlw_startElem( writer, "asteroids" );
      for (int i=0; i<array_size(sys->asteroids); i++) {
         const AsteroidAnchor *ast = &sys->asteroids[i];
         xmlw_startElem( writer, "asteroid" );

         /* Types */
         if (!(ast->ntype == 1 && ast->type[0] == 0)) {
            /* With no <type>, the first asteroid type is the default */
            for (int j=0; j<ast->ntype; j++) {
               xmlw_elem( writer, "type", "%s", space_getType(ast->type[j])->ID );
            }
         }

         /* Radius */
         xmlw_elem( writer, "radius", "%f", ast->radius );

         /* Position */
         xmlw_startElem( writer, "pos" );
         xmlw_attr( writer, "x", "%f", ast->pos.x );
         xmlw_attr( writer, "y", "%f", ast->pos.y );
         xmlw_endElem( writer ); /* "pos" */

         /* Misc. properties. */
         xmlw_elem( writer, "density", "%f", ast->density );
         xmlw_endElem( writer ); /* "asteroid" */
      }
      for (int i=0; i<array_size(sys->astexclude); i++) {
         const AsteroidExclusion *aexcl = &sys->astexclude[i];
         xmlw_startElem( writer, "exclusion" );

         /* Radius */
         xmlw_elem( writer, "radius", "%f", aexcl->radius );

         /* Position */
         xmlw_startElem( writer, "pos" );
         xmlw_attr( writer, "x", "%f", aexcl->pos.x );
         xmlw_attr( writer, "y", "%f", aexcl->pos.y );
         xmlw_endElem( writer ); /* "pos" */
      }
      xmlw_endElem( writer ); /* "asteroids" */
   }

   if (sys->stats != NULL) {
      xmlw_startElem( writer, "stats" );
      ss_listToXML( writer, sys->stats );
      xmlw_endElem( writer ); /* "stats" */
   }

   if (array_size(sys->tags)>0) {
      xmlw_startElem( writer, "tags" );
      for (int i=0; i<array_size(sys->tags); i++)
         xmlw_elem( writer, "tag", "%s", sys->tags[i] );
      xmlw_endElem( writer ); /* "tags" */
   }

   xmlw_endElem( writer ); /** "ssys" */
   xmlw_done(writer);

   /* No need for writer anymore. */
   xmlFreeTextWriter(writer);

   /* Write data. */
   cleanName = uniedit_nameFilter( sys->name );
   asprintf( &file, "%s/%s.xml", conf.dev_save_sys, cleanName );
   if (xmlSaveFileEnc( file, doc, "UTF-8" ) < 0)
      WARN("Failed writing '%s'!", file);

   /* Clean up. */
   xmlFreeDoc(doc);
   free(cleanName);
   free(file);

   return 0;
}

/**
 * @brief Saves all the star systems.
 *
 *    @return 0 on success.
 */
int dsys_saveAll (void)
{
   StarSystem *sys = system_getAll();

   /* Write systems. */
   for (int i=0; i<array_size(sys); i++)
      dsys_saveSystem( &sys[i] );

   return 0;
}
