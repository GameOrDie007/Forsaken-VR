/* Forsaken VR: OpenXR session, views and input
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
#include "vr_openxr.h"
#include "vr_target.h"

bool vr_xr_ready       = false;
int  vr_xr_view_width  = 0;
int  vr_xr_view_height = 0;

vr_eye_view_t vr_eye[VR_EYE_COUNT];
bool          vr_views_valid   = false;
bool          vr_eyes_rendered = false;

// Forsaken world units per metre. The ships are roughly 20 units across and
// read as a few metres of vehicle, so ~30 is the starting guess. This is the
// number that decides whether the world feels the right size, and it is very
// hard to get right except by wearing the headset: hence -vrscale.
float vr_world_scale = 30.0f;

int vr_res_scale = 100;

float vr_chase_dist = 0.0f;
float vr_head_yaw   = 0.0f;
float vr_head_pitch = 0.0f;
static float xr_head_raw[3] = { 0.0f, 0.0f, 0.0f };   /* head centre, runtime space */
bool  vr_panel_frame = false;
int   vr_current_eye = 0;

/* Menu panel size and distance, both in metres. 2.2 m at 1.75 m was confirmed
   in the headset as the right size for the main menu: do not "fix" it again.
   (It was briefly reduced to 1.6/2.0 on the theory that the panel was too big;
   it is not. The pause menu looking too large is its own 2D layout, not this.)
   Still cli options because only wearing it can settle them: -vrpanel and
   -vrpaneldist. */
float vr_panel_width = 2.2f;
float vr_panel_dist  = 1.75f;

/* What the quad shows this frame; see vr_openxr.h. */
int   vr_panel_kind       = VR_PANEL_MENU;
int   vr_panel_kind_shown = VR_PANEL_MENU;
float vr_title_panel_dist = 1.75f;

/* The HUD screen's half width over its distance: the box the HUD has always
   been laid out in (VR_HUD_BOX of the eye's width), so it keeps the size it
   had when it was drawn into the eyes. */
float vr_hud_half_tan_x( void )
{
	float t = vr_eye[0].tan_right > -vr_eye[0].tan_left ? vr_eye[0].tan_right : -vr_eye[0].tan_left;
	if( t < 0.2f || t > 5.0f )
		t = 1.0f;
	return VR_HUD_BOX * t;
}

#ifdef VR_OPENXR

#include "gl_headers.h"

#ifdef _WIN32
  #include <windows.h>
  #define XR_USE_PLATFORM_WIN32
#endif
#define XR_USE_GRAPHICS_API_OPENGL

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <string.h>
#include <math.h>
#include <SDL.h>   /* the performance counter, for the timing line */

#define VR_MAX_EYES 2

static XrInstance  xr_instance = XR_NULL_HANDLE;
static XrSystemId  xr_system   = XR_NULL_SYSTEM_ID;

// session state (Phase 3b): declared here so shutdown below can reach them
bool vr_xr_session_running = false;

/* Headless measurement mode (-fakeeye). Renders the whole VR path into an
   eye-sized offscreen target with NO OpenXR runtime, so the geometry the
   headset would get can be captured and measured at a desk. Every entry point
   below returns immediately when this is set: there is no instance, no
   session and no swapchain to talk to. */
bool vr_headless = false;

/* -fakeyaw:D: turn the synthetic head D degrees, to prove a view follows it. */
float vr_fake_yaw = 0.0f;
float vr_fake_yaw_rate = 0.0f;   /* -fakeyawrate:D, degrees a second: a head that keeps turning left */
float vr_fake_roll = 0.0f;   /* -fakeroll:D, the head tilted D degrees */
bool  vr_fake_asym = false;  /* -fakeasym: VDXR's Quest 3 tangents, not a symmetric frustum */
float vr_hud_anchor = 1.0f;

/* In-world pause menu: with a menu open in game, the world stays drawn in
   stereo and follows the head, and the menu hangs on its own panel in front of
   it: rather than the whole mono frame, world included, pasted onto a flat
   screen in a black void. -vrworldmenu:0 / VRWorldMenu = false goes back. */
/* Why VR was abandoned, for the one-line summary. Empty while all is well. */
static char vr_flat_reason[256] = "";

bool  vr_world_menu = true;

/* Photo mode: with the pause menu up, a left-stick click hides it (menu and
   everything else 2D) so the frozen world can be looked around and captured
   with the headset's own screenshot. Another click brings it back. It does not
   unpause, and it swallows every other button while the menu is hidden. */
bool  vr_photo_mode = false;

/* The Leftorium (left-handed play) and Swap sticks. VRLeftorium / VRSwapSticks,
   or -leftorium:1 / -swapsticks:1. Both off by default. */
bool  vr_leftorium   = false;
bool  vr_swap_sticks = false;

/* Left-handed play, as a permutation of what the controllers already report.

   Every physical control is bound to exactly one action, so mirroring the
   hands is a swap of values after reading: switchable at any moment, with no
   rebinding and no restart. It works on the BUTTONS, before the Controls page
   maps them to game actions, so a remapped button mirrors too.

   - The Leftorium mirrors everything that belongs to a HAND (triggers,
     grips, and on Touch the face buttons (right A/B <-> left X/Y)) so
     whatever the game asks of the right hand comes from the left. Not the menu
     button: it exists on the left controller only, and would otherwise be
     lost. Not the Steam Frame's face buttons: all four are on its right
     controller, so there is nothing to mirror them to.
   - Swap sticks swaps the sticks, axes AND clicks. It is a separate switch
     because the sticks do NOT go with the hands: left-handed players asked
     still move with the left stick, as most games with a left-handed mode
     have it. Each works with or without the other.

   Pure, so vr_hand_swap_selftest() can drive it with synthetic presses. */
static void swap_b( bool * a, bool * b ) { bool t = *a; *a = *b; *b = t; }

static void vr_apply_hand_swaps( vr_input_t * in, bool leftorium, bool swap_sticks,
                                 bool frame )
{
	float f;

	if( leftorium )
	{
		f = in->fire1; in->fire1 = in->fire2; in->fire2 = f;
		swap_b( &in->slide_up, &in->slide_down );
		if( !frame )
		{
			swap_b( &in->pad[VRB_PAD_A], &in->pad[VRB_PAD_X] );
			swap_b( &in->pad[VRB_PAD_B], &in->pad[VRB_PAD_Y] );
		}
	}
	if( swap_sticks )
	{
		f = in->move_x; in->move_x = in->look_x; in->look_x = f;
		f = in->move_y; in->move_y = in->look_y; in->look_y = f;
		swap_b( &in->pad[VRB_PAD_LSTICK], &in->pad[VRB_PAD_RSTICK] );
	}
}

/* After dying, the game respawns you on "any key released" (AnyKeyReleased,
   controls.c): keyboard, mouse or a gamepad. A headset player has only the
   controllers, which reach none of those, so a release of any of their
   buttons, triggers or grips counts as well. Edge-detected once a frame. */
bool vr_any_button_released = false;
static void any_release_update( void )
{
	static bool was = false;
	bool now = vr_input.fire1 > 0.5f || vr_input.fire2 > 0.5f || vr_input.slide_up || vr_input.slide_down;
	int  i;
	for( i = 0; i < VRB_PAD_COUNT; i++ )
		if( vr_input.pad[i] )
			now = true;
	vr_any_button_released = was && !now;
	was = now;
}

void vr_input_resolve( vr_input_t * in )
{
	static bool sl_was = false, sr_was = false;
	bool sl = in->pad[VRB_PAD_LSTICK], sr = in->pad[VRB_PAD_RSTICK];

	vrb_resolve( in->pad, in->act, in->act_edge );
	in->nitro       = in->act[VRB_ACT_NITRO];
	in->mine        = in->act[VRB_ACT_MINE];
	in->roll_left   = in->act[VRB_ACT_ROLL_LEFT];
	in->roll_right  = in->act[VRB_ACT_ROLL_RIGHT];
	/* Menus keep A to choose and B to go back whatever A and B do in flight
	   (mirrored to X and Y with the Leftorium on Touch, as always). */
	in->menu_select = in->pad[VRB_PAD_A];
	in->menu_back   = in->pad[VRB_PAD_B];
	in->stick_l_edge = sl && !sl_was;  sl_was = sl;
	in->stick_r_edge = sr && !sr_was;  sr_was = sr;
}

/* -testhands: press each physical control on its own, through the same
   permutation and the same default maps the controllers go through, and check
   where it lands. The feature list's proof, "pull each physical trigger and
   check only the left one fires", done for every control in all four
   combinations on Touch, and on the Steam Frame with the Leftorium off and on.
   -testhandsbroken leaves out the Touch face-button mirror: the positive
   control, which must fail. */
static const char * selftest_got( const vr_input_t * in )
{
	const char * got = "nothing";
	int a;
	if( in->fire1 > 0.5f )  got = "fire primary";
	if( in->fire2 > 0.5f )  got = "fire missiles";
	if( in->slide_up )      got = "slide up";
	if( in->slide_down )    got = "slide down";
	if( in->move_y > 0.5f ) got = "move";
	if( in->look_y > 0.5f ) got = "look";
	for( a = 0; a < VRB_ACT_COUNT; a++ )
		if( in->act[a] )    got = vrb_act_label( a );
	return got;
}

void vr_hand_swap_selftest( bool sabotage )
{
	static const char * names[] = { "R trigger", "L trigger", "R grip", "L grip",
	                                "A", "B", "X", "Y", "L stick", "R stick",
	                                "L click", "R click" };
	static const int touch_pad[12] = { 0, 0, 0, 0, VRB_PAD_A, VRB_PAD_B, VRB_PAD_X,
	                                   VRB_PAD_Y, 0, 0, VRB_PAD_LSTICK, VRB_PAD_RSTICK };
	bool keep_frame = vrb_frame, keep_defaults = vrb_force_defaults;
	int  mode, ctl, fails = 0, checks = 0;

	vrb_force_defaults = true;

	/* Touch: 12 controls x Leftorium x Swap sticks */
	vrb_frame = false;
	for( mode = 0; mode < 4; mode++ )
	{
		bool lo = ( mode & 1 ) != 0, ss = ( mode & 2 ) != 0;
		for( ctl = 0; ctl < 12; ctl++ )
		{
			vr_input_t in;
			const char * got, * want;

			memset( &in, 0, sizeof(in) );
			switch( ctl )             /* one PHYSICAL control pressed */
			{
			case 0:  in.fire1 = 1.0f;      break;
			case 1:  in.fire2 = 1.0f;      break;
			case 2:  in.slide_up = true;   break;
			case 3:  in.slide_down = true; break;
			case 8:  in.move_y = 1.0f;     break;
			case 9:  in.look_y = 1.0f;     break;
			default: in.pad[ touch_pad[ctl] ] = true; break;
			}
			vr_apply_hand_swaps( &in, lo, ss, false );
			if( sabotage && lo )   /* undo the face-button mirror */
			{
				swap_b( &in.pad[VRB_PAD_A], &in.pad[VRB_PAD_X] );
				swap_b( &in.pad[VRB_PAD_B], &in.pad[VRB_PAD_Y] );
			}
			vr_input_resolve( &in );
			got = selftest_got( &in );

			/* what a correct build must do, written out rather than derived */
			{
				static const char * plain[12] = { "fire primary", "fire missiles",
					"slide up", "slide down", "Nitro", "Drop mine", "Roll left",
					"Roll right", "move", "look", "Next primary", "Pull-back view" };
				static const char * mirrored[12] = { "fire missiles", "fire primary",
					"slide down", "slide up", "Roll left", "Roll right", "Nitro",
					"Drop mine", "move", "look", "Next primary", "Pull-back view" };
				want = lo ? mirrored[ctl] : plain[ctl];
				if( ss && ctl >= 8 )
				{
					static const char * swapped[4] = { "look", "move",
						"Pull-back view", "Next primary" };
					want = swapped[ctl - 8];
				}
			}
			checks++;
			if( strcmp( got, want ) )
			{
				fails++;
				DebugPrintf( "handtest FAIL Quest leftorium=%d swapsticks=%d: %-9s -> %s, wanted %s\n",
				             lo, ss, names[ctl], got, want );
			}
		}
	}

	/* Steam Frame: every button, Leftorium off and on: the buttons stay put */
	vrb_frame = true;
	for( mode = 0; mode < 2; mode++ )
	{
		/* pads: none, A, B, X, Y, L click, R click, D-pad up / down / left /
		   right, L shoulder, R shoulder, as the Quest where the buttons match */
		static const char * want_frame[VRB_PAD_COUNT] = { "nothing",
			"Nitro", "Drop mine", "nothing", "nothing",
			"Next primary", "Pull-back view",
			"Rear-view mirror", "nothing", "nothing", "Next secondary",
			"Roll left", "Roll right" };
		int pad;
		for( pad = 1; pad < VRB_PAD_COUNT; pad++ )
		{
			vr_input_t in;
			const char * got;
			memset( &in, 0, sizeof(in) );
			in.pad[pad] = true;
			vr_apply_hand_swaps( &in, mode != 0, false, true );
			vr_input_resolve( &in );
			got = selftest_got( &in );
			checks++;
			if( strcmp( got, want_frame[pad] ) )
			{
				fails++;
				DebugPrintf( "handtest FAIL Frame leftorium=%d: %s -> %s, wanted %s\n",
				             mode, vrb_pad_label( pad, true ), got, want_frame[pad] );
			}
		}
	}

	vrb_frame = keep_frame;
	vrb_force_defaults = keep_defaults;
	DebugPrintf( "handtest%s: %d of %d checks failed\n",
	             sabotage ? " (sabotaged -- these SHOULD fail)" : "", fails, checks );
}

/* Headless dumps for -fakeeye: when set for one frame, each eye and the panel
   are written out as they would be handed to the compositor, so the in-world
   menu can be checked (world in both eyes, menu alone on the panel) with no
   headset. Set by the probe on its shot frame. */
bool  vr_dump_frame = false;

static void dump_target( const char * path )
{
	int    w = 0, h = 0, y;
	GLuint fbo = (GLuint) vr_target_fbo_id();
	unsigned char * pix;
	FILE * f;

	vr_target_get_size( &w, &h );
	if( !fbo || w <= 0 || h <= 0 )
		return;
	pix = (unsigned char *) malloc( (size_t) w * h * 3 );
	if( !pix )
		return;
	glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( 0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pix );
	f = fopen( path, "wb" );
	if( f )
	{
		fprintf( f, "P6\n%d %d\n255\n", w, h );
		for( y = h - 1; y >= 0; y-- )
			fwrite( pix + (size_t) y * w * 3, 1, (size_t) w * 3, f );
		fclose( f );
		DebugPrintf( "openxr: dumped %s (%dx%d)\n", path, w, h );
	}
	free( pix );
}

