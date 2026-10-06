/* Forsaken VR: offscreen render target for the VR path
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

#include "main.h"
#include "util.h"
#include "render.h"
#include "vr_target.h"
#include "vr_openxr.h"
#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// the mirror blit needs the real window size, which is not the render size
extern render_info_t render_info;

// Parsed from the command line in main.c. Defined here so that the option
// exists even in builds where there is no GL render path to act on it.
bool vr_enabled = false;

// Physical render surface. Defaults to zero and is set from the window size
// during startup, so a build that never calls vr_surface_set() still behaves.
int vr_surface_width  = 0;
int vr_surface_height = 0;

void vr_surface_set( int width, int height )
{
	if( width <= 0 || height <= 0 )
		return;
	vr_surface_width  = width;
	vr_surface_height = height;
}

#if defined(GL) && (GL >= 2) && !defined(RENDER_DISABLED)

#include "gl_headers.h"

static GLuint fbo       = 0;
static GLuint color_tex = 0;
static GLuint depth_rb  = 0;
static int    fbo_w     = 0;
static int    fbo_h     = 0;
static bool   fbo_ready = false;

/* ------------------------------------------------------------ spectator view
   What the desktop window shows in VR: a steady cut-out of the eye, at the
   window's shape, that follows the head's turns but not its wobble and keeps
   the ROOM's horizon level (the ship's own roll stays: that is the game).
   Before this the window showed the whole eye squeezed to 4:3: stretched
   sideways and shaking with every movement of the head.

   Done here for a single GL context: the eye picture is still
   in our render target when the frame ends, and the orientation and frustum
   it was drawn with are vr_eye[]. One textured quad, no extra render.

   Only when the target holds an eye: menus, the title's VDU pages and the
   intro leave something else in it, and keep the plain view. */

bool vr_spectator      = true;    /* VRSpectator / -vrspectator:0|1 */
int  vr_spectator_eye  = -1;      /* the eye kept for the observer this frame, or -1 */
bool vr_observer_panel = false;   /* a menu screen was drawn this frame */
bool vr_observer_windowed = false;/* VRObserverWindowed: Alt+Enter */
bool vr_observer_force_window = false; /* -observerwindow: desk runs, never saved */

/* The OBSERVER VIEW (the house name; "spectator" in the code): what the
   monitor shows (and so what a stream or a recording shows) while the
   headset is on. Everything is on it: the game, the HUD, and a menu screen
   laid over the game at the size it has in the headset. The menu screen is
   drawn into the same render target after the eyes, so the eye picture is
   copied aside first (vr_observer_keep_eye). */
static GLuint obs_fbo = 0, obs_tex = 0;
static int    obs_w = 0, obs_h = 0;

void vr_observer_keep_eye( int eye )
{
	if( !fbo_ready )
		return;
	if( !obs_fbo || obs_w != fbo_w || obs_h != fbo_h )
	{
		if( obs_fbo ) { glDeleteFramebuffers( 1, &obs_fbo ); obs_fbo = 0; }
		if( obs_tex ) { glDeleteTextures( 1, &obs_tex );     obs_tex = 0; }
		glGenTextures( 1, &obs_tex );
		glBindTexture( GL_TEXTURE_2D, obs_tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, fbo_w, fbo_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
		glGenFramebuffers( 1, &obs_fbo );
		glBindFramebuffer( GL_FRAMEBUFFER, obs_fbo );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, obs_tex, 0 );
		obs_w = fbo_w; obs_h = fbo_h;
		glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	}
	glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, obs_fbo );
	glBlitFramebuffer( 0, 0, fbo_w, fbo_h, 0, 0, fbo_w, fbo_h, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	vr_spectator_eye = eye;
}

static GLuint spec_prog = 0, spec_vbo = 0;
static GLint  spec_tex_loc = -1, spec_keyed_loc = -1, spec_solid_loc = -1;
static bool   spec_failed = false;

