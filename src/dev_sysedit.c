/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file dev_sysedit.c
 *
 * @brief Handles the star system editor.
 */
/** @cond */
#include "physfs.h"
#include "SDL.h"

#include "naev.h"
/** @endcond */

#include "dev_sysedit.h"

#include "array.h"
#include "conf.h"
#include "dev_planet.h"
#include "dev_system.h"
#include "dev_uniedit.h"
#include "dialogue.h"
#include "economy.h"
#include "map.h"
#include "ndata.h"
#include "nstring.h"
#include "opengl.h"
#include "opengl_render.h"
#include "safelanes.h"
#include "space.h"
#include "tk/toolkit_priv.h"
#include "toolkit.h"
#include "unidiff.h"

#define BUTTON_WIDTH    90 /**< Map button width. */
#define BUTTON_HEIGHT   30 /**< Map button height. */

#define SYSEDIT_EDIT_WIDTH       500 /**< System editor width. */
#define SYSEDIT_EDIT_HEIGHT      400 /**< System editor height. */

#define SYSEDIT_DRAG_THRESHOLD   300   /**< Drag threshold. */
#define SYSEDIT_MOVE_THRESHOLD   10    /**< Movement threshold. */

#define SYSEDIT_ZOOM_STEP        1.2   /**< Factor to zoom by for each zoom level. */
#define SYSEDIT_ZOOM_MAX         1     /**< Maximum zoom level (close). */
#define SYSEDIT_ZOOM_MIN         -23   /**< Minimum zoom level (far). */

/*
 * Selection types.
 */
#define SELECT_NONE        0 /**< No selection. */
#define SELECT_PLANET      1 /**< Selection is a planet. */
#define SELECT_JUMPPOINT   2 /**< Selection is a jump point. */

/**
 * @brief Selection generic for stuff in a system.
 */
typedef struct Select_s {
   int type; /**< Type of selection. */
   union {
      int planet;
      int jump;
   } u; /**< Data itself. */
} Select_t;
static Select_t *sysedit_select  = NULL; /**< Current system selection. */
static int sysedit_nselect       = 0; /**< Number of selections in current system. */
static int sysedit_mselect       = 0; /**< Memory allocated for selections. */
static Select_t sysedit_tsel;         /**< Temporary selection. */
static int  sysedit_tadd         = 0; /**< Add to selection. */

/*
 * System editor stuff.
 */
static StarSystem *sysedit_sys = NULL; /**< Currently opened system. */
static unsigned int sysedit_wid = 0; /**< Sysedit wid. */
static unsigned int sysedit_widEdit = 0; /**< Planet editor wid. */
static int sysedit_grid       = 1;  /**< Grid is visible. */
static double sysedit_xpos    = 0.; /**< Viewport X position. */
static double sysedit_ypos    = 0.; /**< Viewport Y position. */
static double sysedit_zoom    = 1.; /**< Viewport zoom level. */
static int sysedit_moved      = 0;  /**< Space moved since mouse down. */
static unsigned int sysedit_dragTime = 0; /**< Tick last started to drag. */
static int sysedit_drag       = 0;  /**< Dragging viewport around. */
static int sysedit_dragSel    = 0;  /**< Dragging system around. */
static double sysedit_mx      = 0.; /**< Cursor X position. */
static double sysedit_my      = 0.; /**< Cursor Y position. */

/* Stored checkbox values. */
static int jp_hidden = 0; /**< Jump point hidden checkbox value. */
static int jp_exit   = 0; /**< Jump point exit only checkbox value. */

/*
 * System editor Prototypes.
 */
/* Custom system editor widget. */
static void sysedit_buttonZoom( unsigned int wid, const char* str );
static void sysedit_render( double bx, double by, double w, double h, void *data );
static void sysedit_renderAsteroidsField( double bx, double by, AsteroidAnchor *ast, int selected );
static void sysedit_renderAsteroidExclusion( double bx, double by, AsteroidExclusion *aexcl, int selected );
static void sysedit_renderBG( double bx, double bw, double w, double h, double x, double y );
static void sysedit_renderSprite( glTexture *gfx, double bx, double by, double x, double y,
      int sx, int sy, const glColour *c, int selected, const char *caption );
static void sysedit_renderOverlay( double bx, double by, double bw, double bh, void* data );
static void sysedit_focusLose( unsigned int wid, const char* wgtname );
static int sysedit_mouse( unsigned int wid, SDL_Event* event, double mx, double my,
      double w, double h, double xr, double yr, void *data );
/* Button functions. */
static void sysedit_close( unsigned int wid, const char *wgt );
static void sysedit_btnNew( unsigned int wid_unused, const char *unused );
static void sysedit_btnRename( unsigned int wid_unused, const char *unused );
static void sysedit_btnRemove( unsigned int wid_unused, const char *unused );
static void sysedit_btnReset( unsigned int wid_unused, const char *unused );
static void sysedit_btnScale( unsigned int wid_unused, const char *unused );
static void sysedit_btnGrid( unsigned int wid_unused, const char *unused );
static void sysedit_btnEdit( unsigned int wid_unused, const char *unused );
/* Planet editing. */
static void sysedit_editPnt( void );
static void sysedit_editPntClose( unsigned int wid, const char *unused );
static void sysedit_planetDesc( unsigned int wid, const char *unused );
static void sysedit_planetDescReturn( unsigned int wid, const char *unused );
static void sysedit_planetDescClose( unsigned int wid, const char *unused );
static void sysedit_genServicesList( unsigned int wid );
static void sysedit_btnTechEdit( unsigned int wid, const char *unused );
static void sysedit_genTechList( unsigned int wid );
static void sysedit_btnAddTech( unsigned int wid, const char *unused );
static void sysedit_btnRmTech( unsigned int wid, const char *unused );
static void sysedit_btnAddService( unsigned int wid, const char *unused );
static void sysedit_btnRmService( unsigned int wid, const char *unused );
static void sysedit_planetGFX( unsigned int wid_unused, const char *wgt );
static void sysedit_btnGFXClose( unsigned int wid, const char *wgt );
static void sysedit_btnGFXApply( unsigned int wid, const char *wgt );
static void sysedit_btnFaction( unsigned int wid_unused, const char *unused );
static void sysedit_btnFactionSet( unsigned int wid, const char *unused );
static void sysedit_editJumpClose( unsigned int wid, const char *unused );
/* Jump editing */
static void sysedit_editJump( void );
/* Keybindings handling. */
static int sysedit_keys( unsigned int wid, SDL_Keycode key, SDL_Keymod mod );
/* Selection. */
static int sysedit_selectCmp( Select_t *a, Select_t *b );
static void sysedit_checkButtons (void);
static void sysedit_deselect (void);
static void sysedit_selectAdd( Select_t *sel );
static void sysedit_selectRm( Select_t *sel );

/**
 * @brief Opens the system editor interface.
 */
