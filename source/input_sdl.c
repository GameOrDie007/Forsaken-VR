#include "main.h"
#include "input.h"
#include "util.h"
#include "render.h"
#include <SDL.h>

JOYSTICKINFO JoystickInfo[MAX_JOYSTICKS];

extern bool RenderModeReset( void );
extern void SetGamePrefs( void );
extern void FadeHoloLight(float Brightness);
extern float HoloLightBrightness;
extern float RoomDarkness;
extern void DarkenRoom2(float darkness);
extern void ProcessVduItems( MENU * Menu );
extern bool InitialTexturesSet;
extern bool cursor_clipped;
extern BYTE MyGameStatus;
extern render_info_t render_info;
extern void CleanUpAndPostQuit(void);

int Num_Joysticks = 0;

//////////////////////////////////////////////
// Generic Routines
//////////////////////////////////////////////

void input_grab( bool grab )
{
	// 1. always acquire and hide mouse if in fullscreen
	// 2. took this out cause a player asked... if there is issues add it back...
	// 3. don't remember who asked or why ? need to enable again.
	if( render_info.fullscreen )
	{
		input_grabbed = true;
#if SDL_VERSION_ATLEAST(2,0,0)
		SDL_SetWindowGrab( render_info.window, SDL_TRUE );
#else
		SDL_WM_GrabInput( SDL_GRAB_ON );
#endif
		SDL_ShowCursor( SDL_DISABLE );
		return;
	}
	// window mode
	input_grabbed = grab;

#ifdef LUA_BOT
	SDL_WM_GrabInput( SDL_GRAB_OFF );
	SDL_ShowCursor( SDL_ENABLE );
#else
	#if SDL_VERSION_ATLEAST(2,0,0)
	SDL_SetWindowGrab( render_info.window, grab ? SDL_TRUE : SDL_FALSE );
	#else
	SDL_WM_GrabInput( grab ? SDL_GRAB_ON : SDL_GRAB_OFF );
	#endif
	SDL_ShowCursor( grab ? SDL_DISABLE : SDL_ENABLE );
#endif

	//DebugPrintf("input state: %s\n",(grab?"grabbed":"free"));
}

//////////////////////////////////////////////
// Buffered High Level Input Events
//////////////////////////////////////////////

void input_buffer_send( int code )
{
	if( input_buffer_count >= MAX_INPUT_BUFFER )
		return;
	input_buffer[ input_buffer_count++ ] = code;
}

void input_buffer_reset( void )
{
	input_buffer_count = 0;
}

int input_buffer_find( int code )
{
	int i;
	for ( i = 0 ; i < input_buffer_count; i++ )
		if( input_buffer[ i ] == code )
			return 1;
	return 0;
}

//////////////////////////////////////////////
// Window and Activate Events
//////////////////////////////////////////////

// TODO
//	when this is ready then pass SDL_RESIZABLE to SDL_SetVideoMode
//  need to resize video with SDL_SetVideoMode
bool bIgnoreWM_SIZE = false; // ignores resize events
#if SDL_VERSION_ATLEAST(2,0,0)
void app_resize( SDL_WindowEvent * window )
#else
void app_resize( SDL_ResizeEvent * resize )
#endif
{
	DebugPrintf("Window size changed.\n");

	// TODO - need to save resize.w/h somewhere

	if (!bIgnoreWM_SIZE)
		return;

	// we should save the size
	// then startup at this size next time

	if( MyGameStatus == STATUS_Title )
	{
		LastMenu = CurrentMenu;	
		VduClear();
	}

	// resize d3d to match the window size..
	// TODO does d3d do the resizing on it's own now?

	if( MyGameStatus == STATUS_Title )
	{
		FadeHoloLight(HoloLightBrightness);
		DarkenRoom2(RoomDarkness);
		ProcessVduItems( CurrentMenu );
		InitialTexturesSet = false;
	}

	SetGamePrefs();
}