/* A see-through panel. The menu screen and the HUD are drawn over black, so
   black is "nothing here": keyed, each pixel's alpha is its brightest channel
   and its colour stays as drawn, which is premultiplied alpha: what an
   OpenXR quad with SOURCE_ALPHA expects, and what the observer blends with
   (ONE, ONE_MINUS_SRC_ALPHA). The text floats in the world with no box
   behind it. Pictures (the rear-view mirror, the missile camera) are not
   text on black, so their rectangles are kept solid: x0, y0, x1, y1 as
   fractions of the target, measured from the TOP left. */
bool  vr_panel_keyed = false;
float vr_panel_solid[2][4];
int   vr_panel_solid_n = 0;

static GLuint spec_shader( GLenum type, const char * src )
{
	GLuint s = glCreateShader( type );
	GLint  ok = 0;
	glShaderSource( s, 1, &src, NULL );
	glCompileShader( s );
	glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
	if( !ok )
	{
		char log[512];
		glGetShaderInfoLog( s, sizeof(log), NULL, log );
		DebugPrintf( "vr spectator: shader did not compile: %s\n", log );
		glDeleteShader( s );
		return 0;
	}
	return s;
}

static bool spec_init( void )
{
	static const char * vs =
		"#version 150\n"
		"in vec2 pos;\n"
		"in vec2 uv;\n"
		"out vec2 v_uv;\n"
		"void main() { v_uv = uv; gl_Position = vec4( pos, 0.0, 1.0 ); }\n";
	static const char * fs =
		"#version 150\n"
		"uniform sampler2D tex;\n"
		"uniform int  keyed;\n"
		"uniform vec4 solid[2];\n"
		"in vec2 v_uv;\n"
		"out vec4 color;\n"
		"bool inside( vec4 r, vec2 p ) { return p.x >= r.x && p.x <= r.z && p.y >= r.y && p.y <= r.w; }\n"
		"void main()\n"
		"{\n"
		"	vec4 c = texture( tex, v_uv );\n"
		"	if( keyed != 0 )\n"
		"	{\n"
		"		vec2 p = vec2( v_uv.x, 1.0 - v_uv.y );\n"
		"		float a = max( c.r, max( c.g, c.b ) );\n"
		"		if( inside( solid[0], p ) || inside( solid[1], p ) ) a = 1.0;\n"
		"		c = vec4( c.rgb, a );\n"
		"	}\n"
		"	color = c;\n"
		"}\n";
	GLuint v, f;
	GLint  ok = 0;

	if( spec_prog || spec_failed )
		return spec_prog != 0;

	v = spec_shader( GL_VERTEX_SHADER, vs );
	f = spec_shader( GL_FRAGMENT_SHADER, fs );
	if( !v || !f )
	{
		spec_failed = true;
		return false;
	}
	spec_prog = glCreateProgram();
	glAttachShader( spec_prog, v );
	glAttachShader( spec_prog, f );
	glBindAttribLocation( spec_prog, 0, "pos" );
	glBindAttribLocation( spec_prog, 1, "uv" );
	glLinkProgram( spec_prog );
	glDeleteShader( v );
	glDeleteShader( f );
	glGetProgramiv( spec_prog, GL_LINK_STATUS, &ok );
	if( !ok )
	{
		DebugPrintf( "vr spectator: program did not link\n" );
		glDeleteProgram( spec_prog );
		spec_prog = 0;
		spec_failed = true;
		return false;
	}
	spec_tex_loc   = glGetUniformLocation( spec_prog, "tex" );
	spec_keyed_loc = glGetUniformLocation( spec_prog, "keyed" );
	spec_solid_loc = glGetUniformLocation( spec_prog, "solid" );
	glGenBuffers( 1, &spec_vbo );
	DebugPrintf( "vr spectator: steady desktop view ready\n" );
	return true;
}

/* small vector helpers, tracking space: x right, y up, -z forward */
static void v3_norm( float v[3] )
{
	float l = (float) sqrt( v[0]*v[0] + v[1]*v[1] + v[2]*v[2] );
	if( l > 1e-9f ) { v[0] /= l; v[1] /= l; v[2] /= l; }
}
static float v3_dot( const float a[3], const float b[3] ) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

