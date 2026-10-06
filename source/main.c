#include "main.h"
#include "main_sdl.h"
#include <stdio.h>
#include <string.h>
#include <search.h>
#include "version.h"
#include "render.h"
#include "util.h"
#include "file.h"
#include "net.h"
#include "title.h"
#include "lua_common.h"
#include "sfx.h"
#include <SDL.h>
#include "input.h"
#include "sound.h"
#include "vr_target.h"
#include "vr_openxr.h"
#include "music.h"
#include "debug_probe.h"
#include "movie.h"
#include "crash.h"
#include "oct2.h"
#include "vr_haptics.h"
#include "vr_splash.h"

/* menu slider that owns music volume; see -musicvol below */
extern SLIDER MusicSlider;

#ifndef WIN32
#include <unistd.h>
#endif

#ifdef __WINE__
#define LR_VGACOLOR LR_VGA_COLOR
#endif

//
// GLOBAL VARIABLES
//

bool Debug = true;
bool ShowFrameRate = false;
bool ShowInfo = false;

int cliSleep = 0;
static bool crash_want_test = false;

/* -hudfont:X. 0 means derive it from the resolution; see ApplyTextScale(). */
float hud_font_scale = 0.0F;
bool  vr_listener_log = false;   /* -vrlistenerlog */

/* -fakeeye:WxH: the pretend per-eye size, see vr_openxr.h */
int vr_fake_w = 0;
int vr_fake_h = 0;

render_info_t render_info;

//
// Parses the directory to change to from the command line options
//

static bool parse_chdir( char *cli )
{
    char * option;
	char cmdline[256];
	size_t size;

	size = strlen(cli)+1;
	if ( size > sizeof(cmdline) )
	{
		Msg("Command line to long!");
		return false;
	}
	strcpy(cmdline,cli);

	option = strtok(cmdline, " -+'\"");

    while(option != NULL )
	{
		// WARNING:	chdir can only be the LAST option !
		// For running the exe outside the root folder
		// -chdir c:\\Program Files\\ProjectX
		if (!strcasecmp(option,"chdir"))
		{
			// get option enclosed in quotes
			// this does not consider space as separator
			option = strtok(NULL, "\"'");

			if (!option)
			{
				Msg("Error using chdir");
				return false;
				break;
			}

			// change to root directory
			// the rest of the command line will be used as the path
			if( chdir( option ) != 0 )
			{
				// error
				Msg("Could not change to directory: %s", option);
				return false;
			}

			// dont loop anymore were done
			break;
		}
		// get the next token
        option = strtok(NULL, " -+'\"");
	}

	return true;
}

//
// checks for all critical folders
//

#define CRITICAL_FOLDERS 4
static bool missing_folders( void )
{
	int x = 0;
	char* folders[CRITICAL_FOLDERS] = {"Configs","Data","Pilots","Scripts"};
	for( x = 0; x < CRITICAL_FOLDERS; x++ )
		if( ! is_folder(folders[x]) )
		{
			Msg("Could not locate the '%s' folder...\n%s\n%s", folders[x],
				"exe is most likely in the wrong directory.",
				"or you just need to create the folder.");
			return true;
		}
	return false;
}


//
// Parse the Command Line
//

extern bool NoSFX;
extern float normal_fov;
extern float UV_Fix;
extern int NetUpdateIntervalCmdLine;
extern char *config_name;
extern int cliSleep;
extern TEXT local_port_str;
extern bool SpaceOrbSetup;
extern TEXT TCPAddress;
extern TEXT local_port_str;
extern TEXT host_port_str;
extern bool DebugLog;
extern u_int8_t QuickStart;
extern bool IpOnCLI;