void sysedit_open( StarSystem *sys )
{
   unsigned int wid;
   char buf[128];
   int i;

   /* Reconstructs the jumps - just in case. */
   systems_reconstructJumps();

   /* Reset some variables. */
   sysedit_sys    = sys;
   sysedit_drag   = 0;
   sysedit_zoom   = pow(SYSEDIT_ZOOM_STEP, SYSEDIT_ZOOM_MIN);
   sysedit_xpos   = 0.;
   sysedit_ypos   = 0.;

   /* Load graphics. */
   space_gfxLoad( sysedit_sys );

   /* Create the window. */
   snprintf( buf, sizeof(buf), _("%s - Star System Editor"), sys->name );
   wid = window_create( "wdwSysEdit", buf, -1, -1, -1, -1 );
   window_handleKeys( wid, sysedit_keys );
   sysedit_wid = wid;

   window_setAccept( wid, sysedit_close );

   /* Close button. */
   window_addButtonKey( wid, -15, 20, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnClose", _("Exit"), sysedit_close, SDLK_x );
   i = 1;

   /* Autosave toggle. */
   window_addCheckbox( wid, -150, 25, SCREEN_W/2 - 150, 20,
         "chkEditAutoSave", _("Automatically save changes"), uniedit_autosave, conf.devautosave );

   /* Scale. */
   window_addButton( wid, -15, 20+(BUTTON_HEIGHT+20)*i, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnScale", _("Scale"), sysedit_btnScale );
   i += 1;

   /* Reset. */
   window_addButtonKey( wid, -15, 20+(BUTTON_HEIGHT+20)*i, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnReset", _("Reset Jumps"), sysedit_btnReset, SDLK_r );
   i += 1;

   /* Editing. */
   window_addButtonKey( wid, -15, 20+(BUTTON_HEIGHT+20)*i, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnEdit", _("Edit"), sysedit_btnEdit, SDLK_e );
   i += 1;

   /* Remove. */
   window_addButton( wid, -15, 20+(BUTTON_HEIGHT+20)*i, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnRemove", _("Remove"), sysedit_btnRemove );
   i += 1;

   /* Rename. */
   window_addButton( wid, -15, 20+(BUTTON_HEIGHT+20)*i, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnRename", _("Rename"), sysedit_btnRename );
   i += 1;

   /* New planet. */
   window_addButtonKey( wid, -15, 20+(BUTTON_HEIGHT+20)*i, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnNew", _("New Planet"), sysedit_btnNew, SDLK_n );
   i += 2;

   /* Toggle Grid. */
   window_addButtonKey( wid, -15, 20+(BUTTON_HEIGHT+20)*i, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnGrid", _("Grid"), sysedit_btnGrid, SDLK_g );

   /* Zoom buttons */
   window_addButton( wid, 40, 20, 30, 30, "btnZoomIn", "+", sysedit_buttonZoom );
   window_addButton( wid, 80, 20, 30, 30, "btnZoomOut", "-", sysedit_buttonZoom );

   /* Selected text. */
   snprintf( buf, sizeof(buf), _("Radius: %.0f"), sys->radius );
   window_addText( wid, 140, 10, SCREEN_W/2-140, 30, 0,
         "txtSelected", &gl_smallFont, NULL, buf );

   /* Actual viewport. */
   window_addCust( wid, 20, -40, SCREEN_W - 150, SCREEN_H - 100,
         "cstSysEdit", 1, sysedit_render, sysedit_mouse, NULL, sysedit_focusLose, NULL );
   window_custSetOverlay( wid, "cstSysEdit", sysedit_renderOverlay );

   /* Deselect everything. */
   sysedit_deselect();
}

/**
 * @brief Handles keybindings.
 */
static int sysedit_keys( unsigned int wid, SDL_Keycode key, SDL_Keymod mod )
{
   (void) wid;
   (void) mod;

   switch (key) {

      default:
         return 0;
   }
}

/**
 * @brief Closes the system editor widget.
 */
static void sysedit_close( unsigned int wid, const char *wgt )
{
   /* Unload graphics. */
   space_gfxLoad( sysedit_sys );

   /* Remove selection. */
   sysedit_deselect();

   /* Set the dominant faction. */
   system_setFaction( sysedit_sys );

   /* Save the system */
   if (conf.devautosave)
      dsys_saveSystem( sysedit_sys );

   /* Reconstruct universe presences. */
   space_reconstructPresences();
   safelanes_recalculate();

   /* Close the window. */
   window_close( wid, wgt );

   /* Update the universe editor's sidebar text. */
   uniedit_selectText();

   /* Propagate autosave checkbox state */
   uniedit_updateAutosave();

   /* Unset. */
   sysedit_wid = 0;
}

/**
 * @brief Closes the planet editor, saving the changes made.
 */
static void sysedit_editPntClose( unsigned int wid, const char *unused )
{
   (void) unused;
   Planet *p;
   const char *inp;

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];

   p->population = (uint64_t)strtoull( window_getInput( sysedit_widEdit, "inpPop" ), 0, 10);

   inp = window_getInput( sysedit_widEdit, "inpClass" );
   free( p->class );

   if ( inp[0] == '\0' )
      p->class = NULL;
   else
      p->class = strdup( inp );

   inp = window_getInput( sysedit_widEdit, "inpLand" );
   free( p->land_func );

   if ((inp == NULL) || (strlen(inp) == 0))
      p->land_func = NULL;
   else
      p->land_func = strdup( inp );

   p->presence.base  = atof(window_getInput( sysedit_widEdit, "inpPresenceBase" ));
   p->presence.bonus = atof(window_getInput( sysedit_widEdit, "inpPresenceBonus" ));
   p->presence.range = atoi(window_getInput( sysedit_widEdit, "inpPresenceRange" ));
   p->hide           = atof(window_getInput( sysedit_widEdit, "inpHide" ));

   /* Have to recompute presences if stuff changed. */
   space_reconstructPresences();

   if (conf.devautosave)
      dpl_savePlanet( p );

   /* Clean up presences. */
   space_reconstructPresences();
   safelanes_recalculate();

   window_close( wid, unused );
}

/**
 * @brief Enters the editor in new planet mode.
 */
static void sysedit_btnNew( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;
   Planet *p, *b;
   char *name;

   /* Get new name. */
   name = dialogue_inputRaw( _("New Planet Creation"), 1, 32, _("What do you want to name the new planet?") );
   if (name == NULL)
      return;

   /* Check for collision. */
   if (planet_exists( name )) {
      dialogue_alert( _("Planet by the name of #r'%s'#0 already exists in the #r'%s'#0 system"),
            name, planet_getSystem( name ) );
      free(name);
      sysedit_btnNew( 0, NULL );
      return;
   }

   /* Create the new planet. */
   p        = planet_new();
   p->name  = name;

   /* Base planet data off another. */
   b                    = planet_get( space_getRndPlanet(0, 0, NULL) );
   p->class             = strdup( b->class );
   p->gfx_spacePath     = strdup( b->gfx_spacePath );
   p->gfx_spaceName     = strdup( b->gfx_spaceName );
   p->gfx_exterior      = strdup( b->gfx_exterior );
   p->gfx_exteriorPath  = strdup( b->gfx_exteriorPath );
   p->pos.x             = sysedit_xpos / sysedit_zoom;
   p->pos.y             = sysedit_ypos / sysedit_zoom;
   p->hide              = HIDE_DEFAULT_PLANET;
   p->radius            = b->radius;

   /* Add new planet. */
   system_addPlanet( sysedit_sys, name );

   /* Update economy due to galaxy modification. */
   economy_execQueued();

   if (conf.devautosave)
      dpl_savePlanet( p );

   /* Reload graphics. */
   space_gfxLoad( sysedit_sys );
}

static void sysedit_btnRename( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;
   for (int i=0; i<sysedit_nselect; i++) {
      Select_t *sel = &sysedit_select[i];
      if (sel->type == SELECT_PLANET) {
         char *name, *oldName, *newName, *filtered;
         Planet *p = sysedit_sys[i].planets[ sel->u.planet ];

         /* Get new name. */
         name = dialogue_input( _("New Planet Creation"), 1, 32,
               _("What do you want to rename the planet #r%s#0?"), p->name );
         if (name == NULL)
            continue;

         /* Check for collision. */
         if (planet_exists( name )) {
            dialogue_alert( _("Planet by the name of #r'%s'#0 already exists in the #r'%s'#0 system"),
                  name, planet_getSystem( name ) );
            free(name);
            continue;
         }

         /* Rename. */
         filtered = uniedit_nameFilter(p->name);
         asprintf(&oldName, "dat/assets/%s.xml", filtered);
         free(filtered);

         filtered = uniedit_nameFilter(name);
         asprintf(&newName, "dat/assets/%s.xml", filtered);
         free(filtered);

         rename(oldName, newName);

         free(oldName);
         free(newName);
         free(p->name);

         p->name = name;
         window_modifyText( sysedit_widEdit, "txtName", p->name );
         dpl_savePlanet( p );
      }
   }
}

/**
 * @brief Removes planets.
 */
static void sysedit_btnRemove( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;
   char *file, *filtered;

   if (dialogue_YesNo( _("Remove selected planets?"), _("This can not be undone.") )) {
      for (int i=0; i<sysedit_nselect; i++) {
         Select_t *sel = &sysedit_select[i];
         if (sel->type == SELECT_PLANET) {
            filtered = uniedit_nameFilter( sysedit_sys->planets[ sel->u.planet ]->name );
            asprintf(&file, "dat/assets/%s.xml", filtered);
            remove(file);

            free(filtered);
            free(file);

            system_rmPlanet( sysedit_sys, sysedit_sys->planets[ sel->u.planet ]->name );
         }
      }

      /* Update economy due to galaxy modification. */
      economy_execQueued();
   }
}

/**
 * @brief Resets jump points.
 */
static void sysedit_btnReset( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;
   for (int i=0; i<sysedit_nselect; i++) {
      Select_t *sel = &sysedit_select[i];
      if (sel->type == SELECT_JUMPPOINT)
         sysedit_sys[i].jumps[ sel->u.jump ].flags |= JP_AUTOPOS;
   }

   /* Must reconstruct jumps. */
   systems_reconstructJumps();
}

/**
 * @brief Interface for scaling a system from the system view.
 */
static void sysedit_btnScale( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;
   char *str;
   double s;
   StarSystem *sys;

   /* Prompt scale amount. */
   str = dialogue_inputRaw( _("Scale Star System"), 1, 32, _("By how much do you want to scale the star system?") );
   if (str == NULL)
      return;

   sys   = sysedit_sys; /* Comfort. */
   s     = atof(str);
   free(str);

   /* In case screwed up. */
   if ((s < 0.1) || (s > 10.)) {
      int i = dialogue_YesNo( _("Scale Star System"), _("Are you sure you want to scale the star system by %.2f (from %.2f to %.2f)?"),
            s, sys->radius, sys->radius*s );
      if (i==0)
         return;
   }

   sysedit_sysScale(sys, s);
}

/**
 * @brief Scales a system.
 */
void sysedit_sysScale( StarSystem *sys, double factor )
{
   char buf[STRMAX];

   /* Scale radius. */
   sys->radius *= factor;
   snprintf( buf, sizeof(buf), _("Radius: %.0f"), sys->radius );
   if (sysedit_wid > 0)
      window_modifyText( sysedit_wid, "txtSelected", buf );

   /* Scale planets. */
   for (int i=0; i<array_size(sys->planets); i++) {
      Planet *p = sys->planets[i];
      vect_cset( &p->pos, p->pos.x*factor, p->pos.y*factor );
   }

   /* Scale jumps. */
   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *jp = &sys->jumps[i];
      vect_cset( &jp->pos, jp->pos.x*factor, jp->pos.y*factor );
   }

   /* Must reconstruct jumps. */
   systems_reconstructJumps();
}

/**
 * @brief Toggles the grid.
 */
static void sysedit_btnGrid( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;

   sysedit_grid = !sysedit_grid;
}

/**
 * @brief System editor custom widget rendering.
 */
static void sysedit_render( double bx, double by, double w, double h, void *data )
{
   (void) data;
   StarSystem *sys;
   double x,y, z;
   const glColour *c;
   glColour col;
   int selected;
   Select_t sel;

   /* Comfort++. */
   sys   = sysedit_sys;
   z     = sysedit_zoom;

   /* Coordinate translation. */
   x = bx - sysedit_xpos + w/2;
   y = by - sysedit_ypos + h/2;

   /* First render background with lines. */
   sysedit_renderBG( bx, by, w, h, x, y );

   /* Render planets. */
   for (int i=0; i<array_size(sys->planets); i++) {
      Planet *p      = sys->planets[i];

      /* Check if selected. */
      sel.type       = SELECT_PLANET;
      sel.u.planet   = i;
      selected       = 0;
      for (int j=0; j<sysedit_nselect; j++) {
         if (sysedit_selectCmp( &sel, &sysedit_select[j] )) {
            selected = 1;
            break;
         }
      }

      /* Render. */
      sysedit_renderSprite( p->gfx_space, x, y, p->pos.x, p->pos.y, 0, 0, NULL, selected, p->name );
   }

   /* Render jump points. */
   for (int i=0; i<array_size(sys->jumps); i++) {
      JumpPoint *jp = &sys->jumps[i];

      /* Choose colour. */
      if (jp->flags & JP_AUTOPOS)
         c = &cGreen;
      else
         c = NULL;

      /* Check if selected. */
      sel.type       = SELECT_JUMPPOINT;
      sel.u.planet   = i;
      selected       = 0;
      for (int j=0; j<sysedit_nselect; j++) {
         if (sysedit_selectCmp( &sel, &sysedit_select[j] )) {
            selected = 1;
            break;
         }
      }

      /* Render. */
      sysedit_renderSprite( jumppoint_gfx, x, y, jp->pos.x, jp->pos.y,
            jp->sx, jp->sy, c, selected, jp->target->name );
   }

   /* Render asteroids */
   for (int i=0; i<array_size(sys->asteroids); i++) {
      AsteroidAnchor *ast = &sys->asteroids[i];
      selected = 0;
      sysedit_renderAsteroidsField( x, y, ast, selected );
   }

   /* Render asteroid exclusions */
   for (int i=0; i<array_size(sys->astexclude); i++) {
      AsteroidExclusion *aexcl = &sys->astexclude[i];
      selected = 0;
      sysedit_renderAsteroidExclusion( x, y, aexcl, selected );
   }

   /* Render safe lanes. */
   SafeLane* safelanes = safelanes_get( -1, 0, sys );
   for (int i=0; i<array_size(safelanes); i++) {
      Vector2d *posns[2];
      Planet *pnt;
      JumpPoint *njp;
      SafeLane *sf = &safelanes[i];

      for (int j=0; j<2; j++) {
         switch(sf->point_type[j]) {
            case SAFELANE_LOC_PLANET:
               pnt = planet_getIndex( sf->point_id[j] );
               posns[j] = &pnt->pos;
               break;
            case SAFELANE_LOC_DEST_SYS:
               njp = jump_getTarget( system_getIndex( sf->point_id[j] ), sys );
               posns[j] = &njp->pos;
               break;
            default:
               ERR( _("Invalid vertex type.") );
         }
      }

      col = *faction_colour( sf->faction );
      col.a = 0.3;

      /* Get positions and stuff. */
      double x1, y1, x2, y2, ry, rx, r, rw, rh;
      x1 = x + posns[0]->x * z;
      y1 = y + posns[0]->y * z;
      x2 = x + posns[1]->x * z;
      y2 = y + posns[1]->y * z;
      rx = x2-x1;
      ry = y2-y1;
      r  = atan2( ry, rx );
      rw = MOD(rx,ry)/2.;
      rh = 9.;

      /* Render. */
      glUseProgram(shaders.safelane.program);
      gl_renderShader( (x1+x2)/2., (y1+y2)/2., rw, rh, r, &shaders.safelane, &col, 1 );
   }

   /* Render cursor position. */
   gl_print( &gl_smallFont, bx + 5., by + 5.,
         &cWhite, "%.2f, %.2f",
         (bx + sysedit_mx - x)/z,
         (by + sysedit_my - y)/z );
}

/**
 * @brief Draws an asteroid field on the map.
 *
 */
static void sysedit_renderAsteroidsField( double bx, double by, AsteroidAnchor *ast, int selected )
{
   double tx, ty, z;

   /* Render icon */
   sysedit_renderSprite( asteroid_gfx[0], bx, by, ast->pos.x, ast->pos.y,
                         0, 0, NULL, selected, _("Asteroid Field") );

   /* Inits. */
   z  = sysedit_zoom;

   /* Translate asteroid field center's coords. */
   tx = bx + ast->pos.x*z;
   ty = by + ast->pos.y*z;

   gl_renderCircle( tx, ty, ast->radius * sysedit_zoom, &cOrange, 0 );
}

/**
 * @brief Draws an asteroid exclusion zone on the map.
 *
 */
static void sysedit_renderAsteroidExclusion( double bx, double by, AsteroidExclusion *aexcl, int selected )
{
   (void) selected;
   double tx, ty, z, r, rr;

   /* Inits. */
   z  = sysedit_zoom;

   /* Translate asteroid field center's coords. */
   tx = bx + aexcl->pos.x*z;
   ty = by + aexcl->pos.y*z;
   r = aexcl->radius * sysedit_zoom;
   rr = r * sin(M_PI / 4.);

   gl_renderCircle( tx, ty, r, &cRed, 0 );
   gl_renderCross( tx, ty, r, &cRed );
   gl_renderRectEmpty( tx - rr, ty - rr, rr * 2, rr * 2, &cRed );
}

/**
 * @brief Renders the custom widget background.
 */
static void sysedit_renderBG( double bx, double by, double w, double h, double x, double y )
{
   /* Comfort. */
   const double z = sysedit_zoom;
   const double s = 1000.;

   /* Vars */
   double startx, starty, spacing;
   int nx, ny;

   /* Render blackness. */
   gl_renderRect( bx, by, w, h, &cBlack );

   /* Must have grid activated. */
   if (!sysedit_grid)
      return;

   /* Draw lines that go through 0,0 */
   gl_renderRect( x - 1., by, 3., h, &cLightBlue );
   gl_renderRect( bx, y - 1., w, 3., &cLightBlue );

   /* Render lines. */
   spacing = s * z;
   startx  = bx + fmod( x - bx, spacing );
   starty  = by + fmod( y - by, spacing );

   nx = lround( w / spacing );
   ny = lround( h / spacing );

   /* Vertical. */
   for (int i=0; i<nx; i++) {
      double d = startx + (i * spacing);
      gl_renderLine( d, by, d, by + h, &cBlue );
   }
   /* Horizontal. */
   for (int i=0; i<ny; i++) {
      double d = starty + (i * spacing);
      gl_renderLine( bx, d, bx + w, d, &cBlue );
   }

   gl_renderCircle( x, y, sysedit_sys->radius * z, &cLightBlue, 0 );
}

/**
 * @brief Renders a sprite for the custom widget.
 */
static void sysedit_renderSprite( glTexture *gfx, double bx, double by, double x, double y,
      int sx, int sy, const glColour *c, int selected, const char *caption )
{
   double tx, ty, z;
   glColour cc;
   const glColour *col;

   /* Comfort. */
   z  = sysedit_zoom;

   /* Translate coords. */
   tx = bx + (x - gfx->sw/2.)*z;
   ty = by + (y - gfx->sh/2.)*z;

   /* Selection graphic. */
   if (selected) {
      cc.r = cFontBlue.r;
      cc.g = cFontBlue.g;
      cc.b = cFontBlue.b;
      cc.a = 0.5;
      gl_renderCircle( bx + x*z, by + y*z, gfx->sw*z*1.1, &cc, 1 );
   }

   /* Blit the planet. */
   gl_renderScaleSprite( gfx, tx, ty, sx, sy, gfx->sw*z, gfx->sh*z, c );

   /* Display caption. */
   if (caption != NULL) {
      if (selected)
         col = &cRed;
      else
         col = c;
      gl_printMidRaw( &gl_smallFont, gfx->sw*z+100,
            tx - 50, ty - gl_smallFont.h - 5, col, -1., caption );
   }
}

/**
 * @brief Renders the overlay.
 */
static void sysedit_renderOverlay( double bx, double by, double bw, double bh, void* data )
{
   (void) bx;
   (void) by;
   (void) bw;
   (void) bh;
   (void) data;
}

/**
 * @brief Called when it's de-focused.
 */
static void sysedit_focusLose( unsigned int wid, const char* wgtname )
{
   (void) wid;
   (void) wgtname;
   sysedit_drag = sysedit_dragSel = 0;
}

/**
 * @brief System editor custom widget mouse handling.
 */
static int sysedit_mouse( unsigned int wid, SDL_Event* event, double mx, double my,
      double w, double h, double xr, double yr, void *data )
{
   (void) data;
   int i, j;
   double x,y, t;
   SDL_Keymod mod;
   StarSystem *sys;
   Planet *p;
   JumpPoint *jp;
   Select_t sel;

   /* Comfort. */
   sys = sysedit_sys;

   /* Handle modifiers. */
   mod = SDL_GetModState();

   switch (event->type) {

      case SDL_MOUSEWHEEL:
         /* Must be in bounds. */
         if ((mx < 0.) || (mx > w) || (my < 0.) || (my > h))
            return 0;

         if (event->wheel.y > 0)
            sysedit_buttonZoom( 0, "btnZoomIn" );
         else if (event->wheel.y < 0)
            sysedit_buttonZoom( 0, "btnZoomOut" );

         return 1;

      case SDL_MOUSEBUTTONDOWN:
         /* Must be in bounds. */
         if ((mx < 0.) || (mx > w) || (my < 0.) || (my > h))
            return 0;
	 window_setFocus( wid, "cstSysEdit" );

         /* selecting star system */
         mx -= w/2 - sysedit_xpos;
         my -= h/2 - sysedit_ypos;

         /* Check planets. */
         for (i=0; i<array_size(sys->planets); i++) {
            p = sys->planets[i];

            /* Position. */
            x = p->pos.x * sysedit_zoom;
            y = p->pos.y * sysedit_zoom;

            /* Set selection. */
            sel.type       = SELECT_PLANET;
            sel.u.planet   = i;

            /* Threshold. */
            t  = p->gfx_space->sw * p->gfx_space->sh / 4.; /* Radius^2 */
            t *= pow2(2.*sysedit_zoom);

            /* Can select. */
            if ((pow2(mx-x)+pow2(my-y)) < t) {

               /* Check if already selected. */
               for (j=0; j<sysedit_nselect; j++) {
                  if (sysedit_selectCmp( &sel, &sysedit_select[j] )) {
                     sysedit_dragSel   = 1;
                     sysedit_tsel      = sel;

                     /* Check modifier. */
                     if (mod & (KMOD_LCTRL | KMOD_RCTRL))
                        sysedit_tadd      = 0;
                     else {
                        /* Detect double click to open planet editor. */
                        if ((SDL_GetTicks() - sysedit_dragTime < SYSEDIT_DRAG_THRESHOLD*2)
                              && (sysedit_moved < SYSEDIT_MOVE_THRESHOLD)) {
                           sysedit_editPnt();
                           sysedit_dragSel = 0;
                           return 1;
                        }
                        sysedit_tadd      = -1;
                     }
                     sysedit_dragTime  = SDL_GetTicks();
                     sysedit_moved     = 0;
                     return 1;
                  }
               }

               /* Add the system if not selected. */
               if (mod & (KMOD_LCTRL | KMOD_RCTRL))
                  sysedit_selectAdd( &sel );
               else {
                  sysedit_deselect();
                  sysedit_selectAdd( &sel );
               }
               sysedit_tsel.type = SELECT_NONE;

               /* Start dragging anyway. */
               sysedit_dragSel   = 1;
               sysedit_dragTime  = SDL_GetTicks();
               sysedit_moved     = 0;
               return 1;
            }
         }

         /* Check jump points. */
         for (i=0; i<array_size(sys->jumps); i++) {
            jp = &sys->jumps[i];

            /* Position. */
            x = jp->pos.x * sysedit_zoom;
            y = jp->pos.y * sysedit_zoom;

            /* Set selection. */
            sel.type       = SELECT_JUMPPOINT;
            sel.u.planet   = i;

            /* Threshold. */
            t  = jumppoint_gfx->sw * jumppoint_gfx->sh / 4.; /* Radius^2 */
            t *= pow2(2.*sysedit_zoom);

            /* Can select. */
            if ((pow2(mx-x)+pow2(my-y)) < t) {

               /* Check if already selected. */
               for (j=0; j<sysedit_nselect; j++) {
                  if (sysedit_selectCmp( &sel, &sysedit_select[j] )) {
                     sysedit_dragSel   = 1;
                     sysedit_tsel      = sel;

                     /* Check modifier. */
                     if (mod & (KMOD_LCTRL | KMOD_RCTRL))
                        sysedit_tadd      = 0;
                     else {
                        /* Detect double click to open planet editor. */
                        if ((SDL_GetTicks() - sysedit_dragTime < SYSEDIT_DRAG_THRESHOLD*2)
                              && (sysedit_moved < SYSEDIT_MOVE_THRESHOLD)) {
                           sysedit_editJump();
                           sysedit_dragSel = 0;
                           return 1;
                        }
                        sysedit_tadd      = -1;
                     }
                     sysedit_dragTime  = SDL_GetTicks();
                     sysedit_moved     = 0;
                     return 1;
                  }
               }

               /* Add the system if not selected. */
               if (mod & (KMOD_LCTRL | KMOD_RCTRL))
                  sysedit_selectAdd( &sel );
               else {
                  sysedit_deselect();
                  sysedit_selectAdd( &sel );
               }
               sysedit_tsel.type = SELECT_NONE;

               /* Start dragging anyway. */
               sysedit_dragSel   = 1;
               sysedit_dragTime  = SDL_GetTicks();
               sysedit_moved     = 0;
               return 1;
            }
         }

         /* Start dragging. */
         if (!(mod & (KMOD_LCTRL | KMOD_RCTRL))) {
            sysedit_drag      = 1;
            sysedit_dragTime  = SDL_GetTicks();
            sysedit_moved     = 0;
            sysedit_tsel.type = SELECT_NONE;
         }
         return 1;

      case SDL_MOUSEBUTTONUP:
         if (sysedit_drag) {
            if ((SDL_GetTicks() - sysedit_dragTime < SYSEDIT_DRAG_THRESHOLD) && (sysedit_moved < SYSEDIT_MOVE_THRESHOLD)) {
               if (sysedit_tsel.type == SELECT_NONE)
                  sysedit_deselect();
               else
                  sysedit_selectAdd( &sysedit_tsel );
            }
            sysedit_drag      = 0;

            if (conf.devautosave)
               for (i=0; i<sysedit_nselect; i++)
                  dpl_savePlanet(sysedit_sys->planets[ sysedit_select[i].u.planet ]);
         }
         if (sysedit_dragSel) {
            if ((SDL_GetTicks() - sysedit_dragTime < SYSEDIT_DRAG_THRESHOLD) &&
                  (sysedit_moved < SYSEDIT_MOVE_THRESHOLD) && (sysedit_tsel.type != SELECT_NONE)) {
               if (sysedit_tadd == 0)
                  sysedit_selectRm( &sysedit_tsel );
               else {
                  sysedit_deselect();
                  sysedit_selectAdd( &sysedit_tsel );
               }
            }
            sysedit_dragSel   = 0;

            /* Save all planets in our selection - their positions might have changed. */
            if (conf.devautosave)
               for (i=0; i<sysedit_nselect; i++)
                  if (sysedit_select[i].type == SELECT_PLANET)
                     dpl_savePlanet( sys->planets[ sysedit_select[i].u.planet ] );
         }
         break;

      case SDL_MOUSEMOTION:
         /* Update mouse positions. */
         sysedit_mx  = mx;
         sysedit_my  = my;

         /* Handle dragging. */
         if (sysedit_drag) {
            /* axis is inverted */
            sysedit_xpos -= xr;
            sysedit_ypos += yr;

            /* Update mouse movement. */
            sysedit_moved += ABS(xr) + ABS(yr);
         }
         /* Dragging selection around. */
         else if (sysedit_dragSel && (sysedit_nselect > 0)) {
            if ((sysedit_moved > SYSEDIT_MOVE_THRESHOLD) || (SDL_GetTicks() - sysedit_dragTime > SYSEDIT_DRAG_THRESHOLD)) {
               for (i=0; i<sysedit_nselect; i++) {

                  /* Planets. */
                  if (sysedit_select[i].type == SELECT_PLANET) {
                     p = sys->planets[ sysedit_select[i].u.planet ];
                     p->pos.x += xr / sysedit_zoom;
                     p->pos.y -= yr / sysedit_zoom;
                  }
                  /* Jump point. */
                  else if (sysedit_select[i].type == SELECT_JUMPPOINT) {
                     jp         = &sys->jumps[ sysedit_select[i].u.jump ];
                     jp->flags &= ~(JP_AUTOPOS);
                     jp->pos.x += xr / sysedit_zoom;
                     jp->pos.y -= yr / sysedit_zoom;
                  }
               }
            }

            /* Update mouse movement. */
            sysedit_moved += ABS(xr) + ABS(yr);
         }
         break;
   }

   return 0;
}

/**
 * @brief Handles the button zoom clicks.
 *
 *    @param wid Unused.
 *    @param str Name of the button creating the event.
 */
static void sysedit_buttonZoom( unsigned int wid, const char* str )
{
   (void) wid;

   /* Transform coords to normal. */
   sysedit_xpos /= sysedit_zoom;
   sysedit_ypos /= sysedit_zoom;

   /* Apply zoom. */
   if (strcmp(str,"btnZoomIn")==0) {
      sysedit_zoom *= SYSEDIT_ZOOM_STEP;
      sysedit_zoom = MIN( pow(SYSEDIT_ZOOM_STEP, SYSEDIT_ZOOM_MAX), sysedit_zoom );
   }
   else if (strcmp(str,"btnZoomOut")==0) {
      sysedit_zoom /= SYSEDIT_ZOOM_STEP;
      sysedit_zoom = MAX( pow(SYSEDIT_ZOOM_STEP, SYSEDIT_ZOOM_MIN), sysedit_zoom );
   }

   /* Transform coords back. */
   sysedit_xpos *= sysedit_zoom;
   sysedit_ypos *= sysedit_zoom;
}

/**
 * @brief Deselects everything.
 */
static void sysedit_deselect (void)
{
   if (sysedit_nselect > 0)
      free( sysedit_select );
   sysedit_select    = NULL;
   sysedit_nselect   = 0;
   sysedit_mselect   = 0;

   /* Button check. */
   sysedit_checkButtons();
}

/**
 * @brief Checks to see which buttons should be active and the likes.
 */
static void sysedit_checkButtons (void)
{
   int sel_planet, sel_jump;

   /* See if a planet or jump is selected. */
   sel_planet  = 0;
   sel_jump    = 0;
   for (int i=0; i<sysedit_nselect; i++) {
      Select_t *sel = &sysedit_select[i];
      if (sel->type == SELECT_PLANET)
         sel_planet++;
      else if (sel->type == SELECT_JUMPPOINT)
         sel_jump++;
   }

   /* Planet dependent. */
   if (sel_planet) {
      window_enableButton( sysedit_wid, "btnRemove" );
      window_enableButton( sysedit_wid, "btnRename" );
   }
   else {
      window_disableButton( sysedit_wid, "btnRemove" );
      window_disableButton( sysedit_wid, "btnRename" );
   }

   /* Jump dependent. */
   if (sel_jump)
      window_enableButton( sysedit_wid, "btnReset" );
   else
      window_disableButton( sysedit_wid, "btnReset" );

   /* Editor - just one planet. */
   if (((sel_planet==1) && (sel_jump==0)) || ((sel_planet==0) && (sel_jump==1)))
      window_enableButton( sysedit_wid, "btnEdit" );
   else
      window_disableButton( sysedit_wid, "btnEdit" );
}

/**
 * @brief Adds a system to the selection.
 */
static void sysedit_selectAdd( Select_t *sel )
{
   /* Allocate if needed. */
   if (sysedit_mselect < sysedit_nselect+1) {
      if (sysedit_mselect == 0)
         sysedit_mselect = 1;
      sysedit_mselect  *= 2;
      sysedit_select    = realloc( sysedit_select,
            sizeof(Select_t) * sysedit_mselect );
   }

   /* Add system. */
   sysedit_select[ sysedit_nselect ] = *sel;
   sysedit_nselect++;

   /* Button check. */
   sysedit_checkButtons();
}

/**
 * @brief Removes a system from the selection.
 */
static void sysedit_selectRm( Select_t *sel )
{
   for (int i=0; i<sysedit_nselect; i++) {
      if (sysedit_selectCmp( &sysedit_select[i], sel )) {
         sysedit_nselect--;
         memmove( &sysedit_select[i], &sysedit_select[i+1],
               sizeof(Select_t) * (sysedit_nselect - i) );
         /* Button check. */
         sysedit_checkButtons();
         return;
      }
   }
   WARN(_("Trying to deselect item that is not in selection!"));
}

/**
 * @brief Compares two selections to see if they are the same.
 *
 *    @return 1 if both selections are the same.
 */
static int sysedit_selectCmp( Select_t *a, Select_t *b )
{
   return (memcmp(a, b, sizeof(Select_t)) == 0);
}

/**
 * @brief Edits a planet.
 */
static void sysedit_editPnt( void )
{
   unsigned int wid;
   int x, y, w, l, bw;
   char buf[STRMAX_SHORT], title[128];
   const char *s;
   Planet *p;

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];

   /* Create the window. */
   snprintf(title, sizeof(title), _("Planet Property Editor - %s"), p->name);
   wid = window_create( "wdwSysEditPnt", title, -1, -1, SYSEDIT_EDIT_WIDTH, SYSEDIT_EDIT_HEIGHT );
   sysedit_widEdit = wid;

   window_setCancel( wid, sysedit_editPntClose );

   bw = (SYSEDIT_EDIT_WIDTH - 40 - 15 * 3) / 4.;

   /* Rename button. */
   y = -40;
   snprintf( buf, sizeof(buf), "%s ", _("Name:") );
   w = gl_printWidthRaw( NULL, buf );
   window_addText( wid, 20, y, 180, 15, 0, "txtNameLabel", &gl_smallFont, NULL, buf );
   snprintf( buf, sizeof(buf), "%s", p->name );
   window_addText( wid, 20 + w, y, 180, 15, 0, "txtName", &gl_smallFont, NULL, buf );
   window_addButton( wid, -20, y - gl_defFont.h/2. + BUTTON_HEIGHT/2., bw, BUTTON_HEIGHT, "btnRename",
         _("Rename"), sysedit_btnRename );
   window_addButton( wid, -20 - 15 - bw, y - gl_defFont.h/2. + BUTTON_HEIGHT/2., bw, BUTTON_HEIGHT, "btnFaction",
         _("Faction"), sysedit_btnFaction );

   y -= gl_defFont.h + 5;

   snprintf( buf, sizeof(buf), "%s ", _("Faction:") );
   w = gl_printWidthRaw( NULL, buf );
   window_addText( wid, 20, y, 180, 15, 0, "txtFactionLabel", &gl_smallFont, NULL, buf );
   snprintf( buf, sizeof(buf), "%s", p->presence.faction > 0 ? faction_name( p->presence.faction ) : _("None") );
   window_addText( wid, 20 + w, y, 180, 15, 0, "txtFaction", &gl_smallFont, NULL, buf );
   y -= gl_defFont.h + 5;

   /* Input widgets and labels. */
   x = 20;
   s = _("Population");
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtPop",
         NULL, NULL, s );
   window_addInput( wid, x += l + 5, y, 80, 20, "inpPop", 12, 1, NULL );
   window_setInputFilter( wid, "inpPop", INPUT_FILTER_NUMBER );
   x += 80 + 10;

   s = _("Class");
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtClass",
         NULL, NULL, s );
   window_addInput( wid, x += l + 5, y, 30, 20, "inpClass", 1, 1, NULL );
   x += 30 + 10;

   s = _("Land");
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtLand",
         NULL, NULL, s );
   window_addInput( wid, x += l + 5, y, 150, 20, "inpLand", 20, 1, NULL );
   y -= gl_defFont.h + 15;

   /* Second row. */
   x = 20;
   s = _("Base Presence");
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtPresenceBase",
         NULL, NULL, s );
   window_addInput( wid, x += l + 5, y, 50, 20, "inpPresenceBase", 5, 1, NULL );
   window_setInputFilter( wid, "inpPresenceBase", INPUT_FILTER_NUMBER );
   x += 50 + 10;

   s = _("Bonus Presence");
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtPresenceBonus",
         NULL, NULL, s );
   window_addInput( wid, x += l + 5, y, 50, 20, "inpPresenceBonus", 5, 1, NULL );
   window_setInputFilter( wid, "inpPresenceBonus", INPUT_FILTER_NUMBER );
   x += 50 + 10;

   s = _("Range");
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtPresenceRange",
         NULL, NULL, s );
   window_addInput( wid, x += l + 5, y, 30, 20, "inpPresenceRange", 1, 1, NULL );
   window_setInputFilter( wid, "inpPresenceRange", INPUT_FILTER_NUMBER );
   //x += 30 + 10;

   x = 250;
   y -= gl_defFont.h + 15;
   s = _("hide");
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtHide",
         NULL, NULL, s );
   window_addInput( wid, x += l + 5, y, 50, 20, "inpHide", 4, 1, NULL );
   window_setInputFilter( wid, "inpHide", INPUT_FILTER_NUMBER );
   x += 50 + 10;

   /* Tags. */
   x = 250;
   y -= gl_defFont.h + 20;
   l = scnprintf( buf, sizeof(buf), "#nTags:#0 " );
   for (int i=0; i<array_size(p->tags); i++)
      l += scnprintf( &buf[l], sizeof(buf)-l, "%s%s", ((i>0) ? ", " : ""), p->tags[i] );
   window_addText( wid, x, y, 300, 20, 0, "txtTags", NULL, NULL, buf );

   /* Bottom buttons. */
   window_addButton( wid, -20 - bw*3 - 15*3, 35 + BUTTON_HEIGHT, bw, BUTTON_HEIGHT,
         "btnRmService", _("Rm Service"), sysedit_btnRmService );
   window_addButton( wid, -20 - bw*2 - 15*2, 35 + BUTTON_HEIGHT, bw, BUTTON_HEIGHT,
         "btnAddService", _("Add Service"), sysedit_btnAddService );
   window_addButton( wid, -20 - bw - 15, 35 + BUTTON_HEIGHT, bw, BUTTON_HEIGHT,
         "btnEditTech", _("Edit Tech"), sysedit_btnTechEdit );
   window_addButton( wid, -20 - bw*3 - 15*3, 20, bw, BUTTON_HEIGHT,
         "btnDesc", _("Description"), sysedit_planetDesc );
   window_addButton( wid, -20 - bw*2 - 15*2, 20, bw, BUTTON_HEIGHT,
         "btnLandGFX", _("Land GFX"), sysedit_planetGFX );
   window_addButton( wid, -20 - bw - 15, 20, bw, BUTTON_HEIGHT,
         "btnSpaceGFX", _("Space GFX"), sysedit_planetGFX );
   window_addButton( wid, -20, 20, bw, BUTTON_HEIGHT,
         "btnClose", _("Close"), sysedit_editPntClose );

   /* Load current values. */
   snprintf( buf, sizeof(buf), "%"PRIu64, p->population );
   window_setInput( wid, "inpPop", buf );
   snprintf( buf, sizeof(buf), "%s", p->class );
   window_setInput( wid, "inpClass", buf );
   window_setInput( wid, "inpLand", p->land_func );
   snprintf( buf, sizeof(buf), "%g", p->presence.base );
   window_setInput( wid, "inpPresenceBase", buf );
   snprintf( buf, sizeof(buf), "%g", p->presence.bonus );
   window_setInput( wid, "inpPresenceBonus", buf );
   snprintf( buf, sizeof(buf), "%d", p->presence.range );
   window_setInput( wid, "inpPresenceRange", buf );
   snprintf( buf, sizeof(buf), "%g", p->hide );
   window_setInput( wid, "inpHide", buf );

   /* Generate the list. */
   sysedit_genServicesList( wid );
}

