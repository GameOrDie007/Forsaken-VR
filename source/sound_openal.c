#ifdef SOUND_OPENAL

#include "main.h"
#include "util.h"
#include "sound.h"
#include "vr_target.h"
#include "file.h"
#include <SDL.h>
#include <AL/al.h>
#include <AL/alc.h>
//#include <efx.h>
//#include <efx-creative.h>
#include <math.h>

bool Sound3D = false; // enable 3d sound

struct {
	int buffers;
	int sources;
	int playing;
} stats;

#define MAX_PATH 500

struct sound_buffer_t {
	ALuint id;
	char path[MAX_PATH];
};

struct sound_source_t {
	ALuint id;
	ALuint buffer;
	bool playing;
	char path[MAX_PATH];
};

//
// sound system load / unload
//

static ALCdevice* Device = NULL;
static ALCcontext* Context = NULL;

static void print_info ( void ) 
{
	ALint version_major, version_minor;
	ALenum error;
	ALCdevice *device;

	DebugPrintf("openal: info start\n");

	// Check for EAX 2.0 support
	DebugPrintf("EAX2.0 support = %s\n",
		alIsExtensionPresent("EAX2.0")?"true":"false");

	if(alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT") == AL_TRUE) 
	{
		DebugPrintf("default playback: %s\n",alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER));
		DebugPrintf("default capture: %s\n",alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER));
		{	
			// print all playback devices
			const char * s = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
			while(s && *s != '\0')
			{
				DebugPrintf("playback: %s\n", s); 
				s += strlen(s)+1;
			}
			// print all capture devices
			s = alcGetString(NULL, ALC_CAPTURE_DEVICE_SPECIFIER);
			while(s && *s != '\0')
			{ 
				DebugPrintf("capture: %s\n", s); 
				s += strlen(s)+1;
			}
		}
	} 
	else 
		DebugPrintf("No device enumeration available\n");

	DebugPrintf("Default device: %s\n", alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER));
	DebugPrintf("Default capture device: %s\n", (alcGetString(NULL, ALC_CAPTURE_DEFAULT_DEVICE_SPECIFIER)));

	device = alcGetContextsDevice(alcGetCurrentContext());

	if ((error = alGetError()) != AL_NO_ERROR)
		DebugPrintf("error:  :%s\n", alGetString(error));

	alcGetIntegerv(device, ALC_MAJOR_VERSION, 1, &version_major);
	alcGetIntegerv(device, ALC_MINOR_VERSION, 1, &version_minor);
	
	if ((error = alGetError()) != AL_NO_ERROR)
		DebugPrintf("error:  :%s\n", alGetString(error));

	DebugPrintf("ALC version: %d.%d\n", (int)version_major, (int)version_minor);
	DebugPrintf("ALC extensions: %s\n", alcGetString(device, ALC_EXTENSIONS));

	if ((error = alGetError()) != AL_NO_ERROR)
		DebugPrintf("error:  :%s\n", alGetString(error));

	DebugPrintf("OpenAL vendor string: %s\n", alGetString(AL_VENDOR));
	DebugPrintf("OpenAL renderer string: %s\n", alGetString(AL_RENDERER));
	DebugPrintf("OpenAL version string: %s\n", alGetString(AL_VERSION));
	DebugPrintf("OpenAL extensions: %s\n", alGetString(AL_EXTENSIONS));

	if ((error = alGetError()) != AL_NO_ERROR)
		DebugPrintf("error:  :%s\n", alGetString(error));

	DebugPrintf("openal: info end\n");
}

/* Output device name from -aldevice; empty means the Windows default. */
char al_device_name[256] = "";

/* case-insensitive substring test; device names vary in capitalisation */
static bool str_contains_nocase( const char * hay, const char * needle )
{
	size_t nl = strlen( needle );
	if( !nl ) return false;
	for( ; *hay; hay++ )
		if( !strncasecmp( hay, needle, nl ) )
			return true;
	return false;
}

