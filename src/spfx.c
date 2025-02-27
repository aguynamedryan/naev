/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file spfx.c
 *
 * @brief Handles the special effects.
 */
/** @cond */
#include <inttypes.h>
#include "SDL.h"
#include "SDL_haptic.h"

#include "naev.h"
/** @endcond */

#include "spfx.h"

#include "array.h"
#include "camera.h"
#include "debris.h"
#include "log.h"
#include "ndata.h"
#include "nxml.h"
#include "opengl.h"
#include "pause.h"
#include "perlin.h"
#include "physics.h"
#include "render.h"
#include "rng.h"
#include "space.h"
#include "nlua_shader.h"

#define SPFX_XML_ID    "spfx" /**< SPFX XML node tag. */

/*
 * Effect parameters.
 */
#define SHAKE_MASS      (1./400.) /** Shake mass. */
#define SHAKE_K         (1./50.) /**< Constant for virtual spring. */
#define SHAKE_B         (3.*sqrt(SHAKE_K*SHAKE_MASS)) /**< Constant for virtual dampener. */

#define HAPTIC_UPDATE_INTERVAL   0.1 /**< Time between haptic updates. */

/* Trail stuff. */
#define TRAIL_UPDATE_DT       0.05  /**< Rate (in seconds) at which trail is updated. */
static TrailSpec* trail_spec_stack; /**< Trail specifications. */
static Trail_spfx** trail_spfx_stack; /**< Active trail effects. */

/*
 * Special hard-coded special effects
 */
/* shake aka rumble */
static unsigned int shake_shader_pp_id = 0; /**< ID of the post-processing shader for the shake. */
static LuaShader_t shake_shader; /**< Shader to use for shake effects. */
static Vector2d shake_pos = { .x = 0., .y = 0. }; /**< Current shake position. */
static Vector2d shake_vel = { .x = 0., .y = 0. }; /**< Current shake velocity. */
static double shake_force_mod = 0.; /**< Shake force modifier. */
static double shake_force_mean = 0.; /**< Running mean of the force. */
static float shake_force_ang = 0.; /**< Shake force angle. */
static perlin_data_t *shake_noise = NULL; /**< Shake noise. */
/* Haptic stuff. */
extern SDL_Haptic *haptic; /**< From joystick.c */
extern unsigned int haptic_query; /**< From joystick.c */
static int haptic_rumble         = -1; /**< Haptic rumble effect ID. */
static SDL_HapticEffect haptic_rumbleEffect; /**< Haptic rumble effect. */
static double haptic_lastUpdate  = 0.; /**< Timer to update haptic effect again. */
/* damage effect */
static unsigned int damage_shader_pp_id = 0; /**< ID of the post-processing shader (0 when disabled) */
static LuaShader_t damage_shader; /**< Shader to use. */
static double damage_strength = 0.; /**< Damage shader strength intensity. */

/*
 * Trail colours handling.
 */
static int trailSpec_load (void);
static void trailSpec_parse( xmlNodePtr cur, TrailSpec *tc );
static TrailSpec* trailSpec_getRaw( const char* name );

/*
 * Misc functions.
 */
static void spfx_updateShake( double dt );
static void spfx_updateDamage( double dt );

/**
 * @struct SPFX_Base
 *
 * @brief Generic special effect.
 */
typedef struct SPFX_Base_ {
   char* name; /**< Name of the special effect. */

   double ttl; /**< Time to live */
   double anim; /**< Total duration in ms */

   /* Use texture when not using shaders. */
   glTexture *gfx; /**< Will use each sprite as a frame */

   /* Shaders! */
   double size; /**< Default size. */
   GLint shader; /**< Shader to use. */
   GLint vertex;
   GLint projection;
   GLint u_time; /**< Time variable in shader. */
   GLint u_r; /**< Unique shader value. */
   GLint u_size; /**< Size of the shader. */
} SPFX_Base;

static SPFX_Base *spfx_effects = NULL; /**< Total special effects. */

/**
 * @struct SPFX
 *
 * @brief An actual in-game active special effect.
 */
typedef struct SPFX_ {
   Vector2d pos; /**< Current position. */
   Vector2d vel; /**< Current velocity. */

   int lastframe; /**< Needed when paused */
   int effect; /**< The real effect */

   double timer; /**< Time left */

   /* For shaders. */
   GLfloat time; /**< Time elapsed (not left). */
   GLfloat unique; /**< Uniqueness value in the shader. */
} SPFX;

/* front stack is for effects on player, back is for the rest */
static SPFX *spfx_stack_front = NULL; /**< Frontal special effect layer. */
static SPFX *spfx_stack_middle = NULL; /**< Middle special effect layer. */
static SPFX *spfx_stack_back = NULL; /**< Back special effect layer. */

/*
 * prototypes
 */
/* General. */
static int spfx_base_parse( SPFX_Base *temp, const char *filename );
static void spfx_base_free( SPFX_Base *effect );
static void spfx_update_layer( SPFX *layer, const double dt );
/* Haptic. */
static int spfx_hapticInit (void);
static void spfx_hapticRumble( double mod );
/* Trail. */
static void spfx_update_trails( double dt );
static void spfx_trail_update( Trail_spfx* trail, double dt );
static void spfx_trail_free( Trail_spfx* trail );

