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

#include <stdio.h>
#include <string.h>
#include "main.h"
#include "config.h"
#include "util.h"
#include "vr_binds.h"

bool vrb_frame = false;
bool vrb_force_defaults = false;

static const char * act_names[VRB_ACT_COUNT] = {
	"Nitro", "Drop mine", "Roll left", "Roll right",
	"Next primary", "Next secondary", "Pull-back view", "Rear-view mirror"
};
static const char * act_keys[VRB_ACT_COUNT] = {
	"Nitro", "Mine", "RollLeft", "RollRight",
	"NextPrimary", "NextSecondary", "PullBack", "RearView"
};

/* Quest (Touch): the layout the port has always shipped. */
static const int touch_defaults[VRB_ACT_COUNT] = {
	VRB_PAD_A,         /* nitro          */
	VRB_PAD_B,         /* drop mine      */
	VRB_PAD_X,         /* roll left      */
	VRB_PAD_Y,         /* roll right     */
	VRB_PAD_LSTICK,    /* next primary   */
	VRB_PAD_NONE,      /* next secondary */
	VRB_PAD_RSTICK,    /* pull-back view */
	VRB_PAD_NONE       /* rear view      */
};

/* Steam Frame: as the Quest wherever the Frame has the same button, so a
   player moving between the two finds the same controls: left stick click
   next primary, right stick click pull-back view, A nitro, B drop mine. The
   Frame's left hand has no X / Y, so the rolls go to the shoulders, which
   need no thumb off the sticks. The D-pad takes what the Quest leaves
   unassigned: right next secondary, up the rear-view mirror. */
static const int frame_defaults[VRB_ACT_COUNT] = {
	VRB_PAD_A,         /* nitro          */
	VRB_PAD_B,         /* drop mine      */
	VRB_PAD_LSHOULDER, /* roll left      */
	VRB_PAD_RSHOULDER, /* roll right     */
	VRB_PAD_LSTICK,    /* next primary   */
	VRB_PAD_DRIGHT,    /* next secondary */
	VRB_PAD_RSTICK,    /* pull-back view */
	VRB_PAD_DUP        /* rear view      */
};

static int map_touch[VRB_ACT_COUNT];
static int map_frame[VRB_ACT_COUNT];
static bool maps_ready = false;

static void ensure_maps( void )
{
	if( maps_ready )
		return;
	memcpy( map_touch, touch_defaults, sizeof(map_touch) );
	memcpy( map_frame, frame_defaults, sizeof(map_frame) );
	maps_ready = true;
}

const char * vrb_act_label( int act )
{
	return ( act >= 0 && act < VRB_ACT_COUNT ) ? act_names[act] : "?";
}

const char * vrb_pad_label( int pad, bool frame )
{
	switch( pad )
	{
	case VRB_PAD_NONE:      return "nothing";
	case VRB_PAD_A:         return frame ? "A" : "A (right hand)";
	case VRB_PAD_B:         return frame ? "B" : "B (right hand)";
	case VRB_PAD_X:         return frame ? "X" : "X (left hand)";
	case VRB_PAD_Y:         return frame ? "Y" : "Y (left hand)";
	case VRB_PAD_LSTICK:    return "left stick click";
	case VRB_PAD_RSTICK:    return "right stick click";
	case VRB_PAD_DUP:       return "D-pad up";
	case VRB_PAD_DDOWN:     return "D-pad down";
	case VRB_PAD_DLEFT:     return "D-pad left";
	case VRB_PAD_DRIGHT:    return "D-pad right";
	case VRB_PAD_LSHOULDER: return "left shoulder";
	case VRB_PAD_RSHOULDER: return "right shoulder";
	}
	return "?";
}

int vrb_pad_count( bool frame )
{
	return frame ? VRB_PAD_COUNT : VRB_PAD_RSTICK + 1;
}

int vrb_default( int act, bool frame )
{
	if( act < 0 || act >= VRB_ACT_COUNT )
		return VRB_PAD_NONE;
	return frame ? frame_defaults[act] : touch_defaults[act];
}

int vrb_get( int act, bool frame )
{
	ensure_maps();
	if( act < 0 || act >= VRB_ACT_COUNT )
		return VRB_PAD_NONE;
	return frame ? map_frame[act] : map_touch[act];
}

static void save_one( int act, bool frame )
{
	char key[48];
	snprintf( key, sizeof(key), "VRBind%s%s", frame ? "Frame" : "Quest", act_keys[act] );
	config_set_int( key, frame ? map_frame[act] : map_touch[act] );
}

void vrb_set( int act, bool frame, int pad )
{
	int * map;
	int   a;

	ensure_maps();
	if( act < 0 || act >= VRB_ACT_COUNT || pad < 0 || pad >= vrb_pad_count( frame ) )
		return;
	map = frame ? map_frame : map_touch;

	/* one button, one job */
	if( pad != VRB_PAD_NONE )
		for( a = 0; a < VRB_ACT_COUNT; a++ )
			if( a != act && map[a] == pad )
			{
				map[a] = VRB_PAD_NONE;
				save_one( a, frame );
				DebugPrintf( "vr binds: %s moved off %s\n", act_names[a], vrb_pad_label( pad, frame ) );
			}

	map[act] = pad;
	save_one( act, frame );
	config_save();
	DebugPrintf( "vr binds (%s): %s = %s\n", frame ? "Steam Frame" : "Quest",
	             act_names[act], vrb_pad_label( pad, frame ) );
}

void vrb_reset( bool frame )
{
	int a;
	ensure_maps();
	for( a = 0; a < VRB_ACT_COUNT; a++ )
	{
		if( frame ) map_frame[a] = frame_defaults[a];
		else        map_touch[a] = touch_defaults[a];
		save_one( a, frame );
	}
	config_save();
	DebugPrintf( "vr binds (%s): reset to defaults\n", frame ? "Steam Frame" : "Quest" );
}

void vrb_load( void )
{
	int  a, f;
	char key[48];

	ensure_maps();
	for( f = 0; f < 2; f++ )
		for( a = 0; a < VRB_ACT_COUNT; a++ )
		{
			int def = f ? frame_defaults[a] : touch_defaults[a];
			int v;
			snprintf( key, sizeof(key), "VRBind%s%s", f ? "Frame" : "Quest", act_keys[a] );
			v = config_get_int( key, def );
			if( v < 0 || v >= vrb_pad_count( f != 0 ) )
				v = def;
			if( f ) map_frame[a] = v; else map_touch[a] = v;
		}
}

void vrb_resolve( const bool pads[VRB_PAD_COUNT], bool held[VRB_ACT_COUNT],
                  bool pressed[VRB_ACT_COUNT] )
{
	static bool was[VRB_ACT_COUNT];
	int a;

	ensure_maps();
	for( a = 0; a < VRB_ACT_COUNT; a++ )
	{
		int pad = vrb_force_defaults ? vrb_default( a, vrb_frame )
		        : ( vrb_frame ? map_frame[a] : map_touch[a] );
		held[a]    = ( pad > VRB_PAD_NONE && pad < VRB_PAD_COUNT ) ? pads[pad] : false;
		pressed[a] = held[a] && !was[a];
		was[a]     = held[a];
	}
}
