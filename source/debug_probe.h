/* Forsaken VR: unattended diagnostic harness
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

#ifndef DEBUG_PROBE_H
#define DEBUG_PROBE_H

/* Unattended diagnostic harness.
 *
 * Three command line options, all inert unless asked for:
 *
 *   -autolevel:N   once the title screen is up, set NewLevelNum to N and call
 *                  StartASinglePlayerGame(): the engine's own entry point,
 *                  not a simulated walk through the menus.
 *   -autoquit:S    request a clean quit S seconds after the game starts, so a
 *                  run ends by itself. Never kill the process instead: under
 *                  -vr that leaves the OpenXR session wedged.
 *   -hudprobe      dump what the 2D pass actually hands the GPU, for a few
 *                  frames once in game. See probe_ortho_draw().
 *
 * This exists because the in-game HUD text has never rendered on the GL3 path
 * and every measurement so far was taken on the engine's own CPU structures,
 * which are all correct. Nobody has yet looked at what reaches OpenGL.
 */

#include "render.h"

extern int  probe_autolevel;    /* -1 = off */
extern bool probe_ignore_mouse; /* set by -autolevel: nobody is at the mouse */
extern int  probe_autoquit_ms;  /* 0 = off */
extern bool probe_hud;
extern int  probe_capture_ms;  /* -shot:S, 0 = off */
extern bool probe_injected_click;

/* Returns true if it consumed the option. Called from the cli loop in main.c. */
bool probe_parse_option( const char * option );

/* Once per frame from the main loop. Drives -autolevel and -autoquit. */
void probe_frame( void );

/* From draw_render_object() when orthographic. Cheap no-op unless -hudprobe. */
void probe_ortho_draw( RENDEROBJECT * renderObject, unsigned int program );

/* After the frame is drawn, before it is swapped. Writes logs/probe_frame.ppm. */
void probe_capture( void );

#endif