#if SDL_VERSION_ATLEAST(2,0,0)
void app_active( SDL_WindowEvent * window )
#else
void app_active( SDL_ActiveEvent * active )
#endif
{
//	DebugPrintf("window active state set to: %s\n",(active->gain?"true":"false"));

// since sdl 2 can support more than one type of event we want to explicitly check both gained/lost state
#if SDL_VERSION_ATLEAST(2,0,0)
	bool gained  = window -> event == SDL_WINDOWEVENT_FOCUS_GAINED;
	bool lost    = window -> event == SDL_WINDOWEVENT_FOCUS_LOST;
	bool resized = window -> event == SDL_WINDOWEVENT_RESIZED;
	bool exposed = window -> event == SDL_WINDOWEVENT_EXPOSED;
#else
	bool gained = active -> gain;
	bool lost   = ! gained;
#endif

	if( lost )
	{
		DebugPrintf( "window: focus lost\n" );   /* a focus change is a known VR fault: say when */
		input_grab( false );
	}

	if( gained )
	{
		DebugPrintf( "window: focus gained\n" );
		// fullscreen always has exclusive inputs
		if( render_info.fullscreen )
		{
			input_grab(true);
		}

		// window mode
		else
		{
			// only grab inputs if we are playing and not in menu
			if( !CurrentMenu && MyGameStatus == STATUS_Normal )
				input_grab(true);
			else
				input_grab(false);
		}

		RenderModeReset();
	}

#if SDL_VERSION_ATLEAST(2,0,0)

	if( resized )
		app_resize( window );

	if( exposed )
		render_flip(&render_info);

#endif

#if !SDL_VERSION_ATLEAST(2,0,0)
	switch( active->state )
	{
	case SDL_APPMOUSEFOCUS: // mouse
		//DebugPrintf("Mouse event\n");
		break;
	case SDL_APPINPUTFOCUS: // keyboard
		//DebugPrintf("keyboard event\n");
		break;
	case SDL_APPACTIVE: // minimize/iconified
		//DebugPrintf("Iconify event\n");
		break;
	}
#endif
}

void app_quit( void )
{
	// in case the window isn't in the foreground
	// we'd end up with a huge black spot from being previously covered
	render_flip(&render_info);

	// release mouse so they can interact with message box
	input_grab( false );

	// ask them to confirm clossing
	if(Msg("Are you sure you want to exit?") )
	{
		// user wants to quit
		// let our code know we're quitting and not failing
		// let the message reach DefWindowProc so it calls CloseWindow
		CleanUpAndPostQuit();
		return;
	}

	// let them click to get focus again
	if( ! render_info.fullscreen )
		input_grab( true );
}

//////////////////////////////////////////////
// Keyboard Events
//////////////////////////////////////////////

extern bool vr_enabled;
void app_keyboard( SDL_KeyboardEvent * key )
{
	if( key->type == SDL_KEYUP )
	{
		/* Alt+Enter: under VR, the observer view between borderless and a
		   window (MenuGoFullScreen does the same for Shift+F12 and the menu) */
		if( vr_enabled && ( key->keysym.sym == SDLK_RETURN || key->keysym.sym == SDLK_KP_ENTER ) &&
		    ( key->keysym.mod & KMOD_ALT ) )
		{
				MenuGoFullScreen( NULL );
		}
		else if( key->keysym.sym == SDLK_F12 && key->keysym.mod & KMOD_SHIFT )
		{
				MenuGoFullScreen( NULL );
		}
		else if( key->keysym.sym == SDLK_RSHIFT || key->keysym.sym == SDLK_LSHIFT )
		{
#if SDL_VERSION_ATLEAST(2,0,0)
				u_int8_t *keystate = SDL_GetKeyboardState(NULL);
				if ( keystate[SDL_SCANCODE_F12] )
#else
				u_int8_t *keystate = SDL_GetKeyState(NULL);
				if ( keystate[SDLK_F12] )
#endif
					MenuGoFullScreen( NULL );
		}
	}
	if( key->type == SDL_KEYDOWN )
	{
		// sdl2 removed keysym.unicode; translated text now arrives as a
		// separate SDL_TEXTINPUT event.  the raw sym is good enough for
		// the menu/bind system, which is all input_buffer_send feeds.
#if SDL_VERSION_ATLEAST(2,0,0)
		input_buffer_send( key->keysym.sym );
#else
		input_buffer_send(
			key->keysym.unicode ?
				key->keysym.unicode :
				key->keysym.sym
		);
#endif
	}
}

//////////////////////////////////////////////
// Mouse Events
//////////////////////////////////////////////