static bool ParseCommandLine(char* lpCmdLine)
{
	
	//
	//  Locals
	//

    char * option;
	char cmdline[256];
	size_t size;
	int    introsecs = 0;
	int    fakew = 0, fakeh = 0;
	float  fakeyaw = 0.0f;
	float  hudanchor = 1.0f;
	int    worldmenu = 1;
	int    leftorium = 0, swapsticks = 0;
	int    vignette = 0;
	int    vibration = 0;
	int    spectator = 1;
	float  fakeroll = 0.0f;
	float  fakeaimy = 0.0f, fakeaimp = 0.0f;
	int    aimmode = 1;
	int    shipbob = 1;
	int    swivel = 0;
	int    autolevel = 1;
	int    turnspeed = 75;
	float  vigforce = -1.0f;
	float  hudfont = 0.0f;

    //
    //  Set Global Defaults
    //
	
	NoSFX					= false; // turns off sound
	Debug					= false; // turns it off now

	NetUpdateIntervalCmdLine	= 0;

	//
	// Get the command line string
	//

	size = strlen(lpCmdLine)+1;
	if ( size > sizeof(cmdline) )
	{
		Msg("Command line to long!");
		return false;
	}
	strcpy(cmdline,lpCmdLine);

	//
	// extract and process tokens from command line
	//

	option = strtok(cmdline, " -+'\"");

	// loop over every option
    while(option != NULL )
	{

		// last option
		if (!strcasecmp(option,"chdir"))
		{
			// dont loop anymore were done
			break;
		}

		// sdl + opengl setting
		else if (!strcasecmp(option,"ForceAccel")){
			render_info.force_accel = true;
		}

		// off only works in full screen...
		// turn on vertical syncing
		else if (!strcasecmp(option,"VSync")){
			render_info.vsync = true;
		}

		// debugging information send to Log...
        else if (!strcasecmp(option, "log"))
		{
            DebugLog = true;
		}

		// debugging information
        else if (!strcasecmp(option, "Debug"))
		{
            Debug = true;
		}

		// render through the VR offscreen path instead of straight to the
		// window. without this every VR addition stays completely inert.
		else if (!strcasecmp(option, "vr"))
		{
			extern void VRApplyPrefs( void );
			vr_enabled = true;
			/* the settings whose VR default differs, as soon as VR is known,
			   so options after -vr (-proberear) still win over them */
			VRApplyPrefs();
		}

		// pick the OpenAL output device by name, e.g. -aldevice "Virtual Desktop"
		else if (!strcasecmp(option, "aldevice"))
		{
			option = strtok(NULL, "\"'");
			if (option)
				strncpy( al_device_name, option, sizeof(al_device_name) - 1 );
			continue;
		}

		// Percentage of the runtime's recommended per-eye resolution to render
		// at. 100 means render at exactly what the runtime asks for, which on a
		// Quest 3 over VDXR is 3072x3264 per eye: about 20 MP per frame
		// across both. Lower it if the frame rate suffers; raise it past 100
		// only to supersample.
		else if (!strcasecmp(option, "vrres"))
		{
			option = strtok(NULL, " -+'\"");
			if (option)
			{
				int p = atoi(option);
				if (p >= 10 && p <= 200)
					vr_res_scale = p;
				else
					Msg( "-vrres %d out of range (10-200), using %d\n",
					     p, vr_res_scale );
			}
			continue;
		}

		// how wide the floating menu panel is, in metres
		else if (!strcasecmp(option, "vrpanel"))
		{
			option = strtok(NULL, " -+'\"");
			if (option)
			{
				float w = (float) atof(option);
				if (w >= 0.3f && w <= 8.0f)
					vr_panel_width = w;
				else
					Msg( "-vrpanel %g out of range (0.3-8.0), using %g\n",
					     w, vr_panel_width );
			}
			continue;
		}

		// how far in front of the player the menu panel sits, in metres
		else if (!strcasecmp(option, "vrpaneldist"))
		{
			option = strtok(NULL, " -+'\"");
			if (option)
			{
				float d = (float) atof(option);
				if (d >= 0.5f && d <= 10.0f)
					vr_panel_dist = d;
				else
					Msg( "-vrpaneldist %g out of range (0.5-10.0), using %g\n",
					     d, vr_panel_dist );
			}
			continue;
		}

		// world units per metre: decides how big the world feels in the
		// headset. Only tunable by wearing it, so it is a cli option.
		else if (!strcasecmp(option, "vrscale"))
		{
			option = strtok(NULL, " -+'\"");
			if (option)
			{
				float s = (float) atof(option);
				if (s > 0.0f)
					vr_world_scale = s;
			}
			continue;
		}
		
		// start in window mode
		else if (!strcasecmp(option,"Fullscreen"))
		{
			render_info.fullscreen = true;
		}

		// start in window mode
		else if (!strcasecmp(option,"Window"))
		{
			render_info.fullscreen = false;
		}

		// turn off sound
		else if (!strcasecmp(option, "NoSFX"))
		{
			NoSFX = true;
        }

		// turn off the streaming soundtrack, leaving sound effects alone
		else if (!strcasecmp(option, "NoMusic"))
		{
			music_disabled = true;
		}

		// music volume, 0.0 (silent) to 1.0 (full). Sets the menu slider
		// rather than bypassing it: SetSoundLevels() applies the slider, so
		// setting only the volume here would be overwritten at startup.
		else if (!strcasecmp(option, "MusicVol"))
		{
			option = strtok(NULL, " -+'\"");
			if (option)
			{
				float v = (float) atof(option);
				if (v < 0.0f) v = 0.0f;
				if (v > 1.0f) v = 1.0f;
				MusicSlider.value = (int) ( v * MusicSlider.max + 0.5f );
				music_set_volume( v );
			}
			continue;
		}
		
		// jump to the host screen
		else if ( !strcasecmp( option, "QuickHost" ) ) 
		{
			QuickStart = QUICKSTART_Start;
		}

		// jump to the join game screen
		else if ( !strcasecmp( option, "QuickJoin" ) ) 
		{
			QuickStart = QUICKSTART_Join;
		}

		// set the ip address for game to join
		else if ( !strcasecmp( option, "TCP" ) )
		{
			char * port;
			char address[255];

			IpOnCLI = true;

			// extract the address
	        option = strtok(NULL, " ");
			strcpy( address, option );

			// try to find a port in the address
			port = strchr(address,':');

			// if port found assign it
			if( port )
			{
				*port = 0; // separate hostname from port
				strcpy( (char*) host_port_str.text, ++port );
			}
			// other wise set default port
			else
			{
				sprintf( (char*) host_port_str.text, "%d", NETWORK_DEFAULT_PORT );
			}

			// copy in the hostname
			strcpy( (char*)TCPAddress.text, address );
		}

		// unattended diagnostics: -autolevel:N, -autoquit:S, -hudprobe
		else if ( probe_parse_option( option ) ) {}

		// HUD text size multiplier. Absent means derive it from the vertical
		// resolution so the HUD keeps its 1998 proportions at any size.
		else if ( sscanf( option, "hudfont:%f", &hudfont ) == 1 )
		{
			if ( hudfont >= 0.5f && hudfont <= 12.0f )
				hud_font_scale = hudfont;
			else
				Msg( "-hudfont %g out of range (0.5-12.0), ignored\n", hudfont );
		}

		// cap intro playback, for testing. Colon form: the cli tokenizer
		// splits on spaces, so "-introsecs:15" arrives as a single token.
		else if ( sscanf( option, "introsecs:%d", &introsecs ) == 1 )
		{
			movie_max_seconds = introsecs;
		}

		// force the intro on, whatever a config said
		else if (!strcasecmp(option, "intro"))
		{
			movie_enabled = true;
		}

		// skip the intro movie entirely
		else if (!strcasecmp(option, "nointro"))
		{
			movie_enabled = false;
		}

		// Headless VR geometry measurement: render the VR path into an
		// eye-sized target with no runtime. -fakeeye:3072x3264
		else if ( sscanf( option, "fakeeye:%dx%d", &fakew, &fakeh ) == 2 )
		{
			if ( fakew > 0 && fakeh > 0 && fakew <= 8192 && fakeh <= 8192 )
			{
				vr_headless = true;
				vr_enabled  = true;
				vr_fake_w   = fakew;
				vr_fake_h   = fakeh;
			}
			else
				Msg( "-fakeeye %dx%d out of range, ignored\n", fakew, fakeh );
		}

		// a synthetic head that keeps turning, degrees a second (swivel chair proof)
		else if ( sscanf( option, "fakeyawrate:%f", &fakeyaw ) == 1 )
		{
			vr_fake_yaw_rate = fakeyaw;
		}
		// turn the synthetic head, to prove a view follows it
		else if ( sscanf( option, "fakeyaw:%f", &fakeyaw ) == 1 )
		{
			vr_fake_yaw = fakeyaw;
		}

		// fault on purpose, to prove the crash handler works
		else if (!strcasecmp(option, "crashtest"))
		{
			crash_want_test = true;
		}

		// 0 = HUD rides your head, 1 = it stays with the cockpit
		else if ( sscanf( option, "vrhudanchor:%f", &hudanchor ) == 1 )
		{
			if ( hudanchor >= 0.0f && hudanchor <= 1.0f )
				vr_hud_anchor = hudanchor;
		}

		// ask for a recentre on the first frame (headless proof)
		else if (!strcasecmp(option, "fakerecenter"))
		{
			vr_recenter();
		}

		// in-world pause menu on (1) or the old flat screen (0)
		else if ( sscanf( option, "vrworldmenu:%d", &worldmenu ) == 1 )
		{
			vr_world_menu = ( worldmenu != 0 );
		}

		// left-handed play, and swapping the sticks (separate on purpose)
		else if ( sscanf( option, "leftorium:%d", &leftorium ) == 1 )
		{
			vr_leftorium = ( leftorium != 0 );
		}
		else if ( sscanf( option, "swapsticks:%d", &swapsticks ) == 1 )
		{
			vr_swap_sticks = ( swapsticks != 0 );
		}
		// check every control lands where it should, in all four combinations
		else if ( !strcasecmp( option, "testhandsbroken" ) )
		{
			vr_hand_swap_selftest( true );
		}
		else if ( !strcasecmp( option, "testhands" ) )
		{
			vr_hand_swap_selftest( false );
		}

		// comfort vignette: 0 off, 1 light, 2 strong
		else if ( sscanf( option, "vrvignette:%d", &vignette ) == 1 )
		{
			vr_vignette = vignette < 0 ? 0 : ( vignette > 2 ? 2 : vignette );
		}
		// hold the vignette at a fixed level, for measuring it
		else if ( sscanf( option, "vignetteforce:%f", &vigforce ) == 1 )
		{
			vr_vignette_force = vigforce;
		}

		// controller vibration: 0 off, 1 on
		else if ( sscanf( option, "vrvibration:%d", &vibration ) == 1 )
		{
			vr_vibration = vibration ? 1 : 0;
		}
		// log every vibration pulse with the reason it fired
		else if ( !strcasecmp( option, "vrhapticslog" ) )
		{
			vr_haptics_log = true;
		}

		// the steady desktop view: 1 on (default), 0 the whole eye as before
		else if ( sscanf( option, "vrspectator:%d", &spectator ) == 1 )
		{
			vr_spectator = spectator != 0;
		}
		// Play style: 1 Modern (aim with the controller), 0 Classic (the ship)
		else if ( sscanf( option, "vraim:%d", &aimmode ) == 1 )
		{
			extern int vr_aim_mode;
			vr_aim_mode = aimmode ? 1 : 0;
		}
		else if ( !strcasecmp( option, "vraimlog" ) )
		{
			extern bool vr_aim_log;
			vr_aim_log = true;
		}
		// headless: the right hand aimed Y degrees left, P up
		else if ( sscanf( option, "fakeaim:%f,%f", &fakeaimy, &fakeaimp ) >= 1 )
		{
			vr_fake_aim_yaw = fakeaimy;
			vr_fake_aim_pitch = fakeaimp;
		}
		// the idle drift of our own ship in VR: 1 on (as Probe made it), 0 off
		else if ( sscanf( option, "vrshipbob:%d", &shipbob ) == 1 )
		{
			extern bool vr_ship_bob;
			vr_ship_bob = shipbob != 0;
		}
		// the right stick's turn rate, % of the game's own (this run only)
		else if ( sscanf( option, "vrturnspeed:%d", &turnspeed ) == 1 )
		{
			extern int vr_turn_speed;
			if ( turnspeed >= 40 && turnspeed <= 150 )
				vr_turn_speed = turnspeed;
		}
		// auto-level in VR: 0 = the ship stays rolled where you leave it (this run only)
		else if ( sscanf( option, "vrautolevel:%d", &autolevel ) == 1 )
		{
			extern bool vr_auto_level;
			vr_auto_level = autolevel != 0;
		}
		// swivel chair: 1 = turning your body turns the ship (this run only)
		else if ( sscanf( option, "vrswivel:%d", &swivel ) == 1 )
		{
			vr_swivel = swivel != 0;
		}
		else if ( !strcasecmp( option, "vrpointerlog" ) )
		{
			extern bool vr_pointer_log;
			vr_pointer_log = true;
		}
		else if ( !strcasecmp( option, "askplaystyle" ) )
		{
			extern bool vr_play_style_force;
			vr_play_style_force = true;
		}
		else if ( !strcasecmp( option, "observerwindow" ) )
		{
			extern bool vr_observer_force_window;
			vr_observer_force_window = true;
		}
		else if ( !strcasecmp( option, "vrspritelog" ) )
		{
			extern bool vr_sprite_log;
			vr_sprite_log = true;
		}
		else if ( !strcasecmp( option, "vrlistenerlog" ) )
		{
			extern bool vr_listener_log;
			vr_listener_log = true;
		}
		else if ( !strcasecmp( option, "vrspeclog" ) )
		{
			vr_spectator_log = true;
		}
		// headless: the synthetic head tilted D degrees, and a Quest 3's frusta
		else if ( sscanf( option, "fakeroll:%f", &fakeroll ) == 1 )
		{
			vr_fake_roll = fakeroll;
		}
		else if ( !strcasecmp( option, "fakeasym" ) )
		{
			vr_fake_asym = true;
		}

		// supposedly to set wire mode for mxv's...
		else if (!strcasecmp(option, "wireframe"))
		{
            render_info.wireframe = true;
        }

		// special override to allow setting up of spaceorb
		else if ( !strcasecmp( option, "SetupSpaceOrb" ) )
		{
			SpaceOrbSetup = true;
		}

		// use sscanf
		else 
		{
			int w,h;

			// override local port
			if ( sscanf( option, "port:%s", (char*)&local_port_str.text[0] ) )
			{
				DebugPrintf("Command Line: local port set to %s\n", local_port_str.text);
			}

			// sleep time for every loop
			else if ( sscanf( option, "sleep:%d", &cliSleep )){}

			// select the pilot
			else if ( sscanf( option , "pilot:%s", config_name )){}

			// set the packets per second
			else if ( sscanf( option, "PPS:%d", &NetUpdateIntervalCmdLine ) ){}

			// resolution mode
			// must be a valid resolution list in the resolution list in game
			// other wise you will end up with the default highest possible resolution
			// note: if you pick a value not in the list then your window will be 1 size and your resolution another
			else if ( sscanf( option, "mode:%d:%d", &render_info.default_mode.w, &render_info.default_mode.h ) ){}

			// let user fix up aspect
			else if ( sscanf( option, "aspect:%d:%d", &w, &h ) )
			{
				DebugPrintf("cli: aspect ratio set to %d:%d\n",w,h);
				render_info.aspect_ratio = (float) w / (float) h;
			}

			// modifies texture dimentions.. don't now what uv stands for..
			else if ( sscanf( option, "UVFix:%f", &UV_Fix ) ){}

			// set the horizontal frame of view
			// this is the screen stretching when you go into nitro
			// default is 90... max is 120...
			else if ( sscanf( option, "fov:%f", &normal_fov ) ){}

			//
			else {
				DebugPrintf("cli: unknown option: %s\n",option);
			}
        }

		// get the next token
        option = strtok(NULL, " -+'\"");

    }

	return true;
}

