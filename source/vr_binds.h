/* Forsaken VR, which controller button does which game action
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

#ifndef VR_BINDS_H
#define VR_BINDS_H

/* The Controls page in VR Options, and the one place the game asks which
 * button does what.
 *
 * ONE MAP PER CONTROLLER. Quest Touch controllers and the Steam Frame's are
 * laid out differently (the Frame's left hand has a D-pad and a shoulder
 * button where Touch has X and Y, and all four face buttons are on its right
 * hand) so each keeps its own map, and the page edits the one in the
 * player's hands. Stored as VRBindQuest<Action> / VRBindFrame<Action> in the
 * config, holding a button number; a key that is not there means the
 * default, so a config written before this existed plays exactly as before.
 *
 * NOT MAPPABLE, on purpose: the triggers (fire), the grips (rise and sink),
 * the sticks' movement and the menu button. Each map gives one button one
 * job: choosing a button for an action takes it off whatever had it.
 *
 * LEFTORIUM. On Touch the face buttons mirror with the hands (A/B with X/Y),
 * as they always have. The Frame's face buttons are all on one controller,
 * so its map stays where it is. */

#include <stdbool.h>

enum
{
	VRB_PAD_NONE = 0,
	VRB_PAD_A, VRB_PAD_B, VRB_PAD_X, VRB_PAD_Y,
	VRB_PAD_LSTICK, VRB_PAD_RSTICK,
	/* Steam Frame only */
	VRB_PAD_DUP, VRB_PAD_DDOWN, VRB_PAD_DLEFT, VRB_PAD_DRIGHT,
	VRB_PAD_LSHOULDER, VRB_PAD_RSHOULDER,
	VRB_PAD_COUNT
};

enum
{
	VRB_ACT_NITRO, VRB_ACT_MINE, VRB_ACT_ROLL_LEFT, VRB_ACT_ROLL_RIGHT,
	VRB_ACT_NEXT_PRIMARY, VRB_ACT_NEXT_SECONDARY,
	VRB_ACT_CHASE_VIEW, VRB_ACT_REAR_VIEW,
	VRB_ACT_COUNT
};

extern bool vrb_frame;   /* the Steam Frame's profile is the one bound */
extern bool vrb_force_defaults;   /* -testhands: resolve with the shipped maps */

const char * vrb_act_label( int act );               /* "Nitro"            */
const char * vrb_pad_label( int pad, bool frame );   /* "X (left hand)"    */
int          vrb_pad_count( bool frame );            /* pads incl. NONE    */

int  vrb_default( int act, bool frame );
int  vrb_get( int act, bool frame );
void vrb_set( int act, bool frame, int pad );        /* one button, one job */
void vrb_reset( bool frame );

/* Read the saved maps; once, after the config is loaded. */
void vrb_load( void );

/* Physical buttons (after the hand swaps) -> held and pressed-this-frame
   per action, for whichever controller is bound. Pure apart from the
   pressed edges it keeps between calls. */
void vrb_resolve( const bool pads[VRB_PAD_COUNT], bool held[VRB_ACT_COUNT],
                  bool pressed[VRB_ACT_COUNT] );

#endif