// mouse wheel button down/up are sent at same time
// so if we react to the up event then we undo the down event !
// so we must ignore up events and reset the value each game loop
// since a wheel event can never be held down this is ok...

void mouse_wheel_up( void )
{
	//DebugPrintf("mouse wheel up pressed\n");
	mouse_state.wheel = 1;
}

void mouse_wheel_down( void )
{
	//DebugPrintf("mouse wheel down pressed\n");
	mouse_state.wheel = -1;
}

void reset_mouse_wheel( void )
{
	//DebugPrintf("mouse wheel state reset\n");
	mouse_state.wheel = 0;
}

#if SDL_VERSION_ATLEAST(2,0,0)
void app_mouse_wheel( SDL_MouseWheelEvent * _event )
{
	//printf("mouse wheel = { x = %d, y = %d }\n", _event->x, _event->y);
	if(_event->y > 0) { mouse_wheel_up();   input_buffer_send( UP_MOUSE   ); }
	if(_event->y < 0) { mouse_wheel_down(); input_buffer_send( DOWN_MOUSE ); }
}
#endif

void app_mouse_button( SDL_MouseButtonEvent * _event )
{
	// we index mouse starting at 0
	int button = _event->button - 1;

	// pass down mouse events for menu processing
	// note: wheel events are sent above in app_mouse_wheel for sdl2
	if(  _event->type == SDL_MOUSEBUTTONDOWN )
		input_buffer_send( button + LEFT_MOUSE );

	switch( _event->button )
	{
#if !SDL_VERSION_ATLEAST(2,0,0)
	// mouse wheel button down/up are sent at same time
	// so if we react to the up event then we undo the down event !
	// so we must ignore up events and reset it further bellow manually
	// since a wheel event can never be held down this is ok...
	case SDL_BUTTON_WHEELUP:
		if( _event->type == SDL_MOUSEBUTTONDOWN )
			mouse_wheel_up();
		break;
	case SDL_BUTTON_WHEELDOWN:
		if( _event->type == SDL_MOUSEBUTTONDOWN )
			mouse_wheel_down();
		break;
#endif

	//
	// Every other mouse button works like normal
	//
	default: 
		// save the button state
		mouse_state.buttons[ button ] = ( _event->type == SDL_MOUSEBUTTONDOWN );
		//DebugPrintf("sdl mouse button %d %s\n",_event->button,
		//	(mouse_state.buttons[ button ]?"pressed":"released"));
		break;
	}
}

void app_mouse_motion( SDL_MouseMotionEvent * motion )
{
	mouse_state.xrel += motion->xrel;
	mouse_state.yrel += motion->yrel;
}

#ifndef DXMOUSE
extern float real_framelag;
mouse_state_t* read_mouse(void)
{
	static mouse_state_t state;
	// the mouse only updates at about 125fps (8ms)
	// today we mostly render faster then input events from the mouse
	// so the mouse ends up with 0's most of the time cause there is no pending update
	// the game was designed to repeat the last mouse value for 0.028 seconds
	// other wise mouse responsiveness drops off greatly
	static float framelagfix = 0.0f;
	framelagfix -= real_framelag;
	if(framelagfix <= 0.0f)
	{
		framelagfix = 0.028f;
	}
	else
	{
		return &state;
	}
	state.xrel = mouse_state.xrel;
	state.yrel = mouse_state.yrel;
	mouse_state.xrel = 0;
	mouse_state.yrel = 0;
	return &state;
}
#endif

//////////////////////////////////////////////
// Joystick Events
//////////////////////////////////////////////

static int get_deadzone( int joy, int axis )
{
	// make sure this is a valid joystick/axis
	if( joy > Num_Joysticks || axis > JoystickInfo[joy].NumAxis )
		return 0;
	return JoystickInfo[ joy ].Axis[ axis ].deadzone;
}

/* Menus read input_buffer and only ever compare against keyboard codes, so
   the joystick codes sent below mean nothing to them, which is why a pad
   cannot drive the front end. Translating pad presses into the codes menus
   already understand is the whole fix.

   Gated on a menu actually being up so in-game behaviour is untouched: A
   keeps firing the primary rather than injecting Return, which is bound to
   chat. */
extern bool MenuIsActive( void );

static bool pad_drives_menu( int which )
{
	return which >= 0 && which < MAX_JOYSTICKS &&
	       JoystickInfo[ which ].is_gamecontroller &&
	       MenuIsActive();
}