/**
 * @brief Updates the jump point checkboxes.
 */
static void jp_type_check_hidden_update( unsigned int wid, const char* str )
{
   (void) str;
   if (jp_hidden == 0) {
      jp_hidden = 1;
      jp_exit   = 0;
   }
   else
      jp_hidden = 0;
   window_checkboxSet( wid, "chkHidden", jp_hidden );
   window_checkboxSet( wid, "chkExit",   jp_exit );
}

/**
 * @brief Updates the jump point checkboxes.
 */
static void jp_type_check_exit_update( unsigned int wid, const char* str )
{
   (void) str;
   if (jp_exit == 0) {
      jp_exit   = 1;
      jp_hidden = 0;
   }
   else
      jp_exit = 0;
   window_checkboxSet( wid, "chkHidden", jp_hidden );
   window_checkboxSet( wid, "chkExit",   jp_exit );
}

/**
 * @brief Edits a jump.
 */
static void sysedit_editJump( void )
{
   unsigned int wid;
   int x, y, w, l, bw;
   char buf[STRMAX_SHORT];
   const char *s;
   JumpPoint *j = &sysedit_sys->jumps[ sysedit_select[0].u.jump ];

   /* Create the window. */
   wid = window_create( "wdwJumpPointEditor", _("Jump Point Editor"), -1, -1, SYSEDIT_EDIT_WIDTH, SYSEDIT_EDIT_HEIGHT );
   sysedit_widEdit = wid;

   bw = (SYSEDIT_EDIT_WIDTH - 40 - 15 * 3) / 4.;

   /* Target lable. */
   y = -40;
   snprintf( buf, sizeof(buf), _("Target: ") );
   w = gl_printWidthRaw( NULL, buf );
   window_addText( wid, 20, y, 180, 15, 0, "txtTargetLabel", &gl_smallFont, NULL, buf );
   snprintf( buf, sizeof(buf), "%s", j->target->name );
   window_addText( wid, 20 + w, y, 180, 15, 0, "txtName", &gl_smallFont, NULL, buf );

   y -= gl_defFont.h + 10;

   /* Input widgets and labels. */
   x = 20;

   /* Initial checkbox state */
   jp_hidden = 0;
   jp_exit   = 0;
   if (jp_isFlag( j, JP_HIDDEN ))
      jp_hidden = 1;
   else if (jp_isFlag( j, JP_EXITONLY ))
      jp_exit   = 1;
   /* Create check boxes. */
   window_addCheckbox( wid, x, y, 100, 20,
         "chkHidden", _("Hidden"), jp_type_check_hidden_update, jp_hidden );
   y -= 20;
   window_addCheckbox( wid, x, y, 100, 20,
         "chkExit", _("Exit only"), jp_type_check_exit_update, jp_exit );
   y -= 30;

   s = _("Hide"); /* TODO: if inpType == 0 disable hide box */
   l = gl_printWidthRaw( NULL, s );
   window_addText( wid, x, y, l, 20, 1, "txtHide",
         NULL, NULL, s );
   window_addInput( wid, x + l + 8, y, 50, 20, "inpHide", 4, 1, NULL );
   window_setInputFilter( wid, "inpHide", INPUT_FILTER_NUMBER );
   x += 50 + 10;

   (void)x;

   /* Bottom buttons. */
   window_addButton( wid, -20, 20, bw, BUTTON_HEIGHT,
         "btnClose", _("Close"), sysedit_editJumpClose );

   /* Load current values. */
   snprintf( buf, sizeof(buf), "%g", j->hide );
   window_setInput( wid, "inpHide", buf );
}