/**
 * @brief Parses an xml node containing a SPFX.
 *
 *    @param temp Address to load SPFX into.
 *    @param filename Name of the file to parse.
 *    @return 0 on success.
 */
static int spfx_base_parse( SPFX_Base *temp, const char *filename )
{
   xmlNodePtr node, cur, uniforms;
   char *shadervert, *shaderfrag;
   const char *name;
   int isint;
   GLint loc, dim;
   xmlDocPtr doc;

   /* Load and read the data. */
   doc = xml_parsePhysFS( filename );
   if (doc == NULL)
      return -1;

   /* Check to see if document exists. */
   node = doc->xmlChildrenNode;
   if (!xml_isNode(node,SPFX_XML_ID)) {
      ERR( _("Malformed '%s' file: missing root element '%s'"), filename, SPFX_XML_ID);
      return -1;
   }

   /* Clear data. */
   memset( temp, 0, sizeof(SPFX_Base) );
   temp->shader = -1;
   shadervert = NULL;
   shaderfrag = NULL;
   uniforms = NULL;

   xmlr_attr_strd( node, "name", temp->name );

   /* Extract the data. */
   node = node->xmlChildrenNode;
   do {
      xml_onlyNodes(node);
      xmlr_float(node, "anim", temp->anim);
      xmlr_float(node, "ttl", temp->ttl);
      if (xml_isNode(node,"gfx")) {
         temp->gfx = xml_parseTexture( node,
               SPFX_GFX_PATH"%s", 6, 5, 0 );
         continue;
      }

      if (xml_isNode(node,"shader")) {
         cur = node->xmlChildrenNode;
         do {
            xml_onlyNodes(cur);
            xmlr_strd(cur, "vert", shadervert);
            xmlr_strd(cur, "frag", shaderfrag);
            xmlr_float(cur, "size", temp->size);
            if (xml_isNode(cur,"uniforms")) {
               uniforms = cur;
               continue;
            }
         } while (xml_nextNode(cur));
         continue;
      }
      WARN(_("SPFX '%s' has unknown node '%s'."), temp->name, node->name);
   } while (xml_nextNode(node));

   /* Convert from ms to s. */
   temp->anim /= 1000.;
   temp->ttl  /= 1000.;
   if (temp->ttl == 0.)
      temp->ttl = temp->anim;

   /* Has shaders. */
   if (shadervert != NULL && shaderfrag != NULL) {
      temp->shader      = gl_program_vert_frag( shadervert, shaderfrag );
      temp->vertex      = glGetAttribLocation( temp->shader, "vertex");
      temp->projection  = glGetUniformLocation( temp->shader, "projection");
      temp->u_r         = glGetUniformLocation( temp->shader, "u_r" );
      temp->u_time      = glGetUniformLocation( temp->shader, "u_time" );
      temp->u_size      = glGetUniformLocation( temp->shader, "u_size" );
      if (uniforms != NULL) {
         glUseProgram( temp->shader );
         node = uniforms->xmlChildrenNode;
         do {
            xml_onlyNodes(node);
            name = (char*)node->name;
            loc = glGetUniformLocation( temp->shader, name );
            if (loc < 0) {
               WARN(_("SPFX '%s' is trying to set uniform '%s' not in shader!"), temp->name, name );
               continue;
            }
            xmlr_attr_int_def(node,"int",isint,0);
            /* Get dimension */
            if (xmlHasProp(node,(xmlChar*)"w"))       dim = 4;
            else if (xmlHasProp(node,(xmlChar*)"z"))  dim = 3;
            else if (xmlHasProp(node,(xmlChar*)"y"))  dim = 2;
            else                                      dim = 1;
            /* Values default to 0. */
            if (isint) {
               int ix, iy, iz, iw;
               xmlr_attr_int(node, "x", ix );
               xmlr_attr_int(node, "y", iy );
               xmlr_attr_int(node, "z", iz );
               xmlr_attr_int(node, "w", iw );
               switch (dim) {
                  case 1:
                     glUniform1i( loc, ix );
                     break;
                  case 2:
                     glUniform2i( loc, ix, iy );
                     break;
                  case 3:
                     glUniform3i( loc, ix, iy, iz );
                     break;
                  case 4:
                     glUniform4i( loc, ix, iy, iz, iw );
                     break;
               }
            }
            else {
               double x, y, z, w;
               xmlr_attr_float(node, "x", x );
               xmlr_attr_float(node, "y", y );
               xmlr_attr_float(node, "z", z );
               xmlr_attr_float(node, "w", w );
               switch (dim) {
                  case 1:
                     glUniform1f( loc, x );
                     break;
                  case 2:
                     glUniform2f( loc, x, y );
                     break;
                  case 3:
                     glUniform3f( loc, x, y, z );
                     break;
                  case 4:
                     glUniform4f( loc, x, y, z, w );
                     break;
               }
            }
         } while (xml_nextNode(node));
         glUseProgram( 0 );
      }
      gl_checkErr();
   }

#define MELEMENT(o,s) \
   if (o) WARN( _("SPFX '%s' missing/invalid '%s' element"), temp->name, s) /**< Define to help check for data errors. */
   MELEMENT(temp->anim==0.,"anim");
   MELEMENT(temp->ttl==0.,"ttl");
   MELEMENT(temp->gfx==NULL && (shadervert==NULL || shaderfrag==NULL),"gfx or shader");
   MELEMENT(temp->shader>=0 && temp->size<=0., "shader/size");
#undef MELEMENT

   free(shadervert);
   free(shaderfrag);

   /* Clean up. */
   xmlFreeDoc(doc);

   return 0;
}

