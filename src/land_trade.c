/*
 * See Licensing and Copyright notice in naev.h
 */
/**
 * @file land_trade.c
 *
 * @brief Handles the Trading Center at land.
 */
/** @cond */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "naev.h"
/** @endcond */

#include "land_trade.h"

#include "array.h"
#include "commodity.h"
#include "dialogue.h"
#include "economy.h"
#include "hook.h"
#include "land_shipyard.h"
#include "log.h"
#include "map_find.h"
#include "ndata.h"
#include "nstring.h"
#include "player.h"
#include "space.h"
#include "tk/toolkit_priv.h"
#include "toolkit.h"

/*
 * Quantity to buy on one click
*/
static int commodity_mod = 10;

/**
 * @brief Opens the local market window.
 */
void commodity_exchange_open( unsigned int wid )
{
   int i, ngoods;
   ImageArrayCell *cgoods;
   int w, h, iw, ih, dw, bw, titleHeight, infoHeight;
   char buf[STRMAX_SHORT];
   size_t l = 0;
   int iconsize;

   /* Mark as generated. */
   land_tabGenerate(LAND_WINDOW_COMMODITY);

   /* Get window dimensions. */
   window_dimWindow( wid, &w, &h );

   /* Calculate image array dimensions. */
   /* Window size minus right column size minus space on left and right */
   iw = 565 + (w - LAND_WIDTH);
   ih = h - 60;
   dw = w - iw - 60;

   /* buttons */
   bw = (dw - 40) / 3;
   window_addButtonKey( wid, 40 + iw, 20, bw, LAND_BUTTON_HEIGHT,
         "btnCommodityBuy", _("Buy"), commodity_buy, SDLK_b );
   window_addButtonKey( wid, 60 + iw + bw, 20, bw, LAND_BUTTON_HEIGHT,
         "btnCommoditySell", _("Sell"), commodity_sell, SDLK_s );
   window_addButtonKey( wid, 80 + iw + 2*bw, 20, bw, LAND_BUTTON_HEIGHT,
         "btnCommodityClose", _("Take Off"), land_buttonTakeoff, SDLK_t );

      /* cust draws the modifier : # of tons one click buys or sells */
   window_addCust( wid, 40 + iw, 40 + LAND_BUTTON_HEIGHT, 2*bw + 20,
         gl_smallFont.h + 6, "cstMod", 0, commodity_renderMod, NULL, NULL, NULL, NULL );

   /* store gfx */
   window_addRect( wid, -20, -40, 192, 192, "rctStore", &cBlack, 0 );
   window_addImage( wid, -20, -40, 192, 192, "imgStore", NULL, 1 );

   /* text */
   titleHeight = gl_printHeightRaw(&gl_defFont, LAND_BUTTON_WIDTH+80, _("None"));
   window_addText( wid, 40 + iw, -40, dw, titleHeight, 0,
         "txtName", &gl_defFont, NULL, _("None") );

   l += scnprintf( &buf[l], sizeof(buf)-l, "#n%s#0", _("You have:") );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n#n%s#0", _("Purchased for:") );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n#n%s#0", _("Market Price:") );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n#n%s#0", _("Free Space:") );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n#n%s#0", _("Money:") );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n#n%s#0", _("Average price here:") );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n#n%s#0", _("Average price all:") );
   infoHeight = gl_printHeightRaw( &gl_smallFont, LAND_BUTTON_WIDTH+80, buf );
   window_addText( wid, 40 + iw, -60 - titleHeight, 200, infoHeight, 0,
         "txtSInfo", &gl_smallFont, NULL, buf );
   window_addText( wid, 40 + iw + 224, -60 - titleHeight,
         dw - (200 + 20+192), infoHeight, 0,
         "txtDInfo", &gl_smallFont, NULL, NULL );

   window_addText( wid, 40 + iw, MIN(-80-titleHeight-infoHeight, -192-60),
         dw, h - (80+titleHeight+infoHeight) - (40+LAND_BUTTON_HEIGHT), 0,
         "txtDesc", &gl_smallFont, NULL, NULL );

   /* goods list */
   if (array_size(land_planet->commodities) > 0) {
      ngoods = array_size( land_planet->commodities );
      cgoods = calloc( ngoods, sizeof(ImageArrayCell) );
      for (i=0; i<ngoods; i++) {
         cgoods[i].image = gl_dupTexture(land_planet->commodities[i]->gfx_store);
         cgoods[i].caption = strdup( _(land_planet->commodities[i]->name) );
      }
   }
   else {
      ngoods   = 1;
      cgoods   = calloc( ngoods, sizeof(ImageArrayCell) );
      cgoods[0].image = NULL;
      cgoods[0].caption = strdup(_("None"));
   }

   /* set up the goods to buy/sell */
   iconsize = 128;
   if (!conf.big_icons) {
      if (toolkit_simImageArrayVisibleElements(iw,ih,iconsize,iconsize) < ngoods)
         iconsize = 96;
      if (toolkit_simImageArrayVisibleElements(iw,ih,iconsize,iconsize) < ngoods)
         iconsize = 64;
   }
   window_addImageArray( wid, 20, 20,
         iw, ih, "iarTrade", iconsize, iconsize,
         cgoods, ngoods, commodity_update, commodity_update, commodity_update );

   /* Set default keyboard focuse to the list */
   window_setFocus( wid , "iarTrade" );
 }