/**
 * @brief Closes the jump editor, saving the changes made.
 */
static void sysedit_editJumpClose( unsigned int wid, const char *unused )
{
   (void) unused;
   JumpPoint *j = &sysedit_sys->jumps[ sysedit_select[0].u.jump ];
   if (jp_hidden == 1) {
      jp_setFlag( j, JP_HIDDEN );
      jp_rmFlag(  j, JP_EXITONLY );
   }
   else if (jp_exit == 1) {
      jp_setFlag( j, JP_EXITONLY );
      jp_rmFlag(  j, JP_HIDDEN );
   }
   else {
      jp_rmFlag( j, JP_HIDDEN );
      jp_rmFlag( j, JP_EXITONLY );
   }
   j->hide  = atof(window_getInput( sysedit_widEdit, "inpHide" ));

   window_close( wid, unused );
}

/**
 * @brief Displays the planet landing description and bar description in a separate window.
 */
static void sysedit_planetDesc( unsigned int wid, const char *unused )
{
   (void) unused;
   int x, y, h, w, bw;
   Planet *p;
   const char *desc, *bardesc;
   char title[128];

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];

   /* Create the window. */
   snprintf(title, sizeof(title), _("Planet Information - %s"), p->name);
   wid = window_create( "wdwPlanetDesc", title, -1, -1, SYSEDIT_EDIT_WIDTH, SYSEDIT_EDIT_HEIGHT );
   window_setCancel( wid, window_close );

   x = 20;
   y = -40;
   w = SYSEDIT_EDIT_WIDTH - 40;
   h = (SYSEDIT_EDIT_HEIGHT - gl_defFont.h * 2 - 30 - 60 - BUTTON_HEIGHT - 10) / 2.;
   desc    = p->description ? p->description : _("None");
   bardesc = p->bar_description ? p->bar_description : _("None");
   bw = (SYSEDIT_EDIT_WIDTH - 40 - 15 * 3) / 4.;

   window_addButton( wid, -20 - bw*3 - 15*3, 20, bw, BUTTON_HEIGHT,
         "btnProperties", _("Properties"), sysedit_planetDescReturn );

   window_addButton( wid, -20, 20, bw, BUTTON_HEIGHT,
         "btnClose", _("Close"), sysedit_planetDescClose );

   /* Description label and text. */
   window_addText( wid, x, y, w, gl_defFont.h, 0, "txtDescriptionLabel", &gl_defFont, NULL,
         _("Landing Description") );
   y -= gl_defFont.h + 10;
   window_addInput( wid, x, y, w, h, "txtDescription", 1024, 0,
         NULL );
   window_setInputFilter( wid, "txtDescription",
         "[]{}~<>@#$^|_" );
   y -= h + 10;
   /* Load current values. */
   window_setInput( wid, "txtDescription", desc );

   /* Bar description label and text. */
   window_addText( wid, x, y, w, gl_defFont.h, 0, "txtBarDescriptionLabel", &gl_defFont, NULL,
         _("Bar Description") );
   y -= gl_defFont.h + 10;
   window_addInput( wid, x, y, w, h, "txtBarDescription", 1024, 0,
         NULL );
   window_setInputFilter( wid, "txtBarDescription",
         "[]{}~<>@#$^|_" );
   /* Load current values. */
   window_setInput( wid, "txtBarDescription", bardesc );
}