//
// Cleans up the application before quiting
//

extern void ReleaseView(void);
extern void DestroySound( int flags );
extern void render_cleanup( render_info_t * info );
extern void ReleaseScene(void);

bool QuitRequested = false;
void CleanUpAndPostQuit(void)
{
	// check if this function was ran already
    if (QuitRequested)
		return;

	// Save the VR settings FIRST, while Lua is still alive. Calling this at
	// the end of main() instead (after lua_shutdown() below) crashed every
	// quit that came through here, which is every real one: closing the window,
	// Alt+F4, quitting from the menu. The desk harness never saw it because it
	// quit by setting QuitRequested directly and skipped this function.
	SaveVRPrefs();

	// VR teardown has to happen HERE, before anything else, because this
	// function ends in SDL_Quit(), which destroys the window and with it the
	// GL context. The OpenXR session was created against that context and its
	// swapchains are GL textures, so releasing it afterwards means handing the
	// runtime a dead WGL context. VDXR does not clean its threads up when that
	// happens and the process stays alive after the window disappears, which is
	// the "quit to desktop leaves projectx.exe in Task Manager" bug.
	//
	// Both calls are idempotent (they null their handles), so the pair at the
	// bottom of main() still runs harmlessly on the paths that never reach here.
	vr_openxr_shutdown();
	vr_target_cleanup();

	// Music owns an OpenAL source and buffers, so it has to go before
	// DestroySound() below tears the context down underneath it.
	music_shutdown();

	// kill stuff
    ReleaseView();

	// stop rendering and destroy objects
	render_cleanup( &render_info );

	// destroy the sound
	DestroySound( DESTROYSOUND_All );

	// destroy direct input
	joysticks_cleanup();
  
	// release the scene
	ReleaseScene();

	// set flag
    QuitRequested = true;

	// we dont control the cursor anymore
	input_grab( false );

	// close up lua
	lua_shutdown();

	// cleanup networking
	network_cleanup();

#ifdef SOUND_SUPPORT

	// cleanup sound system
	sound_destroy();

#endif

	// should come last
	SDL_Quit();
}

