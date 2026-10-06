/*
  Streaming Ogg Vorbis music. See music.h for the rationale.

  Shape of the thing:

    - one OpenAL source, never spatialised, with a small ring of buffers
      queued ahead of the play cursor
    - music_update() unqueues whatever the source has finished with, decodes
      more Vorbis into it, and queues it back
    - a tiny state machine on top picks the track from MyGameStatus and the
      current level, and crossfades when the answer changes

  Decoding happens on the main thread. That is fine at frame rates: refilling
  one 64 KB buffer is well under a millisecond, and the ring holds about three
  seconds. Level loads stall the main loop for longer than that, so the source
  can run dry: alSourcePlay() is reissued below when that happens, which
  costs a short gap rather than silence for the rest of the track.
*/

#include "main.h"

#ifdef MUSIC_OGG

#include "util.h"
#include "music.h"

#include <AL/al.h>
#include <AL/alc.h>
#include <vorbis/vorbisfile.h>

#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Engine state we react to. None of these live in a header. */
extern BYTE     MyGameStatus;
extern int16_t  LevelNum;
#include "oct2.h"        /* ShortLevelNames */

bool music_disabled = false;

#define MUSIC_DIR       "Music/OGG/"
#define MUSIC_PLAYLIST  "Music/playlist.txt"

/* Ring of streaming buffers. 8 x 64 KB is a little under three seconds of
   44.1 kHz stereo: enough to ride out a hitching frame without being so
   much memory that it matters. */
#define MUSIC_BUFFERS   8
#define MUSIC_BUFSZ     65536

#define MUSIC_MAX_ENTRIES 128
#define MUSIC_KEY_LEN     64
#define MUSIC_FILE_LEN    192

typedef struct {
	char key[MUSIC_KEY_LEN];    /* level short name, "menu", or "*" for the pool */
	char file[MUSIC_FILE_LEN];  /* filename under Music/OGG, spaces allowed */
} music_entry_t;

static music_entry_t entries[MUSIC_MAX_ENTRIES];
static int           entry_count = 0;

static bool   ready = false;       /* source and buffers exist */
static bool   auto_select = true;  /* state machine allowed to change tracks */

static ALuint source = 0;
static ALuint buffers[MUSIC_BUFFERS];

/* Which buffers are not currently queued on the source. Tracked explicitly
   rather than inferred from AL_BUFFERS_QUEUED: once buffers have been
   recycled, the queue no longer holds them in buffers[] order, and picking
   the next one by index will eventually re-queue a buffer that is still in
   the queue: an AL_INVALID_OPERATION that silently stalls the stream. */
static ALuint free_buffers[MUSIC_BUFFERS];
static int    free_count = 0;

static void free_list_reset_all( void )
{
	int i;
	for( i = 0; i < MUSIC_BUFFERS; i++ )
		free_buffers[i] = buffers[i];
	free_count = MUSIC_BUFFERS;
}

/* The open stream. */
static OggVorbis_File vf;
static bool           vf_open  = false;
static bool           vf_loop  = false;
static ALenum         vf_format = AL_FORMAT_STEREO16;
static ALsizei        vf_rate   = 44100;
static bool           vf_eof    = false;   /* decoder hit the end, non-looping */

static char current_file[MUSIC_FILE_LEN] = "";
static char pending_file[MUSIC_FILE_LEN] = "";
static bool pending_loop = true;
static bool pending_valid = false;

/* Volume and fading. volume is the user setting; fade is what multiplies it. */
static float volume     = 0.65f;
static float fade       = 0.0f;
static float fade_target = 0.0f;
static Uint32 last_ticks = 0;

/* Seconds for a full fade. Short enough not to feel sluggish on a level
   change, long enough not to click. */
#define MUSIC_FADE_SECS 1.2f

static void apply_gain( void )
{
	if( source )
		alSourcef( source, AL_GAIN, volume * fade );
}

/* ------------------------------------------------------------------ */
/* playlist                                                            */
/* ------------------------------------------------------------------ */

static void trim( char * s )
{
	char * p;
	size_t n;

	/* trailing */
	n = strlen( s );
	while( n && ( s[n-1] == '\n' || s[n-1] == '\r' ||
	              s[n-1] == ' '  || s[n-1] == '\t' ) )
		s[--n] = 0;

	/* leading */
	p = s;
	while( *p == ' ' || *p == '\t' )
		p++;
	if( p != s )
		memmove( s, p, strlen(p) + 1 );
}

/* "key   some file name.ogg": the key is the first whitespace-delimited
   token, the file is the rest of the line. Track filenames have spaces and
   brackets in them, so the file half cannot be tokenised. */