void app_joy_axis( SDL_JoyAxisEvent * axis )
{
	long value;
	int deadzone;

	if(axis->axis > MAX_JOYSTICK_AXIS)
	{
		DebugPrintf(
			"sdl_input: ignoring joy %d axis %d > max axises\n",
			axis->which, axis->axis);
		return;
	}

	// sdl axis value (range: -32768 to 32767)
	// forsaken expects (range: -100 to 100)
	value = (long) (((float)axis->value) / 327.67f);

	// get the joystick deadzone
	deadzone = get_deadzone(axis->which,axis->axis);

	// if movement greater then deadzone then apply
	// other wise no movement at all is registered
	joy_axis_state[ axis->which ][ axis->axis ] = 
		( abs(value) > deadzone ) ? value : 0 ;

	/* Triggers as buttons. Axis 4 is the left trigger and axis 5 the right on
	   any pad SDL recognises, measured on an Xbox One Controller. They rest at
	   -100 and reach +100 when squeezed, so the crossing point is 0; the two
	   thresholds give hysteresis so a trigger held near the edge does not
	   chatter. */
	if( axis->which >= 0 && axis->which < MAX_JOYSTICKS &&
	    JoystickInfo[ axis->which ].is_gamecontroller &&
	    JoystickInfo[ axis->which ].trigger_button_base > 0 &&
	    ( axis->axis == 4 || axis->axis == 5 ) )
	{
		int vb = JoystickInfo[ axis->which ].trigger_button_base +
		         ( axis->axis == 4 ? 0 : 1 );
		bool held = joy_button_state[ axis->which ][ vb ];

		if( !held && value >  20 ) held = true;
		else if( held && value < -20 ) held = false;

		joy_button_state[ axis->which ][ vb ] = held;
	}

}

void app_joy_ball( SDL_JoyBallEvent * ball )
{
}

void app_joy_button( SDL_JoyButtonEvent * button )
{
	if(button->button > MAX_JOYSTICK_BUTTONS)
	{
		DebugPrintf(
			"sdl_input: ignoring joy %d button %d > max buttons\n",
			button->which, button->button);
		return;
	}

	joy_button_state[ button->which ][ button->button ] =
		(button->type == SDL_JOYBUTTONDOWN);

	// pass down mouse events for menu processing
	if(  button->type == SDL_JOYBUTTONDOWN )
	{
		input_buffer_send(
			button->button + DIK_JOYSTICK
		);

		/* Menus only understand keyboard codes: the joystick code above means
		   nothing to them, which is why a pad cannot drive the front end. Send
		   Escape as well for Start on a recognised pad, so it opens and closes
		   the pause menu. Matters most in the headset, where the keyboard is
		   not visible. */
		if( button->which >= 0 && button->which < MAX_JOYSTICKS &&
		    JoystickInfo[ button->which ].is_gamecontroller &&
		    button->button == 7 )
		{
			input_buffer_send( SDLK_ESCAPE );
		}

		/* A selects, B backs out: only while a menu is up, so in game they
		   keep firing the primary and secondary. */
		if( pad_drives_menu( button->which ) )
		{
			if( button->button == 0 )
				input_buffer_send( SDLK_RETURN );
			else if( button->button == 1 )
				input_buffer_send( SDLK_ESCAPE );
		}
	}
}