/* Recentering.
 *
 * The reference space is created once at session start, so "forward" is
 * wherever the player happened to be looking then. Sit down turned, or let the
 * tracking drift, and there was no way to put it right without restarting,
 * which is the most conspicuous thing a VR port can be missing.
 *
 * Rather than recreate the reference space, the head pose is offset on the way
 * out: subtract where the player was standing and rotate by minus their yaw.
 * Height is deliberately NOT zeroed: standing height is real and wanted,
 * and neither is pitch or roll, because levelling those would tilt the world.
 */
static bool  rc_have  = false;
static float rc_yaw   = 0.0f;
static float rc_x     = 0.0f;
static float rc_z     = 0.0f;
static bool  rc_want  = false;

/* Predicted display time for this frame. Declared here rather than beside the
   other per-frame state because the input code below needs it for hold timing. */
static XrTime xr_display_time = 0;

void vr_recenter( void )
{
	rc_want = true;
}

/* Swivel chair; see vr_openxr.h. */
bool  vr_swivel      = false;
bool  vr_swivel_live = false;
static float sw_prev_yaw = 0.0f;   /* the raw head yaw last frame */
static bool  sw_have_prev = false;
static float sw_pending = 0.0f;    /* turned, not yet taken by the ship */
static float sw_total = 0.0f;      /* for the log line */

float vr_swivel_take( void )
{
	float t = sw_pending;
	sw_pending = 0.0f;
	return t;
}

static float quat_yaw( const float * q );
static void  rc_apply( vr_eye_view_t * e );
static void  fake_aims( void );
static void  locate_aims( void );

/* Capture a pending recenter from the RAW pose (before any previous one is
   applied, so repeated recenters do not compound) then apply to both eyes.
   Shared by the real path and by -fakeeye, so the headless instrument
   exercises the same arithmetic the headset does rather than a copy of it. */
static void rc_update( void )
{
	int eye;

	if( rc_want )
	{
		rc_want = false;
		rc_have = true;
		rc_yaw  = quat_yaw( vr_eye[0].quat );
		rc_x    = ( vr_eye[0].pos[0] + vr_eye[1].pos[0] ) * 0.5f;
		rc_z    = ( vr_eye[0].pos[2] + vr_eye[1].pos[2] ) * 0.5f;
		DebugPrintf( "openxr: recentered, yaw %.1f deg at (%.2f, %.2f)\n",
		             rc_yaw * 57.29578f, rc_x, rc_z );
	}

	/* Swivel chair: the head's turn since last frame goes out of the view and
	   into the ship. Measured on the RAW pose, so it is the body turning in
	   the room, not anything we did. Only while flying (the game says so each
	   frame); in a menu the turn is simply let be, so the frozen world stays
	   still around the head. A jump of more than half a radian in one frame is
	   tracking, not a person, and is let be too. */
	{
		float now = quat_yaw( vr_eye[0].quat );
		/* How far up or down the head looks: the forward vector's height.
		   Yaw read from a pose is unreliable looking steeply up or down (a
		   small nod or tilt reads as a large turn), so the swivel eases out
		   between 45 and 60 degrees of elevation and stops beyond. */
		const float * q = vr_eye[0].quat;
		float up_dot = fabsf( 2.0f * ( q[3] * q[0] - q[1] * q[2] ) );
		float weight = ( 0.866f - up_dot ) / ( 0.866f - 0.707f );
		if( weight < 0.0f ) weight = 0.0f;
		if( weight > 1.0f ) weight = 1.0f;
		bool  live = vr_swivel && vr_swivel_live;

		vr_swivel_live = false;   /* the game sets it again while it applies */
		if( live && sw_have_prev )
		{
			float d = now - sw_prev_yaw;
			while( d >  3.14159265f ) d -= 6.2831853f;
			while( d < -3.14159265f ) d += 6.2831853f;
			d *= weight;
			if( fabsf( d ) < 0.5f && fabsf( d ) > 0.00001f )
			{
				if( !rc_have )
				{
					rc_have = true;
					rc_yaw = 0.0f; rc_x = 0.0f; rc_z = 0.0f;
				}
				rc_yaw     += d;
				sw_pending += d;
				sw_total   += d;
			}
		}
		sw_prev_yaw  = now;
		sw_have_prev = true;

		{
			static unsigned int last_ms = 0;
			unsigned int ms = SDL_GetTicks();
			if( vr_swivel && ms - last_ms > 10000 )
			{
				last_ms = ms;
				DebugPrintf( "vr: swivel chair turned the ship %.0f deg so far\n", sw_total * 57.29578f );
			}
		}
	}

	for( eye = 0; eye < VR_EYE_COUNT; eye++ )
		rc_apply( &vr_eye[eye] );
}

/* Yaw of a quaternion about +Y. */
static float quat_yaw( const float * q )
{
	return (float) atan2( 2.0 * ( (double) q[3] * q[1] + (double) q[0] * q[2] ),
	                      1.0 - 2.0 * ( (double) q[1] * q[1] + (double) q[2] * q[2] ) );
}

/* Apply the stored recenter to one eye, in place. */
static void rc_apply( vr_eye_view_t * e )
{
	float c, sn, x, z, qc, qs, bx, by, bz, bw;

	if( !rc_have )
		return;

	/* translate, then rotate about Y by -rc_yaw */
	x = e->pos[0] - rc_x;
	z = e->pos[2] - rc_z;
	c  = (float) cos( rc_yaw );
	sn = (float) sin( rc_yaw );
	e->pos[0] = x * c - z * sn;
	e->pos[2] = x * sn + z * c;

	/* orientation: q' = yaw(-rc_yaw) * q */
	qs = (float) sin( -rc_yaw * 0.5 );
	qc = (float) cos( -rc_yaw * 0.5 );
	bx = e->quat[0]; by = e->quat[1]; bz = e->quat[2]; bw = e->quat[3];
	e->quat[0] = qc * bx + qs * bz;
	e->quat[1] = qc * by + qs * bw;
	e->quat[2] = qc * bz - qs * bx;
	e->quat[3] = qc * bw - qs * by;
}

static XrSession       xr_session = XR_NULL_HANDLE;
static XrSpace         xr_space   = XR_NULL_HANDLE;
static XrSpace         xr_view_space = XR_NULL_HANDLE;   /* the head: a HUD worn like a helmet */
static XrSpace         xr_aim_space[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };   /* aim poses, left / right */
static XrSessionState  xr_state   = XR_SESSION_STATE_UNKNOWN;

static XrSwapchain     xr_swapchain[VR_MAX_EYES]   = { XR_NULL_HANDLE, XR_NULL_HANDLE };
static XrSwapchainImageOpenGLKHR * xr_images[VR_MAX_EYES] = { NULL, NULL };
static uint32_t        xr_image_count[VR_MAX_EYES] = { 0, 0 };
static int             xr_sc_width = 0, xr_sc_height = 0;

/* A second swapchain holding just the menu, submitted as an OpenXR quad
   layer. The runtime composites that as a genuine flat panel at a fixed pose
   in world space: correct stereo, depth and perspective, and it stays put
   because the runtime places it in the world rather than in the view. */
static XrSwapchain     xr_quad_swapchain = XR_NULL_HANDLE;
static XrSwapchainImageOpenGLKHR * xr_quad_images = NULL;
static uint32_t        xr_quad_image_count = 0;
static int             xr_quad_w = 2048, xr_quad_h = 1536;   /* 4:3 */
static bool            xr_quad_ready = false;
static bool            xr_quad_this_frame = false;
static XrPosef         xr_quad_pose;     /* a MENU screen's place, kept while it is up */
static bool            xr_quad_placed = false;

/* The quad as submitted this frame, whatever its kind: end_frame() and the
   pointer read these. */
static XrPosef         xr_shown_pose;
static XrSpace         xr_shown_space = XR_NULL_HANDLE;
static float           xr_shown_w = 2.2f, xr_shown_dist = 1.75f;
static bool            xr_shown_keyed = false;
static bool            xr_shown_ok = false;

/* Frames since tracking began. Virtual Desktop reports a placeholder head
   pose for the first few frames, and a screen placed from it hung down by the
   floor. Until VR_SETTLE_FRAMES real frames have passed, the menu screen is
   placed afresh every frame, so it settles where the head really is. */
#define VR_SETTLE_FRAMES 30
#define VR_GRIP_CHORD_FRAMES 8   /* a lone grip waits this long for the other (the recenter chord) */
static int             xr_settle_frames = 0;

/* Consecutive frames with no quad submitted. Past VR_QUAD_IDLE_RESET the menu
   counts as closed and the next one is placed afresh. ~40 frames is half a
   second at 72-90 Hz: far longer than the world/menu alternation while
   dying, far shorter than any real gap between menus. */
#define VR_QUAD_IDLE_RESET 40
static int             xr_quad_idle_frames = VR_QUAD_IDLE_RESET;

// scratch FBO used as the blit destination when copying into a swapchain image
static GLuint xr_blit_fbo = 0;

// The loader only guarantees the core entry points; extension functions have
// to be fetched through xrGetInstanceProcAddr.
static PFN_xrGetOpenGLGraphicsRequirementsKHR pfn_get_gl_requirements = NULL;

// Turns an XrResult into something readable. xrResultToString needs a live
// instance, and the most interesting failures happen before we have one, so
// the common codes are spelled out by hand as well.
static const char * xr_str( XrResult r )
{
	static char buf[XR_MAX_RESULT_STRING_SIZE];

	if( xr_instance != XR_NULL_HANDLE &&
	    XR_SUCCEEDED( xrResultToString( xr_instance, r, buf ) ) )
		return buf;

	switch( r )
	{
	case XR_ERROR_VALIDATION_FAILURE:      return "XR_ERROR_VALIDATION_FAILURE";
	case XR_ERROR_RUNTIME_FAILURE:         return "XR_ERROR_RUNTIME_FAILURE";
	case XR_ERROR_OUT_OF_MEMORY:           return "XR_ERROR_OUT_OF_MEMORY";
	case XR_ERROR_API_VERSION_UNSUPPORTED: return "XR_ERROR_API_VERSION_UNSUPPORTED";
	case XR_ERROR_INITIALIZATION_FAILED:   return "XR_ERROR_INITIALIZATION_FAILED";
	case XR_ERROR_FUNCTION_UNSUPPORTED:    return "XR_ERROR_FUNCTION_UNSUPPORTED";
	case XR_ERROR_FEATURE_UNSUPPORTED:     return "XR_ERROR_FEATURE_UNSUPPORTED";
	case XR_ERROR_EXTENSION_NOT_PRESENT:   return "XR_ERROR_EXTENSION_NOT_PRESENT";
	case XR_ERROR_FORM_FACTOR_UNSUPPORTED: return "XR_ERROR_FORM_FACTOR_UNSUPPORTED";
	case XR_ERROR_FORM_FACTOR_UNAVAILABLE: return "XR_ERROR_FORM_FACTOR_UNAVAILABLE";
	case XR_ERROR_RUNTIME_UNAVAILABLE:     return "XR_ERROR_RUNTIME_UNAVAILABLE";
	default: break;
	}

	snprintf( buf, sizeof(buf), "XrResult %d", (int) r );
	return buf;
}

static bool has_extension( const XrExtensionProperties * props, uint32_t count,
                           const char * name )
{
	uint32_t i;
	for( i = 0; i < count; i++ )
		if( strcmp( props[i].extensionName, name ) == 0 )
			return true;
	return false;
}

#define XR_VALVE_FRAME_EXT "XR_VALVE_frame_controller_interaction"
static bool xr_has_frame_ext = false;

static bool log_available_extensions( bool * out_has_opengl )
{
	XrExtensionProperties * props = NULL;
	uint32_t count = 0, i;
	XrResult r;

	*out_has_opengl = false;

	r = xrEnumerateInstanceExtensionProperties( NULL, 0, &count, NULL );
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: could not enumerate extensions: %s\n", xr_str(r) );
		Msg( "openxr: this usually means no OpenXR runtime is installed or "
		     "the loader could not be reached.\n" );
		return false;
	}

	props = (XrExtensionProperties *) malloc( count * sizeof(*props) );
	if( !props )
		return false;
	for( i = 0; i < count; i++ )
	{
		memset( &props[i], 0, sizeof(props[i]) );
		props[i].type = XR_TYPE_EXTENSION_PROPERTIES;
	}

	r = xrEnumerateInstanceExtensionProperties( NULL, count, &count, props );
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: could not read extensions: %s\n", xr_str(r) );
		free( props );
		return false;
	}

	DebugPrintf( "openxr: %u instance extensions available\n", count );
	for( i = 0; i < count; i++ )
		DebugPrintf( "openxr:   %s (v%u)\n",
		             props[i].extensionName, props[i].extensionVersion );

	*out_has_opengl = has_extension( props, count, XR_KHR_OPENGL_ENABLE_EXTENSION_NAME );
	/* Requesting an extension the runtime lacks fails xrCreateInstance
	   outright, so the Frame's profile is asked for only when it is listed. */
	xr_has_frame_ext = has_extension( props, count, XR_VALVE_FRAME_EXT );
	free( props );
	return true;
}

#ifdef _WIN32
#include <tlhelp32.h>

/* Which OpenXR runtime this launch uses. Windows has one active runtime
   (the registry's ActiveRuntime), and on a PC that also has a Quest it is
   usually Virtual Desktop's, which answers "no headset" while a Steam Frame
   is on, because the Frame runs through SteamVR. A game launched from Steam
   is handed SteamVR's runtime in XR_RUNTIME_JSON; one started from its own
   folder or a shortcut is not. So: when SteamVR is running and nothing has
   chosen a runtime, this launch uses SteamVR's. Only this process's
   environment changes, never the system setting, and with SteamVR not
   running nothing changes at all. */
