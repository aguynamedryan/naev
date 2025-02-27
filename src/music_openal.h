/*
 * See Licensing and Copyright notice in naev.h
 */
#pragma once

/** @cond */
#include "SDL_rwops.h"
/** @endcond */

#include "nopenal.h"

/*
 * Init/exit.
 */
int music_al_init (void);
void music_al_exit (void);

/*
 * Loading.
 */
int music_al_load( const char* name, SDL_RWops *rw );
void music_al_free (void);

/*
 * Music control.
 */
int music_al_volume( const double vol );
double music_al_getVolume (void);
double music_al_getVolumeLog(void);
void music_al_play (void);
void music_al_stop (void);
void music_al_pause (void);
void music_al_resume (void);
void music_al_setPos( double sec );
int music_al_isPlaying (void);