/**
 * @brief Frees a SPFX_Base.
 *
 *    @param effect SPFX_Base to free.
 */
static void spfx_base_free( SPFX_Base *effect )
{
   free(effect->name);
   gl_freeTexture(effect->gfx);
}

/**
 * @brief Gets the id of an spfx based on name.
 *
 *    @param name Name to match.
 *    @return ID of the special effect or -1 on error.
 */
int spfx_get( char* name )
{
   for (int i=0; i<array_size(spfx_effects); i++)
      if (strcmp(spfx_effects[i].name, name)==0)
         return i;
   return -1;
}

/**
 * @brief Loads the spfx stack.
 *
 *    @return 0 on success.
 *
 * @todo Make spfx not hard-coded.
 */
int spfx_load (void)
{
   int n, ret;
   char **spfx_files;

   spfx_effects = array_create(SPFX_Base);

   spfx_files = ndata_listRecursive( SPFX_DATA_PATH );
   for (int i=0; i<array_size(spfx_files); i++) {
      if (!ndata_matchExt( spfx_files[i], "xml" ))
         continue;

      ret = spfx_base_parse( &array_grow(&spfx_effects), spfx_files[i] );
      if (ret < 0) {
         n = array_size(spfx_effects);
         array_erase( &spfx_effects, &spfx_effects[n-1], &spfx_effects[n] );
      }
      free( spfx_files[i] );
   }
   array_free( spfx_files );

   /* Reduce size. */
   array_shrink( &spfx_effects );

   /* Trail colour sets. */
   trailSpec_load();

   /*
    * Now initialize force feedback.
    */
   memset( &shake_shader, 0, sizeof(LuaShader_t) );
   shake_shader.program       = shaders.shake.program;
   shake_shader.VertexPosition= shaders.shake.VertexPosition;
   shake_shader.ClipSpaceFromLocal = shaders.shake.ClipSpaceFromLocal;
   shake_shader.MainTex       = shaders.shake.MainTex;
   spfx_hapticInit();
   shake_noise = noise_new( 1, NOISE_DEFAULT_HURST, NOISE_DEFAULT_LACUNARITY );

   /*
    * Misc shaders.
    */
   memset( &damage_shader, 0, sizeof(LuaShader_t) );
   damage_shader.program       = shaders.damage.program;
   damage_shader.VertexPosition= shaders.damage.VertexPosition;
   damage_shader.ClipSpaceFromLocal = shaders.damage.ClipSpaceFromLocal;
   damage_shader.MainTex       = shaders.damage.MainTex;

   /* Stacks. */
   spfx_stack_front = array_create( SPFX );
   spfx_stack_middle = array_create( SPFX );
   spfx_stack_back = array_create( SPFX );

   DEBUG( n_( "Loaded %d Special Effect", "Loaded %d Special Effects", array_size(spfx_effects) ), array_size(spfx_effects) );

   return 0;
}

/**
 * @brief Frees the spfx stack.
 */
void spfx_free (void)
{
   /* Clean up the debris. */
   debris_cleanup();

   /* get rid of all the particles and free the stacks */
   spfx_clear();
   array_free(spfx_stack_front);
   spfx_stack_front = NULL;
   array_free(spfx_stack_middle);
   spfx_stack_middle = NULL;
   array_free(spfx_stack_back);
   spfx_stack_back = NULL;

   /* now clear the effects */
   for (int i=0; i<array_size(spfx_effects); i++)
      spfx_base_free( &spfx_effects[i] );
   array_free(spfx_effects);
   spfx_effects = NULL;

   /* Free the noise. */
   noise_delete( shake_noise );

   /* Free the trails. */
   for (int i=0; i<array_size(trail_spfx_stack); i++)
      spfx_trail_free( trail_spfx_stack[i] );
   array_free( trail_spfx_stack );

   /* Free the trail styles. */
   for (int i=0; i<array_size(trail_spec_stack); i++)
      free( trail_spec_stack[i].name );
   array_free( trail_spec_stack );
}

/**
 * @brief Creates a new special effect.
 *
 *    @param effect Base effect identifier to use.
 *    @param px X position of the effect.
 *    @param py Y position of the effect.
 *    @param vx X velocity of the effect.
 *    @param vy Y velocity of the effect.
 *    @param layer Layer to put the effect on.
 */