static bool steamvr_dir_from_vrserver( char * dir, size_t size )
{
	HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
	PROCESSENTRY32 pe;
	bool found = false;

	if( snap == INVALID_HANDLE_VALUE )
		return false;
	pe.dwSize = sizeof( pe );
	if( Process32First( snap, &pe ) )
	{
		do
		{
			if( _stricmp( pe.szExeFile, "vrserver.exe" ) == 0 )
			{
				HANDLE h = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID );
				DWORD  n = (DWORD) size;
				found = true;   /* running, even if its path cannot be read */
				dir[0] = 0;
				if( h )
				{
					if( QueryFullProcessImageNameA( h, 0, dir, &n ) )
					{
						/* ...\SteamVR\bin\win64\vrserver.exe: up three levels */
						int up;
						for( up = 0; up < 3; up++ )
						{
							char * slash = strrchr( dir, '\\' );
							if( !slash )
							{
								dir[0] = 0;
								break;
							}
							*slash = 0;
						}
					}
					else
						dir[0] = 0;
					CloseHandle( h );
				}
				break;
			}
		} while( Process32Next( snap, &pe ) );
	}
	CloseHandle( snap );
	return found;
}

static bool rt_picked = false;   /* this launch chose SteamVR's runtime itself */

/* SteamVR was running but had no headset (left open after a Frame session,
   say, while the player is on a Quest through Virtual Desktop): forget the
   choice, so the next try gets the system's runtime as it always did. */
static bool unpick_runtime( void )
{
	if( !rt_picked )
		return false;
	rt_picked = false;
	SetEnvironmentVariableA( "XR_RUNTIME_JSON", NULL );
	_putenv( "XR_RUNTIME_JSON=" );
	return true;
}

static bool file_exists( const char * path )
{
	DWORD a = GetFileAttributesA( path );
	return a != INVALID_FILE_ATTRIBUTES && !( a & FILE_ATTRIBUTE_DIRECTORY );
}

static void pick_runtime( void )
{
	char dir[ MAX_PATH ] = "", json[ MAX_PATH + 32 ] = "";
	char cur[ 8 ];
	DWORD n;

	if( GetEnvironmentVariableA( "XR_RUNTIME_JSON", cur, sizeof( cur ) ) > 0 )
	{
		DebugPrintf( "openxr: runtime chosen by XR_RUNTIME_JSON (from Steam or the launcher)\n" );
		return;
	}
	if( !steamvr_dir_from_vrserver( dir, sizeof( dir ) ) )
	{
		DebugPrintf( "openxr: SteamVR is not running; using the system's OpenXR runtime\n" );
		return;
	}
	if( dir[0] )
		snprintf( json, sizeof( json ), "%s\\steamxr_win64.json", dir );
	if( !json[0] || !file_exists( json ) )
	{
		/* its path could not be read: Steam's own folder, where SteamVR lives */
		HKEY key;
		json[0] = 0;
		if( RegOpenKeyExA( HKEY_CURRENT_USER, "Software\\Valve\\Steam", 0, KEY_READ, &key ) == ERROR_SUCCESS )
		{
			n = sizeof( dir );
			if( RegQueryValueExA( key, "SteamPath", NULL, NULL, (LPBYTE) dir, &n ) == ERROR_SUCCESS )
			{
				dir[ sizeof( dir ) - 1 ] = 0;
				snprintf( json, sizeof( json ), "%s\\steamapps\\common\\SteamVR\\steamxr_win64.json", dir );
			}
			RegCloseKey( key );
		}
	}
	if( !json[0] || !file_exists( json ) )
	{
		DebugPrintf( "openxr: SteamVR is running but its runtime file was not found; using the system's OpenXR runtime\n" );
		return;
	}
	SetEnvironmentVariableA( "XR_RUNTIME_JSON", json );
	rt_picked = true;
	{
		char env[ MAX_PATH + 64 ];
		snprintf( env, sizeof( env ), "XR_RUNTIME_JSON=%s", json );
		_putenv( env );
	}
	DebugPrintf( "openxr: SteamVR is running, so this launch uses its runtime: %s\n", json );
}
#else
static void pick_runtime( void ) {}
static bool unpick_runtime( void ) { return false; }
#endif

bool vr_openxr_init( void )
{
	if( vr_headless ) return false;

	XrInstanceCreateInfo       create   = { XR_TYPE_INSTANCE_CREATE_INFO };
	XrInstanceProperties       instprop = { XR_TYPE_INSTANCE_PROPERTIES };
	XrSystemGetInfo            sysget   = { XR_TYPE_SYSTEM_GET_INFO };
	XrSystemProperties         sysprop  = { XR_TYPE_SYSTEM_PROPERTIES };
	XrGraphicsRequirementsOpenGLKHR glreq = { XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
	XrViewConfigurationView    views[2];
	XrEnvironmentBlendMode     blend[8];
	const char *               enabled_ext[2];
	uint32_t                   view_count = 0, blend_count = 0, i;
	bool                       has_opengl = false;
	XrResult                   r;

	if( !vr_enabled )
		return true;

	vr_xr_ready = false;

	pick_runtime();

retry_runtime:
	if( !log_available_extensions( &has_opengl ) )
		goto fail;

	if( !has_opengl )
	{
		Msg( "openxr: the runtime does not support %s, which we need to hand "
		     "it OpenGL textures.\n", XR_KHR_OPENGL_ENABLE_EXTENSION_NAME );
		goto fail;
	}

	enabled_ext[0] = XR_KHR_OPENGL_ENABLE_EXTENSION_NAME;
	enabled_ext[1] = XR_VALVE_FRAME_EXT;

	strcpy( create.applicationInfo.applicationName, "Forsaken VR" );
	strcpy( create.applicationInfo.engineName,      "ProjectX" );
	create.applicationInfo.applicationVersion = 1;
	create.applicationInfo.engineVersion      = 1;
	create.enabledExtensionCount              = xr_has_frame_ext ? 2 : 1;
	if( xr_has_frame_ext )
		DebugPrintf( "openxr: Steam Frame controller profile available (%s)\n", XR_VALVE_FRAME_EXT );
	create.enabledExtensionNames              = enabled_ext;

	// Ask for the SDK's version first, then fall back to 1.0. The SDK we build
	// against is OpenXR 1.1, but VDXR (and several other runtimes) still only
	// implement 1.0 and reject a 1.1 request outright with
	// XR_ERROR_API_VERSION_UNSUPPORTED. Nothing we use is 1.1-only.
	create.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
	r = xrCreateInstance( &create, &xr_instance );

	if( r == XR_ERROR_API_VERSION_UNSUPPORTED )
	{
		DebugPrintf( "openxr: runtime rejected API %u.%u, retrying as 1.0\n",
			(unsigned) XR_VERSION_MAJOR( XR_CURRENT_API_VERSION ),
			(unsigned) XR_VERSION_MINOR( XR_CURRENT_API_VERSION ) );
		create.applicationInfo.apiVersion = XR_API_VERSION_1_0;
		r = xrCreateInstance( &create, &xr_instance );
	}

	if( XR_FAILED( r ) )
	{
		xr_instance = XR_NULL_HANDLE;
		Msg( "openxr: xrCreateInstance failed: %s\n", xr_str(r) );
		snprintf( vr_flat_reason, sizeof(vr_flat_reason), "the OpenXR runtime would not start (%s)", xr_str(r) );
		goto fail;
	}

	DebugPrintf( "openxr: instance created against API %u.%u.%u\n",
		(unsigned) XR_VERSION_MAJOR( create.applicationInfo.apiVersion ),
		(unsigned) XR_VERSION_MINOR( create.applicationInfo.apiVersion ),
		(unsigned) XR_VERSION_PATCH( create.applicationInfo.apiVersion ) );

	if( XR_SUCCEEDED( xrGetInstanceProperties( xr_instance, &instprop ) ) )
	{
		DebugPrintf( "openxr: runtime '%s' version %u.%u.%u\n",
			instprop.runtimeName,
			(unsigned) XR_VERSION_MAJOR( instprop.runtimeVersion ),
			(unsigned) XR_VERSION_MINOR( instprop.runtimeVersion ),
			(unsigned) XR_VERSION_PATCH( instprop.runtimeVersion ) );
	}

	// A missing headset shows up here rather than at instance creation.
	sysget.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
	r = xrGetSystem( xr_instance, &sysget, &xr_system );
	if( XR_FAILED( r ) && unpick_runtime() )
	{
		/* the loader lets the runtime go with the last instance, so the next
		   call loads the system's runtime */
		DebugPrintf( "openxr: SteamVR has no headset (%s); trying the system's OpenXR runtime instead\n", xr_str(r) );
		xrDestroyInstance( xr_instance );
		xr_instance = XR_NULL_HANDLE;
		goto retry_runtime;
	}
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: no head-mounted system available: %s\n", xr_str(r) );
		snprintf( vr_flat_reason, sizeof(vr_flat_reason), "no headset available (%s) -- is it connected and streaming?", xr_str(r) );
		Msg( "openxr: check the headset is connected before launching: for a "
		     "Steam Frame, SteamVR running; for a Quest, Virtual Desktop streaming.\n" );
		goto fail;
	}

	if( XR_SUCCEEDED( xrGetSystemProperties( xr_instance, xr_system, &sysprop ) ) )
	{
		DebugPrintf( "openxr: system '%s' (vendor %u)\n",
			sysprop.systemName, (unsigned) sysprop.vendorId );
		DebugPrintf( "openxr: max swapchain %ux%u, %u layers\n",
			(unsigned) sysprop.graphicsProperties.maxSwapchainImageWidth,
			(unsigned) sysprop.graphicsProperties.maxSwapchainImageHeight,
			(unsigned) sysprop.graphicsProperties.maxLayerCount );
		DebugPrintf( "openxr: orientation tracking=%d, position tracking=%d\n",
			(int) sysprop.trackingProperties.orientationTracking,
			(int) sysprop.trackingProperties.positionTracking );
	}

	// What OpenGL version does the runtime demand? This is the check most
	// likely to bite: our context is 3.2 core, and some runtimes require 4.x.
	r = xrGetInstanceProcAddr( xr_instance, "xrGetOpenGLGraphicsRequirementsKHR",
	                           (PFN_xrVoidFunction *) &pfn_get_gl_requirements );
	if( XR_SUCCEEDED( r ) && pfn_get_gl_requirements )
	{
		r = pfn_get_gl_requirements( xr_instance, xr_system, &glreq );
		if( XR_SUCCEEDED( r ) )
		{
			DebugPrintf( "openxr: requires OpenGL %u.%u to %u.%u\n",
				(unsigned) XR_VERSION_MAJOR( glreq.minApiVersionSupported ),
				(unsigned) XR_VERSION_MINOR( glreq.minApiVersionSupported ),
				(unsigned) XR_VERSION_MAJOR( glreq.maxApiVersionSupported ),
				(unsigned) XR_VERSION_MINOR( glreq.maxApiVersionSupported ) );
			DebugPrintf( "openxr: our context reports '%s'\n",
			             (const char *) glGetString( GL_VERSION ) );
		}
		else
			DebugPrintf( "openxr: graphics requirements query failed: %s\n", xr_str(r) );
	}
	else
		DebugPrintf( "openxr: xrGetOpenGLGraphicsRequirementsKHR unavailable\n" );

	// Per-eye render resolution the runtime would like us to use.
	for( i = 0; i < 2; i++ )
	{
		memset( &views[i], 0, sizeof(views[i]) );
		views[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;
	}
	r = xrEnumerateViewConfigurationViews( xr_instance, xr_system,
	        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 2, &view_count, views );
	if( XR_FAILED( r ) || view_count < 2 )
	{
		Msg( "openxr: could not get stereo view configuration: %s\n", xr_str(r) );
		snprintf( vr_flat_reason, sizeof(vr_flat_reason), "the runtime offered no stereo view (%s)", xr_str(r) );
		goto fail;
	}

	vr_xr_view_width  = (int) views[0].recommendedImageRectWidth;
	vr_xr_view_height = (int) views[0].recommendedImageRectHeight;

	for( i = 0; i < view_count; i++ )
		DebugPrintf( "openxr: view %u recommended %ux%u (max %ux%u), "
		             "%u samples\n", i,
			(unsigned) views[i].recommendedImageRectWidth,
			(unsigned) views[i].recommendedImageRectHeight,
			(unsigned) views[i].maxImageRectWidth,
			(unsigned) views[i].maxImageRectHeight,
			(unsigned) views[i].recommendedSwapchainSampleCount );

	r = xrEnumerateEnvironmentBlendModes( xr_instance, xr_system,
	        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO, 8, &blend_count, blend );
	if( XR_SUCCEEDED( r ) )
		for( i = 0; i < blend_count; i++ )
			DebugPrintf( "openxr: blend mode %u = %d\n", i, (int) blend[i] );

	vr_xr_ready = true;
	DebugPrintf( "openxr: instance and system ready, per-eye target %dx%d\n",
	             vr_xr_view_width, vr_xr_view_height );
	return true;

fail:
	/* One line that says why, so a player's log answers "why is it flat?"
	   without reading the lines above it. */
	Msg( "openxr: running FLAT because %s\n",
	     vr_flat_reason[0] ? vr_flat_reason : "VR bring-up failed (see the lines above)" );
	vr_openxr_shutdown();
	vr_enabled = false;
	return false;
}

/* What the runtime was handed and whether it took it, for the shutdown line.
   Two layers is the in-world menu: the stereo world with the menu screen over
   it. A desk cannot see what a headset shows, but it can see the runtime
   accept every frame of it. */
static unsigned long xr_ended[3]    = { 0, 0, 0 };   /* by layer count */
static unsigned long xr_end_failed  = 0;

void vr_openxr_shutdown( void )
{
	if( vr_headless ) return;

	int eye;

	DebugPrintf( "openxr: frames ended: %lu with no layer, %lu with one, %lu with two "
	             "(world + menu screen); %lu refused\n",
	             xr_ended[0], xr_ended[1], xr_ended[2], xr_end_failed );
	for( eye = 0; eye < VR_MAX_EYES; eye++ )
	{
		if( xr_swapchain[eye] != XR_NULL_HANDLE )
		{
			xrDestroySwapchain( xr_swapchain[eye] );
			xr_swapchain[eye] = XR_NULL_HANDLE;
		}
		/* Guarded because this function runs twice on a normal quit (once
		   from CleanUpAndPostQuit() and again at the end of main()) and this
		   engine's free() wrapper logs "Tried to free NULL block" on the second.
		   Harmless, but it was the one line in a crash log that looked like a
		   second fault, and a log that cries wolf costs the next diagnosis. */
		if( xr_images[eye] )
			free( xr_images[eye] );
		xr_images[eye] = NULL;
		xr_image_count[eye] = 0;
	}
	if( xr_blit_fbo ) { glDeleteFramebuffers( 1, &xr_blit_fbo ); xr_blit_fbo = 0; }
	{
		int hnd;
		for( hnd = 0; hnd < 2; hnd++ )
			if( xr_aim_space[hnd] != XR_NULL_HANDLE ) { xrDestroySpace( xr_aim_space[hnd] ); xr_aim_space[hnd] = XR_NULL_HANDLE; }
	}
	if( xr_view_space != XR_NULL_HANDLE ) { xrDestroySpace( xr_view_space ); xr_view_space = XR_NULL_HANDLE; }
	if( xr_space   != XR_NULL_HANDLE ) { xrDestroySpace( xr_space );     xr_space   = XR_NULL_HANDLE; }
	if( xr_session != XR_NULL_HANDLE ) { xrDestroySession( xr_session ); xr_session = XR_NULL_HANDLE; }
	vr_xr_session_running = false;

	if( xr_instance != XR_NULL_HANDLE )
	{
		xrDestroyInstance( xr_instance );
		xr_instance = XR_NULL_HANDLE;
	}
	xr_system   = XR_NULL_SYSTEM_ID;
	vr_xr_ready = false;
	pfn_get_gl_requirements = NULL;
}

/* ------------------------------------------------------------------------
   Phase 3b: session, swapchains and frame submission.

   The engine still renders one monoscopic frame into vr_target. We copy that
   same image into both eye swapchains, so the headset shows the flat game
   picture. Head tracking does not move the camera yet, that is Phase 3c,
   where per-eye view and projection come from xrLocateViews.

   Known rough edge, deliberate for now: xrWaitFrame is called *after* the
   engine has rendered rather than before, because the render is driven by the
   existing main loop. That is legal ordering but poor pacing. Phase 7 moves
   the wait to the top of the frame.
   ------------------------------------------------------------------------ */

static bool create_swapchains( void )
{
	int64_t * formats = NULL;
	uint32_t  format_count = 0, i;
	int64_t   chosen = 0;
	XrResult  r;
	int       eye;

	r = xrEnumerateSwapchainFormats( xr_session, 0, &format_count, NULL );
	if( XR_FAILED( r ) || format_count == 0 )
	{
		Msg( "openxr: no swapchain formats: %s\n", xr_str(r) );
		return false;
	}
	formats = (int64_t *) malloc( format_count * sizeof(int64_t) );
	if( !formats )
		return false;
	r = xrEnumerateSwapchainFormats( xr_session, format_count, &format_count, formats );
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: could not read swapchain formats: %s\n", xr_str(r) );
		free( formats );
		return false;
	}

	// Prefer GL_SRGB8_ALPHA8.
	//
	// The bytes the engine produces are already sRGB-encoded, like any normal
	// 8-bit framebuffer. The swapchain format tells the runtime how to
	// interpret them. With a linear GL_RGBA8 swapchain the compositor reads
	// those values as if they were linear light and gamma-encodes them again
	// on the way to the display: a double conversion that lifts the midtones
	// and washes the image out, which is exactly what we saw in the headset.
	// Declaring sRGB lets it decode correctly. (Phase 3b originally chose
	// RGBA8 on the opposite, and wrong, reasoning.)
	for( i = 0; i < format_count && !chosen; i++ )
		if( formats[i] == GL_SRGB8_ALPHA8 )
			chosen = formats[i];
	if( !chosen )
		for( i = 0; i < format_count && !chosen; i++ )
			if( formats[i] == GL_RGBA8 )
				chosen = formats[i];
	if( !chosen )
		chosen = formats[0];

	DebugPrintf( "openxr: %u swapchain formats offered, using 0x%x\n",
	             format_count, (unsigned) chosen );
	free( formats );

	xr_sc_width  = vr_xr_view_width;
	xr_sc_height = vr_xr_view_height;

	for( eye = 0; eye < VR_MAX_EYES; eye++ )
	{
		XrSwapchainCreateInfo sci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		uint32_t n = 0, j;

		sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
		                  XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		sci.format      = chosen;
		sci.sampleCount = 1;
		sci.width       = xr_sc_width;
		sci.height      = xr_sc_height;
		sci.faceCount   = 1;
		sci.arraySize   = 1;
		sci.mipCount    = 1;

		r = xrCreateSwapchain( xr_session, &sci, &xr_swapchain[eye] );
		if( XR_FAILED( r ) )
		{
			Msg( "openxr: xrCreateSwapchain(eye %d) failed: %s\n", eye, xr_str(r) );
			return false;
		}

		r = xrEnumerateSwapchainImages( xr_swapchain[eye], 0, &n, NULL );
		if( XR_FAILED( r ) || n == 0 )
		{
			Msg( "openxr: no swapchain images for eye %d: %s\n", eye, xr_str(r) );
			return false;
		}
		xr_images[eye] = (XrSwapchainImageOpenGLKHR *)
		                 malloc( n * sizeof(XrSwapchainImageOpenGLKHR) );
		if( !xr_images[eye] )
			return false;
		for( j = 0; j < n; j++ )
		{
			memset( &xr_images[eye][j], 0, sizeof(xr_images[eye][j]) );
			xr_images[eye][j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
		}
		r = xrEnumerateSwapchainImages( xr_swapchain[eye], n, &n,
		        (XrSwapchainImageBaseHeader *) xr_images[eye] );
		if( XR_FAILED( r ) )
		{
			Msg( "openxr: could not read swapchain images for eye %d: %s\n",
			     eye, xr_str(r) );
			return false;
		}
		xr_image_count[eye] = n;
		DebugPrintf( "openxr: eye %d swapchain %dx%d, %u images\n",
		             eye, xr_sc_width, xr_sc_height, n );
	}

	/* the menu panel's own swapchain */
	{
		XrSwapchainCreateInfo sci = { XR_TYPE_SWAPCHAIN_CREATE_INFO };
		uint32_t n = 0, j;

		sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
		                  XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT;
		sci.format      = chosen;
		sci.sampleCount = 1;
		sci.width       = xr_quad_w;
		sci.height      = xr_quad_h;
		sci.faceCount   = 1;
		sci.arraySize   = 1;
		sci.mipCount    = 1;

		if( XR_SUCCEEDED( xrCreateSwapchain( xr_session, &sci, &xr_quad_swapchain ) ) &&
		    XR_SUCCEEDED( xrEnumerateSwapchainImages( xr_quad_swapchain, 0, &n, NULL ) ) && n )
		{
			xr_quad_images = (XrSwapchainImageOpenGLKHR *)
			                 malloc( n * sizeof(XrSwapchainImageOpenGLKHR) );
			if( xr_quad_images )
			{
				for( j = 0; j < n; j++ )
				{
					memset( &xr_quad_images[j], 0, sizeof(xr_quad_images[j]) );
					xr_quad_images[j].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR;
				}
				if( XR_SUCCEEDED( xrEnumerateSwapchainImages( xr_quad_swapchain, n, &n,
				        (XrSwapchainImageBaseHeader *) xr_quad_images ) ) )
				{
					xr_quad_image_count = n;
					xr_quad_ready = true;
					DebugPrintf( "openxr: menu quad swapchain %dx%d, %u images\n",
					             xr_quad_w, xr_quad_h, n );
				}
			}
		}
		if( !xr_quad_ready )
			Msg( "openxr: no menu quad layer; menus will fill the view\n" );
	}

	glGenFramebuffers( 1, &xr_blit_fbo );
	return true;
}


/* ------------------------------------------------------------------------
   Quest 3 controller input (OpenXR action sets).

   Actions are named intents; the runtime binds them to whatever the physical
   device offers. Bindings below are for the Oculus Touch profile, which is
   what a Quest 3 reports through VDXR. Layout mirrors the gamepad.
   ------------------------------------------------------------------------ */

vr_input_t vr_input;
bool       vr_input_active = false;

static XrActionSet xr_actions   = XR_NULL_HANDLE;
static XrAction    act_move     = XR_NULL_HANDLE;  /* left  thumbstick */
static XrAction    act_look     = XR_NULL_HANDLE;  /* right thumbstick */
static XrAction    act_fire1    = XR_NULL_HANDLE;  /* right trigger    */
static XrAction    act_fire2    = XR_NULL_HANDLE;  /* left  trigger    */
static XrAction    act_menu     = XR_NULL_HANDLE;  /* menu             */
static XrAction    act_pad[VRB_PAD_COUNT];          /* one per button; vr_binds.h */
static XrAction    act_aim[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };   /* aim poses, left / right */
vr_hand_pose_t     vr_aim[2];
static vr_hand_pose_t vr_aim_raw[2];   /* before our recenter: the menu screen's space */
bool               vr_panel_last = false;   /* a menu screen was up last frame */
float              vr_fake_aim_yaw = 0.0f, vr_fake_aim_pitch = 0.0f;
static XrAction    act_wep1     = XR_NULL_HANDLE;  /* left grip  slide down */
static XrAction    act_wep2     = XR_NULL_HANDLE;  /* right grip slide up   */
static XrAction    act_buzz_l   = XR_NULL_HANDLE;  /* left  haptic output   */
static XrAction    act_buzz_r   = XR_NULL_HANDLE;  /* right haptic output   */
static bool        xr_haptics_bound = false;

static XrPath xr_path( const char * s )
{
	XrPath p = XR_NULL_PATH;
	xrStringToPath( xr_instance, s, &p );
	return p;
}

static bool make_action( XrActionType type, const char * name,
                         const char * label, XrAction * out )
{
	XrActionCreateInfo ci = { XR_TYPE_ACTION_CREATE_INFO };
	XrResult r;

	ci.actionType = type;
	strncpy( ci.actionName,          name,  XR_MAX_ACTION_NAME_SIZE - 1 );
	strncpy( ci.localizedActionName, label, XR_MAX_LOCALIZED_ACTION_NAME_SIZE - 1 );

	r = xrCreateAction( xr_actions, &ci, out );
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: xrCreateAction(%s) failed: %s\n", name, xr_str(r) );
		return false;
	}
	return true;
}