static void read_playlist( void )
{
	FILE * f;
	char   line[512];

	entry_count = 0;

	f = fopen( convert_path( MUSIC_PLAYLIST ), "r" );
	if( !f )
	{
		DebugPrintf( "music: no %s, nothing will play\n", MUSIC_PLAYLIST );
		return;
	}

	while( fgets( line, sizeof(line), f ) )
	{
		char * sep;

		trim( line );
		if( !line[0] || line[0] == '#' || line[0] == ';' )
			continue;

		if( entry_count >= MUSIC_MAX_ENTRIES )
		{
			DebugPrintf( "music: playlist truncated at %d entries\n", entry_count );
			break;
		}

		sep = line;
		while( *sep && *sep != ' ' && *sep != '\t' )
			sep++;
		if( !*sep )
		{
			DebugPrintf( "music: playlist line has no file: \"%s\"\n", line );
			continue;
		}
		*sep++ = 0;

		while( *sep == ' ' || *sep == '\t' )
			sep++;

		strncpy( entries[entry_count].key,  line, MUSIC_KEY_LEN - 1 );
		strncpy( entries[entry_count].file, sep,  MUSIC_FILE_LEN - 1 );
		entries[entry_count].key [MUSIC_KEY_LEN  - 1] = 0;
		entries[entry_count].file[MUSIC_FILE_LEN - 1] = 0;
		entry_count++;
	}

	fclose( f );
	DebugPrintf( "music: playlist has %d entries\n", entry_count );
}

/* Exact key first; then the "*" pool indexed by level so custom levels still
   get something rather than silence. Returns NULL if neither exists. */
static const char * lookup_track( const char * key, int index )
{
	int i, pool = 0;

	for( i = 0; i < entry_count; i++ )
		if( !strcasecmp( entries[i].key, key ) )
			return entries[i].file;

	for( i = 0; i < entry_count; i++ )
		if( entries[i].key[0] == '*' && !entries[i].key[1] )
			pool++;

	if( pool > 0 )
	{
		int want = ( index < 0 ? 0 : index ) % pool;
		int seen = 0;
		for( i = 0; i < entry_count; i++ )
			if( entries[i].key[0] == '*' && !entries[i].key[1] )
			{
				if( seen == want )
					return entries[i].file;
				seen++;
			}
	}

	return NULL;
}

/* ------------------------------------------------------------------ */
/* decoding                                                            */
/* ------------------------------------------------------------------ */

static void close_stream( void )
{
	if( vf_open )
	{
		ov_clear( &vf );          /* also closes the FILE* we handed it */
		vf_open = false;
	}
	vf_eof = false;
	current_file[0] = 0;
}

static bool open_stream( const char * filename, bool loop )
{
	char           path[MUSIC_FILE_LEN + 64];
	FILE *         fp;
	vorbis_info *  vi;

	close_stream();

	snprintf( path, sizeof(path), "%s%s", MUSIC_DIR, filename );

	fp = fopen( convert_path( path ), "rb" );
	if( !fp )
	{
		DebugPrintf( "music: cannot open %s\n", path );
		return false;
	}

	/* ov_open_callbacks rather than ov_open: ov_open takes ownership of a
	   FILE* through the library's own CRT, which is only safe when the
	   library and the exe share one. The callbacks form has no such
	   requirement. On failure the FILE* is still ours to close. */
	if( ov_open_callbacks( fp, &vf, NULL, 0, OV_CALLBACKS_DEFAULT ) != 0 )
	{
		DebugPrintf( "music: %s is not a readable Ogg Vorbis stream\n", path );
		fclose( fp );
		return false;
	}
	vf_open = true;
	vf_eof  = false;
	vf_loop = loop;

	vi = ov_info( &vf, -1 );
	if( !vi )
	{
		DebugPrintf( "music: %s has no stream info\n", path );
		close_stream();
		return false;
	}

	vf_format = ( vi->channels == 1 ) ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16;
	vf_rate   = (ALsizei) vi->rate;

	strncpy( current_file, filename, MUSIC_FILE_LEN - 1 );
	current_file[MUSIC_FILE_LEN - 1] = 0;

	DebugPrintf( "music: playing %s (%d ch, %ld Hz, %s)\n",
	             filename, vi->channels, vi->rate, loop ? "looping" : "once" );
	return true;
}

/* Fills one buffer from the stream. Returns false when the stream has ended
   and is not looping, meaning no more buffers should be queued. */