void spfx_add( int effect,
      const double px, const double py,
      const double vx, const double vy,
      int layer )
{
   SPFX *cur_spfx;
   double ttl, anim;

   if ((effect < 0) || (effect > array_size(spfx_effects))) {
      WARN(_("Trying to add spfx with invalid effect!"));
      return;
   }

   /*
    * Select the Layer
    */
   if (layer == SPFX_LAYER_FRONT) /* front layer */
      cur_spfx = &array_grow( &spfx_stack_front );
   else if (layer == SPFX_LAYER_MIDDLE) /* middle layer */
      cur_spfx = &array_grow( &spfx_stack_middle );
   else if (layer == SPFX_LAYER_BACK) /* back layer */
      cur_spfx = &array_grow( &spfx_stack_back );
   else {
      WARN(_("Invalid SPFX layer."));
      return;
   }

   /* The actual adding of the spfx */
   cur_spfx->effect = effect;
   vect_csetmin( &cur_spfx->pos, px, py );
   vect_csetmin( &cur_spfx->vel, vx, vy );
   /* Timer magic if ttl != anim */
   ttl = spfx_effects[effect].ttl;
   anim = spfx_effects[effect].anim;
   if (ttl != anim)
      cur_spfx->timer = ttl + RNGF()*anim;
   else
      cur_spfx->timer = ttl;

   /* Shader magic. */
   cur_spfx->unique = RNGF();
   cur_spfx->time = 0.0;
}

/**
 * @brief Clears all the currently running effects.
 */
void spfx_clear (void)
{
   /* Clear rumble */
   shake_force_mod = 0.;
   shake_force_mean = 0.;
   vectnull( &shake_pos );
   vectnull( &shake_vel );
   if (shake_shader_pp_id > 0)
      render_postprocessRm( shake_shader_pp_id );
   shake_shader_pp_id = 0;
   if (damage_shader_pp_id > 0)
      render_postprocessRm( damage_shader_pp_id );
   damage_shader_pp_id = 0;

   for (int i=0; i<array_size(trail_spfx_stack); i++)
      spfx_trail_free( trail_spfx_stack[i] );
   array_erase( &trail_spfx_stack, array_begin(trail_spfx_stack), array_end(trail_spfx_stack) );
}

/**
 * @brief Updates all the spfx.
 *
 *    @param dt Current delta tick.
 *    @param real_dt Real delta tick.
 */
void spfx_update( const double dt, const double real_dt )
{
   spfx_update_layer( spfx_stack_front, dt );
   spfx_update_layer( spfx_stack_middle, dt );
   spfx_update_layer( spfx_stack_back, dt );
   spfx_update_trails( dt );

   /* Decrement the haptic timer. */
   if (haptic_lastUpdate > 0.)
      haptic_lastUpdate -= real_dt; /* Based on real delta-tick. */

   /* Shake. */
   spfx_updateShake( dt );

   /* Damage. */
   spfx_updateDamage( dt );
}

/**
 * @brief Updates an individual spfx.
 *
 *    @param layer Layer the spfx is on.
 *    @param dt Current delta tick.
 */
static void spfx_update_layer( SPFX *layer, const double dt )
{
   for (int i=0; i<array_size(layer); i++) {
      layer[i].timer -= dt; /* less time to live */

      /* time to die! */
      if (layer[i].timer < 0.) {
         array_erase( &layer, &layer[i], &layer[i+1] );
         i--;
         continue;
      }
      layer[i].time  += dt; /* Shader timer. */

      /* actually update it */
      vect_cadd( &layer[i].pos, dt*VX(layer[i].vel), dt*VY(layer[i].vel) );
   }
}

/**
 * @brief Updates the shake position.
 */
static void spfx_updateShake( double dt )
{
   double mod, vmod, angle;
   double force_x, force_y;
   double dupdate;
   int forced;

   /* Must still be on. */
   if (shake_shader_pp_id == 0)
      return;

   /* The shake decays over time */
   forced = 0;
   if (shake_force_mod > 0.) {
      shake_force_mod -= SPFX_SHAKE_DECAY*dt;
      if (shake_force_mod < 0.)
         shake_force_mod   = 0.;
      else
         forced            = 1;
   }
   dupdate = dt*2.0;
   shake_force_mean = dupdate*shake_force_mod + (1.0-dupdate)*shake_force_mean;

   /* See if it's settled down. */
   mod      = VMOD( shake_pos );
   vmod     = VMOD( shake_vel );
   if (!forced && (mod < 0.01) && (vmod < 0.01)) {
      render_postprocessRm( shake_shader_pp_id );
      shake_shader_pp_id = 0;
      if (fabs(shake_force_ang) > 1e3)
         shake_force_ang = RNGF();
      return;
   }

   /* Calculate force. */
   force_x  = -SHAKE_K*shake_pos.x + -SHAKE_B*shake_vel.x;
   force_y  = -SHAKE_K*shake_pos.y + -SHAKE_B*shake_vel.y;

   /* Apply force if necessary. */
   if (forced) {
      shake_force_ang  += dt;
      angle             = noise_simplex1( shake_noise, &shake_force_ang ) * 5.*M_PI;
      force_x          += shake_force_mod * cos(angle);
      force_y          += shake_force_mod * sin(angle);
   }

   /* Update velocity. */
   vect_cadd( &shake_vel, (1./SHAKE_MASS) * force_x * dt, (1./SHAKE_MASS) * force_y * dt );

   /* Update position. */
   vect_cadd( &shake_pos, shake_vel.x * dt, shake_vel.y * dt );

   /* Set the uniform. */
   glUseProgram( shaders.shake.program );
   glUniform2f( shaders.shake.shake_pos, shake_pos.x / SCREEN_W, shake_pos.y / SCREEN_H );
   glUniform2f( shaders.shake.shake_vel, shake_vel.x / SCREEN_W, shake_vel.y / SCREEN_H );
   glUniform1f( shaders.shake.shake_force, shake_force_mean );
   glUseProgram( 0 );

   gl_checkErr();
}