static bool create_actions( void )
{
	XrActionSetCreateInfo asci = { XR_TYPE_ACTION_SET_CREATE_INFO };
	XrInteractionProfileSuggestedBinding sug = { XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
	XrSessionActionSetsAttachInfo attach = { XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
	XrActionSuggestedBinding binds[ 32 ];
	XrResult r;

	strcpy( asci.actionSetName,          "forsaken" );
	strcpy( asci.localizedActionSetName, "Forsaken" );
	asci.priority = 0;

	r = xrCreateActionSet( xr_instance, &asci, &xr_actions );
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: xrCreateActionSet failed: %s\n", xr_str(r) );
		return false;
	}

	if( !make_action( XR_ACTION_TYPE_VECTOR2F_INPUT, "move",  "Move",           &act_move  ) ||
	    !make_action( XR_ACTION_TYPE_VECTOR2F_INPUT, "look",  "Look",           &act_look  ) ||
	    !make_action( XR_ACTION_TYPE_FLOAT_INPUT,    "fire1", "Fire Primary",   &act_fire1 ) ||
	    !make_action( XR_ACTION_TYPE_FLOAT_INPUT,    "fire2", "Fire Secondary", &act_fire2 ) ||
	    !make_action( XR_ACTION_TYPE_BOOLEAN_INPUT,  "menu",  "Menu",           &act_menu  ) ||
	    !make_action( XR_ACTION_TYPE_BOOLEAN_INPUT,  "wep1",  "Slide Down",     &act_wep1 ) ||
	    !make_action( XR_ACTION_TYPE_BOOLEAN_INPUT,  "wep2",  "Slide Up",       &act_wep2 ) ||
	    !make_action( XR_ACTION_TYPE_POSE_INPUT,     "aiml",  "Aim Left",       &act_aim[0] ) ||
	    !make_action( XR_ACTION_TYPE_POSE_INPUT,     "aimr",  "Aim Right",      &act_aim[1] ) ||
	    !make_action( XR_ACTION_TYPE_VIBRATION_OUTPUT, "buzzl", "Vibrate Left",  &act_buzz_l ) ||
	    !make_action( XR_ACTION_TYPE_VIBRATION_OUTPUT, "buzzr", "Vibrate Right", &act_buzz_r ) )
		return false;

	/* The buttons are actions of their own, named for the button rather than
	   for a game job: what each does is the Controls page's business
	   (vr_binds.c), so a remap never touches OpenXR. Localized names must be
	   unique: two actions sharing one silently kill the second. */
	{
		static const struct { int pad; const char * name; const char * label; } pads[] = {
			{ VRB_PAD_A, "pada", "Button A" },           { VRB_PAD_B, "padb", "Button B" },
			{ VRB_PAD_X, "padx", "Button X" },           { VRB_PAD_Y, "pady", "Button Y" },
			{ VRB_PAD_LSTICK, "padls", "Left Stick Click" },
			{ VRB_PAD_RSTICK, "padrs", "Right Stick Click" },
			{ VRB_PAD_DUP, "paddu", "D-pad Up" },        { VRB_PAD_DDOWN, "paddd", "D-pad Down" },
			{ VRB_PAD_DLEFT, "paddl", "D-pad Left" },    { VRB_PAD_DRIGHT, "paddr", "D-pad Right" },
			{ VRB_PAD_LSHOULDER, "padlsh", "Left Shoulder" },
			{ VRB_PAD_RSHOULDER, "padrsh", "Right Shoulder" },
		};
		int i;
		memset( act_pad, 0, sizeof(act_pad) );
		for( i = 0; i < (int)( sizeof(pads) / sizeof(pads[0]) ); i++ )
			if( !make_action( XR_ACTION_TYPE_BOOLEAN_INPUT, pads[i].name, pads[i].label,
			                  &act_pad[ pads[i].pad ] ) )
				return false;
	}

	/* Quest Touch. The haptic outputs go last, so that dropping them is a
	   shorter count: a runtime that refused them would refuse the WHOLE
	   profile, and the controls with it, so on a refusal, try again without
	   them. No vibration is a far better failure than no controls. */
	{
		int n = 0;
		binds[n].action = act_move;   binds[n++].binding = xr_path( "/user/hand/left/input/thumbstick" );
		binds[n].action = act_look;   binds[n++].binding = xr_path( "/user/hand/right/input/thumbstick" );
		binds[n].action = act_fire1;  binds[n++].binding = xr_path( "/user/hand/right/input/trigger/value" );
		binds[n].action = act_fire2;  binds[n++].binding = xr_path( "/user/hand/left/input/trigger/value" );
		binds[n].action = act_wep1;   binds[n++].binding = xr_path( "/user/hand/left/input/squeeze/value" );
		binds[n].action = act_wep2;   binds[n++].binding = xr_path( "/user/hand/right/input/squeeze/value" );
		binds[n].action = act_menu;   binds[n++].binding = xr_path( "/user/hand/left/input/menu/click" );
		binds[n].action = act_pad[VRB_PAD_A];      binds[n++].binding = xr_path( "/user/hand/right/input/a/click" );
		binds[n].action = act_pad[VRB_PAD_B];      binds[n++].binding = xr_path( "/user/hand/right/input/b/click" );
		binds[n].action = act_pad[VRB_PAD_X];      binds[n++].binding = xr_path( "/user/hand/left/input/x/click" );
		binds[n].action = act_pad[VRB_PAD_Y];      binds[n++].binding = xr_path( "/user/hand/left/input/y/click" );
		binds[n].action = act_pad[VRB_PAD_LSTICK]; binds[n++].binding = xr_path( "/user/hand/left/input/thumbstick/click" );
		binds[n].action = act_pad[VRB_PAD_RSTICK]; binds[n++].binding = xr_path( "/user/hand/right/input/thumbstick/click" );
		binds[n].action = act_aim[0]; binds[n++].binding = xr_path( "/user/hand/left/input/aim/pose" );
		binds[n].action = act_aim[1]; binds[n++].binding = xr_path( "/user/hand/right/input/aim/pose" );
		binds[n].action = act_buzz_l; binds[n++].binding = xr_path( "/user/hand/left/output/haptic" );
		binds[n].action = act_buzz_r; binds[n++].binding = xr_path( "/user/hand/right/output/haptic" );

		sug.interactionProfile     = xr_path( "/interaction_profiles/oculus/touch_controller" );
		sug.suggestedBindings      = binds;
		sug.countSuggestedBindings = n;
		r = xrSuggestInteractionProfileBindings( xr_instance, &sug );
		xr_haptics_bound = !XR_FAILED( r );
		if( !xr_haptics_bound )
		{
			Msg( "openxr: haptic bindings refused (%s); controls without vibration\n", xr_str(r) );
			sug.countSuggestedBindings = n - 2;
			r = xrSuggestInteractionProfileBindings( xr_instance, &sug );
		}
		if( XR_FAILED( r ) )
		{
			Msg( "openxr: suggested bindings rejected: %s\n", xr_str(r) );
			return false;
		}
	}

	/* Steam Frame, when the runtime offers its profile. A split gamepad: A B X
	   Y and Menu on the right hand, a D-pad and View on the left, a shoulder
	   button on each. View and Menu both open the pause menu. Suggested on its
	   own, so a refusal costs the Frame layout and never Touch's; SteamVR then
	   remaps Touch's onto it, which plays. 'shoulder' was 'bumper' before
	   SteamVR 2.17.10: retried that way if refused. */
	if( xr_has_frame_ext )
	{
		static const char * shoulder_names[2] = { "shoulder", "bumper" };
		int attempt;
		for( attempt = 0; attempt < 2; attempt++ )
		{
			char lsh[96], rsh[96];
			int  n = 0;
			snprintf( lsh, sizeof(lsh), "/user/hand/left/input/%s/click",  shoulder_names[attempt] );
			snprintf( rsh, sizeof(rsh), "/user/hand/right/input/%s/click", shoulder_names[attempt] );
			binds[n].action = act_move;   binds[n++].binding = xr_path( "/user/hand/left/input/thumbstick" );
			binds[n].action = act_look;   binds[n++].binding = xr_path( "/user/hand/right/input/thumbstick" );
			binds[n].action = act_fire1;  binds[n++].binding = xr_path( "/user/hand/right/input/trigger/value" );
			binds[n].action = act_fire2;  binds[n++].binding = xr_path( "/user/hand/left/input/trigger/value" );
			binds[n].action = act_wep1;   binds[n++].binding = xr_path( "/user/hand/left/input/squeeze/value" );
			binds[n].action = act_wep2;   binds[n++].binding = xr_path( "/user/hand/right/input/squeeze/value" );
			binds[n].action = act_menu;   binds[n++].binding = xr_path( "/user/hand/left/input/view/click" );
			binds[n].action = act_menu;   binds[n++].binding = xr_path( "/user/hand/right/input/menu/click" );
			binds[n].action = act_pad[VRB_PAD_A];      binds[n++].binding = xr_path( "/user/hand/right/input/a/click" );
			binds[n].action = act_pad[VRB_PAD_B];      binds[n++].binding = xr_path( "/user/hand/right/input/b/click" );
			binds[n].action = act_pad[VRB_PAD_X];      binds[n++].binding = xr_path( "/user/hand/right/input/x/click" );
			binds[n].action = act_pad[VRB_PAD_Y];      binds[n++].binding = xr_path( "/user/hand/right/input/y/click" );
			binds[n].action = act_pad[VRB_PAD_LSTICK]; binds[n++].binding = xr_path( "/user/hand/left/input/thumbstick/click" );
			binds[n].action = act_pad[VRB_PAD_RSTICK]; binds[n++].binding = xr_path( "/user/hand/right/input/thumbstick/click" );
			binds[n].action = act_pad[VRB_PAD_DUP];    binds[n++].binding = xr_path( "/user/hand/left/input/dpad_up/click" );
			binds[n].action = act_pad[VRB_PAD_DDOWN];  binds[n++].binding = xr_path( "/user/hand/left/input/dpad_down/click" );
			/* D-pad down pauses as well: in a headset run SteamVR passed on one
			   press of View or Menu in many, and took the rest itself (the
			   session lost focus each time), while the D-pad always arrived */
			binds[n].action = act_menu;   binds[n++].binding = xr_path( "/user/hand/left/input/dpad_down/click" );
			binds[n].action = act_pad[VRB_PAD_DLEFT];  binds[n++].binding = xr_path( "/user/hand/left/input/dpad_left/click" );
			binds[n].action = act_pad[VRB_PAD_DRIGHT]; binds[n++].binding = xr_path( "/user/hand/left/input/dpad_right/click" );
			binds[n].action = act_pad[VRB_PAD_LSHOULDER]; binds[n++].binding = xr_path( lsh );
			binds[n].action = act_pad[VRB_PAD_RSHOULDER]; binds[n++].binding = xr_path( rsh );
			binds[n].action = act_aim[0]; binds[n++].binding = xr_path( "/user/hand/left/input/aim/pose" );
			binds[n].action = act_aim[1]; binds[n++].binding = xr_path( "/user/hand/right/input/aim/pose" );
			binds[n].action = act_buzz_l; binds[n++].binding = xr_path( "/user/hand/left/output/haptic" );
			binds[n].action = act_buzz_r; binds[n++].binding = xr_path( "/user/hand/right/output/haptic" );

			sug.interactionProfile     = xr_path( "/interaction_profiles/valve/frame_controller_valve" );
			sug.suggestedBindings      = binds;
			sug.countSuggestedBindings = n;
			r = xrSuggestInteractionProfileBindings( xr_instance, &sug );
			if( !XR_FAILED( r ) )
			{
				DebugPrintf( "openxr: Steam Frame bindings accepted (%d, %s)\n", n, shoulder_names[attempt] );
				break;
			}
			Msg( "openxr: Steam Frame bindings refused with '%s': %s\n", shoulder_names[attempt], xr_str(r) );
		}
	}

	attach.countActionSets = 1;
	attach.actionSets      = &xr_actions;
	r = xrAttachSessionActionSets( xr_session, &attach );
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: xrAttachSessionActionSets failed: %s\n", xr_str(r) );
		return false;
	}

	{
		int hnd;
		for( hnd = 0; hnd < 2; hnd++ )
		{
			XrActionSpaceCreateInfo asi = { XR_TYPE_ACTION_SPACE_CREATE_INFO };
			asi.action = act_aim[hnd];
			asi.poseInActionSpace.orientation.w = 1.0f;
			if( XR_FAILED( xrCreateActionSpace( xr_session, &asi, &xr_aim_space[hnd] ) ) )
			{
				Msg( "openxr: no aim space for the %s hand; aim stays with the ship\n", hnd ? "right" : "left" );
				xr_aim_space[hnd] = XR_NULL_HANDLE;
			}
		}
	}

	memset( &vr_input, 0, sizeof(vr_input) );
	vr_input_active = true;
	DebugPrintf( "openxr: controller actions attached\n" );
	return true;
}