/**
 * @brief Closes the planet description window and returns to the properties window.
 */
static void sysedit_planetDescReturn( unsigned int wid, const char *unused )
{
   Planet *p;
   const char *mydesc, *mybardesc;

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];

   mydesc    = window_getInput( wid, "txtDescription" );
   mybardesc = window_getInput( wid, "txtBarDescription" );

   free(p->description);
   free(p->bar_description);
   p->description     = NULL;
   p->bar_description = NULL;

   if (mydesc != NULL)
      p->description     = strdup( mydesc );
   if (mybardesc != NULL)
      p->bar_description = strdup( mybardesc );

   window_close( wid, unused );
}

/**
 * @brief Closes both the planet description window and the properties window.
 */
static void sysedit_planetDescClose( unsigned int wid, const char *unused )
{
   sysedit_planetDescReturn( wid, unused );
   sysedit_editPntClose( sysedit_widEdit, unused );
}

/**
 * @brief Generates the planet services list.
 */
static void sysedit_genServicesList( unsigned int wid )
{
   int j, n, nservices;
   Planet *p;
   char **have, **lack;
   int x, y, w, h, hpos, lpos;

   hpos = lpos = -1;

   /* Destroy if exists. */
   if (widget_exists( wid, "lstServicesHave" ) &&
         widget_exists( wid, "lstServicesLacked" )) {
      hpos = toolkit_getListPos( wid, "lstServicesHave" );
      lpos = toolkit_getListPos( wid, "lstServicesLacked" );
      window_destroyWidget( wid, "lstServicesHave" );
      window_destroyWidget( wid, "lstServicesLacked" );
   }

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];
   x = 20;
   y = 20 + BUTTON_HEIGHT * 2 + 30;
   w = (SYSEDIT_EDIT_WIDTH - 40 - 15 * 3) / 4.;
   h = SYSEDIT_EDIT_HEIGHT - y - 130;

   /* Get all missing services. */
   n = nservices = 0;
   for (int i=1; i<PLANET_SERVICES_MAX; i<<=1) {
      if (!planet_hasService(p, i) && (i != PLANET_SERVICE_INHABITED))
         n++;
      nservices++; /* Cheaply track all service types. */
   }

   /* Get all the services the planet has. */
   j = 0;
   have = malloc( sizeof(char*) * MAX(nservices - n, 1) );
   if (nservices == n)
      have[j++] = strdup(_("None"));
   else
      for (int i=1; i<PLANET_SERVICES_MAX; i<<=1)
         if (planet_hasService(p, i)  && (i != PLANET_SERVICE_INHABITED))
            have[j++] = strdup( planet_getServiceName( i ) );

   /* Add list. */
   window_addList( wid, x, y, w, h, "lstServicesHave", have, j, 0, NULL, sysedit_btnRmService );
   x += w + 15;

   /* Add list of services the planet lacks. */
   j = 0;
   lack = malloc( sizeof(char*) * MAX(1, n) );
   if (!n)
      lack[j++] = strdup( _("None") );
   else
      for (int i=1; i<PLANET_SERVICES_MAX; i<<=1)
         if (!planet_hasService(p, i) && (i != PLANET_SERVICE_INHABITED))
            lack[j++] = strdup( planet_getServiceName(i) );

   /* Add list. */
   window_addList( wid, x, y, w, h, "lstServicesLacked", lack, j, 0, NULL, sysedit_btnAddService );

   /* Restore positions. */
   if (hpos != -1 && lpos != -1) {
      toolkit_setListPos( wid, "lstServicesHave", hpos );
      toolkit_setListPos( wid, "lstServicesLacked", lpos );
   }
}