static void spfx_updateDamage( double dt )
{
   /* Must still be on. */
   if (damage_shader_pp_id == 0)
      return;

   /* Decrement and turn off if necessary. */
   damage_strength -= SPFX_DAMAGE_DECAY * dt;
   if (damage_strength < 0.) {
      damage_strength = 0.;
      render_postprocessRm( damage_shader_pp_id );
      damage_shader_pp_id = 0;
      return;
   }

   /* Set the uniform. */
   glUseProgram( shaders.damage.program );
   glUniform1f( shaders.damage.damage_strength, damage_strength );
   glUseProgram( 0 );

   gl_checkErr();
}

/**
 * @brief Initalizes a trail.
 *
 *    @return Pointer to initialized trail. When done (due e.g. to pilot death), call spfx_trail_remove.
 */
Trail_spfx* spfx_trail_create( const TrailSpec* spec )
{
   Trail_spfx *trail = calloc( 1, sizeof(Trail_spfx) );
   trail->spec = spec;
   trail->capacity = 1;
   trail->iread = trail->iwrite = 0;
   trail->point_ringbuf = calloc( trail->capacity, sizeof(TrailPoint) );
   trail->refcount = 1;
   trail->r = RNGF();
   trail->ontop = 0;

   if ( trail_spfx_stack == NULL )
      trail_spfx_stack = array_create( Trail_spfx* );
   array_push_back( &trail_spfx_stack, trail );

   return trail;
}

/**
 * @brief Updates all trails (handling dispersal/fadeout).
 *
 *    @param dt Update interval.
 */
void spfx_update_trails( double dt )
{
   int n = array_size( trail_spfx_stack );
   for (int i=0; i<n; i++) {
      Trail_spfx *trail = trail_spfx_stack[i];
      spfx_trail_update( trail, dt );
      if (!trail->refcount && !trail_size(trail) ) {
         spfx_trail_free( trail );
         trail_spfx_stack[i--] = trail_spfx_stack[--n];
      }
   }
   if (n < array_size( trail_spfx_stack ) )
      array_resize( &trail_spfx_stack, n );
}

/**
 * @brief Updates a trail.
 *
 *    @param trail Trail to update.
 *    @param dt Update interval.
 */
static void spfx_trail_update( Trail_spfx* trail, double dt )
{
   GLfloat rel_dt = dt/ trail->spec->ttl;
   /* Remove outdated elements. */
   while (trail->iread < trail->iwrite && trail_front(trail).t < rel_dt)
      trail->iread++;

   /* Update others' timestamps. */
   for (size_t i = trail->iread; i < trail->iwrite; i++)
      trail_at( trail, i ).t -= rel_dt;

   /* Update timer. */
   trail->dt += dt;
}

/**
 * @brief Makes a trail grow.
 *
 *    @param trail Trail to update.
 *    @param x X position of the new control point.
 *    @param y Y position of the new control point.
 *    @param mode Type of trail emission at this point.
 *    @param force Whether or not to force the addition of the sample.
 */
void spfx_trail_sample( Trail_spfx* trail, double x, double y, TrailMode mode, int force )
{
   TrailPoint p;

   if (!force && trail->spec->style[mode].col.a <= 0.)
      return;

   p.x = x;
   p.y = y;
   p.t = 1.;
   p.mode = mode;

   /* The "back" of the trail should always reflect our most recent state.  */
   trail_back( trail ) = p;

   /* We may need to insert a control point, but not if our last sample was recent enough. */
   if (!force && trail_size(trail) > 1 && trail_at( trail, trail->iwrite-2 ).t >= 1.-TRAIL_UPDATE_DT)
      return;

   /* If the last time we inserted a control point was recent enough, we don't need a new one. */
   if (trail_size(trail) == trail->capacity) {
      /* Full! Double capacity, and make the elements contiguous. (We've made space to grow rightward.) */
      trail->point_ringbuf = realloc( trail->point_ringbuf, 2 * trail->capacity * sizeof(TrailPoint) );
      trail->iread %= trail->capacity;
      trail->iwrite = trail->iread + trail->capacity;
      memmove( &trail->point_ringbuf[trail->capacity], trail->point_ringbuf, trail->iread * sizeof(TrailPoint) );
      trail->capacity *= 2;
   }
   trail_at( trail, trail->iwrite++ ) = p;
}

/**
 * @brief Removes a trail.
 *
 *    @param trail Trail to remove.
 */
void spfx_trail_remove( Trail_spfx* trail )
{
   if (trail != NULL)
      trail->refcount--;
}

/**
 * @brief Deallocates an unreferenced, expired trail.
 */
static void spfx_trail_free( Trail_spfx* trail )
{
   assert(trail->refcount == 0);
   free(trail->point_ringbuf);
   free(trail);
}

/**
 * @brief Draws a trail on screen.
 */