/* Which controllers are in the player's hands: the Controls page shows, and
   the game uses, that controller's map. Asked when the runtime says the
   profile changed: it is usually unknown until the first sync. */
static void detect_profile( void )
{
	XrInteractionProfileState st = { XR_TYPE_INTERACTION_PROFILE_STATE };
	char     name[XR_MAX_PATH_LENGTH];
	uint32_t len = 0;

	name[0] = 0;
	if( XR_FAILED( xrGetCurrentInteractionProfile( xr_session, xr_path( "/user/hand/right" ), &st ) ) )
		return;
	if( st.interactionProfile != XR_NULL_PATH )
		xrPathToString( xr_instance, st.interactionProfile, sizeof(name), &len, name );
	vrb_frame = strstr( name, "frame_controller" ) != NULL;
	DebugPrintf( "openxr: right hand bound to %s%s\n", name[0] ? name : "(nothing yet)",
	             vrb_frame ? " -- the Steam Frame's controls" : "" );
	if( name[0] )
	{
		/* which physical buttons the runtime gave the menu action: a runtime
		   may keep a button for itself, and then the menu cannot open */
		XrBoundSourcesForActionEnumerateInfo ei = { XR_TYPE_BOUND_SOURCES_FOR_ACTION_ENUMERATE_INFO };
		XrPath   src[8];
		uint32_t n = 0, i;
		ei.action = act_menu;
		if( XR_FAILED( xrEnumerateBoundSourcesForAction( xr_session, &ei, 8, &n, src ) ) )
			DebugPrintf( "openxr: menu button: the runtime did not say what it is bound to\n" );
		else if( !n )
			DebugPrintf( "openxr: menu button: bound to NOTHING -- the menu cannot be opened from the controllers\n" );
		for( i = 0; i < n && i < 8; i++ )
		{
			char s[XR_MAX_PATH_LENGTH];
			uint32_t l = 0;
			s[0] = 0;
			xrPathToString( xr_instance, src[i], sizeof(s), &l, s );
			DebugPrintf( "openxr: menu button: bound to %s\n", s );
		}
	}
}

static float action_float( XrAction a )
{
	XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateFloat   st = { XR_TYPE_ACTION_STATE_FLOAT };
	gi.action = a;
	if( XR_FAILED( xrGetActionStateFloat( xr_session, &gi, &st ) ) || !st.isActive )
		return 0.0f;
	return st.currentState;
}

static bool action_bool( XrAction a )
{
	XrActionStateGetInfo gi = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateBoolean st = { XR_TYPE_ACTION_STATE_BOOLEAN };
	gi.action = a;
	if( XR_FAILED( xrGetActionStateBoolean( xr_session, &gi, &st ) ) || !st.isActive )
		return false;
	return st.currentState ? true : false;
}

static void action_vec2( XrAction a, float * x, float * y )
{
	XrActionStateGetInfo  gi = { XR_TYPE_ACTION_STATE_GET_INFO };
	XrActionStateVector2f st = { XR_TYPE_ACTION_STATE_VECTOR2F };
	*x = *y = 0.0f;
	gi.action = a;
	if( XR_FAILED( xrGetActionStateVector2f( xr_session, &gi, &st ) ) || !st.isActive )
		return;
	*x = st.currentState.x;
	*y = st.currentState.y;
}

void vr_openxr_sync_input( void )
{
	if( vr_headless ) return;

	XrActionsSyncInfo sync = { XR_TYPE_ACTIONS_SYNC_INFO };
	XrActiveActionSet active;
	static bool   menu_was_down = false;
	static bool   menu_consumed  = false;
	static XrTime menu_held_from = 0;
	bool menu_down;
	int  i;

	if( !vr_input_active || xr_session == XR_NULL_HANDLE || !vr_xr_session_running )
		return;

	active.actionSet     = xr_actions;
	active.subactionPath = XR_NULL_PATH;
	sync.countActiveActionSets = 1;
	sync.activeActionSets      = &active;

	if( XR_FAILED( xrSyncActions( xr_session, &sync ) ) )
		return;

	action_vec2( act_move, &vr_input.move_x, &vr_input.move_y );
	action_vec2( act_look, &vr_input.look_x, &vr_input.look_y );

	vr_input.fire1 = action_float( act_fire1 );
	vr_input.fire2 = action_float( act_fire2 );

	/* The grips lift and drop the ship without tilting it, the way Descent's
	   vertical thrust works. Roll is the axis that makes people ill, so it is
	   on buttons rather than something you hold (X and Y by default). */
	vr_input.slide_down = action_bool( act_wep1 );   /* left grip  */
	vr_input.slide_up   = action_bool( act_wep2 );   /* right grip */

	/* Both grips: the recenter chord, in play and in menus, and the menu
	   screen is hung again in front of the eyes. A lone grip waits a few
	   frames for the other before it rises or sinks the ship, and one let go
	   sooner still arrives as a tap, so the grips keep their own jobs. After
	   a recenter neither grip does anything until both are let go. */
	{
		static int  lone = 0;          /* frames a lone grip has waited */
		static bool chord = false;     /* recentered; waiting for both to let go */
		static bool was_l = false, was_r = false;
		bool l = vr_input.slide_down, r = vr_input.slide_up;

		if( chord )
		{
			if( !l && !r )
				chord = false;
			vr_input.slide_down = vr_input.slide_up = false;
		}
		else if( l && r )
		{
			vr_recenter();
			vr_panel_replace();
			DebugPrintf( "openxr: both grips: recenter\n" );
			chord = true;
			lone = 0;
			vr_input.slide_down = vr_input.slide_up = false;
		}
		else if( l || r )
		{
			if( lone < VR_GRIP_CHORD_FRAMES )
			{
				lone++;
				vr_input.slide_down = vr_input.slide_up = false;
			}
		}
		else
		{
			if( lone > 0 && lone < VR_GRIP_CHORD_FRAMES )
			{
				/* let go before the wait was over: still a tap */
				vr_input.slide_down = was_l;
				vr_input.slide_up   = was_r;
			}
			lone = 0;
		}
		was_l = l;
		was_r = r;
	}

	vr_input.pad[VRB_PAD_NONE] = false;
	for( i = 1; i < VRB_PAD_COUNT; i++ )
		vr_input.pad[i] = act_pad[i] ? action_bool( act_pad[i] ) : false;

	/* Menu button: a short press still opens the pause menu, a long press
	   recenters. Every other control on a Quest 3 is already bound, and the
	   two thumbstick clicks both drive features confirmed in a headset
	   (weapon cycle, pull-back view), so a chord there would either collide
	   with them or need latency added to controls that work.

	   The cost is that the pause menu now fires on RELEASE rather than press,
	   which is a sub-second difference and only when the press is short. */
	menu_down = action_bool( act_menu );
	vr_input.menu_edge = false;
	{
		static int logged = 0;
		if( menu_down != menu_was_down && logged < 20 )
		{
			logged++;
			DebugPrintf( "openxr: menu button %s\n", menu_down ? "down" : "up" );
		}
	}

	if( menu_down && !menu_was_down )
	{
		menu_held_from = xr_display_time;
		menu_consumed  = false;
	}
	else if( menu_down && !menu_consumed && menu_held_from &&
	         ( xr_display_time - menu_held_from ) > VR_RECENTER_HOLD_NS )
	{
		vr_recenter();
		menu_consumed = true;      /* so the release does not also open the menu */
	}
	else if( !menu_down && menu_was_down && !menu_consumed )
	{
		vr_input.menu_edge = true; /* short press */
	}
	menu_was_down = menu_down;

	/* Left-handed play and stick swapping, before any edge is taken; then
	   the Controls page's map. */
	vr_apply_hand_swaps( &vr_input, vr_leftorium, vr_swap_sticks, vrb_frame );
	vr_input_resolve( &vr_input );
}