/**
 * @brief Adds a service to a planet.
 */
static void sysedit_btnAddService( unsigned int wid, const char *unused )
{
   (void) unused;
   const char *selected;
   Planet *p;

   selected = toolkit_getList( wid, "lstServicesLacked" );
   if ((selected == NULL) || (strcmp(selected,_("None"))==0))
      return;

   /* Enable the service. All services imply landability. */
   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];
   p->services |= planet_getService(selected) | PLANET_SERVICE_INHABITED | PLANET_SERVICE_LAND;

   /* Regenerate the list. */
   sysedit_genServicesList( wid );
}

/**
 * @brief Removes a service from a planet.
 */
static void sysedit_btnRmService( unsigned int wid, const char *unused )
{
   (void) unused;
   const char *selected;
   Planet *p;

   selected = toolkit_getList( wid, "lstServicesHave" );
   if ((selected==NULL) || (strcmp(selected,_("None"))==0))
      return;

   /* Flip the bit. Safe enough, as it's always 1 to start with. */
   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];
   p->services ^= planet_getService(selected);

   /* If landability was removed, the rest must go, too. */
   if (strcmp(selected,"Land")==0)
      p->services = 0;

   sysedit_genServicesList( wid );
}

/**
 * @brief Edits a planet's tech.
 */
