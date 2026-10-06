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

/*
  Offscreen render target for the VR path.

  Phase 2 of the VR work: instead of drawing straight to the window, the whole
  scene is rendered into a framebuffer object and then blitted to the window.
  This is still monoscopic and visually identical to flat mode: the point is
  to establish the seam. Later phases replace the blit with submission of the
  colour texture to an OpenXR swapchain, once per eye.

  Everything here is inert unless -vr is passed on the command line, so flat
  mode keeps its original code path exactly.
*/
#ifndef VR_TARGET_INCLUDED
#define VR_TARGET_INCLUDED

#include "main.h"

// set from the -vr command line option, parsed in main.c
extern bool vr_enabled;

// Creates (or recreates) the offscreen target at the given size.
// Safe to call when vr_enabled is false: it does nothing and succeeds.
// On failure it logs, disables the offscreen path and returns false, so the
// game falls back to rendering directly to the window rather than dying.
// The *physical* size the engine renders at: the window in flat mode, the VR
// eye buffer (times the resolution scale) under -vr.
//
// This is deliberately separate from render_info.window_size, which stays the
// *logical* 2D space the HUD and menus are laid out in. They are equal in flat
// mode, which is why nothing ever forced them apart before. Keeping them
// separate is what lets the world render at eye-buffer resolution while the
// HUD keeps its proportions instead of shrinking into a corner.
extern int vr_surface_width;
extern int vr_surface_height;
void vr_surface_set( int width, int height );

bool vr_target_init( int width, int height );

void vr_target_cleanup( void );

// Call immediately before the engine renders a frame: binds the FBO.
void vr_target_begin_frame( void );

// Call after the engine has finished the frame and before the buffer swap:
// blits the FBO to the window and unbinds it.
void vr_target_end_frame( void );

// The finished frame, for whoever needs to copy it somewhere else (the OpenXR
// layer blits it into the eye swapchains). Returns 0 if there is no target.
// Typed as unsigned int so this header does not have to drag in GL.
unsigned int vr_target_fbo_id( void );
void         vr_target_get_size( int * w, int * h );

// A see-through menu screen or HUD this frame: black is transparent, and the
// rectangles in vr_panel_solid (fractions of the target from the top left:
// x0, y0, x1, y1) stay solid: the pictures in pictures. Reset every frame.
extern bool  vr_panel_keyed;
extern float vr_panel_solid[2][4];
extern int   vr_panel_solid_n;

// The target, keyed, drawn to fill another framebuffer (the quad's image).
bool vr_target_key_into( unsigned int dst_fbo, int dw, int dh );

#endif // VR_TARGET_INCLUDED