bool sound_init( void )
{
	// TODO - disabling the ability to re-init sound system
	// on ubuntu using "drivers = pulse" will handle in alcOpenDevice
	// is there really even a reason we need to re-init sound?
	static int initialized = 0;
	if(initialized)
		return true;
	initialized = 1;
	/* Which output device sound goes to.

	   The default device is whatever Windows says, which with a streamed
	   headset is usually still the desktop, so the game plays perfectly and
	   you hear nothing in the headset. Under -vr, prefer a device whose name
	   looks like the headset's; -aldevice overrides with a substring match. */
	{
		const ALCchar * all  = alcGetString( NULL, ALC_ALL_DEVICES_SPECIFIER );
		/* Only ever an explicit choice. Automatically preferring the
		   headset's own device under -vr was tried and does not work:
		   Virtual Desktop streams whatever the Windows *default* device is,
		   so opening its device directly produces silence in the headset. */
		const char *    want = al_device_name[0] ? al_device_name : NULL;

		if( want && all )
		{
			const ALCchar * d = all;
			while( *d )
			{
				if( str_contains_nocase( d, want ) )
				{
					Device = alcOpenDevice( d );
					DebugPrintf( "openal: matched \"%s\" -> %s\n",
						d, Device ? "opened" : "FAILED" );
					if( Device ) break;
				}
				d += strlen( d ) + 1;
			}
			if( !Device )
				DebugPrintf( "openal: nothing matched \"%s\", using default\n", want );
		}

		if( !Device )
			Device = alcOpenDevice( NULL );
	}
	if(!Device)
		return false;

	Context = alcCreateContext(Device,NULL);
	if(!Context)
		return false;

	alcMakeContextCurrent(Context);

	{
		const ALCchar * all  = alcGetString( NULL, ALC_ALL_DEVICES_SPECIFIER );
		if( all )
		{
			DebugPrintf( "openal: available output devices:\n" );
			while( *all )
			{
				DebugPrintf( "openal:   %s\n", all );
				all += strlen( all ) + 1;
			}
		}
	}

	// since the game was based around dsound
	// it performs some volume calculations using this value
	// dsound defines DSBVOLUME_MIN as -10000
	// and forsaken defined min volume as 1/3 of that
	sound_minimum_volume = -10000 / 3;

	// global listener sound set to 50% to reduce crackling
	// TODO - we should probably have a global sound level setting
	alListenerf(AL_GAIN, 0.5f);

	{
		ALfloat f;
		ALfloat pos[3];
		alGetListenerf(AL_GAIN,&f);
		DebugPrintf("listener gain: %f\n",f);
		alGetListenerfv(AL_POSITION,pos);
		DebugPrintf("listener position: %f %f %f\n",pos[0],pos[1],pos[2]);
	}

	print_info();

	return true;
}

void sound_destroy( void )
{
	ALCcontext * Context = alcGetCurrentContext();
	ALCdevice * Device = alcGetContextsDevice(Context);
	alcMakeContextCurrent(NULL);
	alcDestroyContext(Context);
	alcCloseDevice(Device);
}

//
// 3d routines
//

bool sound_listener_position( float x, float y, float z )
{
	alListener3f(AL_POSITION, x, y, -z);
	return true;
}

bool sound_listener_velocity( float x, float y, float z )
{
	alListener3f(AL_VELOCITY, x, y, -z);
	return true;
}

bool sound_listener_orientation( 
	float fx, float fy, float fz, // forward vector
	float ux, float uy, float uz  // up vector
)
{
	float vector[6] = {fx,fy,-fz,ux,uy,-uz};
	alListenerfv(AL_ORIENTATION, &vector[0]);
	return true;
}

// TODO - we'll want to set velocity as well
void sound_position( sound_source_t * source, float x, float y, float z, float min, float max )
{
	alSource3f(source->id, AL_POSITION, x, y, -z);
	alSourcef(source->id, AL_MAX_DISTANCE, max);
	alSourcef(source->id, AL_REFERENCE_DISTANCE, min); // is this right?
}

//
// 2d routines
//

void sound_set_pitch( sound_source_t * source, float pitch )
{
	ALfloat f = pitch ? pitch : 1.0f ; // 1.0f is default
	alSourcef( source->id, AL_PITCH, f );
	//DebugPrintf("sound_pitch: %f\n",f);
}

