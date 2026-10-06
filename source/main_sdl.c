#include "main.h"
#include "util.h"
#include "version.h"
#include "main_sdl.h"
#ifdef GL
#include "gl_headers.h"
#endif

extern render_info_t render_info;
extern bool render_init( render_info_t * info );

bool sdl_init( void )
{
	SDL_version ver;

	SDL_VERSION(&ver);
	DebugPrintf("SDL compile-time version: %u.%u.%u\n", ver.major, ver.minor, ver.patch);

#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_GetVersion(&ver);
#else
	ver = *SDL_Linked_Version();
#endif
	DebugPrintf("SDL runtime version: %u.%u.%u\n", ver.major, ver.minor, ver.patch);

	/* Under VR the window becomes the observer view over the whole monitor:
	   be DPI aware, or a scaled display stretches it soft. Flat play keeps
	   the window size it always had. */
	{
		extern bool vr_enabled;
		if( vr_enabled )
			SDL_SetHint( "SDL_WINDOWS_DPI_AWARENESS", "permonitorv2" );
	}

	if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_JOYSTICK ) < 0
#if !SDL_VERSION_ATLEAST(2,0,0)
		|| !SDL_GetVideoInfo()
#endif
	)
	{
		Msg("Failed to initialize sdl: %s\n",SDL_GetError());
		return false;
	}

	return true;
}

/////////////////////////
//
// Video Initialization
//
/////////////////////////

/*
	You can't trust ListModes.
	Modes are standard and a list is updated at:
		http://en.wikipedia.org/wiki/Display_resolution.
	Default mode should match the current desktop.
	BPP should be left alone to match whatever the desktop is at.
*/

#define NUMBER_MODES 19
render_display_mode_t video_modes[NUMBER_MODES] = {
{0,0}, // current video mode of the desktop
{640,480},
{800,600}, // default window mode
{1024,768},
{1152,864},
{1280,600},
{1280,720},
{1280,768},
{1280,800},
{1280,854},
{1280,960},
{1280,1024},
{1366,768},
{1440,900},
{1600,900},
{1600,1200},
{1680,1050},
{1920,1080},
{1920,1200} 
};

static void init_video_modes( u_int32_t window_flags )
{
	int i;
	render_info.Mode               = video_modes;
	render_info.NumModes           = NUMBER_MODES;

	// in full screen then default to desktop resolution
	if( window_flags &
#if SDL_VERSION_ATLEAST(2,0,0)
		SDL_WINDOW_FULLSCREEN
#else
		SDL_FULLSCREEN
#endif
	) render_info.CurrMode = 0;

	// in window mode default to 800x600 
	else render_info.CurrMode = 2;

	// try to find the users preferred mode
	for( i = 1; i < render_info.NumModes; i++ ) // skip mode 0
	{
		if( render_info.Mode[i].w == render_info.default_mode.w &&
		    render_info.Mode[i].h == render_info.default_mode.h )
		{
			render_info.CurrMode = i;
			DebugPrintf("init_video_modes: found requested resolution of %dx%d\n",
				render_info.default_mode.w,
				render_info.default_mode.h);
			break;
		}
	}

	render_info.ThisMode           = render_info.Mode[ render_info.CurrMode ];
	render_info.window_size.cx     = render_info.ThisMode.w;
	render_info.window_size.cy     = render_info.ThisMode.h;
	render_info.WindowsDisplay     = render_info.Mode[ render_info.CurrMode ];
	render_info.WindowsDisplay.w   = render_info.ThisMode.w;
	render_info.WindowsDisplay.h   = render_info.ThisMode.h;
}

static void set_window_icon( void )
{
// TODO
#if !SDL_VERSION_ATLEAST(2,0,0)
	SDL_Surface* icon = NULL;
	icon = SDL_LoadBMP("Data/ProjectX-32x32.bmp");
	if(icon)
	{
		// remove black pixels
		u_int32_t colorkey = SDL_MapRGB(icon->format, 0, 0, 0);
		SDL_SetColorKey(icon, SDL_SRCCOLORKEY, colorkey);        
		SDL_WM_SetIcon(icon, NULL);
		SDL_FreeSurface(icon);
	}
#endif
}

static void set_opengl_settings( void )
{
#ifdef GL
	// BPP should be left alone to match whatever the desktop is at.

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,	  1);

	// this was never set, so we got whatever the default was (16 bits, or
	// none at all on some drivers). a 3D game wants a real depth buffer.
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE,		 24);