static void sysedit_btnTechEdit( unsigned int wid, const char *unused )
{
   (void) unused;
   int y, w, bw;

   /* Create the window. */
   wid = window_create( "wdwPlanetTechEditor", _("Planet Tech Editor"), -1, -1, SYSEDIT_EDIT_WIDTH, SYSEDIT_EDIT_HEIGHT );
   window_setCancel( wid, window_close );

   w = (SYSEDIT_EDIT_WIDTH - 40 - 15) / 2.;
   bw = (SYSEDIT_EDIT_WIDTH - 40 - 15 * 3) / 4.;

   /* Close button. */
   window_addButton( wid, -20, 20, bw, BUTTON_HEIGHT,
         "btnClose", _("Close"), window_close );
   y = 20 + BUTTON_HEIGHT + 15;

   /* Remove button. */
   window_addButton( wid, -20-(w+15), y, w, BUTTON_HEIGHT,
         "btnRm", _("Rm Tech"), sysedit_btnRmTech );

   /* Add button. */
   window_addButton( wid, -20, y, w, BUTTON_HEIGHT,
         "btnAdd", _("Add Tech"), sysedit_btnAddTech );

   sysedit_genTechList( wid );
}

/**
 * @brief Generates the planet tech list.
 */
static void sysedit_genTechList( unsigned int wid )
{
   Planet *p;
   char **have, **lack, **tmp;
   int i, j, n, x, y, w, h, hpos, lpos;

   hpos = lpos = -1;

   /* Destroy if exists. */
   if (widget_exists( wid, "lstTechsHave" ) &&
         widget_exists( wid, "lstTechsLacked" )) {
      hpos = toolkit_getListPos( wid, "lstTechsHave" );
      lpos = toolkit_getListPos( wid, "lstTechsLacked" );
      window_destroyWidget( wid, "lstTechsHave" );
      window_destroyWidget( wid, "lstTechsLacked" );
   }

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];
   w = (SYSEDIT_EDIT_WIDTH - 40 - 15) / 2.;
   x = -20 - w - 15;
   y = 20 + BUTTON_HEIGHT * 2 + 30;
   h = SYSEDIT_EDIT_HEIGHT - y - 30;

   /* Get all the techs the planet has. */
   n = 0;
   if (p->tech != NULL)
      have = tech_getItemNames( p->tech, &n );
   else {
      have = malloc( sizeof(char*) );
      have[n++] = strdup(_("None"));
   }

   /* Add list. */
   window_addList( wid, x, y, w, h, "lstTechsHave", have, n, 0, NULL, sysedit_btnRmTech );
   x += w + 15;

   /* Omit the techs that the planet already has from the list.  */
   n = 0;
   if (p->tech != NULL) {
      tmp = tech_getAllItemNames( &j );
      for (i=0; i<j; i++)
         if (!tech_hasItem( p->tech, tmp[i] ))
            n++;

      if (!n) {
         lack = malloc( sizeof(char*) );
         lack[n++] = strdup(_("None"));
      }
      else {
         lack = malloc( sizeof(char*) * j );
         n = 0;
         for (i=0; i<j; i++)
            if (!tech_hasItem( p->tech, tmp[i] ))
               lack[n++] = strdup( tmp[i] );
      }

      /* Clean up. */
      for (i=0; i<j; i++)
         free(tmp[i]);

      free(tmp);
   }
   else
      lack = tech_getAllItemNames( &n );

   /* Add list. */
   window_addList( wid, x, y, w, h, "lstTechsLacked", lack, n, 0, NULL, sysedit_btnAddTech );

   /* Restore positions. */
   if (hpos != -1 && lpos != -1) {
      toolkit_setListPos( wid, "lstTechsHave", hpos );
      toolkit_setListPos( wid, "lstTechsLacked", lpos );
   }
}