/**
 * @brief Updates the commodity window.
 *    @param wid Window to update.
 *    @param str Unused.
 */
void commodity_update( unsigned int wid, const char *str )
{
   (void)str;
   char buf[STRMAX];
   char buf_purchase_price[ECON_CRED_STRLEN], buf_credits[ECON_CRED_STRLEN];
   size_t l = 0;
   Commodity *com;
   credits_t mean,globalmean;
   double std, globalstd;
   char buf_mean[ECON_CRED_STRLEN], buf_globalmean[ECON_CRED_STRLEN];
   char buf_std[ECON_CRED_STRLEN], buf_globalstd[ECON_CRED_STRLEN];
   char buf_local_price[ECON_CRED_STRLEN];
   char buf_tonnes_owned[ECON_MASS_STRLEN], buf_tonnes_free[ECON_MASS_STRLEN];
   int owned;
   int i = toolkit_getImageArrayPos( wid, "iarTrade" );
   credits2str( buf_credits, player.p->credits, 2 );
   tonnes2str( buf_tonnes_free, pilot_cargoFree(player.p) );

   if (i < 0 || array_size(land_planet->commodities) == 0) {
      l += scnprintf( &buf[l], sizeof(buf)-l, "%s", _("N/A") );
      l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", "" );
      l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", _("N/A") );
      l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", buf_tonnes_free );
      l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", buf_credits  );
      l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", _("N/A") );
      l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", _("N/A") );
      window_modifyText( wid, "txtDInfo", buf );
      window_modifyText( wid, "txtDesc", _("No commodities available.") );
      window_disableButton( wid, "btnCommodityBuy" );
      window_disableButton( wid, "btnCommoditySell" );
      return;
   }
   com = land_planet->commodities[i];

   /* modify image */
   window_modifyImage( wid, "imgStore", com->gfx_store, 192, 192 );

   planet_averagePlanetPrice( land_planet, com, &mean, &std);
   credits2str( buf_mean, mean, -1 );
   snprintf( buf_std, sizeof(buf_std), _("%.1f ¤"), std ); /* TODO credit2str could learn to do this... */
   economy_getAveragePrice( com, &globalmean, &globalstd );
   credits2str( buf_globalmean, globalmean, -1 );
   snprintf( buf_globalstd, sizeof(buf_globalstd), _("%.1f ¤"), globalstd ); /* TODO credit2str could learn to do this... */
   /* modify text */
   buf_purchase_price[0]='\0';
   owned=pilot_cargoOwned( player.p, com );
   if ( owned > 0 )
      credits2str( buf_purchase_price, com->lastPurchasePrice, -1 );
   credits2str( buf_local_price, planet_commodityPrice( land_planet, com ), -1 );
   tonnes2str( buf_tonnes_owned, owned );
   l += scnprintf( &buf[l], sizeof(buf)-l, "%s", buf_tonnes_owned );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", buf_purchase_price );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", "" );
   l += scnprintf( &buf[l], sizeof(buf)-l, _("%s/t"), buf_local_price );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", buf_tonnes_free );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", buf_credits );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", "" );
   l += scnprintf( &buf[l], sizeof(buf)-l, _("%s/t ± %s/t"), buf_mean, buf_std );
   l += scnprintf( &buf[l], sizeof(buf)-l, "\n%s", "" );
   l += scnprintf( &buf[l], sizeof(buf)-l, _("%s/t ± %s/t"), buf_globalmean, buf_globalstd );

   window_modifyText( wid, "txtDInfo", buf );
   window_modifyText( wid, "txtName", _(com->name) );
   window_modifyText( wid, "txtDesc", _(com->description) );

   /* Button enabling/disabling */
   if (commodity_canBuy( com ))
      window_enableButton( wid, "btnCommodityBuy" );
   else
      window_disableButtonSoft( wid, "btnCommodityBuy" );

   if (commodity_canSell( com ))
      window_enableButton( wid, "btnCommoditySell" );
   else
      window_disableButtonSoft( wid, "btnCommoditySell" );
}