void app_joy_hat( SDL_JoyHatEvent * hat )
{
	if(hat->hat > MAX_JOYSTICK_POVS)
	{
		DebugPrintf(
			"sdl_input: ignoring joy %d hat %d > max hats\n",
			hat->which, hat->hat);
		return;
	}

	if(hat->value == SDL_HAT_CENTERED)
	{
		int d;
		for( d = 0; d < MAX_POV_DIRECTIONS; d++ )
		{
			joy_hat_state[ hat->which ][ hat->hat ][ d ] = 0;
		}
		return;
	}

	if(hat->value & SDL_HAT_UP)
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_UP ] = 1;
		input_buffer_send(
			JOYSTICK_POVDIR_KEYCODE(
				hat->which,
				hat->hat,
				JOY_HAT_UP
			)
		);
		if( pad_drives_menu( hat->which ) )
			input_buffer_send( SDLK_UP );
	}
	else
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_UP ] = 0;
	}

	if(hat->value & SDL_HAT_RIGHT)
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_RIGHT ] = 1;
		input_buffer_send(
			JOYSTICK_POVDIR_KEYCODE(
				hat->which,
				hat->hat,
				JOY_HAT_RIGHT
			)
		);
		if( pad_drives_menu( hat->which ) )
			input_buffer_send( SDLK_RIGHT );
	}
	else
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_RIGHT ] = 0;
	}

	if(hat->value & SDL_HAT_DOWN)
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_DOWN ] = 1;
		input_buffer_send(
			JOYSTICK_POVDIR_KEYCODE(
				hat->which,
				hat->hat,
				JOY_HAT_DOWN
			)
		);
		if( pad_drives_menu( hat->which ) )
			input_buffer_send( SDLK_DOWN );
	}
	else
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_DOWN ] = 0;
	}

	if(hat->value & SDL_HAT_LEFT)
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_LEFT ] = 1;
		input_buffer_send(
			JOYSTICK_POVDIR_KEYCODE(
				hat->which,
				hat->hat,
				JOY_HAT_LEFT
			)
		);
		if( pad_drives_menu( hat->which ) )
			input_buffer_send( SDLK_LEFT );
	}
	else
	{
		joy_hat_state[ hat->which ][ hat->hat ][ JOY_HAT_LEFT ] = 0;
	}
}

#ifndef DINPUTJOY

// sdl1.2's SDL_JoystickName() took a device index; sdl2 renamed that form to
// SDL_JoystickNameForIndex() and made SDL_JoystickName() take an open handle.
static const char * joystick_name_for_index( int index )
{
	const char * name =
#if SDL_VERSION_ATLEAST(2,0,0)
		SDL_JoystickNameForIndex( index );
#else
		SDL_JoystickName( index );
#endif
	return name ? name : "unknown";
}

