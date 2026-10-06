/* Forsaken VR: controller vibration
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

/* Controller vibration.
 *
 * Pulses are read from the RESULT of the frame, never from weapon code: the
 * primary ammo pools going down means a primary fired, the secondary counts
 * going down means a missile or mine left the ship, shield plus hull going
 * down means something hit us. Every weapon, and anything that changes how
 * weapons work, is covered the same way without a hook in each one.
 *
 * Primary buzzes the hand holding the primary trigger, secondary the other
 * hand, damage both: physically, so the Leftorium moves the gun's buzz to
 * the left hand along with the trigger.
 *
 * ON by default (VRVibration 1), now that it has been felt in a headset. A
 * desk can only prove the events, so every pulse can be logged with its
 * reason: -vrhapticslog. */

#include <stdio.h>
#include <math.h>
#include "main.h"
#include "render.h"
#include "vr_openxr.h"
#include "vr_target.h"
#include "new3d.h"
#include "compobjects.h"
#include "quat.h"
#include "object.h"
#include "networking.h"
#include "2dtextures.h"
#include "primary.h"
#include "secondary.h"
#include "util.h"
#include "vr_haptics.h"

extern BYTE    MyGameStatus;
extern float   GeneralAmmo, PyroliteAmmo, SussGunAmmo;
extern int16_t SecondaryAmmo[ MAXSECONDARYWEAPONS ];
extern bool    MenuIsActive( void );

int  vr_vibration   = 1;      /* VRVibration: 0 off, 1 on */
bool vr_haptics_log = false;  /* -vrhapticslog */
int  vr_haptics_pulses = 0;   /* how many were raised, for the shutdown line */

#define HAND_LEFT  0
#define HAND_RIGHT 1

static void pulse( int hand, float amp, int ms, const char * why )
{
	bool sent = vr_openxr_haptic( hand, amp, ms );

	vr_haptics_pulses++;
	if( vr_haptics_log )
		DebugPrintf( "vr: haptic %-5s %.2f %3d ms %s -- %s\n",
		             hand == HAND_LEFT ? "LEFT" : "RIGHT", amp, ms,
		             sent ? "sent" : "not sent (no session)", why );
}

void vr_haptics_update( void )
{
	static bool      have = false;
	static float     prev_prim, prev_hp;
	static int       prev_sec;
	static u_int32_t last_prim = 0;
	float prim, hp;
	int   sec, i;
	int   gun_hand, off_hand;
	u_int32_t now;
	char  why[96];

	if( !vr_enabled || vr_vibration <= 0 )
	{
		have = false;
		return;
	}

	/* Only while flying: the title, menus, death and level loads all rewrite
	   these values without anything having fired or hit. */
	if( ( MyGameStatus != STATUS_SinglePlayer && MyGameStatus != STATUS_Normal ) ||
	    MenuIsActive() || Ships[ WhoIAm ].Object.Mode != NORMAL_MODE )
	{
		have = false;
		return;
	}

	prim = GeneralAmmo + PyroliteAmmo + SussGunAmmo;
	sec  = 0;
	for( i = 0; i < MAXSECONDARYWEAPONS; i++ )
		sec += SecondaryAmmo[ i ];
	hp   = Ships[ WhoIAm ].Object.Shield + Ships[ WhoIAm ].Object.Hull;
	now  = SDL_GetTicks();

	/* the primary trigger's physical hand */
	gun_hand = vr_leftorium ? HAND_LEFT : HAND_RIGHT;
	off_hand = 1 - gun_hand;

	if( have )
	{
		/* Rapid-fire weapons spend every frame; one short tick every 70 ms
		   reads as a continuous buzz without flooding the runtime. */
		if( prim < prev_prim - 0.001f && now - last_prim >= 70 )
		{
			sprintf( why, "primary fired (ammo %.1f -> %.1f)", prev_prim, prim );
			pulse( gun_hand, 0.35f, 30, why );
			last_prim = now;
		}

		if( sec < prev_sec )
		{
			sprintf( why, "secondary fired (%d -> %d)", prev_sec, sec );
			pulse( off_hand, 0.80f, 90, why );
		}

		if( hp < prev_hp - 0.01f )
		{
			float dmg = prev_hp - hp;
			float amp = 0.35f + dmg / 40.0f;
			int   ms  = 60 + (int)( dmg * 3.0f );
			if( amp > 1.0f ) amp = 1.0f;
			if( ms > 250 )   ms  = 250;
			sprintf( why, "took damage %.1f (%.1f -> %.1f)", dmg, prev_hp, hp );
			pulse( HAND_LEFT,  amp, ms, why );
			pulse( HAND_RIGHT, amp, ms, why );
		}
	}

	prev_prim = prim;
	prev_sec  = sec;
	prev_hp   = hp;
	have      = true;
}