void sound_volume( sound_source_t * source, long millibels )
{
	ALfloat f;
	millibels = ( millibels > 0 ) ? 0 : millibels;
	// gain is scaled to (silence) 0.0f through (no change) 1.0f
	// millibels = hundredths of decibels (dB)
	// defined in Dsound.h as (no change) 0 and (silence) -10,000
	f = (ALfloat) pow(10.0, millibels/2000.0);
	alSourcef(source->id, AL_GAIN, f);
	//DebugPrintf("sound_volume: %ld\n",millibels);
}

void sound_pan( sound_source_t * source, long _pan )
{
	// where pan is -1 (left) to +1 (right)
	// must be scaled from -1 <-> +1
	// probably need to scale down by 10,000, since dsound goes from -10000 to +10000
	// so:
	float pan = (float) _pan / 10000.0f;
	float pan2 = (float) sqrt(1 - pan*pan);
	//DebugPrintf("sound_pan: %f - %f\n",pan,pan2);
	alSource3f(source->id, AL_POSITION, pan, pan2, 0.0f);
}

//
//  play / stop
//

void sound_play( sound_source_t * source )
{
	if(!source->playing)
		stats.playing++;
	source->playing = true;
	//
	alSourcePlay( source->id );
	//
	//DebugPrintf("sound_play: playing sound='%s' count=%d source=%d\n",
	//	source->path, stats.playing, source);
}

void sound_play_looping( sound_source_t * source )
{
	if(!source->playing)
		stats.playing++;
	source->playing = true;
	//
	alSourcei( source->id, AL_LOOPING, AL_TRUE );
	sound_play( source );
	//
	//DebugPrintf("sound_play_looping: playing %d\n",stats.playing);
}

void sound_stop( sound_source_t * source )
{
	if(source->playing)
		stats.playing--;
	source->playing = false;
	//
	alSourceStop( source->id );
	//
	//DebugPrintf("sound_stop: playing %d\n",stats.playing);
}

bool sound_is_playing( sound_source_t * source )
{
	ALint state;
	alGetSourcei( source->id, AL_SOURCE_STATE, &state );
	return (state == AL_PLAYING);
}

//
// load resources
//

sound_buffer_t * sound_load(char *path)
{
	ALenum error;
	ALenum format;
	SDL_AudioSpec wav_spec;
	u_int8_t *wav_buffer;
	Uint32 wav_len = 0;   /* must NOT be wav_spec.size: see below */
	sound_buffer_t * buffer;
	char * file_path = convert_path(path);

	// create the buffer
	buffer = malloc(sizeof(sound_buffer_t));
	if(!buffer)
	{
		DebugPrintf("sound_load: failed to malloc buffer\n");
		return 0;
	}
	strncpy(buffer->path,file_path,MAX_PATH-1);

	// clear error code
	alGetError();

	// Generate Buffers
	alGenBuffers(1, &buffer->id);
	if ((error = alGetError()) != AL_NO_ERROR)
	{
		DebugPrintf("alGenBuffers: %s\n", alGetString(error));
		free(buffer);
		return NULL;
	}
	/* The length must go somewhere other than wav_spec.size. SDL_LoadWAV
	   fills in *spec and writes the byte count to *audio_len, so passing
	   a field of the same struct for both aliases them: the size that
	   comes back is whatever SDL happened to leave in spec.size, not the
	   data length. That yielded a zero-length buffer, which OpenAL
	   accepts without error and then stops instantly: silent game, no
	   AL error, source state AL_STOPPED the moment it is played. */

	if( SDL_LoadWAV(file_path, &wav_spec, &wav_buffer, &wav_len) == NULL )
	{
		DebugPrintf("Could not open: %s\n", SDL_GetError());
		alDeleteBuffers( 1, &buffer->id );
		free(buffer);
		return NULL;
	}

	if(wav_spec.format == AUDIO_U8 || wav_spec.format == AUDIO_S8) // 8 bit
	{
		// openal only supports usigned 8bit
		if(wav_spec.format == AUDIO_S8)
		{
			int i;
			for(i = 0; i < (int) wav_len; i++)
				wav_buffer[i] ^= 0x80; // converts S8 to U8
			DebugPrintf("sound_buffer: converted s8 to u8\n");
		}
		if(wav_spec.channels == 1)
			format = AL_FORMAT_MONO8;
		else
			format = AL_FORMAT_STEREO8;
		DebugPrintf("sound_buffer: format = %s\n",
			format == AL_FORMAT_MONO8 ? "mono8" : "stereo8" );
	}
	else // 16 bit
	{
		// openal only supports signed 16bit
		if(wav_spec.format == AUDIO_U16)
		{
			int i;
			for(i = 0;i < (int)wav_len/2;i++)
				((u_int16_t*)wav_buffer)[i] ^= 0x8000; // converts U16 to S16
			DebugPrintf("sound_buffer: converted u16 to s16\n");
		}
		if(wav_spec.channels == 1)
			format = AL_FORMAT_MONO16;
		else
			format = AL_FORMAT_STEREO16;
		DebugPrintf("sound_buffer: format = %s\n",
			format == AL_FORMAT_MONO16 ? "mono16" : "stereo16" );
	}

	// Copy data into AL Buffer 0
	alBufferData(buffer->id,format,wav_buffer,wav_len,wav_spec.freq);
	if ((error = alGetError()) != AL_NO_ERROR)
	{
		DebugPrintf("alBufferData: %s\n", alGetString(error));
		alDeleteBuffers(1, &buffer->id);
		free(buffer);
		SDL_FreeWAV(wav_buffer);
		return NULL;
	}

	// data has been coppied
	SDL_FreeWAV(wav_buffer);

	stats.buffers++;
	DebugPrintf("sound_load: buffers %d sources %d playing %d buffer %d\n",
				stats.buffers,stats.sources,stats.playing,buffer);

	return buffer;
}