bool joysticks_init(void)
{
	int i, j, k;

	// initial memory cleaning
	ZERO_STACK_MEM(JoystickInfo);

	joysticks_cleanup();

	Num_Joysticks = SDL_NumJoysticks();

	DebugPrintf( "joysticks_init: %d joysticks connected\n", Num_Joysticks );

	if (Num_Joysticks > MAX_JOYSTICKS)
		Num_Joysticks = MAX_JOYSTICKS;

	for (i = 0; i < Num_Joysticks; i++)
	{
		SDL_Joystick * joy = SDL_JoystickOpen(i);

		// failed to open joystick
		if(!joy)
		{
			DebugPrintf(
				"joysticks_init: joystick (%d), '%s' failed to open\n",
				i, joystick_name_for_index(i)
			);
			continue;
		}

		// setup defaults
		JoystickInfo[i].sdl_joy 	= joy;
		JoystickInfo[i].assigned	= false;
		JoystickInfo[i].connected	= true;
		JoystickInfo[i].is_gamecontroller = SDL_IsGameController(i) ? true : false;
		JoystickInfo[i].NumAxis		= SDL_JoystickNumAxes(joy);
		JoystickInfo[i].NumButtons	= SDL_JoystickNumButtons(joy);
		JoystickInfo[i].NumPOVs		= SDL_JoystickNumHats(joy);

		/* Reserve two virtual buttons for the triggers so they can be bound
		   like any other button. They are axes on the wire, and resting at
		   -100 means they cannot be mapped to a movement axis without applying
		   permanent full deflection. */
		JoystickInfo[i].trigger_button_base = 0;
		if( JoystickInfo[i].is_gamecontroller &&
		    JoystickInfo[i].NumButtons + 2 <= MAX_JOYSTICK_BUTTONS )
		{
			JoystickInfo[i].trigger_button_base = JoystickInfo[i].NumButtons;
			JoystickInfo[i].NumButtons += 2;
		}

		// TODO
		// JoystickInfo[i].NumBalls = SDL_JoystickNumBalls(joy);

		JoystickInfo[i].Name = strdup( joystick_name_for_index(i) );

		DebugPrintf( 
			"joysticks_init: joystick (%d), name='%s', axises=%d, buttons=%d, hats=%d\n", 
			i, JoystickInfo[i].Name, JoystickInfo[i].NumAxis, JoystickInfo[i].NumButtons,
			JoystickInfo[i].NumPOVs
		);

		/* Instrumentation (-debug -log). Raw SDL_Joystick axis indices differ
		   between XInput and DirectInput, so a hardcoded pad mapping is a guess.
		   SDL_GameController normalises known pads to a standard layout: this
		   says whether SDL recognises this one. */
		DebugPrintf( "pad: index=%d is_gamecontroller=%d controller_name=\"%s\"\n",
			i, (int) SDL_IsGameController(i),
			SDL_IsGameController(i) ? SDL_GameControllerNameForIndex(i) : "(not recognised)" );

		for (j = 0; j < JoystickInfo[i].NumAxis; j++)
		{	
			JoystickInfo[i].Axis[j].action		= SHIPACTION_Nothing;
			JoystickInfo[i].Axis[j].inverted	= false;
			JoystickInfo[i].Axis[j].deadzone	= 20;
			JoystickInfo[i].Axis[j].fine		= true;
			JoystickInfo[i].Axis[j].exists		= true;

			JoystickInfo[i].Axis[j].name =
				(char*) malloc (MAX_JOYNAME+1);

			sprintf(
				JoystickInfo[i].Axis[j].name,
				"Axis %d",
				j
			);
		}

		for( j = 0; j < JoystickInfo[i].NumButtons; j++ )
		{
			JoystickInfo[i].Button[j].action = SHIPACTION_Nothing;

			JoystickInfo[i].Button[j].name = 
				(char *) malloc (MAX_JOYNAME+1);

			sprintf(
				JoystickInfo[i].Button[j].name,
				"Button %d", 
				j
			);
		}

		for (j = 0; j < JoystickInfo[i].NumPOVs; j++)
		{
			JoystickInfo[i].POV[j].name = 
				(char *) malloc (MAX_JOYNAME+1);
  
			sprintf(
				JoystickInfo[i].POV[j].name,
				"Hat %d", 
				j
			);

			for (k = 0; k < MAX_POV_DIRECTIONS; k++)
			{
				JoystickInfo[i].POV[j].action[k] =
					SHIPACTION_Nothing;      
				
				 JoystickInfo[i].POV[j].dirname[k] = 
					(char *) malloc (MAX_JOYNAME+1);

				sprintf(
					JoystickInfo[i].POV[j].dirname[k],
					"%s %d",
					JoystickInfo[i].POV[j].name,
					k
				);
			}
		}
	}

	return true;
}

bool joysticks_cleanup( void )
{
	int i, j, k;
	for (i = 0; i < MAX_JOYSTICKS; i++)
	{
		DebugPrintf("cleaning up joystick: %s %d\n",
			JoystickInfo[i].Name, i);

		JoystickInfo[i].assigned = false;
		JoystickInfo[i].connected = false;
		JoystickInfo[i].NumButtons = 0;
		JoystickInfo[i].NumPOVs = 0;
		JoystickInfo[i].NumAxis = 0;

		if(JoystickInfo[i].Name)
		{
			free(JoystickInfo[i].Name);
		}
		JoystickInfo[i].Name = NULL;

		if(JoystickInfo[i].sdl_joy)
		{
// seems to not be included anymore
// but since sdl_joy is not null then
// seems like we would always have a joystick opened here
#if !SDL_VERSION_ATLEAST(2,0,0)
			if(SDL_JoystickOpened(i))
#endif
			{
				SDL_JoystickClose(
					JoystickInfo[i].sdl_joy
				);
			}
		}

		for (j = 0; j < JoystickInfo[i].NumButtons; i++)
		{
			if(JoystickInfo[i].Button[j].name)
			{
				free(JoystickInfo[i].Button[j].name);
			}
			JoystickInfo[i].Button[j].name = NULL;
			JoystickInfo[i].Button[j].action = SHIPACTION_Nothing;
		}

		for (j = 0; j < JoystickInfo[i].NumPOVs; j++)
		{
			if(JoystickInfo[i].POV[j].name)
			{
				free(JoystickInfo[i].POV[j].name);
			}
			JoystickInfo[i].POV[j].name = NULL;
			for ( k = 0; k < MAX_POV_DIRECTIONS; k++ )
			{
				if ( JoystickInfo[ i ].POV[ j ].dirname[ k ] )
				{
					free( JoystickInfo[ i ].POV[ j ].dirname[ k ] );
				}
				JoystickInfo[ i ].POV[ j ].dirname[ k ] = NULL;
				JoystickInfo[ i ].POV[ j ].action[ k ] = SHIPACTION_Nothing;
			}
		}

		for (j = 0; j < MAX_JOYSTICK_AXIS; j++)
		{
			if(JoystickInfo[i].Axis[j].name)
			{
				free(JoystickInfo[i].Axis[j].name);
			}
			JoystickInfo[i].Axis[j].name = NULL;
			JoystickInfo[i].Axis[j].exists = false;
			JoystickInfo[i].Axis[j].action = SHIPACTION_Nothing;
			JoystickInfo[i].Axis[j].sensitivity = 0.0f;
			JoystickInfo[i].Axis[j].deadzone = 0;
			JoystickInfo[i].Axis[j].inverted = false;
			JoystickInfo[i].Axis[j].fine = false;
		}
	}
	return true;
}