static void quat_axes( const float q[4], float r[3], float u[3], float f[3] )
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	/* columns of the rotation matrix: +X right, +Y up, and forward = -Z */
	r[0] = 1 - 2*(y*y + z*z); r[1] = 2*(x*y + w*z);     r[2] = 2*(x*z - w*y);
	u[0] = 2*(x*y - w*z);     u[1] = 1 - 2*(x*x + z*z); u[2] = 2*(y*z + w*x);
	f[0] = -( 2*(x*z + w*y) ); f[1] = -( 2*(y*z - w*x) ); f[2] = -( 1 - 2*(x*x + y*y) );
}

/* a tracking-space direction -> eye image pixel (x right, y DOWN); false if behind */
static bool spec_project( const vr_eye_view_t * e, const float r[3], const float u[3],
                          const float f[3], const float d[3], float W, float H,
                          float * px, float * py )
{
	float z = v3_dot( d, f ), tx, ty;
	if( z < 1e-4f )
		return false;
	tx = v3_dot( d, r ) / z;
	ty = v3_dot( d, u ) / z;
	*px = ( tx - e->tan_left ) / ( e->tan_right - e->tan_left ) * W;
	*py = ( e->tan_up - ty )   / ( e->tan_up - e->tan_down )   * H;
	return true;
}

static void spec_unproject( const vr_eye_view_t * e, const float r[3], const float u[3],
                            const float f[3], float px, float py, float W, float H, float d[3] )
{
	float tx = e->tan_left + px / W * ( e->tan_right - e->tan_left );
	float ty = e->tan_up   - py / H * ( e->tan_up - e->tan_down );
	int   i;
	for( i = 0; i < 3; i++ )
		d[i] = tx * r[i] + ty * u[i] + f[i];
	v3_norm( d );
}

static float spec_alpha( float dt, float cutoff )
{
	float tau = 1.0f / ( 2.0f * 3.14159265f * cutoff );
	return 1.0f / ( 1.0f + tau / dt );
}

/* Draws the cut-out into the window. Returns false to fall back to the plain
   blit. The numbers that matter are logged once a second under -vrspeclog. */
bool vr_spectator_log = false;
float vr_spec_last_roll = 0.0f, vr_spec_last_w = 0.0f;

static bool spec_quad( GLuint tex, const float verts[16], bool blend_alpha,
                       int dw, int dh, bool clear_first );
static bool spec_quad_ex( GLuint dst, GLuint tex, const float verts[16], int blend,
                          bool keyed, int dw, int dh, bool clear_first );