#if SDL_VERSION_ATLEAST(2,0,0)
#if GL == 3
	// Version is requested per-context in create_video_surface(); see the
	// ladder there for why we no longer just ask for 3.2.
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
#endif
#endif

	// this causes issues on at least one card I've seen.
	// best to leave this as an option.  if someone says they have
	// low frame rate then suggest them to pass -forceaccel on cli
	if(render_info.force_accel)
	{
		DebugPrintf("main_sdl: enabling accelerated visual\n");
		SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL,	1  );
	}

// this is now done during creation of the renderer
#if !SDL_VERSION_ATLEAST(2,0,0)
	DebugPrintf("vsync set to %d\n",render_info.vsync);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL,	render_info.vsync );
#endif

#endif
}

static void print_info( void )
{
	int bpp;
	char driver[64];

// TODO
#if !SDL_VERSION_ATLEAST(2,0,0)
	// surface created by sdl
	DebugPrintf("main_sdl: surface { w=%d, h=%d, bpp=%d, Bpp=%d }\n",
		render_info.screen->w,
		render_info.screen->h,
		render_info.screen->format->BitsPerPixel,
		render_info.screen->format->BytesPerPixel
	);
#endif

	// actual depth size set by sdl
	SDL_GL_GetAttribute(SDL_GL_DEPTH_SIZE, &bpp);
	DebugPrintf("main_sdl: depth buffer is %d bpp\n", bpp);

// TODO
#if !SDL_VERSION_ATLEAST(2,0,0)
	// video driver
	if(SDL_VideoDriverName(driver, 64)!=NULL)
		DebugPrintf("main_sd: sdl video driver name: %s\n",driver);
	else
		DebugPrintf("main_sdl: failed to obtain the video driver name.\n");
#endif
}

#if !SDL_VERSION_ATLEAST(2,0,0)
static u_int32_t create_video_flags( void )
{
	u_int32_t flags = SDL_ANYFORMAT;

#ifdef GL
	flags |= SDL_OPENGL;
#endif

	if(render_info.fullscreen)
		flags |= SDL_FULLSCREEN;

	return flags;
}
#endif

#if SDL_VERSION_ATLEAST(2,0,0)
static u_int32_t create_window_flags( void )
{
	u_int32_t window_flags = SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE; // TODO - we really want resizable ?

	if(render_info.fullscreen)
		window_flags |= SDL_WINDOW_FULLSCREEN;

#ifdef GL
	window_flags |= SDL_WINDOW_OPENGL;
#endif

	return window_flags;
}
#endif