#endif // ! DINPUTJOY

//////////////////////////////////////////////
// Main Events
//////////////////////////////////////////////

void reset_events( void )
{
	reset_mouse_wheel();
	input_buffer_reset();
}

bool handle_events( void )
{
	SDL_Event _event;

	reset_events();

	//DebugPrintf("processing events\n");

	while( SDL_PollEvent( &_event ) )
	{
		switch( _event.type )
		{


// previously mouse wheel was a button code
// now it's an x/y axis since it supports mouse wheel balls
#if SDL_VERSION_ATLEAST(2,0,0)
		case SDL_MOUSEWHEEL:
			app_mouse_wheel( &_event.wheel );
			break;
#endif


// this event name has changed
#if SDL_VERSION_ATLEAST(2,0,0)
// TODO - we should call something like app_window
//        which then delegates to app_active in correct case
		case SDL_WINDOWEVENT:
			app_active( &_event.window );
			break;
#else
		case SDL_ACTIVEEVENT:
			app_active( &_event.active );
			break;
#endif


// these have been moved into the window event above
#if !SDL_VERSION_ATLEAST(2,0,0)
		case SDL_VIDEORESIZE:
			app_resize( &_event.resize );
			break;

		case SDL_VIDEOEXPOSE: // need redraw
			render_flip(&render_info);
			break;
#endif

// sdl1.2 and sdl2 name these union members identically, so one
// dispatch serves both.  (the old sdl2 branch here passed &_event
// itself, which does not compile: sdl2 never dropped the sub structs.)
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			app_keyboard( &_event.key );
			break;

		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			app_mouse_button( &_event.button );
			break;

		case SDL_MOUSEMOTION:
			app_mouse_motion( &_event.motion );
			break;

		case SDL_JOYAXISMOTION:
			app_joy_axis( &_event.jaxis );
			break;

		case SDL_JOYBALLMOTION:
			app_joy_ball( &_event.jball );
			break;

		case SDL_JOYBUTTONDOWN:
		case SDL_JOYBUTTONUP:
			app_joy_button( &_event.jbutton );
			break;

		case SDL_JOYHATMOTION:
			app_joy_hat( &_event.jhat );
			break;

		case SDL_QUIT:
			app_quit();
			break;

#if SDL_VERSION_ATLEAST(2,0,0)
		// sdl2 emits these alongside every keypress. we take key input from
		// the raw syms in app_keyboard(), so they are just noise here.
		case SDL_TEXTINPUT:
		case SDL_TEXTEDITING:
			break;
#endif

		// platform specific _event type
		// must be enabled using SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE)
		case SDL_SYSWMEVENT:
			DebugPrintf("recived a platform specific _event type\n");
			break;

		// to avoid threading issues timers will add an event to the queue
		// so that we can call the callbacks from the same thread
		case SDL_USEREVENT:
			{
				void (*p) (void*) = _event.user.data1; // callback
				p(_event.user.data2); // callback( data )
			}
			break;

// TODO
		default:
			printf("Unknown event type: %d\n",_event.type);
			break;
		}
	}

	//DebugPrintf("DONE processing events\n");

	if ( quitting )
	{
		DebugPrintf("about to CleanUpAndPostQuit ( from WindowProc )\n");
		quitting = false;
		CleanUpAndPostQuit();
	}

	return true;
}

