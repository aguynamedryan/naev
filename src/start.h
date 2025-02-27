/*
 * See Licensing and Copyright notice in naev.h
 */
#pragma once

#include "ntime.h"

/*
 * Initialization/clean up.
 */
int start_load (void);
void start_cleanup (void);

/*
 * Getting data.
 */
const char* start_name (void);
const char* start_ship (void);
const char* start_shipname (void);
unsigned int start_credits (void);
ntime_t start_date (void);
const char* start_system (void);
void start_position( double *x, double *y );
const char* start_mission (void);
const char* start_event (void);