/**
 * @brief Adds a tech to a planet.
 */
static void sysedit_btnAddTech( unsigned int wid, const char *unused )
{
   (void) unused;
   const char *selected;
   Planet *p;

   selected = toolkit_getList( wid, "lstTechsLacked" );
   if ((selected == NULL) || (strcmp(selected,_("None"))==0))
      return;

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];
   if (p->tech == NULL)
      p->tech = tech_groupCreate();
   tech_addItemTech( p->tech, selected );

   /* Regenerate the list. */
   sysedit_genTechList( wid );
}

/**
 * @brief Removes a tech from a planet.
 */
static void sysedit_btnRmTech( unsigned int wid, const char *unused )
{
   (void) unused;
   const char *selected;
   Planet *p;
   int n;

   selected = toolkit_getList( wid, "lstTechsHave" );
   if ((selected == NULL) || (strcmp(selected,_("None"))==0))
      return;

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];
   if (tech_hasItem( p->tech, selected ))
      tech_rmItemTech( p->tech, selected );

   n = tech_getItemCount( p->tech );
   if (!n)
      p->tech = NULL;

   /* Regenerate the list. */
   sysedit_genTechList( wid );
}

/**
 * @brief Edits a planet's faction.
 */
static void sysedit_btnFaction( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;
   unsigned int wid;
   int pos, j, y, h, bw, *factions;
   char **str;
   const char *s;
   Planet *p;

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];

   /* Create the window. */
   wid = window_create( "wdwModifyFaction", _("Modify Faction"), -1, -1, SYSEDIT_EDIT_WIDTH, SYSEDIT_EDIT_HEIGHT );
   window_setCancel( wid, window_close );

   /* Generate factions list. */
   factions = faction_getAll();
   str = malloc( sizeof(char*) * (array_size(factions) + 1) );
   str[0] = strdup(_("None"));
   j      = 1;
   for (int i=0; i<array_size(factions); i++)
      str[j++] = strdup( faction_name( factions[i] ) ); /* Not translating so we can use faction_get */
   qsort( &str[1], j-1, sizeof(char*), strsort );

   /* Get current faction. */
   if (p->presence.faction >= 0) {
      pos    = 0;
      s      = faction_name(p->presence.faction);
      for (int i=0; i<j; i++)
         if (strcmp(s,str[i])==0)
            pos = i;
   }
   else
      pos = -1;

   bw = (SYSEDIT_EDIT_WIDTH - 40 - 15 * 3) / 4.;
   y = 20 + BUTTON_HEIGHT + 15;
   h = SYSEDIT_EDIT_HEIGHT - y - 30;
   window_addList( wid, 20, -40, SYSEDIT_EDIT_WIDTH-40, h,
         "lstFactions", str, j, pos, NULL, sysedit_btnFactionSet );

   /* Close button. */
   window_addButton( wid, -20, 20, bw, BUTTON_HEIGHT,
         "btnClose", _("Close"), window_close );

   /* Add button. */
   window_addButton( wid, -20-(bw+15), 20, bw, BUTTON_HEIGHT,
         "btnAdd", _("Set"), sysedit_btnFactionSet );

   array_free( factions );
}

/**
 * @brief Actually modifies the faction.
 */
static void sysedit_btnFactionSet( unsigned int wid, const char *unused )
{
   (void) unused;
   const char *selected;
   Planet *p;

   selected = toolkit_getList( wid, "lstFactions" );
   if (selected == NULL)
      return;
   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];

   /* "None" case. */
   if (toolkit_getListPos( wid, "lstFactions")==0) {
      p->presence.faction = -1;
   }
   else {
      /* Set the faction. */
      p->presence.faction = faction_get( selected );
   }

   /* Update the editor window. */
   window_modifyText( sysedit_widEdit, "txtFaction", p->presence.faction > 0 ? faction_name( p->presence.faction ) : _("None") );

   window_close( wid, unused );
}

/**
 * @brief Opens the system property editor.
 */
static void sysedit_btnEdit( unsigned int wid_unused, const char *unused )
{
   (void) wid_unused;
   (void) unused;
   Select_t *sel = &sysedit_select[0];
   if (sel->type==SELECT_PLANET)
      sysedit_editPnt();
   else if (sel->type==SELECT_JUMPPOINT)
      sysedit_editJump();
}

/**
 * @brief Opens the planet landing or space graphic editor.
 */
static void sysedit_planetGFX( unsigned int wid_unused, const char *wgt )
{
   (void) wid_unused;
   unsigned int wid;
   size_t nfiles, j;
   char *path, buf[STRMAX_SHORT];
   char **files;
   glTexture *t;
   ImageArrayCell *cells;
   int w, h, land;
   Planet *p;
   glColour c;

   land = (strcmp(wgt,"btnLandGFX") == 0);

   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];
   /* Create the window. */
   snprintf( buf, sizeof(buf), _("%s - Planet Properties"), p->name );
   wid = window_create( "wdwPlanetGFX", buf, -1, -1, -1, -1 );
   window_dimWindow( wid, &w, &h );

   window_setCancel( wid, sysedit_btnGFXClose );
   window_setAccept( wid, sysedit_btnGFXApply );

   /* Close button. */
   window_addButton( wid, -20, 20, BUTTON_WIDTH, BUTTON_HEIGHT,
         "btnClose", _("Close"), sysedit_btnGFXClose );

   /* Apply button. */
   window_addButton( wid, -20, 20+(20+BUTTON_HEIGHT), BUTTON_WIDTH, BUTTON_HEIGHT,
         land ? "btnApplyLand" : "btnApplySpace", _("Apply"), sysedit_btnGFXApply );

   /* Find images first. */
   path           = land ? PLANET_GFX_EXTERIOR_PATH : PLANET_GFX_SPACE_PATH;
   files          = PHYSFS_enumerateFiles( path );
   for (nfiles=0; files[nfiles]; nfiles++) {}
   cells          = calloc( nfiles, sizeof(ImageArrayCell) );

   j              = 0;
   for (size_t i=0; i<nfiles; i++) {
      PHYSFS_Stat path_stat;
      snprintf( buf, sizeof(buf), "%s/%s", path, files[i] );
      /* Ignore directories. */
      if (!PHYSFS_stat( buf, &path_stat )) {
         WARN(_("Unable to check file type for '%s'!"), buf);
         continue;
      }
      if (path_stat.filetype != PHYSFS_FILETYPE_REGULAR)
         continue;

      t              = gl_newImage( buf, OPENGL_TEX_MIPMAPS );
      if (t == NULL)
         continue;
      cells[j].image   = t;
      cells[j].caption = strdup( files[i] );
      c = strcmp(files[i], land ? p->gfx_exteriorPath : p->gfx_spacePath)==0 ? cOrange : cBlack;
      memcpy( &cells[j].bg, &c, sizeof(glColour) );
      j++;
   }
   PHYSFS_freeList( files );

   /* Add image array. */
   window_addImageArray( wid, 20, 20, w-60-BUTTON_WIDTH, h-60, "iarGFX", 128, 128, cells, j, NULL, NULL, NULL );
   toolkit_setImageArray( wid, "iarGFX", path );
}

/**
 * @brief Closes the planet graphic editor.
 */
static void sysedit_btnGFXClose( unsigned int wid, const char *wgt )
{
   window_close( wid, wgt );
}

/**
 * @brief Apply new graphics.
 */
static void sysedit_btnGFXApply( unsigned int wid, const char *wgt )
{
   Planet *p;
   const char *str;
   char *path, buf[PATH_MAX];
   int land;

   land = (strcmp(wgt,"btnApplyLand") == 0);
   p = sysedit_sys->planets[ sysedit_select[0].u.planet ];

   /* Get output. */
   str = toolkit_getImageArray( wid, "iarGFX" );
   if (str == NULL)
      return;

   /* New path. */
   path = land ? PLANET_GFX_EXTERIOR_PATH : PLANET_GFX_SPACE_PATH;
   snprintf( buf, sizeof(buf), "%s/%s", path, str );

   if (land) {
      free( p->gfx_exteriorPath );
      free( p->gfx_exterior );
      snprintf( buf, sizeof(buf), PLANET_GFX_EXTERIOR_PATH"%s", str );
      p->gfx_exteriorPath = strdup( str );
      p->gfx_exterior = strdup( buf );
   }
   else { /* Free old texture, load new. */
      free( p->gfx_spaceName );
      free( p->gfx_spacePath );
      p->gfx_spaceName = strdup( buf );
      p->gfx_spacePath = strdup( str );
      gl_freeTexture( p->gfx_space );
      p->gfx_space = NULL;
      planet_gfxLoad( p );
   }

   /* For now we close. */
   sysedit_btnGFXClose( wid, wgt );
}