static bool spec_draw( int dw, int dh )
{
	static float fd[3], raw_prev[3], rate[3];
	static bool  have = false;
	static unsigned int last_ms = 0;
	const vr_eye_view_t * e;
	float r[3], u[3], f[3], W = (float) fbo_w, H = (float) fbo_h;
	float dt, cx, cy, hx, hy, theta, cw, ch, ex, ey, aspect;
	float c, s, verts[16];
	unsigned int now;
	int i, k, tries;
	bool clamped = false;

	if( vr_spectator_eye < 0 || vr_spectator_eye >= VR_EYE_COUNT || !obs_tex )
		return false;
	if( dw <= 0 || dh <= 0 || !spec_init() )
		return false;

	e = &vr_eye[ vr_spectator_eye ];
	quat_axes( e->quat, r, u, f );

	now = SDL_GetTicks();
	dt  = last_ms ? ( now - last_ms ) / 1000.0f : 1.0f / 90.0f;
	if( dt <= 0.0f ) dt = 1.0f / 90.0f;
	if( dt > 0.1f )  dt = 0.1f;
	last_ms = now;

	/* follow the head's forward: one-euro, heavy at rest and light while
	   turning. The turn rate is filtered as a VECTOR: a wobble's rate has a
	   size that never averages out, its direction does. A jump over 45
	   degrees (recenter) is taken at once. */
	if( !have || v3_dot( f, fd ) < 0.7071f )
	{
		for( i = 0; i < 3; i++ ) { fd[i] = f[i]; raw_prev[i] = f[i]; rate[i] = 0.0f; }
		have = true;
	}
	else
	{
		float ad = spec_alpha( dt, 1.0f ), speed, a;
		for( i = 0; i < 3; i++ )
		{
			float dr = ( f[i] - raw_prev[i] ) / dt;
			rate[i] += ad * ( dr - rate[i] );
			raw_prev[i] = f[i];
		}
		speed = (float) sqrt( v3_dot( rate, rate ) );       /* ~ rad/s */
		a = spec_alpha( dt, 0.4f + 2.0f * speed );
		for( i = 0; i < 3; i++ )
			fd[i] += a * ( f[i] - fd[i] );
		v3_norm( fd );
	}

	if( !spec_project( e, r, u, f, fd, W, H, &cx, &cy ) )
	{
		for( i = 0; i < 3; i++ ) fd[i] = f[i];
		spec_project( e, r, u, f, fd, W, H, &cx, &cy );
	}

	/* the room's horizontal through that point, as the picture shows it,
	   becomes the window's horizontal. Straight up or down: stay upright. */
	theta = 0.0f;
	if( fabs( fd[1] ) < 0.98f )
	{
		float h[3], p2[3], x2, y2;
		h[0] = -fd[2]; h[1] = 0.0f; h[2] = fd[0];         /* fd x up */
		v3_norm( h );
		for( i = 0; i < 3; i++ ) p2[i] = fd[i] + 0.02f * h[i];
		if( spec_project( e, r, u, f, p2, W, H, &x2, &y2 ) )
		{
			hx = x2 - cx; hy = y2 - cy;
			theta = (float) atan2( hy, hx );
		}
	}

	/* 0.78 of the eye's width at the window's shape; shrunk until its turned
	   corners fit, then kept inside the eye. */
	aspect = (float) dw / (float) dh;
	cw = 0.78f * W;
	c = (float) cos( theta ); s = (float) sin( theta );
	for( tries = 0; tries < 40; tries++ )
	{
		ch = cw / aspect;
		ex = (float)( fabs( cw * 0.5f * c ) + fabs( ch * 0.5f * s ) );
		ey = (float)( fabs( cw * 0.5f * s ) + fabs( ch * 0.5f * c ) );
		if( ex <= W * 0.5f && ey <= H * 0.5f )
			break;
		cw *= 0.95f;
	}
	ch = cw / aspect;
	if( cx < ex )     { cx = ex;     clamped = true; }
	if( cx > W - ex ) { cx = W - ex; clamped = true; }
	if( cy < ey )     { cy = ey;     clamped = true; }
	if( cy > H - ey ) { cy = H - ey; clamped = true; }
	if( clamped )      /* no wind-up past the edge: follow what is drawn */
		spec_unproject( e, r, u, f, cx, cy, W, H, fd );

	/* the quad: window corners (NDC) <- turned rectangle in the eye (tex) */
	for( k = 0; k < 4; k++ )
	{
		float sx = ( k & 1 ) ? 1.0f : -1.0f;            /* right / left  */
		float sy = ( k & 2 ) ? 1.0f : -1.0f;            /* down / up     */
		float px = cx + sx * cw * 0.5f * c - sy * ch * 0.5f * s;
		float py = cy + sx * cw * 0.5f * s + sy * ch * 0.5f * c;
		verts[k*4 + 0] = sx;
		verts[k*4 + 1] = -sy;
		verts[k*4 + 2] = px / W;
		verts[k*4 + 3] = 1.0f - py / H;
	}

	vr_spec_last_roll = theta * 57.29578f;
	vr_spec_last_w    = cw / W;
	if( vr_spectator_log )
	{
		static unsigned int last_log = 0;
		if( now - last_log > 1000 )
		{
			DebugPrintf( "vr spectator: centre %.0f,%.0f of %.0fx%.0f, turned %.2f deg, width %.2f%s\n",
			             cx, cy, W, H, vr_spec_last_roll, vr_spec_last_w, clamped ? " (held at the edge)" : "" );
			last_log = now;
		}
	}

	return spec_quad( obs_tex, verts, false, dw, dh, true );
}

