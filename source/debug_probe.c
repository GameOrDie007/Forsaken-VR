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

/* Unattended diagnostic harness: see debug_probe.h for what and why. */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <SDL.h>

#include "main.h"
#include "render.h"
#include "util.h"
#include "debug_probe.h"
#include "vr_openxr.h"
#include "new3d.h"
#include "compobjects.h"
#include "quat.h"
#include "object.h"
#include "networking.h"
#include "title.h"       /* MENUITEM, needed by singleplayer.h */
#include "text.h"
#include "singleplayer.h"
#include "triggers.h"
#include "new3d.h"

#if GL > 1
#include "render_gl_shared.h"
#endif

extern int16_t NewLevelNum;
extern BYTE    MyGameStatus;
extern bool    QuitRequested;
extern void    input_buffer_send( int code );
extern int     FontWidth;
extern int     FontHeight;
extern SLIDER  TextScaleSlider;

int  probe_draw_seq    = 0;   /* every draw, 2D and 3D, in submission order */
int  probe_capture_ms  = 0;
int  probe_fire_ms     = 0;   /* -probefire:S, hold the primary trigger 1.5 s, S seconds in */
int  probe_fire2_ms    = 0;   /* -probefire2:S, tap the secondary trigger */
static bool probe_shot_missile = false;  /* -shotmissile: dump the frame the missile camera comes on */
extern int16_t LevelNum;
static bool probe_vrkit = false;  /* -vrkit: every weapon at each level start, for headset tests */
int  probe_point_ms    = 0;   /* -probepoint:S, S s after a menu opens: hold the grip 2 s, trigger at 1 s for 0.3 s */
int  probe_hurt_ms     = 0;   /* -probehurt:S, take 16 shield damage S seconds in */
static int probe_nextlevel_ms = 0;   /* -probenextlevel:S, the level's own exit trigger S s in */
static int probe_die_ms       = 0;   /* -probedie:S, our ship destroyed S s in, through DoDamage */
static bool probe_level_ended = false; /* -probenextlevel ended one: carry on into the next */
static bool probe_respawn     = false; /* set by -probedie: pull the trigger until flying again */
static int probe_crash_ms     = 0;   /* -probecrash:S, an access violation S s in: the crash report */
static int probe_stick_ms     = 0;   /* -probestick:S, the right stick held full right for 4 s, S s in */
static int   probe_loop_ms    = 0;   /* -probeloop:S:X, the right stick held up (and X sideways) for 4 s */
static float probe_loop_x     = 0.0f;
static int   probe_flash_ms   = 0;   /* -probeflash:S, a hit through the damage routine S s in, eyes and HUD screen dumped that frame */
static int   probe_roll_ms    = 0;   /* -proberoll:S, roll left for 1.5 s, S s in, then let go and watch the roll */
int  probe_esc_ms      = 0;   /* -probeesc:S, press Escape S seconds into the level */
int  probe_click_ms    = 0;   /* -probeclick:S, left-stick click S seconds into the level */
int  probe_click2_ms   = 0;   /* -probeclick2:S, a second click */
int  probe_title_ms    = 0;   /* -titleshot:S, dump eyes + panel on the TITLE screen */
extern bool MenuIsActive( void );
static bool probe_seq_ingame = false;  /* -pauseseq: the same keys, in the pause menu */
static char probe_title_seq[64] = "";   /* -titleseq:DDDDS, keys to press on the title */
bool probe_injected_click = false;
int  probe_autolevel   = -1;
static bool probe_campaign = false;   /* -autocampaign:N: the single-player levels, not the battle list */

/* An unattended run has nobody at the mouse, so any mouse movement it reads is
   contamination. Found the hard way: when relative mouse mode starts, SDL
   reports the jump from wherever the cursor happened to be to the window's
   centre (dx=-566 dy=-878 in one run) and that kicked the ship to a
   different attitude every time. Two captures of "the same moment" then looked
   at different walls, and a camera fault behind the pause menu was very nearly
   blamed on the menu. */
bool probe_ignore_mouse = false;
int  probe_autoquit_ms = 0;
bool probe_hud         = false;