sound_source_t * sound_source( sound_buffer_t * buffer )
{
	ALenum error;
	sound_source_t * source;

	if(!buffer)
	{
		DebugPrintf("sound_source: null buffer given\n");
		return 0;
	}

	source = malloc(sizeof(sound_source_t));
	if(!source)
	{
		DebugPrintf("sound_source: failed to malloc source\n");
		return 0;
	}
	source->playing = false;
	source->buffer = buffer->id;
	strncpy(source->path,buffer->path,MAX_PATH);

	// clear errors
	alGetError();

	// generate a new source id
	alGenSources(1,&source->id);
	if ((error = alGetError()) != AL_NO_ERROR)
	{
		DebugPrintf("alGenSources: %s\n", alGetString(error));
		free(source);
		source = NULL;
		return NULL;
	}

	// attach the buffer to the source
	alSourcei(source->id, AL_BUFFER, source->buffer);
	if ((error = alGetError()) != AL_NO_ERROR)
	{
		DebugPrintf("alSourcei AL_BUFFER: %s\n", alGetString(error));
		alDeleteSources(1,&source->id);
		free(source);
		source = NULL;
		return NULL;
	}

	alSourcei(source->id,AL_SOURCE_RELATIVE,
		Sound3D ? AL_FALSE : AL_TRUE);

	stats.sources++;
	//DebugPrintf("sound_source: sources %d buffers %d playing %d source %d\n",
	//			stats.sources,stats.buffers,stats.playing,source);

	return source;
}

//
//  release resources
//

void sound_release_source( sound_source_t * source )
{
	if(!source)
		return;
	if(source->playing)
		stats.playing--;
	source->playing = false;
	// deleting source implicitly detaches buffer
	alDeleteSources( 1, &source->id );
	// clean up resources
	free(source);
	// show stats
	stats.sources--;
	//DebugPrintf("sound_release_source: buffers %d sources %d playing %d source %d\n",
	//			stats.buffers,stats.sources,stats.playing,source);
	source = NULL;
}

void sound_release_buffer( sound_buffer_t * buffer )
{
	if(!buffer)
		return;
	// buffers will not delete if they are attached to other sources
	alGetError(); // clear error so we can see if buffer is attached elsewhere
	alDeleteBuffers( 1, &buffer->id );
	// if buffer attached elsewhere than error is generated
	if(alGetError() == AL_NO_ERROR)
		stats.buffers--;
	else
		DebugPrintf("sound_release_buffer: error buffer %d still attached to a source.\n",
			buffer->id);
	// clean up resources
	free(buffer);
	buffer = NULL;
	// show stats
	DebugPrintf("sound_release_buffer: buffers %d sources %d playing %d\n",
				stats.buffers,stats.sources,stats.playing);
}

#endif // SOUND_OPENAL
