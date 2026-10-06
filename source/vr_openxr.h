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

/*
  OpenXR bring-up.

  Phase 3a: create an OpenXR instance, pick the head-mounted system, and report
  everything the runtime tells us about itself: runtime name, per-eye
  recommended resolution, blend modes, and the OpenGL version it requires.
  No session, no swapchains and no rendering change yet.

  The point of doing this as its own step is that it fails cheap. If the loader
  cannot find VDXR, or VDXR wants a newer OpenGL context than we create, we
  learn that from one log line instead of from a black headset.

  Everything here is inert unless -vr is passed. Any failure logs the reason
  and leaves the game rendering flat, per the "degrade, never refuse to start"
  rule that vr_target follows.
*/
#ifndef VR_OPENXR_INCLUDED
#define VR_OPENXR_INCLUDED

#include "vr_binds.h"

#include "main.h"

// True once an instance and system have been acquired.
extern bool vr_xr_ready;

// Per-eye render size the runtime recommends. Valid only when vr_xr_ready.
extern int vr_xr_view_width;
extern int vr_xr_view_height;

// Brings up the OpenXR instance and system. Returns false on any failure,
// having logged the reason. Safe to call when VR is disabled (does nothing).
bool vr_openxr_init( void );

void vr_openxr_shutdown( void );

// Creates the session, reference space and per-eye swapchains. Call after
// vr_openxr_init() and once the GL context is current. Returns false and
// leaves VR disabled on failure.
bool vr_openxr_start_session( void );

// True while the runtime has us in a rendering state (headset on and focused).
extern bool vr_xr_session_running;

#define VR_EYE_COUNT 2

// One eye's view for this frame, in a form the renderer can use without
// dragging in any OpenXR headers. Positions are metres in the OpenXR LOCAL
// reference space; the tangents are of the FOV half-angles and are asymmetric,
// which is why we cannot express them as a single "fov" number.
typedef struct {
	float pos[3];       // x, y, z
	float quat[4];      // x, y, z, w
	float tan_left;     // negative
	float tan_right;    // positive
	float tan_up;       // positive
	float tan_down;     // negative
} vr_eye_view_t;

extern vr_eye_view_t vr_eye[VR_EYE_COUNT];

// The controllers' AIM poses (the pointing ray), per PHYSICAL hand: 0 left,
// 1 right: in the same space as vr_eye[] (recentered the same way). Only
// pos, quat and valid are used.
typedef struct {
	bool  valid;
	float pos[3];
	float quat[4];
} vr_hand_pose_t;
extern vr_hand_pose_t vr_aim[2];
extern float vr_fake_aim_yaw, vr_fake_aim_pitch;   // -fakeaim:Y,P (headless)
extern bool  vr_panel_last;                        // a menu screen was up last frame
// The menu pointer: the gun hand's ray on the menu screen, 0..1 across/down.
bool vr_pointer_uv( float * u, float * v );
/* -fakeeye: render the VR path into an eye-sized target with no runtime, so
   the headset geometry can be measured at a desk. */
extern bool          vr_headless;
extern float         vr_fake_yaw;
extern float         vr_fake_yaw_rate;   /* -fakeyawrate:D, headless */

/* 0 = HUD stays with your head (the old behaviour), 1 = it stays with the
   cockpit. -vrhudanchor:X, because only wearing it can settle the taste. */
extern float         vr_hud_anchor;
extern bool          vr_world_menu;   /* stereo world behind the pause menu */
extern bool          vr_photo_mode;   /* pause menu hidden for a screenshot */
extern bool          vr_leftorium;    /* left-handed: mirror the hands */
extern bool          vr_swap_sticks;  /* swap the sticks, axes and clicks */
void vr_hand_swap_selftest( bool sabotage );
extern bool          vr_dump_frame;   /* headless: dump eyes + panel this frame */
extern bool          vr_spectator;     /* steady desktop view (vr_target.c) */
extern bool          vr_spectator_log; /* -vrspeclog */
extern int           vr_spectator_eye; /* eye kept for the observer view this frame, or -1 */
extern bool          vr_observer_panel;    /* a menu screen was drawn this frame */
extern bool          vr_observer_windowed; /* VRObserverWindowed: Alt+Enter */
void vr_observer_keep_eye( int eye );      /* vr_target.c */
void vr_observer_window_apply( void );
void vr_observer_toggle_window( void );
extern float         vr_fake_roll;     /* -fakeroll:D, headless */
extern bool          vr_fake_asym;     /* -fakeasym, headless: a Quest 3's off-centre frusta */

/* Put "forward" where the player is looking now. Bound to a long press of the
   menu button; also callable from anywhere, e.g. a menu item. */
void vr_recenter( void );

/* Swivel chair (VRSwivel, off by default): turn your body and the ship turns
   with you. Each frame the head's turn about the vertical is taken out of the
   view (a continuous recenter, yaw only) and handed to the ship, so you fly
   where you face and the cockpit (HUD and all) comes round with you. The
   sticks still turn the ship as well.
   vr_swivel_live: set by the game each frame it can take a turn (flying, no
   menu); the layer turns nothing on a frame after one it was not set.
   vr_swivel_take(): the turn since last asked, radians, left positive. */
extern bool  vr_swivel;
extern bool  vr_swivel_live;
float vr_swivel_take( void );

/* One vibration pulse on a PHYSICAL hand (0 left, 1 right), amplitude 0..1.
   Returns false when there is nothing to send it to: no session, headless,
   or a runtime that refused the haptic bindings. */