void spfx_trail_draw( const Trail_spfx* trail )
{
   double x1, y1, x2, y2, z;
   const TrailStyle *sp, *spp, *styles;
   size_t n;
   GLfloat len;
   gl_Matrix4 projection;
   double s;

   n = trail_size(trail);
   if (n==0)
      return;
   styles = trail->spec->style;

   /* Stuff that doesn't change for the entire trail. */
   glUseProgram( shaders.trail.program );
   if (gl_has( OPENGL_SUBROUTINES ))
      glUniformSubroutinesuiv( GL_FRAGMENT_SHADER, 1, &trail->spec->type );
   glEnableVertexAttribArray( shaders.trail.vertex );
   gl_vboActivateAttribOffset( gl_squareVBO, shaders.trail.vertex, 0, 2, GL_FLOAT, 0 );
   glUniform1f( shaders.trail.dt, trail->dt );
   glUniform1f( shaders.trail.r, trail->r );

   z   = cam_getZoom();
   len = 0.;
   for (size_t i=trail->iread + 1; i < trail->iwrite; i++) {
      TrailPoint *tp  = &trail_at( trail, i );
      TrailPoint *tpp = &trail_at( trail, i-1 );

      /* Ignore none modes. */
      if (tp->mode == MODE_NONE || tpp->mode == MODE_NONE)
         continue;

      gl_gameToScreenCoords( &x1, &y1,  tp->x,  tp->y );
      gl_gameToScreenCoords( &x2, &y2, tpp->x, tpp->y );

      s = hypot( x2-x1, y2-y1 );

      /* Make sure in bounds. */
      if ((MAX(x1,x2) < 0.) || (MIN(x1,x2)*z > (double)SCREEN_W) ||
         (MAX(y1,y2) < 0.) || (MIN(y1,y2)*z > (double)SCREEN_H)) {
         len += s;
         continue;
      }

      sp  = &styles[tp->mode];
      spp = &styles[tpp->mode];

      /* Set vertex. */
      projection = gl_Matrix4_Translate(gl_view_matrix, x1, y1, 0);
      projection = gl_Matrix4_Rotate2dv(projection, (x2-x1)/s, (y2-y1)/s);
      projection = gl_Matrix4_Scale(projection, s, z*(sp->thick+spp->thick), 1);
      projection = gl_Matrix4_Translate(projection, 0., -.5, 0);

      /* Set uniforms. */
      gl_Matrix4_Uniform(shaders.trail.projection, projection);
      gl_uniformColor(shaders.trail.c1, &sp->col);
      gl_uniformColor(shaders.trail.c2, &spp->col);
      glUniform1f(shaders.trail.t1, tp->t);
      glUniform1f(shaders.trail.t2, tpp->t);
      glUniform2f(shaders.trail.pos2, len, sp->thick);
      len += s;
      glUniform2f(shaders.trail.pos1, len, spp->thick);

      /* Draw. */
      glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
   }

   /* Clear state. */
   glDisableVertexAttribArray( shaders.trail.vertex );
   glUseProgram(0);

   /* Check errors. */
   gl_checkErr();
}

/**
 * @brief Increases the current rumble level.
 *
 * Rumble will decay over time.
 *
 *    @param mod Modifier to increase the level by.
 */
void spfx_shake( double mod )
{
   /* Add the modifier. */
   shake_force_mod = MIN( SPFX_SHAKE_MAX, shake_force_mod + SPFX_SHAKE_MOD*mod );

   /* Rumble if it wasn't rumbling before. */
   spfx_hapticRumble(mod);

   /* Create the shake. */
   if (shake_shader_pp_id==0)
      shake_shader_pp_id = render_postprocessAdd( &shake_shader, PP_LAYER_GAME, 99 );
}

/**
 * @brief Increases the current damage level.
 *
 * Damage will decay over time.
 *
 *    @param mod Modifier to increase the level by.
 */
void spfx_damage( double mod )
{
   damage_strength = MIN( SPFX_DAMAGE_MAX, damage_strength + SPFX_DAMAGE_MOD*mod );

   /* Create the damage. */
   if (damage_shader_pp_id==0)
      damage_shader_pp_id = render_postprocessAdd( &damage_shader, PP_LAYER_GUI, 98 );
}

/**
 * @brief Initializes the rumble effect.
 *
 *    @return 0 on success.
 */
static int spfx_hapticInit (void)
{
   SDL_HapticEffect *efx;

   /* Haptic must be enabled. */
   if (haptic == NULL)
      return 0;

   efx = &haptic_rumbleEffect;
   memset( efx, 0, sizeof(SDL_HapticEffect) );
   efx->type = SDL_HAPTIC_SINE;
   efx->periodic.direction.type   = SDL_HAPTIC_POLAR;
   efx->periodic.length           = 1000;
   efx->periodic.period           = 200;
   efx->periodic.magnitude        = 0x4000;
   efx->periodic.fade_length      = 1000;
   efx->periodic.fade_level       = 0;

   haptic_rumble = SDL_HapticNewEffect( haptic, efx );
   if (haptic_rumble < 0) {
      WARN(_("Unable to upload haptic effect: %s."), SDL_GetError());
      return -1;
   }

   return 0;
}

/**
 * @brief Runs a rumble effect.
 *
 *    @brief Current modifier being added.
 */