/* One textured quad into the window (verts: x, y, u, v per corner in strip
   order), leaving the GL state as the engine had it. */
static bool spec_quad( GLuint tex, const float verts[16], bool blend_alpha,
                       int dw, int dh, bool clear_first )
{
	return spec_quad_ex( 0, tex, verts, blend_alpha ? 1 : 0, false, dw, dh, clear_first );
}

/* blend: 0 none, 1 straight alpha, 2 premultiplied. dst: the framebuffer
   drawn into (0, the window). keyed: see vr_panel_keyed. */
static bool spec_quad_ex( GLuint dst, GLuint tex, const float verts[16], int blend_mode,
                          bool keyed, int dw, int dh, bool clear_first )
{
	GLint prev_prog = 0, prev_vbo = 0, prev_tex = 0, prev_vp[4], prev_fbo = 0;
	GLint bsrc_rgb = 0, bdst_rgb = 0, bsrc_a = 0, bdst_a = 0;
	GLboolean depth, blend, cull, scissor;

	if( !spec_init() )
		return false;

	/* draw, leaving the GL state as the engine had it */
	glGetIntegerv( GL_FRAMEBUFFER_BINDING, &prev_fbo );
	glGetIntegerv( GL_BLEND_SRC_RGB, &bsrc_rgb );   glGetIntegerv( GL_BLEND_DST_RGB, &bdst_rgb );
	glGetIntegerv( GL_BLEND_SRC_ALPHA, &bsrc_a );   glGetIntegerv( GL_BLEND_DST_ALPHA, &bdst_a );
	glGetIntegerv( GL_CURRENT_PROGRAM, &prev_prog );
	glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &prev_vbo );
	glGetIntegerv( GL_TEXTURE_BINDING_2D, &prev_tex );
	glGetIntegerv( GL_VIEWPORT, prev_vp );
	depth = glIsEnabled( GL_DEPTH_TEST ); blend = glIsEnabled( GL_BLEND );
	cull = glIsEnabled( GL_CULL_FACE );   scissor = glIsEnabled( GL_SCISSOR_TEST );

	glBindFramebuffer( GL_FRAMEBUFFER, dst );
	glViewport( 0, 0, dw, dh );
	glDisable( GL_DEPTH_TEST ); glDisable( GL_CULL_FACE ); glDisable( GL_SCISSOR_TEST );
	if( clear_first )
	{
		glClearColor( 0.0f, 0.0f, 0.0f, 1.0f );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	if( blend_mode == 1 )
	{
		glEnable( GL_BLEND );
		glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	}
	else if( blend_mode == 2 )
	{
		glEnable( GL_BLEND );
		glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	}
	else
		glDisable( GL_BLEND );
	glUseProgram( spec_prog );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, tex );
	if( spec_tex_loc >= 0 ) glUniform1i( spec_tex_loc, 0 );
	if( spec_keyed_loc >= 0 ) glUniform1i( spec_keyed_loc, keyed ? 1 : 0 );
	if( spec_solid_loc >= 0 )
	{
		float s[8] = { 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f, 2.0f };   /* empty */
		int   k;
		for( k = 0; k < vr_panel_solid_n && k < 2; k++ )
			memcpy( &s[k*4], vr_panel_solid[k], 4 * sizeof(float) );
		glUniform4fv( spec_solid_loc, 2, s );
	}
	glBindBuffer( GL_ARRAY_BUFFER, spec_vbo );
	glBufferData( GL_ARRAY_BUFFER, 16 * sizeof(float), verts, GL_STREAM_DRAW );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *) 0 );
	glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)( 2 * sizeof(float) ) );
	glEnableVertexAttribArray( 0 );
	glEnableVertexAttribArray( 1 );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glDisableVertexAttribArray( 0 );
	glDisableVertexAttribArray( 1 );

	glUseProgram( (GLuint) prev_prog );
	glBindBuffer( GL_ARRAY_BUFFER, (GLuint) prev_vbo );
	glBindTexture( GL_TEXTURE_2D, (GLuint) prev_tex );
	glViewport( prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3] );
	if( depth )   glEnable( GL_DEPTH_TEST );
	if( blend )   glEnable( GL_BLEND ); else glDisable( GL_BLEND );
	if( cull )    glEnable( GL_CULL_FACE );
	if( scissor ) glEnable( GL_SCISSOR_TEST );
	glBlendFuncSeparate( (GLenum) bsrc_rgb, (GLenum) bdst_rgb, (GLenum) bsrc_a, (GLenum) bdst_a );
	glBindFramebuffer( GL_FRAMEBUFFER, (GLuint) prev_fbo );
	return true;
}