static bool fill_buffer( ALuint buf )
{
	static char pcm[MUSIC_BUFSZ];
	int    bitstream = 0;
	long   total = 0;

	if( !vf_open || vf_eof )
		return false;

	while( total < MUSIC_BUFSZ )
	{
		long got = ov_read( &vf, pcm + total, (int)( MUSIC_BUFSZ - total ),
		                    0 /* little endian */, 2 /* 16-bit */,
		                    1 /* signed */, &bitstream );

		if( got > 0 )
		{
			total += got;
			continue;
		}

		if( got == 0 )                 /* end of stream */
		{
			if( !vf_loop )
			{
				vf_eof = true;
				break;
			}
			if( ov_pcm_seek( &vf, 0 ) != 0 )
			{
				DebugPrintf( "music: could not rewind %s, stopping\n", current_file );
				vf_eof = true;
				break;
			}
			continue;
		}

		/* got < 0: a corrupt page. ov_read recovers on the next call, but
		   guard against spinning forever on a truncated file. */
		DebugPrintf( "music: decode error %ld in %s\n", got, current_file );
		vf_eof = true;
		break;
	}

	if( total <= 0 )
		return false;

	alGetError();
	alBufferData( buf, vf_format, pcm, (ALsizei) total, vf_rate );
	return alGetError() == AL_NO_ERROR;
}

/* Drops every queued buffer so a new track starts clean. */
static void flush_queue( void )
{
	ALint queued = 0;

	if( !source )
		return;

	alSourceStop( source );
	alGetSourcei( source, AL_BUFFERS_QUEUED, &queued );
	while( queued-- > 0 )
	{
		ALuint b = 0;
		alSourceUnqueueBuffers( source, 1, &b );
	}
	/* belt and braces: detach anything the driver still thinks is attached */
	alSourcei( source, AL_BUFFER, 0 );

	free_list_reset_all();
}

/* ------------------------------------------------------------------ */
/* public entry points                                                 */
/* ------------------------------------------------------------------ */

bool music_init( void )
{
	ALenum error;

	ready = false;

	if( music_disabled )
	{
		DebugPrintf( "music: disabled with -nomusic\n" );
		return false;
	}

	/* -nosfx (and any failure inside sound_init) means no OpenAL context was
	   ever made current, so there is nothing to attach a source to. Check for
	   the context rather than for bSoundEnabled: this file deliberately knows
	   nothing about sfx.c. */
	if( !alcGetCurrentContext() )
	{
		DebugPrintf( "music: no OpenAL context, music unavailable\n" );
		return false;
	}

	alGetError();

	alGenSources( 1, &source );
	if( ( error = alGetError() ) != AL_NO_ERROR )
	{
		DebugPrintf( "music: alGenSources failed: %s\n", alGetString( error ) );
		source = 0;
		return false;
	}

	alGenBuffers( MUSIC_BUFFERS, buffers );
	if( ( error = alGetError() ) != AL_NO_ERROR )
	{
		DebugPrintf( "music: alGenBuffers failed: %s\n", alGetString( error ) );
		alDeleteSources( 1, &source );
		source = 0;
		return false;
	}

	/* Music must not be positioned in the world: in VR especially, a
	   spatialised soundtrack would swing around the head. Relative to the
	   listener at the origin means it always plays dead centre. */
	alSourcei ( source, AL_SOURCE_RELATIVE, AL_TRUE );
	alSource3f( source, AL_POSITION, 0.0f, 0.0f, 0.0f );
	alSource3f( source, AL_VELOCITY, 0.0f, 0.0f, 0.0f );
	alSourcei ( source, AL_LOOPING, AL_FALSE );   /* looping is done by the decoder */
	alSourcef ( source, AL_ROLLOFF_FACTOR, 0.0f );

	free_list_reset_all();

	fade = fade_target = 0.0f;
	apply_gain();

	read_playlist();

	last_ticks = SDL_GetTicks();
	ready = true;

	DebugPrintf( "music: ready\n" );
	return true;
}

void music_shutdown( void )
{
	if( !ready )
		return;
	ready = false;

	flush_queue();
	close_stream();

	alDeleteBuffers( MUSIC_BUFFERS, buffers );
	alDeleteSources( 1, &source );
	source = 0;
}

bool music_play_file( const char * filename, bool loop )
{
	if( !ready || !filename || !filename[0] )
		return false;

	/* Already playing it, and not on the way out. */
	if( !strcasecmp( current_file, filename ) && fade_target > 0.0f )
		return true;

	strncpy( pending_file, filename, MUSIC_FILE_LEN - 1 );
	pending_file[MUSIC_FILE_LEN - 1] = 0;
	pending_loop  = loop;
	pending_valid = true;

	/* Nothing playing: start immediately and fade up. Otherwise let the
	   fade-out in music_update() finish first. */
	if( !vf_open )
		fade = 0.0f;

	fade_target = 0.0f;   /* fade whatever is playing out, then swap */
	return true;
}

void music_stop( void )
{
	pending_valid = false;
	fade_target   = 0.0f;
}