static void spfx_hapticRumble( double mod )
{
   SDL_HapticEffect *efx;
   double len, mag;

   /* Not active. */
   if (haptic_rumble < 0)
      return;

   /* Not time to update yet. */
   if ((haptic_lastUpdate > 0.) || (shake_shader_pp_id==0) || (mod > SPFX_SHAKE_MAX/3.))
      return;

   /* Stop the effect if it was playing. */
   SDL_HapticStopEffect( haptic, haptic_rumble );

   /* Get length and magnitude. */
   len = 1000. * shake_force_mod / SPFX_SHAKE_DECAY;
   mag = 32767. * (shake_force_mod / SPFX_SHAKE_MAX);

   /* Update the effect. */
   efx = &haptic_rumbleEffect;
   efx->periodic.magnitude    = (int16_t)mag;
   efx->periodic.length       = (uint32_t)len;
   efx->periodic.fade_length  = MIN( efx->periodic.length, 1000 );
   if (SDL_HapticUpdateEffect( haptic, haptic_rumble, &haptic_rumbleEffect ) < 0) {
      WARN(_("Failed to update haptic effect: %s."), SDL_GetError());
      return;
   }

   /* Run the new effect. */
   SDL_HapticRunEffect( haptic, haptic_rumble, 1 );

   /* Set timer again. */
   haptic_lastUpdate += HAPTIC_UPDATE_INTERVAL;
}

/**
 * @brief Sets the cinematic mode.
 *
 * Should be run at the end of the render loop if needed.
 */
void spfx_cinematic (void)
{
   gl_renderRect( 0., 0.,           SCREEN_W, SCREEN_H*0.2, &cBlack );
   gl_renderRect( 0., SCREEN_H*0.8, SCREEN_W, SCREEN_H,     &cBlack );
}

/**
 * @brief Renders the entire spfx layer.
 *
 *    @param layer Layer to render.
 */
void spfx_render( int layer )
{
   SPFX *spfx_stack;
   int sx, sy;
   double time;

   /* get the appropriate layer */
   switch (layer) {
      case SPFX_LAYER_FRONT:
         spfx_stack = spfx_stack_front;
         break;

      case SPFX_LAYER_MIDDLE:
         spfx_stack = spfx_stack_middle;
         break;

      case SPFX_LAYER_BACK:
         spfx_stack = spfx_stack_back;
         break;

      default:
         WARN(_("Rendering invalid SPFX layer."));
         return;
   }

   /* Trails are special (for now?). */
   if (layer == SPFX_LAYER_BACK)
      for (int i=0; i<array_size(trail_spfx_stack); i++) {
         Trail_spfx *trail = trail_spfx_stack[i];
         if (!trail->ontop)
            spfx_trail_draw( trail );
      }

   /* Now render the layer */
   for (int i=array_size(spfx_stack)-1; i>=0; i--) {
      SPFX *spfx        = &spfx_stack[i];
      SPFX_Base *effect = &spfx_effects[ spfx->effect ];

      /* Render shader. */
      if (effect->shader >= 0) {
         double x, y, z, s2;
         double w, h;
         gl_Matrix4 projection;

         /* Translate coords. */
         s2 = effect->size/2.;
         z = cam_getZoom();
         gl_gameToScreenCoords( &x, &y, spfx->pos.x-s2, spfx->pos.y-s2 );
         w = h = effect->size*z;

         /* Check if inbounds. */
         if ((x < -w) || (x > SCREEN_W+w) ||
               (y < -h) || (y > SCREEN_H+h))
            continue;

         /* Let's get to business. */
         glUseProgram( effect->shader );

         /* Set up the vertex. */
         projection = gl_view_matrix;
         projection = gl_Matrix4_Translate(projection, x, y, 0);
         projection = gl_Matrix4_Scale(projection, w, h, 1);
         glEnableVertexAttribArray( effect->vertex );
         gl_vboActivateAttribOffset( gl_squareVBO, effect->vertex,
               0, 2, GL_FLOAT, 0 );

         /* Set shader uniforms. */
         gl_Matrix4_Uniform(effect->projection, projection);
         glUniform1f(effect->u_time, spfx->time);
         glUniform1f(effect->u_r, spfx->unique);
         glUniform1f(effect->u_size, effect->size);

         /* Draw. */
         glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

         /* Clear state. */
         glDisableVertexAttribArray( shaders.texture.vertex );

         /* anything failed? */
         gl_checkErr();

         glUseProgram(0);

      }
      /* No shader. */
      else {
         /* Simplifies */
         sx = (int)effect->gfx->sx;
         sy = (int)effect->gfx->sy;

         if (!paused) { /* don't calculate frame if paused */
            time = 1. - fmod(spfx_stack[i].timer,effect->anim) / effect->anim;
            spfx_stack[i].lastframe = sx * sy * MIN(time, 1.);
         }

         /* Renders */
         gl_renderSprite( effect->gfx,
               VX(spfx_stack[i].pos), VY(spfx_stack[i].pos),
               spfx_stack[i].lastframe % sx,
               spfx_stack[i].lastframe / sx,
               NULL );
      }
   }
}

/**
 * @brief Parses raw values out of a "trail" element.
 * \warning This means values like idle->thick aren't ready to use.
 */