/* A texture into a centred rectangle of the window: aspect a (w/h), at most
   frac of the window's height and the whole width. */
static bool obs_centred_ex( GLuint tex, float a, float frac, int blend, bool keyed, bool clear, int dw, int dh );
static bool obs_centred( GLuint tex, float a, float frac, bool blend, bool clear, int dw, int dh )
{
	return obs_centred_ex( tex, a, frac, blend ? 1 : 0, false, clear, dw, dh );
}
static bool obs_centred_ex( GLuint tex, float a, float frac, int blend, bool keyed, bool clear, int dw, int dh )
{
	float h = frac * dh, w = h * a, verts[16];
	int   k;
	if( w > dw ) { w = (float) dw; h = w / a; }
	for( k = 0; k < 4; k++ )
	{
		float sx = ( k & 1 ) ? 1.0f : -1.0f, sy = ( k & 2 ) ? 1.0f : -1.0f;
		verts[k*4 + 0] = sx * w / dw;
		verts[k*4 + 1] = -sy * h / dh;
		verts[k*4 + 2] = sx > 0 ? 1.0f : 0.0f;
		verts[k*4 + 3] = sy > 0 ? 0.0f : 1.0f;
	}
	return spec_quad_ex( 0, tex, verts, blend, keyed, dw, dh, clear );
}

/* The render target, keyed (see vr_panel_keyed), into another framebuffer:
   the menu screen's swapchain image: filling it. */
bool vr_target_key_into( unsigned int dst_fbo, int dw, int dh )
{
	static const float full[16] = { -1.0f,  1.0f, 0.0f, 1.0f,   1.0f,  1.0f, 1.0f, 1.0f,
	                                 -1.0f, -1.0f, 0.0f, 0.0f,   1.0f, -1.0f, 1.0f, 0.0f };
	return fbo_ready && spec_quad_ex( (GLuint) dst_fbo, color_tex, full, 0, true, dw, dh, false );
}

/* The menu screen's share of the window's height, as the headset shows it:
   its angular height against the observer cut-out's. */
static float obs_panel_frac( void )
{
	const vr_eye_view_t * e = &vr_eye[ vr_spectator_eye >= 0 ? vr_spectator_eye : 0 ];
	float half_tan = vr_panel_half_tan_y();   /* the screen shown this frame */
	float view_tan = ( e->tan_up - e->tan_down ) * 0.5f;
	float f;
	if( vr_spectator )   /* the cut-out is 0.78 of the eye wide at the window's shape */
		view_tan = 0.78f * ( e->tan_right - e->tan_left ) * 0.5f * ( 3.0f / 4.0f );
	f = view_tan > 0.01f ? half_tan / view_tan : 0.9f;
	return f > 0.95f ? 0.95f : f;
}

static bool observer_draw( int dw, int dh )
{
	bool eye = vr_spectator_eye >= 0 && obs_tex;

	if( eye )
	{
		if( vr_spectator )
		{
			if( !spec_draw( dw, dh ) )
				return false;
		}
		else if( !obs_centred( obs_tex, (float) fbo_w / fbo_h, 1.0f, false, true, dw, dh ) )
			return false;
		/* a menu over the game: laid on top, as big as it looks in the headset */
		if( vr_observer_panel )
			obs_centred_ex( color_tex, 4.0f / 3.0f, obs_panel_frac(), vr_panel_keyed ? 2 : 0,
			                vr_panel_keyed, false, dw, dh );
		return true;
	}
	/* no game picture this frame (menus, the title's pages, the intro,
	   loading): the menu screen alone, at its own shape */
	return obs_centred( color_tex, 4.0f / 3.0f, 1.0f, false, true, dw, dh );
}