bool probe_parse_option( const char * option )
{
	int n;

	if ( sscanf( option, "autocampaign:%d", &n ) == 1 )
	{
		/* the campaign's level N, as the Single Player disc starts it */
		probe_autolevel = n;
		probe_campaign = true;
		probe_ignore_mouse = true;
		return true;
	}
	if ( sscanf( option, "autolevel:%d", &n ) == 1 )
	{
		probe_autolevel = n;
		probe_ignore_mouse = true;
		DebugPrintf( "probe: -autolevel:%d\n", n );
		return true;
	}
	if ( sscanf( option, "autoquit:%d", &n ) == 1 )
	{
		probe_autoquit_ms = n * 1000;
		DebugPrintf( "probe: -autoquit:%d seconds\n", n );
		return true;
	}
	if ( sscanf( option, "shot:%d", &n ) == 1 )
	{
		probe_capture_ms = n * 1000;
		DebugPrintf( "probe: -shot:%d seconds after the level starts\n", n );
		return true;
	}
	if ( sscanf( option, "probehurt:%d", &n ) == 1 )
	{
		probe_hurt_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probenextlevel:%d", &n ) == 1 )
	{
		probe_nextlevel_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probedie:%d", &n ) == 1 )
	{
		probe_die_ms = n * 1000;
		probe_respawn = true;
		return true;
	}
	/* -probedeath:S: the same death, then stay dead, to look at the death screen */
	if ( sscanf( option, "probedeath:%d", &n ) == 1 )
	{
		probe_die_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probecrash:%d", &n ) == 1 )
	{
		probe_crash_ms = n * 1000;
		return true;
	}
	{
		float x;
		if ( sscanf( option, "probeloop:%d:%f", &n, &x ) == 2 )
		{
			probe_loop_ms = n * 1000;
			probe_loop_x  = x;
			return true;
		}
	}
	if ( sscanf( option, "probeflash:%d", &n ) == 1 )
	{
		probe_flash_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "proberoll:%d", &n ) == 1 )
	{
		probe_roll_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probestick:%d", &n ) == 1 )
	{
		probe_stick_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probefire:%d", &n ) == 1 )
	{
		probe_fire_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probefire2:%d", &n ) == 1 )
	{
		probe_fire2_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probeesc:%d", &n ) == 1 )
	{
		probe_esc_ms = n * 1000;
		DebugPrintf( "probe: -probeesc:%d, Escape that many seconds into the level\n", n );
		return true;
	}
	if ( !strcasecmp( option, "vrkit" ) )
	{
		probe_vrkit = true;
		return true;
	}
	if ( sscanf( option, "probepoint:%d", &n ) == 1 )
	{
		probe_point_ms = n * 1000;
		return true;
	}
	if ( !strcasecmp( option, "shotmissile" ) )
	{
		probe_shot_missile = true;
		return true;
	}
	if ( sscanf( option, "proberear:%d", &n ) == 1 )
	{
		extern bool RearCameraActive;
		RearCameraActive = n != 0;   /* the F6 rear-view mirror, forced on or off */
		return true;
	}
	if ( !strncasecmp( option, "pauseseq:", 9 ) )
	{
		strncpy( probe_title_seq, option + 9, sizeof(probe_title_seq) - 1 );
		probe_seq_ingame   = true;
		probe_ignore_mouse = true;
		return true;
	}
	if ( !strncasecmp( option, "titleseq:", 9 ) )
	{
		strncpy( probe_title_seq, option + 9, sizeof(probe_title_seq) - 1 );
		probe_ignore_mouse = true;
		DebugPrintf( "probe: -titleseq:%s\n", probe_title_seq );
		return true;
	}
	if ( sscanf( option, "titleshot:%d", &n ) == 1 )
	{
		probe_title_ms = n * 1000;
		probe_ignore_mouse = true;
		DebugPrintf( "probe: -titleshot:%d, dump eyes + panel on the title\n", n );
		return true;
	}
	if ( sscanf( option, "probeclick2:%d", &n ) == 1 )
	{
		probe_click2_ms = n * 1000;
		return true;
	}
	if ( sscanf( option, "probeclick:%d", &n ) == 1 )
	{
		probe_click_ms = n * 1000;
		DebugPrintf( "probe: -probeclick:%d, left-stick click that many seconds in\n", n );
		return true;
	}
	if ( !strcasecmp( option, "hudprobe" ) )
	{
		probe_hud = true;
		DebugPrintf( "probe: -hudprobe\n" );
		return true;
	}
	return false;
}

/* ------------------------------------------------------------------ frame */

void probe_frame( void )
{
	static bool     started    = false;
	static u_int32_t title_since = 0;
	static u_int32_t start_time  = 0;
	u_int32_t now = SDL_GetTicks();

	if ( probe_autolevel >= 0 && !started )
	{
		/* Wait until the title screen has been up for a moment: InitLevels()
		   has to have filled the level list before NewLevelNum means anything. */
		if ( MyGameStatus == STATUS_Title )
		{
			if ( !title_since )
				title_since = now;

			if ( now - title_since > 3000 )
			{
				if ( probe_campaign )
				{
					extern bool    InitLevels( char * levels_list );
					extern int16_t Lives;
					InitLevels( SINGLEPLAYER_LEVELS );   /* as InitSinglePlayerGame does */
					Lives = 5;
					DebugPrintf( "probe: the campaign's levels\n" );
				}
				DebugPrintf( "probe: starting single player on level %d\n", probe_autolevel );
				NewLevelNum = (int16_t) probe_autolevel;
				StartASinglePlayerGame( NULL );
				started    = true;
				start_time = now;
			}
		}
		else
		{
			title_since = 0;
		}
	}

	/* Pull the triggers with no-one at the controls, through vr_input (the
	   same struct the Quest controllers fill) so the VR control path turns
	   it into fire exactly as a real pull would. Headless has no session to
	   refill vr_input, so what is written here stays until cleared. */
	if ( ( probe_fire_ms || probe_fire2_ms ) && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t fire_since = 0;
		u_int32_t t;
		if ( !fire_since )
			fire_since = now;
		t = now - fire_since;
		vr_input_active = true;
		vr_input.fire1 = ( probe_fire_ms  && t > (u_int32_t) probe_fire_ms &&
		                   t < (u_int32_t) probe_fire_ms + 1500 ) ? 1.0f : 0.0f;
		vr_input.fire2 = ( probe_fire2_ms && t > (u_int32_t) probe_fire2_ms &&
		                   t < (u_int32_t) probe_fire2_ms + 150 ) ? 1.0f : 0.0f;
	}

	/* The VR test kit: every weapon, full power and ammo, once at each level
	   start: the god-mode cheat's own GivemeAllWeapons(), so aiming can be
	   tried with all of them in one headset session. */
	if ( probe_vrkit )
	{
		extern void GivemeAllWeapons( void );
		static int16_t kit_level = -1;
		if ( MyGameStatus == STATUS_SinglePlayer && WhoIAm < MAX_PLAYERS &&
		     Ships[ WhoIAm ].Object.Mode == NORMAL_MODE && kit_level != LevelNum )
		{
			GivemeAllWeapons();
			kit_level = LevelNum;
			DebugPrintf( "probe: VR test kit -- every weapon for level %d\n", (int) LevelNum );
		}
		else if ( MyGameStatus != STATUS_SinglePlayer )
			kit_level = -1;
	}

	if ( probe_point_ms )
	{
		static u_int32_t menu_since = 0;
		/* in a level: from when its menu opens; otherwise the title's */
		bool here = ( MyGameStatus == STATUS_SinglePlayer ) ? MenuIsActive()
		                                                     : ( MyGameStatus == STATUS_Title );
		if ( !here )
			menu_since = 0;
		else
		{
			u_int32_t t;
			if ( !menu_since ) menu_since = now;
			t = now - menu_since;
			vr_input_active = true;
			vr_input.slide_up = ( t > (u_int32_t) probe_point_ms && t < (u_int32_t) probe_point_ms + 2000 );
			vr_input.fire1    = ( t > (u_int32_t) probe_point_ms + 1000 && t < (u_int32_t) probe_point_ms + 1300 ) ? 1.0f : 0.0f;
		}
	}

	if ( probe_shot_missile && MyGameStatus == STATUS_SinglePlayer )
	{
		extern int MissileCameraActive;
		if ( MissileCameraActive )
		{
			vr_dump_frame      = true;
			probe_shot_missile = false;
		}
	}

	/* After a level ended: the game goes back to the title room and shows the
	   next level's page. Press select there, as A on a controller does, every
	   3 s until a level is running again, and say which. */
	{
		static int       stage = 0;   /* 0 idle, 1 level ended, 2 waiting for the next */
		static u_int32_t since = 0;
		if ( probe_level_ended && stage == 0 )
		{
			stage = 1;
			since = now;
		}
		if ( stage && MyGameStatus != STATUS_SinglePlayer && now - since > 3000 )
		{
			DebugPrintf( "probe: between levels, game status %d\n", (int) MyGameStatus );
			DebugPrintf( "probe: pressing select on the next level's page\n" );
			input_buffer_send( SDLK_RETURN );
			since = now;
			stage = 2;
		}
		if ( stage == 2 && MyGameStatus == STATUS_SinglePlayer )
		{
			DebugPrintf( "probe: the next level is running (level %d)\n", (int) LevelNum );
			stage = 0;
			probe_level_ended = false;
		}
	}

	/* End the level the way its exit does: set the level's own end trigger
	   variable, which CheckLevelEnd() reads, so the whole change of level -
	   the stats screen, the load, the next level's start: is the game's. */
	if ( probe_nextlevel_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		extern TRIGGERVAR * Level_End;
		static u_int32_t since = 0;
		if ( !since )
			since = now;
		else if ( now - since > (u_int32_t) probe_nextlevel_ms )
		{
			if ( Level_End )
			{
				DebugPrintf( "probe: ending level %d through its own exit trigger\n", (int) LevelNum );
				Level_End->State = 1;
				probe_level_ended = true;
			}
			else
			{
				extern char       LevelNames[][128];
				extern TRIGGERVAR * TrigVars;
				extern int        NumOfTrigVars;
				int i;
				DebugPrintf( "probe: level %d (%s) has no exit trigger to end it with; %d trigger variables:\n",
				             (int) LevelNum, LevelNum >= 0 ? LevelNames[ LevelNum ] : "?", NumOfTrigVars );
				for ( i = 0; i < NumOfTrigVars && i < 12 && TrigVars; i++ )
					DebugPrintf( "probe:   '%s'\n", TrigVars[ i ].Name );
				for ( i = 0; i < 64 && LevelNames[ i ][ 0 ]; i++ )
					DebugPrintf( "probe:   level %d is %s\n", i, LevelNames[ i ] );
			}
			probe_nextlevel_ms = 0;
		}
	}

	/* Destroy our own ship through the game's damage routine, as a hit that
	   kills does (models.c), then say when the player is flying again. */
	if ( probe_die_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t since = 0;
		if ( !since )
			since = now;
		else if ( now - since > (u_int32_t) probe_die_ms && Ships[ WhoIAm ].Object.Mode == NORMAL_MODE )
		{
			extern int16_t DoDamage( bool OverrideInvul );
			Ships[ WhoIAm ].Damage = 5000.0F;
			Ships[ WhoIAm ].ShipThatLastHitMe = WhoIAm;
			if ( DoDamage( true ) == 1 )
			{
				Ships[ WhoIAm ].ShipThatLastKilledMe = WhoIAm;
				Ships[ WhoIAm ].Object.Mode = DEATH_MODE;
				Ships[ WhoIAm ].Timer = 0.0F;
				DebugPrintf( "probe: our ship destroyed\n" );
			}
			else
				DebugPrintf( "probe: the damage did not destroy our ship\n" );
			probe_die_ms = 0;
		}
	}
	/* After a death: pull the trigger every 2 s, as a player in the headset
	   would to fly again (no keyboard), until the ship is back. Desk runs
	   only (-probedie): in a player's game this must not touch the trigger. */
	{
		static bool      dead = false;
		static u_int32_t dead_since = 0;
		if ( probe_respawn && MyGameStatus == STATUS_SinglePlayer && WhoIAm < MAX_PLAYERS )
		{
			int mode = Ships[ WhoIAm ].Object.Mode;
			if ( mode != NORMAL_MODE && !dead )
			{
				dead = true;
				dead_since = now;
			}
			else if ( mode == NORMAL_MODE && dead )
			{
				dead = false;
				vr_input.fire1 = 0.0f;
				DebugPrintf( "probe: flying again after dying\n" );
			}
			if ( dead )
			{
				u_int32_t t = now - dead_since;
				vr_input_active = true;
				if ( t > 2000 && ( t % 2000 ) < 300 )
				{
					if ( vr_input.fire1 < 0.5f )
						DebugPrintf( "probe: pulling the trigger to fly again\n" );
					vr_input.fire1 = 1.0f;
				}
				else
					vr_input.fire1 = 0.0f;
			}
		}
	}

	/* A hit through the game's own damage routine, which makes the red flash,
	   with both eyes and the HUD screen dumped on the same frame. */
	if ( probe_flash_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t since = 0;
		if ( !since )
			since = now;
		else if ( now - since > (u_int32_t) probe_flash_ms )
		{
			extern int16_t DoDamage( bool OverrideInvul );
			extern u_int16_t FlashScreenPoly;
			Ships[ WhoIAm ].Damage = 8.0F;
			DoDamage( true );
			vr_dump_frame = true;
			DebugPrintf( "probe: hit for the flash, screen poly %d, dumping this frame\n", (int) FlashScreenPoly );
			probe_flash_ms = 0;
		}
	}

	/* Roll left for 1.5 s, let go, and log how far the ship is from level then
	   and 5 s later: with auto-level on it comes back, with it off it stays. */
	if ( probe_roll_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		extern float ShipRollFromLevel( u_int16_t ship );
		static u_int32_t since = 0;
		static int stage = 0;   /* 0 before, 1 rolling, 2 let go, 3 done */
		u_int32_t t;
		if ( !since )
			since = now;
		t = now - since;
		vr_input_active = true;
		if ( stage == 0 && t > (u_int32_t) probe_roll_ms )
		{
			DebugPrintf( "probe: rolling left, %.1f deg from level\n", (float) fabs( ShipRollFromLevel( WhoIAm ) ) );
			stage = 1;
		}
		if ( stage == 1 )
		{
			vr_input.roll_left = true;
			if ( t > (u_int32_t) probe_roll_ms + 1500 )
			{
				vr_input.roll_left = false;
				DebugPrintf( "probe: let go of roll, %.1f deg from level\n", (float) fabs( ShipRollFromLevel( WhoIAm ) ) );
				stage = 2;
			}
		}
		if ( stage == 2 && t > (u_int32_t) probe_roll_ms + 6500 )
		{
			DebugPrintf( "probe: 5 s after letting go, %.1f deg from level\n", (float) fabs( ShipRollFromLevel( WhoIAm ) ) );
			stage = 3;
		}
	}

	/* A loop on the right stick: held full up, X sideways, for 4 s. A clean
	   pitch keeps the ship's right side where it was; yaw mixed in swings it,
	   which is the corkscrew a player reads as rolling. Logs the swing. */
	if ( probe_loop_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		extern VECTOR SlideRight;
		static u_int32_t since = 0;
		static int  stage = 0;   /* 0 before, 1 holding, 2 done */
		static VECTOR right0;
		u_int32_t t;
		if ( !since )
			since = now;
		t = now - since;
		vr_input_active = true;
		if ( stage == 0 && t > (u_int32_t) probe_loop_ms )
		{
			ApplyMatrix( &Ships[ WhoIAm ].Object.Mat, &SlideRight, &right0 );
			DebugPrintf( "probe: right stick held up, %.2f sideways\n", probe_loop_x );
			stage = 1;
		}
		if ( stage == 1 )
		{
			vr_input.look_x = probe_loop_x;
			vr_input.look_y = 1.0f;
			if ( t > (u_int32_t) probe_loop_ms + 4000 )
			{
				VECTOR r;
				float c;
				ApplyMatrix( &Ships[ WhoIAm ].Object.Mat, &SlideRight, &r );
				c = right0.x * r.x + right0.y * r.y + right0.z * r.z;
				if ( c > 1.0f ) c = 1.0f;
				if ( c < -1.0f ) c = -1.0f;
				DebugPrintf( "probe: loop done, the ship's right side swung %.1f deg\n", (float) ( acos( c ) * 57.29578 ) );
				vr_input.look_x = vr_input.look_y = 0.0f;
				stage = 2;
			}
		}
	}

	/* The right stick held full right for 4 s, through vr_input like the
	   controllers: the turn rate it gives is the "Turn speed" setting's. The
	   ship's heading is in the -vraimlog lines once a second. */
	if ( probe_stick_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t since = 0;
		static bool held = false;
		u_int32_t t;
		if ( !since )
			since = now;
		t = now - since;
		vr_input_active = true;
		if ( t > (u_int32_t) probe_stick_ms && t < (u_int32_t) probe_stick_ms + 4000 )
		{
			if ( !held )
				DebugPrintf( "probe: right stick held full right\n" );
			held = true;
			vr_input.look_x = 1.0f;
		}
		else
		{
			if ( held )
				DebugPrintf( "probe: right stick let go\n" );
			held = false;
			vr_input.look_x = 0.0f;
		}
	}

	/* The crash report, proved: an access violation on purpose. */
	if ( probe_crash_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t since = 0;
		if ( !since )
			since = now;
		else if ( now - since > (u_int32_t) probe_crash_ms )
		{
			DebugPrintf( "probe: crashing on purpose\n" );
			*(volatile int *) 0 = 0;
		}
	}

	if ( probe_hurt_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t hurt_since = 0;
		if ( !hurt_since )
			hurt_since = now;
		else if ( now - hurt_since > (u_int32_t) probe_hurt_ms )
		{
			DebugPrintf( "probe: taking 16 shield damage\n" );
			Ships[ WhoIAm ].Object.Shield -= 16.0F;
			probe_hurt_ms = 0;
		}
	}

	/* Open the in-game menu with no-one at the controls, by the same route the
	   Quest menu button takes (input_buffer_send( SDLK_ESCAPE )) so every
	   menu-open measurement exercises the path a player's press does. */
	if ( probe_esc_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t esc_since = 0;
		if ( !esc_since )
			esc_since = now;
		else if ( now - esc_since > (u_int32_t) probe_esc_ms )
		{
			DebugPrintf( "probe: pressing Escape to open the menu\n" );
			input_buffer_send( SDLK_ESCAPE );
			probe_esc_ms = 0;
		}
	}

	/* Drive the title menus with no-one at the controls: one key every 0.7 s,
	   starting 2 s after the title comes up, through input_buffer_send():
	   the same route the Quest controllers take into the menus.
	   D U L R = arrows, S = select (Return), E = escape. */
	if ( probe_title_seq[0] &&
	     ( probe_seq_ingame ? ( MyGameStatus == STATUS_SinglePlayer && MenuIsActive() )
	                        : ( MyGameStatus == STATUS_Title ) ) )
	{
		static u_int32_t seq_first = 0, seq_last = 0;
		static int       seq_pos = 0;
		if ( !seq_first )
			seq_first = now;
		if ( now - seq_first > 2000 && now - seq_last > 700 && probe_title_seq[seq_pos] )
		{
			int key = 0;
			switch ( probe_title_seq[seq_pos] )
			{
			case 'D': case 'd': key = SDLK_DOWN;   break;
			case 'U': case 'u': key = SDLK_UP;     break;
			case 'L': case 'l': key = SDLK_LEFT;   break;
			case 'R': case 'r': key = SDLK_RIGHT;  break;
			case 'S': case 's': key = SDLK_RETURN; break;
			case 'E': case 'e': key = SDLK_ESCAPE; break;
			}
			if ( key )
			{
				DebugPrintf( "probe: menu key '%c'\n", probe_title_seq[seq_pos] );
				input_buffer_send( key );
			}
			seq_pos++;
			seq_last = now;
		}
	}

	/* The same dump on the title screen, where there is no level. */
	if ( probe_title_ms && MyGameStatus == STATUS_Title )
	{
		static u_int32_t title_first = 0;
		if ( !title_first )
			title_first = now;
		else if ( now - title_first >= (u_int32_t) probe_title_ms )
		{
			vr_dump_frame  = true;
			probe_title_ms = 0;
		}
	}

	/* Arm the headless eye + panel dump for the shot frame. This runs BEFORE
	   the frame renders, so the eyes and the panel are dumped as they are made;
	   probe_capture() afterwards only sees whatever was drawn last. */
	if ( probe_capture_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t dump_first = 0;
		static int       dump_done  = 0;
		if ( !dump_first )
			dump_first = now;
		if ( !dump_done && now - dump_first >= (u_int32_t) probe_capture_ms )
		{
			vr_dump_frame = true;
			dump_done = 1;
		}
	}

	/* A left-stick click, set on vr_input exactly as a controller's would be, so
	   the photo-mode toggle is driven through the same line in controls.c. */
	if ( probe_click_ms && MyGameStatus == STATUS_SinglePlayer )
	{
		static u_int32_t click_since = 0;
		if ( !click_since )
			click_since = now;
		else if ( now - click_since > (u_int32_t) probe_click_ms )
		{
			DebugPrintf( "probe: left-stick click\n" );
			vr_input.stick_l_edge = true;
			probe_injected_click  = true;
			probe_click_ms = 0;
		}
	}
	if ( probe_click2_ms && MyGameStatus == STATUS_SinglePlayer && !probe_injected_click )
	{
		static u_int32_t click2_since = 0;
		if ( !click2_since )
			click2_since = now;
		else if ( now - click2_since > (u_int32_t) probe_click2_ms )
		{
			DebugPrintf( "probe: second left-stick click\n" );
			vr_input.stick_l_edge = true;
			probe_injected_click  = true;
			probe_click2_ms = 0;
		}
	}

	if ( probe_autoquit_ms )
	{
		if ( !start_time )
			start_time = now;

		if ( now - start_time > (u_int32_t) probe_autoquit_ms )
		{
			/* Quit the way a player does: post SDL_QUIT, so it goes through
			   handle_events() -> app_quit() -> CleanUpAndPostQuit(), exactly
			   like closing the window or Alt+F4.

			   This used to set QuitRequested directly, which skipped
			   CleanUpAndPostQuit() altogether. Every desk run therefore quit
			   down a path no player ever takes, and a crash that lived only
			   on the real path (saving settings after Lua had been shut down)
			   passed every test here and crashed the first headset session. */
			SDL_Event quit;
			DebugPrintf( "probe: autoquit reached, posting SDL_QUIT like a window close\n" );
			memset( &quit, 0, sizeof(quit) );
			quit.type = SDL_QUIT;
			SDL_PushEvent( &quit );
			probe_autoquit_ms = 0;
		}
	}
}

/* ------------------------------------------------------------- ortho draw */

#if GL > 1

/* gl_Position = M * v, with M as OpenGL stores it: column major, so element
   (row r, column c) is m[c*4+r]. The matrix was uploaded with transpose=TRUE,
   which is exactly why this is read back rather than assumed. */
static void apply( const float * m, float x, float y, float z, float w, float * out )
{
	out[0] = m[0]*x + m[4]*y + m[ 8]*z + m[12]*w;
	out[1] = m[1]*x + m[5]*y + m[ 9]*z + m[13]*w;
	out[2] = m[2]*x + m[6]*y + m[10]*z + m[14]*w;
	out[3] = m[3]*x + m[7]*y + m[11]*z + m[15]*w;
}

/* Read the glyph's own texels back out of the atlas and report whether any of
   them could survive the colour-key. Two possible outcomes, no argument. */
static void probe_atlas_alpha( unsigned int texid, TLVERTEX * v, int n )
{
	GLint w = 0, h = 0;
	unsigned char * pix;
	float umin = 1e9f, umax = -1e9f, vmin = 1e9f, vmax = -1e9f;
	int i, x, y, x0, x1, y0, y1;
	int amin = 256, amax = -1, survive = 0, total = 0, nonblack = 0;

	glBindTexture( GL_TEXTURE_2D, texid );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &w );
	glGetTexLevelParameteriv( GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h );
	if ( w <= 0 || h <= 0 || w > 8192 || h > 8192 )
	{
		DebugPrintf( "PROBE: atlas %u implausible size %dx%d\n", texid, w, h );
		return;
	}

	pix = (unsigned char *) malloc( (size_t) w * h * 4 );
	if ( !pix )
		return;
	glGetTexImage( GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pix );

	for ( i = 0; i < n; i++ )
	{
		if ( v[i].tu < umin ) umin = v[i].tu;
		if ( v[i].tu > umax ) umax = v[i].tu;
		if ( v[i].tv < vmin ) vmin = v[i].tv;
		if ( v[i].tv > vmax ) vmax = v[i].tv;
	}

	x0 = (int)( umin * w ); x1 = (int)( umax * w );
	y0 = (int)( vmin * h ); y1 = (int)( vmax * h );
	if ( x1 <= x0 ) x1 = x0 + 1;
	if ( y1 <= y0 ) y1 = y0 + 1;
	if ( x0 < 0 ) x0 = 0;
	if ( y0 < 0 ) y0 = 0;
	if ( x1 > w ) x1 = w;
	if ( y1 > h ) y1 = h;

	for ( y = y0; y < y1; y++ )
	{
		for ( x = x0; x < x1; x++ )
		{
			unsigned char * p = pix + ( (size_t) y * w + x ) * 4;
			int a = p[3];
			if ( a < amin ) amin = a;
			if ( a > amax ) amax = a;
			if ( a > 100 ) survive++;
			if ( p[0] > 8 || p[1] > 8 || p[2] > 8 ) nonblack++;
			total++;
		}
	}

	DebugPrintf( "PROBE: atlas tex=%u %dx%d glyph uv=(%.4f..%.4f, %.4f..%.4f) texels [%d..%d]x[%d..%d]\n",
		texid, w, h, umin, umax, vmin, vmax, x0, x1, y0, y1 );
	DebugPrintf( "PROBE: atlas alpha min=%d max=%d texels=%d survive_colourkey=%d nonblack_rgb=%d\n",
		amin, amax, total, survive, nonblack );

	free( pix );
}

/* Dump the finished frame to a PPM. A statistic cannot tell a glyph that was
   never drawn from one drawn 8 pixels tall in the corner; the pixels can. */
void probe_capture( void )
{
	static int done = 0;
	static u_int32_t first = 0;
	u_int32_t now = SDL_GetTicks();
	int w, h, y;
	unsigned char * pix;
	FILE * f;
	GLint vp[4];

	if ( !probe_capture_ms || done )
		return;
	if ( MyGameStatus != STATUS_SinglePlayer )
		return;
	if ( !first )
		first = now;
	if ( now - first < (u_int32_t) probe_capture_ms )
		return;
	done = 1;

	glGetIntegerv( GL_VIEWPORT, vp );
	w = vp[2]; h = vp[3];
	if ( w <= 0 || h <= 0 )
		return;

	pix = (unsigned char *) malloc( (size_t) w * h * 3 );
	if ( !pix )
		return;

	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( vp[0], vp[1], w, h, GL_RGB, GL_UNSIGNED_BYTE, pix );

	f = fopen( "logs/probe_frame.ppm", "wb" );
	if ( f )
	{
		fprintf( f, "P6\n%d %d\n255\n", w, h );
		/* GL origin is bottom left; write top down so the file matches the screen */
		for ( y = h - 1; y >= 0; y-- )
			fwrite( pix + (size_t) y * w * 3, 1, (size_t) w * 3, f );
		fclose( f );
		DebugPrintf( "PROBE: wrote logs/probe_frame.ppm %dx%d\n", w, h );
	}
	free( pix );
}

void probe_ortho_draw( RENDEROBJECT * renderObject, unsigned int program )
{
	static int  frames_dumped = 0;
	static int  draws_this_frame = 0;
	static int  last_frame = -1;
	static bool header_done = false;
	static int  frame_counter = 0;
	static int  atlas_done = 0;

	float    ortho[16];
	GLint    viewport[4];
	GLint    scissor[4];
	GLint    loc;
	GLuint   vbo;
	int      g;

	if ( !probe_hud )
		return;

	/* Only in game. The menus use this same path and would swamp the log. */
	if ( MyGameStatus != STATUS_SinglePlayer )
		return;

	/* A handful of frames is plenty; the HUD is redrawn identically each one. */
	if ( frames_dumped > 3 )
		return;

	frame_counter++;
	if ( frame_counter != last_frame )
	{
		last_frame       = frame_counter;
		draws_this_frame = 0;
		frames_dumped++;
	}
	if ( draws_this_frame++ > 40 )
		return;

	memset( ortho, 0, sizeof(ortho) );
	loc = glGetUniformLocation( program, "ortho_proj" );
	if ( loc >= 0 )
		glGetUniformfv( program, loc, ortho );

	glGetIntegerv( GL_VIEWPORT, viewport );
	glGetIntegerv( GL_SCISSOR_BOX, scissor );

	if ( !header_done )
	{
		header_done = true;
		DebugPrintf( "PROBE: ==== 2D pass state ====\n" );
		DebugPrintf( "PROBE: FontWidth=%d FontHeight=%d TextScaleSlider=%d default_mode=%dx%d\n",
			FontWidth, FontHeight, TextScaleSlider.value,
			(int) render_info.default_mode.w, (int) render_info.default_mode.h );
		DebugPrintf( "PROBE: window_size = %ld x %ld\n",
			(long) render_info.window_size.cx, (long) render_info.window_size.cy );
		DebugPrintf( "PROBE: viewport    = %d %d %d %d\n",
			viewport[0], viewport[1], viewport[2], viewport[3] );
		DebugPrintf( "PROBE: scissor     = %d %d %d %d  enabled=%d\n",
			scissor[0], scissor[1], scissor[2], scissor[3],
			(int) glIsEnabled( GL_SCISSOR_TEST ) );
		DebugPrintf( "PROBE: depth_test=%d cull=%d blend=%d\n",
			(int) glIsEnabled( GL_DEPTH_TEST ),
			(int) glIsEnabled( GL_CULL_FACE ),
			(int) glIsEnabled( GL_BLEND ) );
		DebugPrintf( "PROBE: ortho_proj loc=%d (column major readback)\n", (int) loc );
		DebugPrintf( "PROBE:   %8.4f %8.4f %8.4f %8.4f\n", ortho[0], ortho[4], ortho[ 8], ortho[12] );
		DebugPrintf( "PROBE:   %8.4f %8.4f %8.4f %8.4f\n", ortho[1], ortho[5], ortho[ 9], ortho[13] );
		DebugPrintf( "PROBE:   %8.4f %8.4f %8.4f %8.4f\n", ortho[2], ortho[6], ortho[10], ortho[14] );
		DebugPrintf( "PROBE:   %8.4f %8.4f %8.4f %8.4f\n", ortho[3], ortho[7], ortho[11], ortho[15] );
	}

	vbo = (GLuint)(size_t) renderObject->lpVertexBuffer;
	DebugPrintf( "PROBE: draw seq=%d groups=%d vbo=%u\n", probe_draw_seq, renderObject->numTextureGroups, vbo );

	glBindBuffer( GL_ARRAY_BUFFER, vbo );

	for ( g = 0; g < renderObject->numTextureGroups && g < 6; g++ )
	{
		TEXTUREGROUP * grp = &renderObject->textureGroups[g];
		TLVERTEX v[4];
		int      i;
		int      n = grp->numVerts < 4 ? grp->numVerts : 4;

		DebugPrintf( "PROBE:  grp%d startVert=%d numVerts=%d tris=%d tex=%p ckey=%d\n",
			g, grp->startVert, grp->numVerts, grp->numTriangles,
			(void *) grp->texture, (int) grp->colourkey );

		if ( n <= 0 )
			continue;

		memset( v, 0, sizeof(v) );
		glGetBufferSubData( GL_ARRAY_BUFFER,
			(GLintptr)( grp->startVert * sizeof(TLVERTEX) ),
			(GLsizeiptr)( n * sizeof(TLVERTEX) ), v );

		for ( i = 0; i < n; i++ )
		{
			float clip[4];
			apply( ortho, v[i].x, v[i].y, v[i].z, 1.0f, clip );
			DebugPrintf( "PROBE:   v%d screen=(%8.2f,%8.2f,%8.4f) rhw=%.4f uv=(%.3f,%.3f) color=%08X vA=%3d -> clip=(%7.3f,%7.3f,%7.3f,%7.3f)%s\n",
				i, v[i].x, v[i].y, v[i].z, v[i].rhw, v[i].tu, v[i].tv,
				(unsigned int) v[i].color,
				(int)( ( (unsigned int) v[i].color >> 24 ) & 0xFF ),
				clip[0], clip[1], clip[2], clip[3],
				( clip[0] < -1.0f || clip[0] > 1.0f ||
				  clip[1] < -1.0f || clip[1] > 1.0f ||
				  clip[2] < -1.0f || clip[2] > 1.0f ) ? "  OFFSCREEN" : "" );
		}

		/* Does any texel under this glyph survive the colour-key discard?
		   fcolor = texture * vcolor, discarded when alpha <= 100/255. The atlas
		   was checked before for non-black PIXELS; its ALPHA never was. */
		if ( g == 0 && !atlas_done && grp->texture )
		{
			atlas_done = 1;
			probe_atlas_alpha( ( (texture_t *) grp->texture )->id, v, n );
		}
	}
}

#else

void probe_ortho_draw( RENDEROBJECT * renderObject, unsigned int program )
{
	(void) renderObject; (void) program;
}

#endif