bool vr_openxr_start_session( void )
{
	if( vr_headless ) return false;

	XrGraphicsBindingOpenGLWin32KHR gb = { XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
	XrSessionCreateInfo             sci = { XR_TYPE_SESSION_CREATE_INFO };
	XrReferenceSpaceCreateInfo      rsci = { XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
	XrResult                        r;

	if( !vr_enabled || !vr_xr_ready )
		return false;

	// The runtime needs the actual WGL context we render with. It must be
	// current on this thread at this point.
	gb.hDC   = wglGetCurrentDC();
	gb.hGLRC = wglGetCurrentContext();
	if( !gb.hDC || !gb.hGLRC )
	{
		Msg( "openxr: no current WGL context to bind (hDC=%p hGLRC=%p)\n",
		     (void*) gb.hDC, (void*) gb.hGLRC );
		goto fail;
	}

	sci.next     = &gb;
	sci.systemId = xr_system;
	r = xrCreateSession( xr_instance, &sci, &xr_session );
	if( XR_FAILED( r ) )
	{
		xr_session = XR_NULL_HANDLE;
		Msg( "openxr: xrCreateSession failed: %s\n", xr_str(r) );
		goto fail;
	}
	DebugPrintf( "openxr: session created\n" );

	// LOCAL is seated/standing-agnostic and origin-at-startup, which is what
	// we want for a cockpit game. STAGE would tie us to room boundaries.
	rsci.referenceSpaceType            = XR_REFERENCE_SPACE_TYPE_LOCAL;
	rsci.poseInReferenceSpace.orientation.w = 1.0f;
	r = xrCreateReferenceSpace( xr_session, &rsci, &xr_space );
	if( XR_FAILED( r ) )
	{
		Msg( "openxr: xrCreateReferenceSpace failed: %s\n", xr_str(r) );
		goto fail;
	}
	/* VIEW: the head itself, for a HUD worn like a helmet (VRHudAnchor 0).
	   Without it the HUD stays in the cockpit, so a failure is only logged. */
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
	if( XR_FAILED( xrCreateReferenceSpace( xr_session, &rsci, &xr_view_space ) ) )
	{
		xr_view_space = XR_NULL_HANDLE;
		DebugPrintf( "openxr: no VIEW space; the HUD stays in the cockpit\n" );
	}
	rsci.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;

	if( !create_swapchains() )
		goto fail;

	/* Non-fatal: without actions the headset still renders, you just cannot
	   fly with the Touch controllers. */
	create_actions();

	DebugPrintf( "openxr: session ready, waiting for runtime to say READY\n" );
	return true;

fail:
	Msg( "openxr: session start failed -- continuing without VR.\n" );
	vr_enabled = false;
	return false;
}

static void handle_state_change( XrSessionState state )
{
	XrResult r;
	xr_state = state;
	DebugPrintf( "openxr: session state -> %d\n", (int) state );

	switch( state )
	{
	case XR_SESSION_STATE_READY:
		{
			XrSessionBeginInfo bi = { XR_TYPE_SESSION_BEGIN_INFO };
			bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
			r = xrBeginSession( xr_session, &bi );
			if( XR_FAILED( r ) )
				Msg( "openxr: xrBeginSession failed: %s\n", xr_str(r) );
			else
			{
				vr_xr_session_running = true;
				DebugPrintf( "openxr: session begun -- submitting frames\n" );
			}
		}
		break;

	case XR_SESSION_STATE_STOPPING:
		vr_xr_session_running = false;
		xrEndSession( xr_session );
		DebugPrintf( "openxr: session ended\n" );
		break;

	case XR_SESSION_STATE_EXITING:
	case XR_SESSION_STATE_LOSS_PENDING:
		vr_xr_session_running = false;
		DebugPrintf( "openxr: runtime asked us to exit VR\n" );
		break;

	default:
		break;
	}
}

static void poll_events( void )
{
	for(;;)
	{
		XrEventDataBuffer ev = { XR_TYPE_EVENT_DATA_BUFFER };
		XrResult r = xrPollEvent( xr_instance, &ev );
		if( r != XR_SUCCESS )
			break;

		switch( ev.type )
		{
		case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED:
			handle_state_change(
				((XrEventDataSessionStateChanged *) &ev)->state );
			break;
		case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
			Msg( "openxr: instance loss pending\n" );
			vr_xr_session_running = false;
			break;
		case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
			/* The runtime's own recenter: holding the Meta button sends this.
			   The runtime moves the reference space itself, so poses already
			   arrive relative to the new forward. Our own long-press offset
			   would now apply ON TOP of that and the two would compound, so
			   drop it; and re-place any open menu panel in front of the new
			   forward rather than leaving it where the old one was. */
			rc_have        = false;
			xr_quad_placed = false;
			DebugPrintf( "openxr: runtime recenter (reference space change); own offset cleared\n" );
			break;
		case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
			detect_profile();
			break;
		default:
			break;
		}
	}
}

// Copies the finished engine frame into one eye's swapchain image.
static void blit_into_eye( int eye, uint32_t image_index )
{
	GLuint src = (GLuint) vr_target_fbo_id();
	int sw = 0, sh = 0;

	if( !src )
		return;
	vr_target_get_size( &sw, &sh );

	// The source bytes are already sRGB-encoded and the destination is an
	// sRGB-format texture, so this must be a straight byte copy. With
	// GL_FRAMEBUFFER_SRGB enabled the blit would encode them a second time.
	glDisable( GL_FRAMEBUFFER_SRGB );

	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, xr_blit_fbo );
	glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                        GL_TEXTURE_2D, xr_images[eye][image_index].image, 0 );
	glBindFramebuffer( GL_READ_FRAMEBUFFER, src );

	/* World frames fill the eye buffer. LINEAR because the sizes rarely match. */
	glBlitFramebuffer( 0, 0, sw, sh,
	                   0, 0, xr_sc_width, xr_sc_height,
	                   GL_COLOR_BUFFER_BIT, GL_LINEAR );

	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

// --- per-frame state shared between begin/blit/end ---
static XrView    xr_frame_views[VR_MAX_EYES];
static XrCompositionLayerProjectionView xr_proj_views[VR_MAX_EYES];
static bool      xr_frame_open    = false;   // between xrBeginFrame and xrEndFrame
static bool      xr_frame_render  = false;   // runtime wants actual content

/* Synthesise the two eye views that a runtime would hand us, so the REAL
   per-eye render path runs at a desk with no headset.

   This matters more than it looks: with no views the engine takes its mono
   fallback instead, and anything measured there is a measurement of the
   fallback, not of what the headset gets. The two paths place 2D content
   differently, so fixing the one you can see would be fixing the wrong one.

   The numbers are a plausible Quest 3 over VDXR: a symmetric frustum whose
   horizontal extent matches the eye buffer's aspect, identity orientation,
   and the eyes separated by a typical 64 mm IPD. -fakeyaw turns the head, for
   the questions where a fixed view cannot tell you anything. */
static bool vr_headless_frame( void )
{
	int   eye;
	float aspect = ( vr_surface_height > 0 )
	             ? (float) vr_surface_width / (float) vr_surface_height
	             : 1.0f;
	float half_ipd = 0.032f;
	float yaw = ( vr_fake_yaw + vr_fake_yaw_rate * SDL_GetTicks() * 0.001f ) * 0.017453292f;   /* degrees to radians */
	float roll = vr_fake_roll * 0.017453292f;
	float sy = (float) sin( yaw * 0.5 ), cy = (float) cos( yaw * 0.5 );
	float sr = (float) sin( roll * 0.5 ), cr = (float) cos( roll * 0.5 );

	/* a heartbeat for desk checks: a gap in these lines is a stall */
	{
		static unsigned int frames = 0, last = 0;
		unsigned int now = SDL_GetTicks();
		frames++;
		if( now - last >= 2000 )
		{
			last = now;
			DebugPrintf( "vr: headless frames %u\n", frames );
		}
	}

	vr_eyes_rendered = false;

	for( eye = 0; eye < VR_EYE_COUNT; eye++ )
	{
		float sign = ( eye == 0 ) ? -1.0f : 1.0f;

		vr_eye[eye].pos[0] = sign * half_ipd * (float) cos( yaw );
		vr_eye[eye].pos[1] = 0.0f;
		vr_eye[eye].pos[2] = sign * half_ipd * (float) -sin( yaw );

		/* yaw about +Y, then roll about the view axis: q = yaw * roll */
		vr_eye[eye].quat[0] = sy * sr;
		vr_eye[eye].quat[1] = sy * cr;
		vr_eye[eye].quat[2] = cy * sr;
		vr_eye[eye].quat[3] = cy * cr;

		vr_eye[eye].tan_up    =  1.0f;
		vr_eye[eye].tan_down  = -1.0f;
		vr_eye[eye].tan_left  = -aspect;
		vr_eye[eye].tan_right =  aspect;
		if( vr_fake_asym )
		{
			/* a Quest 3 through VDXR: each eye's forward axis well off centre */
			vr_eye[eye].tan_left  = eye == 0 ? -0.942f * aspect : -0.698f * aspect;
			vr_eye[eye].tan_right = eye == 0 ?  0.698f * aspect :  0.942f * aspect;
		}
	}

	{
		const float * q = vr_eye[0].quat;
		float fx = -2.0f * ( q[0]*q[2] + q[3]*q[1] );
		float fz = -( 1.0f - 2.0f * ( q[0]*q[0] + q[1]*q[1] ) );
		vr_head_yaw = atan2f( fx, -fz );
		xr_head_raw[0] = ( vr_eye[0].pos[0] + vr_eye[1].pos[0] ) * 0.5f;
		xr_head_raw[1] = ( vr_eye[0].pos[1] + vr_eye[1].pos[1] ) * 0.5f;
		xr_head_raw[2] = ( vr_eye[0].pos[2] + vr_eye[1].pos[2] ) * 0.5f;
	}
	rc_update();
	fake_aims();
	vr_views_valid = true;
	return true;
}

/* The aim poses, located at the frame's display time like the views and
   moved by the same recenter. Any hand the runtime is not tracking is
   marked invalid, and the game then aims with the ship. */
static void locate_aims( void )
{
	int hnd;
	for( hnd = 0; hnd < 2; hnd++ )
	{
		XrSpaceLocation loc = { XR_TYPE_SPACE_LOCATION };
		vr_eye_view_t   p;

		vr_aim[hnd].valid = false;
		vr_aim_raw[hnd].valid = false;
		if( xr_aim_space[hnd] == XR_NULL_HANDLE )
			continue;
		if( XR_FAILED( xrLocateSpace( xr_aim_space[hnd], xr_space, xr_display_time, &loc ) ) )
			continue;
		if( !( loc.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT ) ||
		    !( loc.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT ) )
			continue;
		memset( &p, 0, sizeof(p) );
		p.pos[0] = loc.pose.position.x; p.pos[1] = loc.pose.position.y; p.pos[2] = loc.pose.position.z;
		p.quat[0] = loc.pose.orientation.x; p.quat[1] = loc.pose.orientation.y;
		p.quat[2] = loc.pose.orientation.z; p.quat[3] = loc.pose.orientation.w;
		vr_aim_raw[hnd].valid = true;
		memcpy( vr_aim_raw[hnd].pos,  p.pos,  sizeof(p.pos) );
		memcpy( vr_aim_raw[hnd].quat, p.quat, sizeof(p.quat) );
		rc_apply( &p );
		memcpy( vr_aim[hnd].pos,  p.pos,  sizeof(p.pos) );
		memcpy( vr_aim[hnd].quat, p.quat, sizeof(p.quat) );
		vr_aim[hnd].valid = true;
	}
}

/* -fakeeye: both hands held out in front, the right one aimed by -fakeaim
   (yaw left-positive and pitch up-positive, degrees), through the recenter. */
static void fake_aims( void )
{
	int hnd;
	for( hnd = 0; hnd < 2; hnd++ )
	{
		vr_eye_view_t p;
		float y = ( hnd == 1 ? vr_fake_aim_yaw   : 0.0f ) * 0.017453292f;
		float x = ( hnd == 1 ? vr_fake_aim_pitch : 0.0f ) * 0.017453292f;
		float sy = (float) sin( y * 0.5 ), cy = (float) cos( y * 0.5 );
		float sx = (float) sin( x * 0.5 ), cx = (float) cos( x * 0.5 );
		memset( &p, 0, sizeof(p) );
		p.pos[0] = hnd ? 0.20f : -0.20f; p.pos[1] = -0.30f; p.pos[2] = -0.35f;
		/* q = yaw(Y) * pitch(X) */
		p.quat[0] = cy * sx; p.quat[1] = sy * cx; p.quat[2] = -sy * sx; p.quat[3] = cy * cx;
		vr_aim_raw[hnd].valid = true;
		memcpy( vr_aim_raw[hnd].pos,  p.pos,  sizeof(p.pos) );
		memcpy( vr_aim_raw[hnd].quat, p.quat, sizeof(p.quat) );
		rc_apply( &p );
		memcpy( vr_aim[hnd].pos,  p.pos,  sizeof(p.pos) );
		memcpy( vr_aim[hnd].quat, p.quat, sizeof(p.quat) );
		vr_aim[hnd].valid = true;
	}
}

/* The headset's timing, logged every 10 s: the rate the runtime asks for,
   frames ended, and how long the game took between xrWaitFrame returning
   and xrEndFrame (average and worst), with frames over the rate's budget
   counted as late. Enough to judge a locked rate from a log. */
static Uint64 tm_wait_done = 0, tm_last_log = 0;
static double tm_sum = 0.0, tm_worst = 0.0, tm_period_ms = 0.0;
static int    tm_frames = 0, tm_late = 0;

static void vr_timing_wait_done( XrDuration period_ns )
{
	tm_wait_done = SDL_GetPerformanceCounter();
	if( period_ns > 0 )
		tm_period_ms = (double) period_ns / 1.0e6;
}

static void vr_timing_frame_end( void )
{
	Uint64 now = SDL_GetPerformanceCounter(), f = SDL_GetPerformanceFrequency();
	double ms;
	if( !tm_wait_done || !f )
		return;
	ms = (double)( now - tm_wait_done ) * 1000.0 / (double) f;
	tm_sum += ms; tm_frames++;
	if( ms > tm_worst ) tm_worst = ms;
	if( tm_period_ms > 0.0 && ms > tm_period_ms ) tm_late++;
	if( !tm_last_log ) tm_last_log = now;
	if( (double)( now - tm_last_log ) / (double) f >= 10.0 )
	{
		DebugPrintf( "vr timing: headset %.0f Hz, %d frames in 10 s, game %.2f ms average, %.2f worst, %d late\n",
		             tm_period_ms > 0.0 ? 1000.0 / tm_period_ms : 0.0, tm_frames,
		             tm_frames ? tm_sum / tm_frames : 0.0, tm_worst, tm_late );
		tm_sum = tm_worst = 0.0; tm_frames = tm_late = 0;
		tm_last_log = now;
	}
}

bool vr_openxr_begin_frame( void )
{
	if( vr_headless )
	{
		any_release_update();   /* the desk's probes write vr_input directly */
		return vr_headless_frame();
	}

	XrFrameWaitInfo   wait = { XR_TYPE_FRAME_WAIT_INFO };
	XrFrameState      fs   = { XR_TYPE_FRAME_STATE };
	XrFrameBeginInfo  begin= { XR_TYPE_FRAME_BEGIN_INFO };
	XrViewLocateInfo  vli  = { XR_TYPE_VIEW_LOCATE_INFO };
	XrViewState       vs   = { XR_TYPE_VIEW_STATE };
	uint32_t view_count = 0;
	int      eye;
	XrResult r;

	vr_views_valid   = false;
	vr_eyes_rendered = false;
	xr_frame_open    = false;
	xr_frame_render  = false;

	if( !vr_enabled || xr_session == XR_NULL_HANDLE )
		return false;

	poll_events();

	if( !vr_xr_session_running )
		return false;

	vr_openxr_sync_input();
	any_release_update();

	r = xrWaitFrame( xr_session, &wait, &fs );
	vr_timing_wait_done( fs.predictedDisplayPeriod );
	if( XR_FAILED( r ) )
	{
		DebugPrintf( "openxr: xrWaitFrame failed: %s\n", xr_str(r) );
		return false;
	}

	r = xrBeginFrame( xr_session, &begin );
	if( XR_FAILED( r ) )
	{
		DebugPrintf( "openxr: xrBeginFrame failed: %s\n", xr_str(r) );
		return false;
	}

	xr_frame_open   = true;
	xr_display_time = fs.predictedDisplayTime;

	// Headset off the head, or otherwise not displaying: the frame still has
	// to be closed or the runtime stalls, but there is nothing to draw.
	if( !fs.shouldRender )
		return false;

	for( eye = 0; eye < VR_MAX_EYES; eye++ )
	{
		memset( &xr_frame_views[eye], 0, sizeof(xr_frame_views[eye]) );
		xr_frame_views[eye].type = XR_TYPE_VIEW;
	}
	vli.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
	vli.displayTime           = xr_display_time;
	vli.space                 = xr_space;
	r = xrLocateViews( xr_session, &vli, &vs, VR_MAX_EYES, &view_count, xr_frame_views );
	if( XR_FAILED( r ) || view_count < VR_MAX_EYES )
		return false;

	// Without both of these the pose is not meaningful and using it would
	// swing the camera around wildly.
	if( !(vs.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) ||
	    !(vs.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT) )
		return false;
	if( xr_settle_frames < VR_SETTLE_FRAMES )
	{
		if( ++xr_settle_frames == VR_SETTLE_FRAMES )
			DebugPrintf( "openxr: head tracking settled after %d frames\n", VR_SETTLE_FRAMES );
		xr_quad_placed = false;   /* placed again from this frame's head */
	}

	// Symmetrise each eye's frustum.
	//
	// The Quest's lenses are canted, so the runtime hands us an off-centre
	// frustum per eye. Reproducing that requires putting offset terms into the
	// projection matrix, and this engine's matrix convention is genuinely
	// ambiguous on paper: ApplyMatrix works off the rows, MatrixMultiply has
	// its own ordering, and mvp_update claims the data is already column-major.
	// Getting those offsets into the wrong slots shears each eye differently,
	// which showed up as a double image that eye-separation tuning could not
	// touch, and as two crosshairs where a screen-space overlay should have
	// fused trivially.
	//
	// So: widen each eye to the symmetric frustum that contains its real one,
	// and report that same widened FOV back to the compositor in the layer, so
	// what we render and what the runtime is told always agree. Costs a few
	// wasted pixels at the edges; removes a whole class of alignment bug.
	for( eye = 0; eye < VR_MAX_EYES; eye++ )
	{
		XrView * v = &xr_frame_views[eye];
		float h = fmaxf( fabsf( tanf( v->fov.angleLeft ) ),
		                 fabsf( tanf( v->fov.angleRight ) ) );
		float w = fmaxf( fabsf( tanf( v->fov.angleUp ) ),
		                 fabsf( tanf( v->fov.angleDown ) ) );
		v->fov.angleLeft  = -atanf( h );
		v->fov.angleRight =  atanf( h );
		v->fov.angleDown  = -atanf( w );
		v->fov.angleUp    =  atanf( w );
	}

	for( eye = 0; eye < VR_MAX_EYES; eye++ )
	{
		const XrView * v = &xr_frame_views[eye];
		vr_eye[eye].pos[0]  = v->pose.position.x;
		vr_eye[eye].pos[1]  = v->pose.position.y;
		vr_eye[eye].pos[2]  = v->pose.position.z;
		vr_eye[eye].quat[0] = v->pose.orientation.x;
		vr_eye[eye].quat[1] = v->pose.orientation.y;
		vr_eye[eye].quat[2] = v->pose.orientation.z;
		vr_eye[eye].quat[3] = v->pose.orientation.w;
		vr_eye[eye].tan_left  = tanf( v->fov.angleLeft );
		vr_eye[eye].tan_right = tanf( v->fov.angleRight );
		vr_eye[eye].tan_up    = tanf( v->fov.angleUp );
		vr_eye[eye].tan_down  = tanf( v->fov.angleDown );
	}

	rc_update();
	locate_aims();

	/* Head yaw and pitch, for placing the floating panel: from the RAW
	   pose. The panel is a layer in xr_space, the runtime's own space, so it
	   has to be placed in that space; the recentered vr_eye[] put it off to
	   the side by however far the player had turned when recentering. */
	{
		float q[4];
		float fx, fy, fz;
		int   e;

		q[0] = xr_frame_views[0].pose.orientation.x;
		q[1] = xr_frame_views[0].pose.orientation.y;
		q[2] = xr_frame_views[0].pose.orientation.z;
		q[3] = xr_frame_views[0].pose.orientation.w;
		for( e = 0; e < 3; e++ )
			xr_head_raw[e] = 0.0f;
		xr_head_raw[0] = ( xr_frame_views[0].pose.position.x + xr_frame_views[1].pose.position.x ) * 0.5f;
		xr_head_raw[1] = ( xr_frame_views[0].pose.position.y + xr_frame_views[1].pose.position.y ) * 0.5f;
		xr_head_raw[2] = ( xr_frame_views[0].pose.position.z + xr_frame_views[1].pose.position.z ) * 0.5f;

		/* head forward vector: rotate (0,0,-1) by the eye quaternion */
		fx = -2.0f * ( q[0]*q[2] + q[3]*q[1] );
		fy = -2.0f * ( q[1]*q[2] - q[3]*q[0] );
		fz = -( 1.0f - 2.0f * ( q[0]*q[0] + q[1]*q[1] ) );

		vr_head_yaw   = atan2f( fx, -fz );
		vr_head_pitch = asinf( fy < -1.0f ? -1.0f : ( fy > 1.0f ? 1.0f : fy ) );
	}


	/* Panel re-placement is driven by how long the panel has been ABSENT, not
	   by "the world rendered this frame": see xr_quad_idle_frames in
	   end_frame. Dying alternates world frames and menu frames, and the old
	   rule re-centred the panel on every world frame, so the menu jumped
	   around and left the previous frame's selection sitting where it was. */

	vr_views_valid  = true;
	xr_frame_render = true;
	return true;
}

/* Puts the finished menu frame on a flat panel fixed in world space.

   Placed once, roughly 1.75 m in front of wherever the player was facing when
   the menu appeared, at their eye height, and yaw-only so it stands upright.
   From then on the runtime holds it there: look around and it stays, exactly
   like a screen hanging in a dark room. Nothing is parented to the head.

   This replaces an earlier attempt that shifted the image inside the eye
   buffer to cancel head rotation. That approximates the position but has no
   depth, no parallax and no perspective, so it never reads as a real object.
   A quad layer is the primitive OpenXR provides for exactly this. */
static void qrot( const float q[4], const float v[3], float out[3] )
{
	/* v' = q v q*, written out */
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float tx = 2.0f * ( y*v[2] - z*v[1] );
	float ty = 2.0f * ( z*v[0] - x*v[2] );
	float tz = 2.0f * ( x*v[1] - y*v[0] );
	out[0] = v[0] + w*tx + ( y*tz - z*ty );
	out[1] = v[1] + w*ty + ( z*tx - x*tz );
	out[2] = v[2] + w*tz + ( x*ty - y*tx );
}

/* Where the gun hand's aim ray meets the menu screen, as 0..1 across and
   down its picture. False when no screen was up last frame, the hand is not
   tracked, or the ray misses. All in the runtime's space, the screen's own. */
bool vr_pointer_uv( float * u, float * v )
{
	static const float fwd[3] = { 0.0f, 0.0f, -1.0f }, zax[3] = { 0.0f, 0.0f, 1.0f };
	static const float xax[3] = { 1.0f, 0.0f, 0.0f }, yax[3] = { 0.0f, 1.0f, 0.0f };
	const vr_hand_pose_t * a = &vr_aim_raw[ vr_leftorium ? 0 : 1 ];
	float d[3], n[3], rx[3], ry[3], q[4], c[3], dn, t, p[3], x, y, w, h;
	int i;

	if( !vr_panel_last || !xr_shown_ok || !a->valid || xr_shown_space != xr_space )
		return false;
	q[0] = xr_shown_pose.orientation.x; q[1] = xr_shown_pose.orientation.y;
	q[2] = xr_shown_pose.orientation.z; q[3] = xr_shown_pose.orientation.w;
	c[0] = xr_shown_pose.position.x; c[1] = xr_shown_pose.position.y; c[2] = xr_shown_pose.position.z;
	qrot( a->quat, fwd, d );
	qrot( q, zax, n );
	qrot( q, xax, rx );
	qrot( q, yax, ry );
	dn = d[0]*n[0] + d[1]*n[1] + d[2]*n[2];
	if( fabs( dn ) < 1e-4f )
		return false;
	t = ( ( c[0]-a->pos[0] )*n[0] + ( c[1]-a->pos[1] )*n[1] + ( c[2]-a->pos[2] )*n[2] ) / dn;
	if( t <= 0.0f )
		return false;
	for( i = 0; i < 3; i++ )
		p[i] = a->pos[i] + t * d[i] - c[i];
	x = p[0]*rx[0] + p[1]*rx[1] + p[2]*rx[2];
	y = p[0]*ry[0] + p[1]*ry[1] + p[2]*ry[2];
	w = xr_shown_w; h = w * 0.75f;
	*u = x / w + 0.5f;
	*v = 0.5f - y / h;
	return *u >= 0.0f && *u <= 1.0f && *v >= 0.0f && *v <= 1.0f;
}

static void place_panel( void )
{
		const float dist = vr_panel_dist;
		float fx =  sinf( vr_head_yaw );
		float fz = -cosf( vr_head_yaw );

		/* Centre on the head, not on vr_eye[0], that is the LEFT eye, so
		   using it put the panel half an IPD to the left of where the player
		   was actually looking. */
		float hx = xr_head_raw[0];   /* raw, like the yaw: see the comment */
		float hy = xr_head_raw[1];   /* where vr_head_yaw is set           */
		float hz = xr_head_raw[2];

		xr_quad_pose.position.x = hx + fx * dist;
		xr_quad_pose.position.y = hy;                        /* eye height */
		xr_quad_pose.position.z = hz + fz * dist;

		/* Yaw only, so the panel stands upright however the head was tilted,
		   and turned to face the head. vr_head_yaw is measured the other way
		   round from a quaternion's turn about +Y (it is atan2 of the forward
		   vector's x against -z), so the turn is MINUS it: with plus, a screen
		   placed while looking 30 degrees aside was turned 60 degrees from
		   facing you. */
		xr_quad_pose.orientation.x = 0.0f;
		xr_quad_pose.orientation.y = sinf( -vr_head_yaw * 0.5f );
		xr_quad_pose.orientation.z = 0.0f;
		xr_quad_pose.orientation.w = cosf( -vr_head_yaw * 0.5f );

		xr_quad_placed = true;
	}

/* A pose given in OUR recentered space: the cockpit, or the title camera:
   expressed in the runtime's space, which is the one layers are placed in.
   The inverse of rc_apply(): turn by +rc_yaw, then add back the centre. */
static void rec_to_raw( float x, float y, float z, float yaw, XrPosef * out )
{
	float c = 1.0f, sn = 0.0f, ox = 0.0f, oz = 0.0f;
	if( rc_have )
	{
		c = cosf( rc_yaw ); sn = sinf( rc_yaw );
		ox = rc_x; oz = rc_z;
		yaw += rc_yaw;
	}
	out->position.x = x * c + z * sn + ox;
	out->position.y = y;
	out->position.z = -x * sn + z * c + oz;
	out->orientation.x = 0.0f;
	out->orientation.y = sinf( yaw * 0.5f );
	out->orientation.z = 0.0f;
	out->orientation.w = cosf( yaw * 0.5f );
}

/* Where this frame's quad hangs, in which space, and how big it is. */
static void shown_pose( int kind )
{
	xr_shown_space = xr_space;
	if( kind == VR_PANEL_TITLE )
	{
		/* Laid over the title camera's own view: the flat game's 90 degree
		   picture, 4:3, straight ahead of where that camera is in our space
		   the origin, facing -Z: at the VDU's distance, so the words
		   land on the VDU in the room and at its depth. */
		float d = vr_title_panel_dist;
		if( d < 0.5f ) d = 0.5f;
		if( d > 8.0f ) d = 8.0f;
		rec_to_raw( 0.0f, 0.0f, -d, 0.0f, &xr_shown_pose );
		xr_shown_dist = d;
		xr_shown_w    = 2.0f * d;   /* tan 45 degrees either side */
	}
	else if( kind == VR_PANEL_HUD )
	{
		float d = vr_panel_dist > 0.3f ? vr_panel_dist : 1.75f;
		xr_shown_dist = d;
		xr_shown_w    = 2.0f * d * vr_hud_half_tan_x();
		if( vr_hud_anchor < 0.5f && xr_view_space != XR_NULL_HANDLE )
		{
			/* worn like a helmet: straight ahead of the eyes, always */
			memset( &xr_shown_pose, 0, sizeof(xr_shown_pose) );
			xr_shown_pose.orientation.w = 1.0f;
			xr_shown_pose.position.z    = -d;
			xr_shown_space = xr_view_space;
		}
		else
			/* fixed in the cockpit: straight ahead of the ship's eye point */
			rec_to_raw( 0.0f, 0.0f, -d, 0.0f, &xr_shown_pose );
	}
	else
	{
		if( !xr_quad_placed )
			place_panel();
		xr_shown_pose = xr_quad_pose;
		xr_shown_dist = vr_panel_dist;
		xr_shown_w    = vr_panel_width;
	}
	xr_shown_ok = true;
}

float vr_panel_half_tan_y( void )
{
	float d = xr_shown_dist > 0.1f ? xr_shown_dist : 1.75f;
	return xr_shown_w * 0.75f * 0.5f / d;
}

void vr_openxr_submit_panel( void )
{
	int kind = vr_panel_kind;

	vr_panel_kind       = VR_PANEL_MENU;   /* one frame only */
	vr_panel_kind_shown = kind;
	vr_observer_panel   = true;   /* the observer lays this screen over the game */
	xr_shown_keyed      = vr_panel_keyed;
	if( vr_headless )
	{
		shown_pose( kind );   /* so the pointer can be proved at a desk */
		if( vr_dump_frame )
			dump_target( kind == VR_PANEL_HUD ? "logs/hudpanel.ppm" : "logs/panel.ppm" );
		{
			static int logged = 0;
			if( logged < 6 && vr_dump_frame )
			{
				logged++;
				DebugPrintf( "quad: kind %d keyed %d at (%.2f, %.2f, %.2f) yaw %.1f, %.2f m wide at %.2f m\n",
				             kind, (int) xr_shown_keyed, xr_shown_pose.position.x, xr_shown_pose.position.y,
				             xr_shown_pose.position.z,
				             2.0f * atan2f( xr_shown_pose.orientation.y, xr_shown_pose.orientation.w ) * 57.29578f,
				             xr_shown_w, xr_shown_dist );
			}
		}
		return;
	}

	uint32_t index = 0;
	XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo    wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	GLuint src = (GLuint) vr_target_fbo_id();
	int sw = 0, sh = 0;

	if( !xr_frame_render || !xr_quad_ready || !src )
		return;

	vr_target_get_size( &sw, &sh );

	shown_pose( kind );

	if( XR_FAILED( xrAcquireSwapchainImage( xr_quad_swapchain, &ai, &index ) ) )
		return;
	wi.timeout = XR_INFINITE_DURATION;
	if( XR_FAILED( xrWaitSwapchainImage( xr_quad_swapchain, &wi ) ) )
		return;

	glDisable( GL_FRAMEBUFFER_SRGB );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, xr_blit_fbo );
	glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
	                        GL_TEXTURE_2D, xr_quad_images[index].image, 0 );
	if( xr_shown_keyed )
	{
		/* see-through: black becomes clear, drawn rather than blitted */
		if( !vr_target_key_into( xr_blit_fbo, xr_quad_w, xr_quad_h ) )
			xr_shown_keyed = false;
	}
	if( !xr_shown_keyed )
	{
		glBindFramebuffer( GL_READ_FRAMEBUFFER, src );
		glBlitFramebuffer( 0, 0, sw, sh,
		                   0, 0, xr_quad_w, xr_quad_h,
		                   GL_COLOR_BUFFER_BIT, GL_LINEAR );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );

	xrReleaseSwapchainImage( xr_quad_swapchain, &ri );
	xr_quad_this_frame = true;

	{
		static int shown = 0;
		if( shown < 3 )
		{
			shown++;
			DebugPrintf( "quad: submitted src=%dx%d kind %d keyed %d pose=(%.2f,%.2f,%.2f) %.2f m wide\n",
				sw, sh, kind, (int) xr_shown_keyed, xr_shown_pose.position.x, xr_shown_pose.position.y,
				xr_shown_pose.position.z, xr_shown_w );
		}
	}
}