/* The window is the observer's: borderless over its monitor by default, a
   plain window after Alt+Enter. Restyled in place: never a new video mode,
   which would rebuild the GL context the headset session holds. */
void vr_observer_window_apply( void )
{
	SDL_Window * w = render_info.window;
	int d;
	SDL_Rect r;

	if( !vr_enabled || !w )
		return;
	d = SDL_GetWindowDisplayIndex( w );
	if( d < 0 ) d = 0;
	if( SDL_GetDisplayBounds( d, &r ) != 0 )
		return;

	SDL_SetWindowFullscreen( w, 0 );
	if( vr_observer_windowed || vr_observer_force_window )
	{
		int ww = r.w * 2 / 3, wh = ww * 3 / 4;
		if( wh > r.h * 5 / 6 ) { wh = r.h * 5 / 6; ww = wh * 4 / 3; }
		SDL_SetWindowBordered( w, SDL_TRUE );
		SDL_SetWindowSize( w, ww, wh );
		SDL_SetWindowPosition( w, r.x + ( r.w - ww ) / 2, r.y + ( r.h - wh ) / 2 );
	}
	else
	{
		SDL_SetWindowBordered( w, SDL_FALSE );
		SDL_SetWindowPosition( w, r.x, r.y );
		SDL_SetWindowSize( w, r.w, r.h );
	}
	DebugPrintf( "vr: observer view %s on display %d (%dx%d)\n",
	             ( vr_observer_windowed || vr_observer_force_window ) ? "in a window" : "borderless over the monitor", d, r.w, r.h );
}

void vr_observer_toggle_window( void )
{
	extern void VRSaveObserverWindowed( void );
	vr_observer_windowed = !vr_observer_windowed;
	vr_observer_window_apply();
	VRSaveObserverWindowed();
}

/* headless: the window as drawn, for the desk proof */
static void spec_dump_window( int dw, int dh )
{
	unsigned char * px = (unsigned char *) malloc( (size_t) dw * dh * 3 );
	FILE * fp;
	int y;
	if( !px ) return;
	glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, dw, dh, GL_RGB, GL_UNSIGNED_BYTE, px );
	fp = fopen( "logs/window.ppm", "wb" );
	if( fp )
	{
		fprintf( fp, "P6\n%d %d\n255\n", dw, dh );
		for( y = dh - 1; y >= 0; y-- )
			fwrite( px + (size_t) y * dw * 3, 1, (size_t) dw * 3, fp );
		fclose( fp );
		DebugPrintf( "vr spectator: dumped logs/window.ppm (%dx%d)\n", dw, dh );
	}
	free( px );
}

static const char * fbo_status_string( GLenum s )
{
	switch( s )
	{
	case GL_FRAMEBUFFER_COMPLETE:                      return "complete";
	case GL_FRAMEBUFFER_UNDEFINED:                     return "undefined";
	case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:         return "incomplete attachment";
	case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "missing attachment";
	case GL_FRAMEBUFFER_UNSUPPORTED:                   return "unsupported";
	case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:        return "incomplete multisample";
	default:                                           return "unknown";
	}
}

void vr_target_cleanup( void )
{
	if( color_tex ) { glDeleteTextures( 1, &color_tex );        color_tex = 0; }
	if( depth_rb )  { glDeleteRenderbuffers( 1, &depth_rb );    depth_rb  = 0; }
	if( fbo )       { glDeleteFramebuffers( 1, &fbo );          fbo       = 0; }
	fbo_ready = false;
	fbo_w = fbo_h = 0;
}

