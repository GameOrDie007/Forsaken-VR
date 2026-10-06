/* Forsaken VR: the splash on the menu screen at start
 *
 * Copyright (C) 2026 Game Or Die
 *
 * New in the VR port. Built on ForsakenX, the community source port of
 * Forsaken (Probe Entertainment / Acclaim, 1998).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version. See LICENSE in the root of this repository.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 */

/* vrsplash.png beside the game, if there is one, shown on the menu screen
 * (and so on the observer view) as the game starts, for a few seconds or
 * until any button. No file, no splash: it is optional art, never an error.
 * The picture is fitted at its own shape with clear bars, the way Descent 1
 * & 2 VR shows its own. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <png.h>
#include <SDL.h>
#include "main.h"
#include "render.h"
#include "util.h"
#include "vr_target.h"
#include "vr_openxr.h"
#include "gl_headers.h"
#include "vr_splash.h"

extern render_info_t render_info;
extern bool render_flip( render_info_t * info );
extern bool QuitRequested;

static unsigned char * load_png_rgba( const char * path, int * w, int * h )
{
	FILE * f = fopen( path, "rb" );
	png_structp p;
	png_infop   info;
	unsigned char * px = NULL;
	png_bytep * rows = NULL;
	png_byte sig[8];
	int y;

	if( !f )
		return NULL;
	if( fread( sig, 1, 8, f ) != 8 || png_sig_cmp( sig, 0, 8 ) )
	{
		fclose( f );
		return NULL;
	}
	p = png_create_read_struct( PNG_LIBPNG_VER_STRING, NULL, NULL, NULL );
	info = p ? png_create_info_struct( p ) : NULL;
	if( !p || !info || setjmp( png_jmpbuf( p ) ) )
	{
		if( p ) png_destroy_read_struct( &p, info ? &info : NULL, NULL );
		free( px ); free( rows );
		fclose( f );
		return NULL;
	}
	png_init_io( p, f );
	png_set_sig_bytes( p, 8 );
	png_read_info( p, info );
	png_set_expand( p );
	png_set_strip_16( p );
	png_set_gray_to_rgb( p );
	png_set_add_alpha( p, 0xff, PNG_FILLER_AFTER );
	png_read_update_info( p, info );
	*w = (int) png_get_image_width( p, info );
	*h = (int) png_get_image_height( p, info );
	px   = (unsigned char *) malloc( (size_t) *w * *h * 4 );
	rows = (png_bytep *) malloc( sizeof(png_bytep) * *h );
	if( !px || !rows )
		longjmp( png_jmpbuf( p ), 1 );
	/* bottom row first, the way GL stores a texture */
	for( y = 0; y < *h; y++ )
		rows[y] = px + (size_t)( *h - 1 - y ) * *w * 4;
	png_read_image( p, rows );
	png_destroy_read_struct( &p, &info, NULL );
	free( rows );
	fclose( f );
	return px;
}

/* any button newly pressed on the controllers, the keyboard or a pad */
static bool splash_dismissed( void )
{
	static bool held_before = true;   /* a button held at start does not count */
	SDL_Event e;
	bool any = false, held;
	int i;

	while( SDL_PollEvent( &e ) )
	{
		if( e.type == SDL_QUIT ) { QuitRequested = true; any = true; }
		if( e.type == SDL_KEYDOWN || e.type == SDL_MOUSEBUTTONDOWN ||
		    e.type == SDL_CONTROLLERBUTTONDOWN || e.type == SDL_JOYBUTTONDOWN )
			any = true;
	}
	vr_openxr_sync_input();
	held = vr_input.fire1 > 0.5f || vr_input.fire2 > 0.5f || vr_input.menu_edge;
	for( i = 1; i < VRB_PAD_COUNT; i++ )
		held = held || vr_input.pad[i];
	if( held && !held_before )
		any = true;
	held_before = held;
	return any;
}

void vr_splash_show( const char * path, int ms )
{
	int w = 0, h = 0, shown = 0, fw = 0, fh = 0;
	unsigned char * px;
	GLuint tex = 0, fbo = 0, target;
	unsigned int start = 0;

	if( !vr_enabled )
		return;
	px = load_png_rgba( path, &w, &h );
	if( !px )
	{
		DebugPrintf( "vr: no splash (%s not there)\n", path );
		return;
	}

	glGenTextures( 1, &tex );
	glBindTexture( GL_TEXTURE_2D, tex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	free( px );
	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
	glFramebufferTexture2D( GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0 );
	vr_target_get_size( &fw, &fh );
	DebugPrintf( "vr: splash %s (%dx%d) on the menu screen\n", path, w, h );

	while( !QuitRequested )
	{
		unsigned int now = SDL_GetTicks();
		if( splash_dismissed() )
			break;
		if( shown && now - start >= (unsigned int) ms )
			break;

		vr_openxr_begin_frame();
		vr_target_begin_frame();

		/* the menu screen shows the whole target at 4:3; fit the picture
		   into that at its own shape, with clear bars */
		target = (GLuint) vr_target_fbo_id();
		if( target && fw > 0 && fh > 0 )
		{
			float a = (float) w / (float) h, sx = 1.0f, sy = 1.0f;
			int x0, y0, x1, y1;
			if( a > 4.0f / 3.0f ) sy = ( 4.0f / 3.0f ) / a; else sx = a / ( 4.0f / 3.0f );
			x0 = (int)( fw * ( 1.0f - sx ) * 0.5f ); x1 = fw - x0;
			y0 = (int)( fh * ( 1.0f - sy ) * 0.5f ); y1 = fh - y0;
			glBindFramebuffer( GL_DRAW_FRAMEBUFFER, target );
			glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
			glBlitFramebuffer( 0, 0, w, h, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, GL_LINEAR );
			glBindFramebuffer( GL_FRAMEBUFFER, target );
		}

		/* Not until head tracking has settled: until then the menu screen is
		   placed again every frame (a runtime's first poses can be anywhere),
		   so the picture would follow the head. After it, the splash is
		   placed once, where the player looks, and stays there. */
		if( vr_views_valid && vr_openxr_settled() )
		{
			vr_panel_frame = true;
			vr_openxr_submit_panel();
			vr_panel_frame = false;
			if( !shown ) start = now;
			shown++;
		}
		vr_openxr_end_frame();
		vr_target_end_frame();
		render_flip( &render_info );
		if( !shown && now > 20000 )   /* a session that never starts: do not wait forever */
			break;
	}

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glDeleteFramebuffers( 1, &fbo );
	glDeleteTextures( 1, &tex );
	DebugPrintf( "vr: splash done after %d frames\n", shown );
}
