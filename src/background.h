/*
 * See Licensing and Copyright notice in naev.h
 */
#pragma once

#include "colour.h"
#include "opengl.h"

/* Render. */
void background_render( double dt );
void background_renderOverlay( double dt );

/* Add images. */
unsigned int background_addImage( glTexture *image, double x, double y,
      double move, double scale, double angle, const glColour *col, int foreground );

/* Stars. */
void background_initStars( int n );
void background_renderStars( const double dt );
void background_moveStars( double x, double y );

/* Init. */
int background_init (void);
int background_load( const char *name );

/* Clean up. */
void background_clear (void);
void background_free (void);

/* Get image textures */
glTexture** background_getTextures (void);