static void trailSpec_parse( xmlNodePtr node, TrailSpec *tc )
{
   static const char *mode_tags[] = MODE_TAGS;
   xmlNodePtr cur = node->children;

   do {
      xml_onlyNodes(cur);
      if (xml_isNode(cur,"thickness"))
         tc->def_thick = xml_getFloat( cur );
      else if (xml_isNode(cur, "ttl"))
         tc->ttl = xml_getFloat( cur );
      else if (xml_isNode(cur, "type")) {
         char *type = xml_get(cur);
         if (gl_has( OPENGL_SUBROUTINES )) {
            tc->type = glGetSubroutineIndex( shaders.trail.program, GL_FRAGMENT_SHADER, type );
            if (tc->type == GL_INVALID_INDEX)
               WARN("Trail '%s' has unknown type '%s'", tc->name, type );
         }
      }
      else if (xml_isNode(cur, "nebula"))
         tc->nebula = xml_getInt( cur );
      else {
         int i;
         for (i=0; i<MODE_MAX; i++)
            if (xml_isNode(cur, mode_tags[i])) {
               xmlr_attr_float_opt( cur, "r", tc->style[i].col.r );
               xmlr_attr_float_opt( cur, "g", tc->style[i].col.g );
               xmlr_attr_float_opt( cur, "b", tc->style[i].col.b );
               xmlr_attr_float_opt( cur, "a", tc->style[i].col.a );
               xmlr_attr_float_opt( cur, "scale", tc->style[i].thick );
               col_gammaToLinear( &tc->style[i].col );
               break;
            }
         if (i == MODE_MAX)
            WARN(_("Trail '%s' has unknown node '%s'."), tc->name, cur->name);
      }
   } while (xml_nextNode(cur));

#define MELEMENT(o,s)   if (o) WARN(_("Trail '%s' missing '%s' element"), tc->name, s)
   MELEMENT( tc->def_thick==0, "thickness" );
   MELEMENT( tc->ttl==0, "ttl" );
#undef MELEMENT
}

/**
 * @brief Loads the trail colour sets.
 *
 *    @return 0 on success.
 */
static int trailSpec_load (void)
{
   TrailSpec *tc, *parent;
   xmlNodePtr first, node;
   xmlDocPtr doc;
   char *name, *inherits;

   /* Load the data. */
   doc = xml_parsePhysFS( TRAIL_DATA_PATH );
   if (doc == NULL)
      return -1;

   /* Get the root node. */
   node = doc->xmlChildrenNode;
   if (!xml_isNode(node,"Trail_types")) {
      WARN( _("Malformed '%s' file: missing root element 'Trail_types'"), TRAIL_DATA_PATH);
      return -1;
   }

   /* Get the first node. */
   first = node->xmlChildrenNode; /* first event node */
   if (node == NULL) {
      WARN( _("Malformed '%s' file: does not contain elements"), TRAIL_DATA_PATH);
      return -1;
   }

   trail_spec_stack = array_create( TrailSpec );

   node = first;
   do {
      xml_onlyNodes( node );
      if (xml_isNode(node,"trail")) {
         tc = &array_grow( &trail_spec_stack );
         memset( tc, 0, sizeof(TrailSpec) );
         for(int i=0; i<MODE_MAX; i++)
            tc->style[i].thick = 1.;
         xmlr_attr_strd( node, "name", tc->name );

         /* Do the first pass for non-inheriting trails. */
         xmlr_attr_strd( node, "inherits", inherits );
         if (inherits == NULL)
            trailSpec_parse( node, tc );
         else
            free( inherits );
      }
   } while (xml_nextNode(node));

   /* Second pass to complete inheritance. */
   node = first;
   do {
      xml_onlyNodes( node );
      if (xml_isNode(node,"trail")) {
         /* Only interested in inherits. */
         xmlr_attr_strd( node, "inherits", inherits );
         if (inherits == NULL)
            continue;
         parent = trailSpec_getRaw( inherits );

         /* Get the style itself. */
         xmlr_attr_strd( node, "name", name );
         tc = trailSpec_getRaw( name );

         /* Make sure we found stuff. */
         if ((tc==NULL) || (parent==NULL)) {
            WARN(_("Trail '%s' that inherits from '%s' has missing reference!"), name, inherits );
            continue;
         }
         free( inherits );
         free( name );

         /* Set properties. */
         name = tc->name;
         memcpy( tc, parent, sizeof(TrailSpec) );
         tc->name = name;

         /* Load remaining properties (overrides parent). */
         trailSpec_parse( node, tc );
      }
   } while (xml_nextNode(node));

   for (tc=array_begin(trail_spec_stack); tc!=array_end(trail_spec_stack); tc++) {
      for(int i=0; i<MODE_MAX; i++)
         tc->style[i].thick *= tc->def_thick;
   }
   array_shrink(&trail_spec_stack);

   /* Clean up. */
   xmlFreeDoc(doc);

   return 0;
}

static TrailSpec* trailSpec_getRaw( const char* name )
{
   for (int i=0; i<array_size(trail_spec_stack); i++) {
      if ( strcmp(trail_spec_stack[i].name, name)==0 )
         return &trail_spec_stack[i];
   }
   WARN(_("Trail type '%s' not found in stack"), name);
   return NULL;
}

/**
 * @brief Gets a trail spec by name.
 *
 *    @return TrailSpec reference if found, else NULL.
 */
const TrailSpec* trailSpec_get( const char* name )
{
   return trailSpec_getRaw( name );
}