//
// Initializes the application
//

#ifdef BREAKPAD
// breakpad running through wine, built for windows doens't work well..
#ifndef __WINE__
extern bool breakpad_init( void );
#endif
#endif

extern bool InitView( void );
extern void GetGamePrefs( void );
extern void SetSoundLevels( int *dummy );
extern void GetDefaultPilot(void);
extern bool InitScene(void);
extern BYTE MyGameStatus;

#include "mload.h"
extern RENDEROBJECT Portal_Execs[ MAXGROUPS ];
extern RENDEROBJECT Skin_Execs[ MAXGROUPS ];
extern RENDEROBJECT RenderBufs[4];

static bool AppInit( char * lpCmdLine )
{
#ifdef DEBUG_ON
	InitMathErrors();
#endif

	ZERO_STACK_MEM(render_info);
	ZERO_STACK_MEM(RenderBufs);
	ZERO_STACK_MEM(Portal_Execs);
	ZERO_STACK_MEM(Skin_Execs);

	render_info.vsync = false;

#ifdef DXMOUSE
	if(!dx_init_mouse())
	{
		DebugPrintf("Could not init dx mouse\n");
		return false;
	}
#endif

#ifdef BREAKPAD
// breakpad running through wine, built for windows doens't work well..
#ifndef __WINE__

	// initialize google breakpad crash reporting
	if(!breakpad_init())
		return false;

	// test breakpad by uncommenting this
	//{ *(int*)0=0; }

#endif
#endif

#ifdef DEBUG_ON

	// special debuggin routines
	XMem_Init();

#endif

	//
	if(!sdl_init())
		return false;

	// parse chdir from command line first
	if(!parse_chdir(lpCmdLine))
		return false;

	// we are now in the skeleton folder
	// now we need to see if we are in right place

	// check for missing folders
	if(missing_folders())
		return false;

	// startup lua
	if( lua_init() != 0 )
		return false;

	// copy game settings from config
	GetGamePrefs();

	// our configs are now loaded
	// now we can check the command line for overrides

	// parse the command line
	if(!ParseCommandLine(lpCmdLine))
		return false;

	//
	// create and show the window
	//
	if(!sdl_init_video())
	{
		Msg("sdl_init_video() returned false");
		return false;
	}

	// OpenXR bring-up. Needs a live GL context, so it has to come after
	// sdl_init_video(). Non-fatal: on any failure it logs why, clears
	// vr_enabled and we carry on rendering flat.
	//
	// This runs *before* the render target is created because it is what tells
	// us the runtime's recommended per-eye resolution, which is what the target
	// should be sized from. Sizing from the window instead is what made the
	// headset image soft: an 800x600 frame stretched over a 3072x3264 eye
	// buffer is roughly a 4x upscale.
	DebugPrintf( "vr: in-world pause menu %s (VRWorldMenu / -vrworldmenu:0|1)\n",
	             vr_world_menu ? "ON" : "off" );
	DebugPrintf( "vr: Leftorium %s, Swap sticks %s (VRLeftorium / VRSwapSticks)\n",
	             vr_leftorium ? "ON" : "off", vr_swap_sticks ? "ON" : "off" );
	DebugPrintf( "vr: comfort vignette %s (VRVignette 0 off, 1 light, 2 strong)\n",
	             vr_vignette >= 2 ? "strong" : ( vr_vignette == 1 ? "light" : "off" ) );
	DebugPrintf( "vr: observer view, steady cut-out %s (VRSpectator / -vrspectator:0|1)\n",
	             vr_spectator ? "ON" : "off" );
	DebugPrintf( "vr: controller vibration %s (VRVibration / -vrvibration:0|1)\n",
	             vr_vibration ? "ON" : "off" );

	if( !vr_headless && vr_openxr_init() )
		vr_openxr_start_session();

	// Offscreen render target for the VR path. A failure here is not fatal
	// either: it logs, clears vr_enabled, and the game renders straight to the
	// window, so we degrade to flat rather than refusing to start.
	{
		int sw = render_info.ThisMode.w;
		int sh = render_info.ThisMode.h;

		/* -fakeeye stands in for the runtime's recommendation, so the whole
		   surface-sizing path below runs exactly as it does in the headset. */
		if( vr_headless && vr_fake_w > 0 && vr_fake_h > 0 )
		{
			vr_xr_view_width  = vr_fake_w;
			vr_xr_view_height = vr_fake_h;
			DebugPrintf( "vr: HEADLESS, pretending the runtime asked for %dx%d per eye\n",
			             vr_fake_w, vr_fake_h );
		}

		if( vr_enabled && vr_xr_view_width > 0 && vr_xr_view_height > 0 )
		{
			sw = ( vr_xr_view_width  * vr_res_scale ) / 100;
			sh = ( vr_xr_view_height * vr_res_scale ) / 100;

			// keep it even; odd sizes upset the portal sub-viewport maths
			sw &= ~1;
			sh &= ~1;

			DebugPrintf( "vr: rendering at %dx%d (%d%% of the runtime's "
			             "recommended %dx%d per eye)\n",
			             sw, sh, vr_res_scale,
			             vr_xr_view_width, vr_xr_view_height );
		}

		// The engine's physical surface. In flat mode this is the window and
		// nothing below behaves any differently than before.
		vr_surface_set( sw, sh );
		vr_target_init( sw, sh );

		/* the window becomes the observer view: borderless over its monitor */
		if( vr_enabled )
			vr_observer_window_apply();
	}

	// appears dinput has to be after init window

	// initialize direct input
	// This requires an application and window handle
	// so it most not come earlier than here
	if (!joysticks_init())
	{
		Msg("Failed to initialized joysticks!");
		return false;
	}
	
	// this needs to come after joysticks_init
	// because joysticks_init will wipe the joystick settings
	GetDefaultPilot();

// this is where it starts to take so long cause it scans directory for dynamic sound files...

	// start the title scene
	MyGameStatus = STATUS_Title;

	if (!InitScene())
		return false;

	// load the view
	if (!InitView() )
	{
	    Msg("InitView failed.\n");
		//CleanUpAndPostQuit();
        return false;
	}

	// exclusively grab input in fullscreen mode
	input_grab( render_info.fullscreen );

	//
	SetSoundLevels( NULL );

	// Streaming soundtrack. Has to come after InitScene(), which is what
	// brings OpenAL up; music_init() checks for the context and bows out
	// quietly if sound failed or -nosfx was passed.
	music_init();

	// done
	DebugPrintf("AppInit finished...\n");
    return true;

}

