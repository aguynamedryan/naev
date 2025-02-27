/*
 * See Licensing and Copyright notice in naev.h
 */
#pragma once

#include "opengl.h"

GLuint gl_program_vert_frag( const char *vert, const char *frag );
GLuint gl_program_vert_frag_string( const char *vert, size_t vert_size, const char *frag, size_t frag_size );
void gl_uniformColor( GLint location, const glColour *c );
void gl_uniformAColor( GLint location, const glColour *c, GLfloat a );