static bool create_video_surface( u_int32_t window_flags )
{
#ifndef RENDER_DISABLED
  #if SDL_VERSION_ATLEAST(2,0,0)
	// A mode change re-enters here with a window already up. Under SDL 1.2
	// SDL_SetVideoMode reused the existing window, so the original code could
	// just call it again, but SDL_CreateWindow does not, and creating a
	// second window orphans the first along with its GL context. That showed
	// up as: change the resolution, get a new black window, everything frozen,
	// and another dead window on every click.
	//
	// Resize in place instead, and keep the GL context. Textures, buffers and
	// shaders all survive, and in VR it means the OpenXR graphics binding is
	// still pointing at a live context.
	if( render_info.window )
	{
		DebugPrintf("main_sdl: resizing existing window to %dx%d (fullscreen=%d)\n",
			render_info.ThisMode.w, render_info.ThisMode.h, (int) render_info.fullscreen);

		if( SDL_SetWindowFullscreen( render_info.window,
				render_info.fullscreen ? SDL_WINDOW_FULLSCREEN : 0 ) < 0 )
			DebugPrintf("main_sdl: SDL_SetWindowFullscreen failed: %s\n", SDL_GetError());

		if( !render_info.fullscreen )
			SDL_SetWindowSize( render_info.window,
				render_info.ThisMode.w, render_info.ThisMode.h );

		SDL_GL_MakeCurrent( render_info.window, render_info.gl_context );
		print_info();
		return true;
	}

	render_info.window = SDL_CreateWindow(
		"ProjectX",
		SDL_WINDOWPOS_CENTERED,
		SDL_WINDOWPOS_CENTERED,
		render_info.ThisMode.w,
		render_info.ThisMode.h,
		window_flags
		);
	if(!render_info.window)
	{
		Msg("main_sdl: failed to create window: %s\n",SDL_GetError());
		return false;
	}

	// This used to call SDL_CreateRenderer(). That is SDL's 2D drawing API:
	// it does spin up a GL context internally, but never hands it back, and
	// does not guarantee it is current on our thread. The engine issues raw
	// GL calls, so on Windows every GL >= 2 entry point failed to resolve
	// (glewInit reported "Missing GL version") and startup aborted.
	// Create and own the context explicitly instead.
  #if GL == 3
	{
		// OpenXR runtimes impose a minimum OpenGL version: VDXR reports
		// "requires OpenGL 4.0 to 5.0" and refuses a 3.2 context outright.
		// The renderer only uses 3.2-era features, and those are all still
		// core in 4.x, so asking for a newer context costs us nothing and is
		// the difference between VR working and not.
		//
		// Walk down from 4.6 so we still start on hardware that cannot manage
		// it, ending at the 3.2 we used to hard-code (flat mode is happy there,
		// VR will not be: vr_openxr logs the mismatch).
		static const struct { int major, minor; } gl_versions[] = {
			{4,6}, {4,5}, {4,3}, {4,1}, {4,0}, {3,3}, {3,2}
		};
		unsigned v;
		for( v = 0; v < sizeof(gl_versions)/sizeof(gl_versions[0]); v++ )
		{
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, gl_versions[v].major);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, gl_versions[v].minor);
			SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
			render_info.gl_context = SDL_GL_CreateContext( render_info.window );
			if( render_info.gl_context )
			{
				DebugPrintf("main_sdl: created OpenGL %d.%d core context\n",
					gl_versions[v].major, gl_versions[v].minor);
				break;
			}
			DebugPrintf("main_sdl: OpenGL %d.%d core unavailable (%s)\n",
				gl_versions[v].major, gl_versions[v].minor, SDL_GetError());
		}
	}
  #else
	render_info.gl_context = SDL_GL_CreateContext( render_info.window );
  #endif
	if(!render_info.gl_context)
	{
		Msg("main_sdl: failed to create GL context: %s\n",SDL_GetError());
		return false;
	}
	if( SDL_GL_MakeCurrent( render_info.window, render_info.gl_context ) < 0 )
	{
		Msg("main_sdl: failed to make GL context current: %s\n",SDL_GetError());
		return false;
	}

	// vsync used to ride along on SDL_RENDERER_PRESENTVSYNC
	DebugPrintf("vsync set to %d\n",render_info.vsync);
	if( SDL_GL_SetSwapInterval( render_info.vsync ? 1 : 0 ) < 0 )
		DebugPrintf("main_sdl: could not set swap interval: %s\n",SDL_GetError());

  #else
	render_info.screen = SDL_SetVideoMode(
		render_info.ThisMode.w,
		render_info.ThisMode.h,
		0, // BPP should be left alone to match whatever the desktop is at.
		window_flags // on older sdl only 1 set of flags was used
	);

	if(!render_info.screen)
	{
		Msg("main_sdl: failed to create video surface: %s\n",SDL_GetError());
		return false;
	}
  #endif
	
	print_info();
#endif

	return true;
}

static void set_window_title( void )
{
// this is now done in CreateWindow
#if !SDL_VERSION_ATLEAST(2,0,0)
	// window title, icon title (taskbar and other places)
	SDL_WM_SetCaption(PXVersion,"ProjectX");
#endif
}

// TODO - we need a ui control for this
static void set_aspect_ratio( void )
{
	render_info.aspect_ratio = (float) render_info.ThisMode.w / (float) render_info.ThisMode.h;
	DebugPrintf("aspect ratio: %d:%d\n",render_info.ThisMode.w,render_info.ThisMode.h);
}

bool sdl_init_video( void )
{
#if SDL_VERSION_ATLEAST(2,0,0)
	u_int32_t window_flags   = create_window_flags();
#else
	u_int32_t window_flags   = create_video_flags();
#endif

	set_window_icon();

	set_opengl_settings();

	init_video_modes( window_flags );

	set_aspect_ratio();

	if(!create_video_surface( window_flags ))
		return false;

#ifdef WIN32
	// on windows the 2nd time you try to set video mode
	// you need to call the opengl states again after wards
	// as stated on http://sdl.beuc.net/sdl.wiki/SDL_SetVideoMode
	// this fixes a bug where the screen goes black on mode change
	set_opengl_settings();
#endif

	set_window_title();

	if (!render_init( &render_info ))
	{
		Msg("render_init() returned false");
		return false;
	}

	return true;
}

void sdl_render_present( render_info_t * info )
{
#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_GL_SwapWindow(info->window);
#else
	SDL_GL_SwapBuffers();
#endif
}