//
// Render the next frame and update the window
//

extern bool RenderScene( void );

static bool RenderLoop()
{
    if ( !render_info.ok_to_render || render_info.minimized || render_info.bPaused || QuitRequested )
		return true;

	// Ask the runtime for this frame's head pose before rendering. This fills
	// vr_eye[] and sets vr_views_valid, which is what makes MainGameRender
	// take the per-eye VR path. Returns false when there is nothing to draw
	// (headset off the head, session not running, or VR disabled), in which
	// case the engine still renders normally for the flat mirror window.
	vr_openxr_begin_frame();

	// Everything the engine draws this frame goes into the VR offscreen
	// target instead of the window. No-op unless -vr was passed.
	vr_target_begin_frame();

    // Call the sample's RenderScene to render this frame. In VR this renders
    // the world once per eye internally, blitting each eye to its swapchain as
    // it goes, but simulation, input and audio still happen exactly once.
    if (!RenderScene())
	{
		vr_openxr_end_frame();
		vr_target_end_frame();
        Msg("RenderScene failed.\n");
        return false;
    }

	// Unattended frame capture. No-op unless -shot was passed.
	probe_capture();

	// Menus, loading screens and anything else that does not go through the
	// per-eye world render never touched the swapchains, which showed up as a
	// black headset. Fall back to putting the mono frame in both eyes so there
	// is always something to look at.
	if( vr_views_valid && !vr_eyes_rendered )
	{
		/* nothing went through the per-eye path: menus, loading screens. Those
		   are placed as a floating panel rather than filling the view. */
		vr_panel_frame = true;
		vr_openxr_submit_panel();
		vr_panel_frame = false;
	}

	// Submit the composition layer, then resolve the offscreen target to the
	// window as the mirror view. Both before render_flip() swaps.
	vr_openxr_end_frame();
	vr_target_end_frame();

	/* One frame only. Cleared HERE, after the panel decision above, because
	   the old flat-panel path submits its panel after probe_capture(); clearing
	   any earlier meant that path never dumped, and the control arm of the
	   in-world menu test came back empty for a reason unrelated to the menu. */
	vr_dump_frame = false;

	/* An edge lasts one frame. A real controller's is recomputed every frame
	   by sync_input(); the harness's has nothing to clear it, so do it here. */
	if ( probe_injected_click )
	{
		vr_input.stick_l_edge = false;
		probe_injected_click  = false;
	}

	if ( quitting )
	{
		quitting = false;
		CleanUpAndPostQuit();
	}

    // Blt or flip the back buffer to the front buffer
	if( !QuitRequested )
	{
#ifdef DEMO_SUPPORT
		if ((!PlayDemo || ( MyGameStatus != STATUS_PlayingDemo ) ||	DemoShipInit[ Current_Camera_View ]	))
#endif
		{
			// this is the actual call to render a frame...
			if (!render_flip(&render_info))
			{
				Msg("RenderLoop: render_flip() failed\n");
				return false;
			}
		}
	}

	//
    return true;
}