int commodity_canBuy( const Commodity* com )
{
   int failure;
   unsigned int q, price;
   char buf[ECON_CRED_STRLEN];

   failure = 0;
   q = commodity_getMod();
   price = planet_commodityPrice( land_planet, com ) * q;

   if (!player_hasCredits( price )) {
      credits2str( buf, price - player.p->credits, 2 );
      land_errDialogueBuild(_("You need %s more."), buf );
      failure = 1;
   }
   if (pilot_cargoFree(player.p) <= 0) {
      land_errDialogueBuild(_("No cargo space available!"));
      failure = 1;
   }

   return !failure;
}

int commodity_canSell( const Commodity* com )
{
   int failure = 0;
   if (pilot_cargoOwned( player.p, com ) == 0) {
      land_errDialogueBuild(_("You can't sell something you don't have!"));
      failure = 1;
   }
   return !failure;
}

/**
 * @brief Buys the selected commodity.
 *    @param wid Window buying from.
 *    @param str Unused.
 */
void commodity_buy( unsigned int wid, const char *str )
{
   (void)str;
   int i;
   Commodity *com;
   unsigned int q;
   credits_t price;
   HookParam hparam[3];

   /* Get selected. */
   q     = commodity_getMod();
   i     = toolkit_getImageArrayPos( wid, "iarTrade" );
   com   = land_planet->commodities[i];
   price = planet_commodityPrice( land_planet, com );

   /* Check stuff. */
   if (land_errDialogue( com->name, "buyCommodity" ))
      return;

   /* Make the buy. */
   q = pilot_cargoAdd( player.p, com, q, 0 );
   com->lastPurchasePrice = price; /* To show the player how much they paid for it */
   price *= q;
   player_modCredits( -price );
   commodity_update(wid, NULL);

   /* Run hooks. */
   hparam[0].type    = HOOK_PARAM_STRING;
   hparam[0].u.str   = com->name;
   hparam[1].type    = HOOK_PARAM_NUMBER;
   hparam[1].u.num   = q;
   hparam[2].type    = HOOK_PARAM_SENTINEL;
   hooks_runParam( "comm_buy", hparam );
   if (land_takeoff)
      takeoff(1);
}

/**
 * @brief Attempts to sell a commodity.
 *    @param wid Window selling commodity from.
 *    @param str Unused.
 */
void commodity_sell( unsigned int wid, const char *str )
{
   (void)str;
   int i;
   Commodity *com;
   unsigned int q;
   credits_t price;
   HookParam hparam[3];

   /* Get parameters. */
   q     = commodity_getMod();
   i     = toolkit_getImageArrayPos( wid, "iarTrade" );
   com   = land_planet->commodities[i];
   price = planet_commodityPrice( land_planet, com );

   /* Check stuff. */
   if (land_errDialogue( com->name, "sellCommodity" ))
      return;

   /* Remove commodity. */
   q = pilot_cargoRm( player.p, com, q );
   price = price * (credits_t)q;
   player_modCredits( price );
   if ( pilot_cargoOwned( player.p, com ) == 0 ) /* None left, set purchase price to zero, in case missions add cargo. */
     com->lastPurchasePrice = 0;
   commodity_update(wid, NULL);

   /* Run hooks. */
   hparam[0].type    = HOOK_PARAM_STRING;
   hparam[0].u.str   = com->name;
   hparam[1].type    = HOOK_PARAM_NUMBER;
   hparam[1].u.num   = q;
   hparam[2].type    = HOOK_PARAM_SENTINEL;
   hooks_runParam( "comm_sell", hparam );
   if (land_takeoff)
      takeoff(1);
}

/**
 * @brief Gets the current modifier status.
 *    @return The amount modifier when buying or selling commodities.
 */
int commodity_getMod (void)
{
   SDL_Keymod mods = SDL_GetModState();
   int q = 10;
   if (mods & (KMOD_LCTRL | KMOD_RCTRL))
      q *= 5;
   if (mods & (KMOD_LSHIFT | KMOD_RSHIFT))
      q *= 10;

   return q;
}

/**
 * @brief Renders the commodity buying modifier.
 *    @param bx Base X position to render at.
 *    @param by Base Y position to render at.
 *    @param w Width to render at.
 *    @param h Height to render at.
 *    @param data Unused.
 */
void commodity_renderMod( double bx, double by, double w, double h, void *data )
{
   (void) data;
   (void) h;
   int q;
   char buf[8];

   q = commodity_getMod();
   if (q != commodity_mod) {
      commodity_update( land_getWid(LAND_WINDOW_COMMODITY), NULL );
      commodity_mod = q;
   }
   snprintf( buf, 8, "%dx", q );
   gl_printMidRaw( &gl_smallFont, w, bx, by, &cFontWhite, -1, buf );
}