void vr_openxr_blit_eye( int eye )
{
	if( vr_headless )
	{
		if( vr_dump_frame )
			dump_target( eye == 0 ? "logs/eye0.ppm" : "logs/eye1.ppm" );
		return;
	}

	uint32_t index = 0;
	XrSwapchainImageAcquireInfo ai = { XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
	XrSwapchainImageWaitInfo    wi = { XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
	XrSwapchainImageReleaseInfo ri = { XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
	XrResult r;

	if( !xr_frame_render || eye < 0 || eye >= VR_MAX_EYES )
		return;
	if( xr_swapchain[eye] == XR_NULL_HANDLE )
		return;

	r = xrAcquireSwapchainImage( xr_swapchain[eye], &ai, &index );
	if( XR_FAILED( r ) )
		return;

	wi.timeout = XR_INFINITE_DURATION;
	r = xrWaitSwapchainImage( xr_swapchain[eye], &wi );
	if( XR_FAILED( r ) )
		return;

	blit_into_eye( eye, index );

	xrReleaseSwapchainImage( xr_swapchain[eye], &ri );

	memset( &xr_proj_views[eye], 0, sizeof(xr_proj_views[eye]) );
	xr_proj_views[eye].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
	xr_proj_views[eye].pose = xr_frame_views[eye].pose;
	xr_proj_views[eye].fov  = xr_frame_views[eye].fov;
	xr_proj_views[eye].subImage.swapchain            = xr_swapchain[eye];
	xr_proj_views[eye].subImage.imageRect.offset.x   = 0;
	xr_proj_views[eye].subImage.imageRect.offset.y   = 0;
	xr_proj_views[eye].subImage.imageRect.extent.width  = xr_sc_width;
	xr_proj_views[eye].subImage.imageRect.extent.height = xr_sc_height;
	xr_proj_views[eye].subImage.imageArrayIndex      = 0;
}

void vr_openxr_end_frame( void )
{
	if( vr_headless ) return;

	XrFrameEndInfo end = { XR_TYPE_FRAME_END_INFO };
	XrCompositionLayerProjection layer = { XR_TYPE_COMPOSITION_LAYER_PROJECTION };
	XrCompositionLayerQuad       quad  = { XR_TYPE_COMPOSITION_LAYER_QUAD };
	const XrCompositionLayerBaseHeader * layers[2];
	XrResult r;

	if( !xr_frame_open )
		return;

	end.displayTime          = xr_display_time;
	end.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
	end.layerCount           = 0;
	end.layers               = NULL;

	if( xr_frame_render && xr_quad_this_frame )
	{
		/* Menu frame: one flat panel fixed in world space and nothing else, so
		   the surround is the environment blend mode: a pure black void. */
		/* Opaque unless keyed: the engine leaves the frame's alpha at zero,
		   so blending the raw frame renders it completely invisible. A keyed
		   screen has its alpha made from the picture (vr_target_key_into),
		   premultiplied, which is what SOURCE_ALPHA alone means. */
		quad.layerFlags         = xr_shown_keyed ? XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT : 0;
		quad.space              = xr_shown_space != XR_NULL_HANDLE ? xr_shown_space : xr_space;
		quad.eyeVisibility      = XR_EYE_VISIBILITY_BOTH;
		quad.subImage.swapchain = xr_quad_swapchain;
		quad.subImage.imageRect.offset.x     = 0;
		quad.subImage.imageRect.offset.y     = 0;
		quad.subImage.imageRect.extent.width  = xr_quad_w;
		quad.subImage.imageRect.extent.height = xr_quad_h;
		quad.subImage.imageArrayIndex = 0;
		quad.pose               = xr_shown_pose;
		quad.size.width         = xr_shown_w;
		/* 4:3, matching the menu's own layout. Blitting it into a 16:9
		   panel stretches it horizontally. */
		quad.size.height        = xr_shown_w * 3.0f / 4.0f;

		if( vr_eyes_rendered )
		{
			/* In-world pause menu: the eyes were drawn this frame, so submit
			   the stereo world first and the menu panel over it. Layers are
			   composited in order, so the quad must come second. */
			layer.space     = xr_space;
			layer.viewCount = VR_MAX_EYES;
			layer.views     = xr_proj_views;
			layers[0]       = (const XrCompositionLayerBaseHeader *) &layer;
			layers[1]       = (const XrCompositionLayerBaseHeader *) &quad;
			end.layerCount  = 2;
		}
		else
		{
			layers[0]      = (const XrCompositionLayerBaseHeader *) &quad;
			end.layerCount = 1;
		}
		end.layers = layers;
	}
	/* The world only when the eyes were drawn this frame: otherwise its views
	   hold no pose and the runtime refuses the frame (XR_ERROR_POSE_INVALID).
	   No layer at all is legal, and shows black: the splash's first frames,
	   before head tracking settles. */
	else if( xr_frame_render && vr_eyes_rendered )
	{
		layer.space     = xr_space;
		layer.viewCount = VR_MAX_EYES;
		layer.views     = xr_proj_views;
		layers[0]       = (const XrCompositionLayerBaseHeader *) &layer;
		end.layerCount  = 1;
		end.layers      = layers;
	}

	r = xrEndFrame( xr_session, &end );
	vr_timing_frame_end();
	if( XR_FAILED( r ) )
	{
		if( xr_end_failed++ < 5 )
			DebugPrintf( "openxr: xrEndFrame failed with %u layer(s): %s\n",
			             end.layerCount, xr_str(r) );
	}
	else if( end.layerCount <= 2 )
		xr_ended[ end.layerCount ]++;

	xr_frame_open   = false;
	xr_frame_render = false;

	/* Decide whether the menu has genuinely closed, so the next one re-centres
	   in front of wherever the player is then looking.

	   "The world rendered, so drop the pose" is wrong: while you are dying the
	   engine alternates world frames and menu frames, and that rule re-placed
	   the panel several times a second. What is actually wanted is "no panel
	   for a while", so a run of world frames long enough to mean the menu is
	   really gone re-arms placement, and a stutter of them does not. Half a
	   second or so at any headset frame rate. */
	/* only a MENU screen holds its place: the HUD and the title's pages
	   are placed every frame and say nothing about whether a menu is up */
	if( xr_quad_this_frame && vr_panel_kind_shown == VR_PANEL_MENU )
		xr_quad_idle_frames = 0;
	else if( xr_quad_idle_frames < VR_QUAD_IDLE_RESET )
	{
		xr_quad_idle_frames++;
		if( xr_quad_idle_frames >= VR_QUAD_IDLE_RESET )
			xr_quad_placed = false;
	}

	xr_quad_this_frame = false;   /* or every later frame submits the menu quad */
}

/* Place the menu screen again on the next frame, from where the head is
   then: after a distance change or a recenter from the menu. */
void vr_panel_replace( void )
{
	xr_quad_placed = false;
}

/* Whether head tracking has settled (VR_SETTLE_FRAMES real frames). Until
   then the menu screen is placed again every frame, so it follows the head. */
bool vr_openxr_settled( void )
{
	return vr_headless || xr_settle_frames >= VR_SETTLE_FRAMES;
}

bool vr_openxr_haptic( int hand, float amplitude, int ms )
{
	XrHapticActionInfo info = { XR_TYPE_HAPTIC_ACTION_INFO };
	XrHapticVibration  vib  = { XR_TYPE_HAPTIC_VIBRATION };

	if( !xr_haptics_bound || !vr_input_active || xr_session == XR_NULL_HANDLE ||
	    !vr_xr_session_running )
		return false;

	if( amplitude < 0.0f ) amplitude = 0.0f;
	if( amplitude > 1.0f ) amplitude = 1.0f;

	info.action     = hand == 0 ? act_buzz_l : act_buzz_r;
	vib.amplitude   = amplitude;
	vib.duration    = (XrDuration) ms * 1000000;
	vib.frequency   = XR_FREQUENCY_UNSPECIFIED;

	return !XR_FAILED( xrApplyHapticFeedback( xr_session, &info,
	                                          (const XrHapticBaseHeader *) &vib ) );
}

#else // built without OpenXR support

bool vr_openxr_init( void )
{
	if( vr_enabled )
		Msg( "openxr: this build has no OpenXR support compiled in; "
		     "-vr will only use the offscreen path.\n" );
	return true;
}
void vr_openxr_shutdown( void ) {}
bool vr_openxr_start_session( void ) { return false; }
bool vr_openxr_begin_frame( void ) { return false; }
void vr_openxr_blit_eye( int eye ) { (void) eye; }
void vr_openxr_end_frame( void ) {}
bool vr_xr_session_running = false;
void vr_panel_replace( void ) {}
bool vr_openxr_settled( void ) { return true; }
float vr_panel_half_tan_y( void ) { return vr_panel_width * 0.375f / vr_panel_dist; }
bool vr_openxr_haptic( int hand, float amplitude, int ms ) { (void) hand; (void) amplitude; (void) ms; return false; }

#endif