bool vr_target_init( int width, int height )
{
	GLenum status;

	if( !vr_enabled )
		return true;

	if( width <= 0 || height <= 0 )
	{
		DebugPrintf( "vr_target: refusing to create a %dx%d target\n", width, height );
		return false;
	}

	// already the right size, nothing to do
	if( fbo_ready && width == fbo_w && height == fbo_h )
		return true;

	vr_target_cleanup();

	glGenFramebuffers( 1, &fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );

	glGenTextures( 1, &color_tex );
	glBindTexture( GL_TEXTURE_2D, color_tex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0,
	              GL_RGBA, GL_UNSIGNED_BYTE, NULL );
	// no mips, and clamp: later phases hand this texture to OpenXR
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                        GL_TEXTURE_2D, color_tex, 0 );

	// depth only; the engine never reads it back, so a renderbuffer is enough
	glGenRenderbuffers( 1, &depth_rb );
	glBindRenderbuffer( GL_RENDERBUFFER, depth_rb );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
	                           GL_RENDERBUFFER, depth_rb );

	status = glCheckFramebufferStatus( GL_FRAMEBUFFER );

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glBindRenderbuffer( GL_RENDERBUFFER, 0 );

	if( status != GL_FRAMEBUFFER_COMPLETE )
	{
		Msg( "vr_target: framebuffer incomplete (%s) -- falling back to "
		     "direct rendering\n", fbo_status_string( status ) );
		vr_target_cleanup();
		vr_enabled = false;   // fail open to flat rendering
		return false;
	}

	fbo_w = width;
	fbo_h = height;
	fbo_ready = true;

	DebugPrintf( "vr_target: offscreen target ready, %dx%d "
	             "(fbo=%u colour=%u depth=%u)\n",
	             fbo_w, fbo_h, fbo, color_tex, depth_rb );
	return true;
}

void vr_target_begin_frame( void )
{
	if( !vr_enabled || !fbo_ready )
		return;
	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
}

void vr_target_end_frame( void )
{
	if( !vr_enabled || !fbo_ready )
		return;

	// Mirror the finished frame into the window. The engine already renders
	// with the orientation it wants on screen, so no flip is needed here.
	//
	// The target is no longer necessarily the window's size: under -vr it is
	// the eye buffer, which is both much larger and a different aspect, so
	// this scales. LINEAR when the sizes differ, NEAREST when they match so the
	// flat path stays a bit-exact copy.
	{
		int dw = render_info.ThisMode.w;
		int dh = render_info.ThisMode.h;
		GLenum filter = ( dw == fbo_w && dh == fbo_h ) ? GL_NEAREST : GL_LINEAR;

		/* VR: the observer view, at the window's real size. Flat: the
		   whole frame, exactly as before. */
		if( vr_enabled && render_info.window )
			SDL_GL_GetDrawableSize( render_info.window, &dw, &dh );
		if( !vr_enabled || !observer_draw( dw, dh ) )
		{
			glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
			glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
			glBlitFramebuffer( 0, 0, fbo_w, fbo_h,
			                   0, 0, dw, dh,
			                   GL_COLOR_BUFFER_BIT, filter );
		}
		if( vr_dump_frame )
			spec_dump_window( dw, dh );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
		vr_spectator_eye  = -1;
		vr_panel_last     = vr_observer_panel && vr_panel_kind_shown != VR_PANEL_HUD;
		vr_observer_panel = false;
		vr_panel_keyed    = false;
		vr_panel_solid_n  = 0;
	}
}

unsigned int vr_target_fbo_id( void )
{
	return fbo_ready ? (unsigned int) fbo : 0u;
}

void vr_target_get_size( int * w, int * h )
{
	if( w ) *w = fbo_w;
	if( h ) *h = fbo_h;
}

#else // no GL render path to hang this off

bool vr_target_init( int width, int height )
{
	(void) width; (void) height;
	if( vr_enabled )
	{
		Msg( "vr_target: this build has no GL>=2 render path; -vr ignored\n" );
		vr_enabled = false;
	}
	return true;
}
void vr_target_cleanup( void )     {}
void vr_target_begin_frame( void ) {}
void vr_target_end_frame( void )   {}
unsigned int vr_target_fbo_id( void ) { return 0u; }
void vr_target_get_size( int * w, int * h ) { if(w) *w=0; if(h) *h=0; }

#endif