bool vr_openxr_haptic( int hand, float amplitude, int ms );

/* Place the menu screen again on the next frame. */
void vr_panel_replace( void );
bool vr_openxr_settled( void );   /* head tracking has settled; the menu screen stays where it is placed */

/* What the quad layer shows, and so where it hangs. Set vr_panel_kind before
   vr_openxr_submit_panel(); it goes back to MENU after every submit.
     MENU   a menu screen, placed once in front of the head and left there
     TITLE  the title's VDU pages, laid over the title camera's own view, so
            the words sit on the VDU in the room as they do on a monitor
     HUD    the HUD, fixed in the cockpit (VRHudAnchor 1) or to the head (0) */
enum { VR_PANEL_MENU, VR_PANEL_TITLE, VR_PANEL_HUD };
#define VR_HUD_BOX 0.60f   /* the HUD's box, as a fraction of the eye's width (and of the logical screen) */
extern int   vr_panel_kind;
extern int   vr_panel_kind_shown;   /* what this frame's quad was, for the observer */
extern float vr_title_panel_dist;   /* metres from the title camera to its VDU */
float vr_panel_half_tan_y( void );  /* the quad's half height over its distance */
float vr_hud_half_tan_x( void );    /* the HUD screen's half width over its distance */
#define VR_RECENTER_HOLD_NS 600000000LL   /* 0.6 s, in XrTime nanoseconds */
#define VR_HUD_ANCHOR_LIMIT 0.85f

extern bool          vr_views_valid;   // are vr_eye[] usable this frame?

// Set by the renderer when it has drawn and submitted both eyes itself.
// Cleared at the start of every frame. When it is still false at the end of a
// frame: menus, loading screens, anything that does not go through the
// per-eye world render: the caller blits the mono frame to both eyes so the
// headset shows something instead of black.
extern bool          vr_eyes_rendered;

// Metres-to-world-units. OpenXR speaks metres; Forsaken does not. Scaling the
// head translation and the eye separation by this is what makes the world feel
// the right size. Tunable with -vrscale.
extern float vr_world_scale;

// Quest 3 controller input, filled by vr_openxr_sync_input() each frame.
// Everything is already in the sense the ship wants: +move_y is forward,
// +move_x is strafe right, +look_x is yaw right, +look_y is pitch up.
typedef struct {
	float move_x, move_y;    // left thumbstick,  -1..1
	float look_x, look_y;    // right thumbstick, -1..1
	float fire1,  fire2;     // right/left trigger, 0..1
	bool  slide_up, slide_down;    // right / left grip
	bool  menu_edge;               // menu button, true for one frame

	// The buttons, physically, after the hand swaps; then what the Controls
	// page maps them to (vr_binds.h). The four below are act[] by name.
	bool  pad[VRB_PAD_COUNT];
	bool  act[VRB_ACT_COUNT];      // held
	bool  act_edge[VRB_ACT_COUNT]; // pressed this frame
	bool  roll_left, roll_right;
	bool  nitro, mine;
	bool  menu_select, menu_back;  // A / B in menus, whatever they do in flight
	bool  stick_l_edge;            // left thumbstick click, one frame (photo mode)
	bool  stick_r_edge;            // right thumbstick click, one frame
} vr_input_t;

// Buttons -> actions -> the named fields above, plus the stick-click edges.
// Called after the controllers are read and the hands swapped.
void vr_input_resolve( vr_input_t * in );

extern vr_input_t vr_input;
extern bool       vr_input_active;   // action set attached and syncing

void vr_openxr_sync_input( void );

// Percentage of the runtime's recommended per-eye resolution to render at.
// See -vrres in main.c.
extern int vr_res_scale;

// How far back the camera sits from the ship, in metres. 0 is the cockpit.
// Toggled with right-stick click; helps a lot in tight corridors.
extern float vr_chase_dist;

// Menu panel placement, in NDC, filled each frame while a menu is up.
// The 2D pass is head-locked by nature; shifting it by how far the head has
// turned since the menu opened makes it behave like a panel fixed in space.
extern float vr_head_yaw;     // radians, from the runtime's head pose
extern float vr_head_pitch;

// True for frames the engine draws once and we copy to both eyes: menus,
// loading screens, anything outside the per-eye world render. Those get placed
// as a floating panel rather than filling the eye buffer.
extern bool vr_panel_frame;

// Which eye is being rendered right now (0 left, 1 right), so the 2D pass can
// give the menu a finite distance instead of sitting at infinity.
extern int vr_current_eye;

// Menu panel width and distance, in metres. How big a floating screen feels
// is only judgeable by wearing it, so both are cli options: -vrpanel and
// -vrpaneldist. The panel keeps the menu's native 4:3 whatever the width.
extern float vr_panel_width;
extern float vr_panel_dist;


// Start of frame: pumps runtime events, waits on the runtime's frame pacing,
// and fills vr_eye[]. Returns true if the engine should render this frame.
// When it returns false the frame has already been closed out; render nothing.
bool vr_openxr_begin_frame( void );

// Copies whatever is currently in vr_target into the given eye's swapchain.
// Call once per eye, after rendering that eye.
void vr_openxr_blit_eye( int eye );

// Menus and loading screens: puts the finished frame on a flat panel fixed
// in world space, submitted as an OpenXR quad layer. Call instead of
// blitting both eyes.
void vr_openxr_submit_panel( void );

// End of frame: submits the projection layer built from vr_eye[].
void vr_openxr_end_frame( void );

#endif // VR_OPENXR_INCLUDED