//
// The main routine
//

extern void network_cleanup( void );
extern bool SeriousError;
extern void CleanUpAndPostQuit(void);

int main( int argc, char* argv[] )
{
	int i;
	char cli[500];
    int failcount = 0; // number of times RenderLoop has failed

	// Before anything that can fault, and before the window exists. Also
	// clears any stale crash.txt, so a report present afterwards is
	// unambiguously from this run.
	crash_install();

	// build cli string
	// TODO - cli parsing should be updated
	strncpy(cli, " ", 500);
	for ( i=1; i<argc; i++ )
	{
		if( strlen(cli) + strlen(argv[i]) -1 > 500 )
		{
			DebugPrintf("Stopped parsing cli options at argc='%s' because greator than 500 characters\n",argv[i]);
			break;
		}
		strcat( cli, " " );
		strcat( cli, argv[i] );
	}
	DebugPrintf("cli: %s\n",cli);

    // Create the window and initialize all objects needed to begin rendering
    if(!AppInit(cli))
		goto FAILURE;

	if ( crash_want_test )
		crash_test();

	// The intro movie, which retail played off the CD and this port never had.
	// Blocking, skippable, and silently a no-op if the file is not there --
	// setup.ps1 only copies it when the player owns Forsaken Remastered.
	// Plays in the headset too now: the movie submits through the same quad
	// panel the menus use, and the 2D aspect correction makes it land at its
	// true 16:9 inside the eye buffer. Verified headlessly with -fakeeye.
	/* Game Or Die's splash on the menu screen, if vrsplash.png is beside the
	   game (vr_splash.c); then the intro. */
	vr_splash_show( "vrsplash.png", 3000 );

	movie_play( "Movies/intro.ogv" );

	while( !QuitRequested )
	{
		// process system events
		if(!handle_events())
			goto FAILURE;

		// Keep the soundtrack fed and pick the track for whatever the game is
		// doing now. Cheap when nothing has changed, and independent of the
		// render path: music keeps playing while menus are up and in VR.
		music_update();

		// The HUD and the menus want different text sizes under -vr (eye
		// buffer vs quad panel), and which one applies changes as menus open
		// and close, so this is per-frame rather than per-slider-change.
		ApplyTextScale();

		// -autolevel / -autoquit. Inert unless asked for.
		probe_frame();

		// comfort vignette level, once a frame from the sticks
		vr_vignette_update();

		// controller vibration, from what the frame did
		vr_haptics_update();

        // Attempt to render a frame, if it fails, take a note.  If
        // rendering fails more than twice, abort execution.
        if( !RenderLoop() )
		{
            ++failcount;

			if( SeriousError )
			{
				CleanUpAndPostQuit();
				break;
			}

			if (failcount == 3) {
				DebugPrintf("Rendering has failed too many times.  Aborting execution.\n");
				CleanUpAndPostQuit();
				break;
			}

        }

		// command line asks us to sleep and free up sys resources a bit...
		if ( cliSleep )
			SDL_Delay( cliSleep );

#if defined(DEBUG_ON) && defined(DEBUG_ALL_MATH_ERRORS)
		DebugMathErrors();
#endif
	}

	// Tear the OpenXR session and instance down explicitly. Without this the
	// runtime keeps threads alive after our window closes and the process
	// lingers, so you have to kill it from Task Manager.
	vr_openxr_shutdown();
	vr_target_cleanup();

	// Persist the VR block on the paths that never went through
	// CleanUpAndPostQuit(). SaveVRPrefs() runs once, so the normal quit --
	// which already saved before tearing Lua down: does nothing here.
	SaveVRPrefs();

	DebugPrintf("exit(0)\n");

	return 0;

FAILURE:

	vr_openxr_shutdown();
	vr_target_cleanup();

	//
	CleanUpAndPostQuit();

#ifdef DEBUG_ON

	DebugMathErrors();

	if ( UnMallocedBlocks() )
		DebugPrintf( "Un-malloced blocks found!\n" );

#endif

	DebugPrintf("exit(1)\n");

	return 1;
}