void music_set_volume( float v )
{
	volume = ( v < 0.0f ) ? 0.0f : ( v > 1.0f ? 1.0f : v );
	apply_gain();
}

float music_get_volume( void )
{
	return volume;
}

bool music_is_playing( void )
{
	return ready && vf_open;
}

void music_set_auto( bool on )
{
	auto_select = on;
}

/* What should be playing right now, given the game's state. Returns NULL for
   "nothing in particular": the caller then leaves the current track alone. */
static const char * wanted_track( void )
{
	const char * key;
	int          index;

	if( MyGameStatus == STATUS_Title )
	{
		key   = "menu";
		index = 0;
	}
	else if( LevelNum >= 0 && LevelNum < MAXLEVELS &&
	         ShortLevelNames[ LevelNum ][ 0 ] )
	{
		key   = ShortLevelNames[ LevelNum ];
		index = LevelNum;
	}
	else
		return NULL;

	return lookup_track( key, index );
}

void music_update( void )
{
	Uint32 now;
	float  dt;
	ALint  processed = 0, queued = 0, state = 0;

	if( !ready )
		return;

	/* ---- time base for the fades ---- */
	now = SDL_GetTicks();
	dt  = ( now - last_ticks ) / 1000.0f;
	last_ticks = now;
	/* A level load can stall the loop for seconds. Clamp so the fade does not
	   jump the whole way across in one frame. */
	if( dt < 0.0f )   dt = 0.0f;
	if( dt > 0.10f )  dt = 0.10f;

	/* ---- decide what should be playing ---- */
	if( auto_select )
	{
		const char * want = wanted_track();

		if( want )
		{
			/* Only act when it differs from both what is playing and what is
			   already on its way in, or music_play_file would restart the
			   fade every frame. */
			const char * settled = pending_valid ? pending_file : current_file;
			if( strcasecmp( settled, want ) )
				music_play_file( want, true );
			else if( !pending_valid && vf_open )
				fade_target = 1.0f;
		}
	}

	/* ---- advance the fade ---- */
	if( fade != fade_target )
	{
		float step = dt / MUSIC_FADE_SECS;
		if( fade < fade_target )
		{
			fade += step;
			if( fade > fade_target ) fade = fade_target;
		}
		else
		{
			fade -= step;
			if( fade < fade_target ) fade = fade_target;
		}
		apply_gain();
	}

	/* ---- swap tracks once the old one has faded out ---- */
	if( fade <= 0.0f && fade_target <= 0.0f )
	{
		if( pending_valid )
		{
			flush_queue();
			if( open_stream( pending_file, pending_loop ) )
				fade_target = 1.0f;
			pending_valid = false;
		}
		else if( vf_open )
		{
			/* faded out with nothing to follow */
			flush_queue();
			close_stream();
			return;
		}
	}

	if( !vf_open )
		return;

	/* ---- reclaim buffers the source has finished with ---- */
	alGetError();
	alGetSourcei( source, AL_BUFFERS_PROCESSED, &processed );
	while( processed-- > 0 )
	{
		ALuint b = 0;
		alSourceUnqueueBuffers( source, 1, &b );
		if( free_count < MUSIC_BUFFERS )
			free_buffers[ free_count++ ] = b;
	}

	/* ---- refill and queue everything that is free ---- */
	while( free_count > 0 && !vf_eof )
	{
		ALuint b = free_buffers[ free_count - 1 ];
		if( !fill_buffer( b ) )
			break;                 /* leave it free; nothing left to decode */
		alSourceQueueBuffers( source, 1, &b );
		free_count--;
	}

	/* ---- keep it running ---- */
	alGetSourcei( source, AL_SOURCE_STATE, &state );
	if( state != AL_PLAYING )
	{
		alGetSourcei( source, AL_BUFFERS_QUEUED, &queued );
		if( queued > 0 )
		{
			/* Either the first start, or the stream ran dry during a level
			   load and OpenAL stopped the source. Both want the same call. */
			alSourcePlay( source );
		}
		else if( vf_eof )
		{
			/* A non-looping track finished and drained. */
			close_stream();
			fade = fade_target = 0.0f;
			apply_gain();
		}
	}
}

#else  /* !MUSIC_OGG: built without libvorbis */

#include "music.h"

bool music_disabled = true;

bool  music_init( void )                                  { return false; }
void  music_shutdown( void )                              { }
void  music_update( void )                                { }
bool  music_play_file( const char * f, bool loop )        { (void)f; (void)loop; return false; }
void  music_stop( void )                                  { }
void  music_set_volume( float v )                         { (void)v; }
float music_get_volume( void )                            { return 0.0f; }
bool  music_is_playing( void )                            { return false; }
void  music_set_auto( bool on )                           { (void)on; }

#endif /* MUSIC_OGG */
