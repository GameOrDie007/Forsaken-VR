/* Forsaken VR: Ogg Theora / Vorbis intro playback
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

/* Ogg Theora / Vorbis playback: see movie.h for what and why.
 *
 * Plays at the correct speed with sound, flat and in the headset.
 * -nointro disables it, -introsecs:N caps it for testing.
 *
 * Three bugs are worth remembering, because each one looked like something
 * else and each is a general trap, not a Forsaken one:
 *
 * 1. libvorbis keeps only a couple of blocks of decoded PCM, and
 *    vorbis_synthesis_blockin() REFUSES data once that is full, reporting it
 *    in a return value. Draining every available packet in a burst and
 *    ignoring that return silently discarded 98.8% of the audio: 676,000
 *    samples a second in, 8,000 out. That starved the clock the video is
 *    scheduled against and played the film at a fifth speed, while every
 *    other indicator stayed healthy. Feed one packet, drain it, repeat.
 * 2. A 2D pass must set its own render state. This quad inherited whatever
 *    depth state the previous pass left and never reached the screen: 296
 *    frames "shown" into a black window. The HUD's own 2D draws sit inside
 *    disable_zbuff()/reset_zbuff(); so does this one now.
 * 3. When the audio ends the clock STOPS, because the clock IS the device's
 *    playback position. Any video frame still held then waits on a clock that
 *    will never advance, and the player hangs one frame from the end. At that
 *    point timing has to pass to the wall clock so the tail can drain.
 *
 * alSourcePlay() on a STOPPED source also rewinds the whole queue and resets
 * AL_BUFFERS_PROCESSED, so the source is only ever restarted when fresh data
 * was just queued.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "render.h"
#include "util.h"
#include "movie.h"

bool   movie_enabled        = true;   /* -nointro turns it off */
int    movie_max_seconds    = 0;
double movie_worst_drift_ms = 0.0;

#ifdef MOVIE_THEORA

#include <SDL.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>
#include <vorbis/codec.h>
#include <AL/al.h>
#include <AL/alc.h>

#include "render_gl_shared.h"
#include "vr_target.h"
#include "vr_openxr.h"

extern RENDEROBJECT RenderBufs[4];
extern bool render_flip( render_info_t *info );
extern bool QuitRequested;

#define MOVIE_AL_BUFFERS 8
#define MOVIE_READ_CHUNK 8192
#define MOVIE_PCM_FRAMES 4096

typedef struct
{
	FILE *           f;
	ogg_sync_state   sync;

	int              have_theora;
	int              have_vorbis;
	ogg_stream_state t_stream;
	ogg_stream_state v_stream;

	th_info          t_info;
	th_comment       t_comment;
	th_setup_info *  t_setup;
	th_dec_ctx *     t_dec;

	vorbis_info      v_info;
	vorbis_comment   v_comment;
	vorbis_dsp_state v_dsp;
	vorbis_block     v_block;
	int              v_ready;

	ALuint           source;
	ALuint           albuf[MOVIE_AL_BUFFERS];
	ALuint           freebuf[MOVIE_AL_BUFFERS];
	int              free_count;
	ALenum           alfmt;
	short            pcm[MOVIE_PCM_FRAMES * 2];
	double           samples_done;

	GLuint           tex;
	int              tex_w, tex_h;
	unsigned char *  rgb;

	int              frames_shown;
	long             blockin_refused;
	ALint            al_queued;
	ALint            al_state;
} movie_t;

/* ----------------------------------------------------------------- demux */

static int movie_read( movie_t * m )
{
	char * buf = ogg_sync_buffer( &m->sync, MOVIE_READ_CHUNK );
	size_t n;

	if ( !buf )
		return 0;
	n = fread( buf, 1, MOVIE_READ_CHUNK, m->f );
	ogg_sync_wrote( &m->sync, (long) n );
	return (int) n;
}

static void movie_page( movie_t * m, ogg_page * pg )
{
	long serial = ogg_page_serialno( pg );

	if ( m->have_theora && serial == m->t_stream.serialno )
		ogg_stream_pagein( &m->t_stream, pg );
	else if ( m->have_vorbis && serial == m->v_stream.serialno )
		ogg_stream_pagein( &m->v_stream, pg );
}

/* ----------------------------------------------------------------- video */

#define CLAMP8(v) ( (v) < 0 ? 0 : ( (v) > 255 ? 255 : (v) ) )

static void movie_yuv_to_rgb( movie_t * m, th_ycbcr_buffer yuv )
{
	int x, y;
	int w = m->tex_w, h = m->tex_h;
	int xoff = m->t_info.pic_x, yoff = m->t_info.pic_y;
	int cshift_x = ( m->t_info.pixel_fmt == TH_PF_444 ) ? 0 : 1;
	int cshift_y = ( m->t_info.pixel_fmt == TH_PF_420 ) ? 1 : 0;

	for ( y = 0; y < h; y++ )
	{
		unsigned char * dst = m->rgb + (size_t) y * w * 4;
		int sy = y + yoff;
		const unsigned char * yr = yuv[0].data + (size_t) sy * yuv[0].stride;
		const unsigned char * ur = yuv[1].data + (size_t) ( sy >> cshift_y ) * yuv[1].stride;
		const unsigned char * vr = yuv[2].data + (size_t) ( sy >> cshift_y ) * yuv[2].stride;

		for ( x = 0; x < w; x++ )
		{
			int sx = x + xoff;
			int Y = ( yr[sx] - 16 ) * 298;
			int U = ur[sx >> cshift_x] - 128;
			int V = vr[sx >> cshift_x] - 128;
			int r = ( Y + 409 * V + 128 ) >> 8;
			int g = ( Y - 100 * U - 208 * V + 128 ) >> 8;
			int b = ( Y + 516 * U + 128 ) >> 8;

			dst[0] = (unsigned char) CLAMP8(r);
			dst[1] = (unsigned char) CLAMP8(g);
			dst[2] = (unsigned char) CLAMP8(b);
			dst[3] = 255;
			dst += 4;
		}
	}
}

/* Letterboxed full screen quad through the engine's own 2D path, so it uses
   the same shader and ortho projection as the HUD rather than inventing a
   second way to put pixels on the screen. */
static void movie_draw( movie_t * m )
{
	RENDEROBJECT * ro = &RenderBufs[3];
	texture_t      tex;
	TLVERTEX *     v = NULL;
	WORD *         idx = NULL;
	float          sw = (float) render_info.window_size.cx;
	float          sh = (float) render_info.window_size.cy;
	float          src = (float) m->tex_w / (float) m->tex_h;
	float          dst = sw / sh;
	float          x0, y0, x1, y1;
	int            i;
	static int     logged = 0;

	if ( src > dst )
	{
		float hgt = sw / src;
		x0 = 0.0f; x1 = sw;
		y0 = ( sh - hgt ) * 0.5f; y1 = y0 + hgt;
	}
	else
	{
		float wid = sh * src;
		y0 = 0.0f; y1 = sh;
		x0 = ( sw - wid ) * 0.5f; x1 = x0 + wid;
	}

	if ( !FSLockVertexBuffer( ro, (LVERTEX **) &v ) )
		return;
	if ( !FSLockIndexBuffer( ro, &idx ) )
	{
		FSUnlockVertexBuffer( ro );
		return;
	}

	for ( i = 0; i < 4; i++ )
	{
		v[i].z     = 0.0f;
		v[i].rhw   = 1.0f;
		v[i].color = 0xFFFFFFFF;
	}
	v[0].x = x0; v[0].y = y0; v[0].tu = 0.0f; v[0].tv = 0.0f;
	v[1].x = x1; v[1].y = y0; v[1].tu = 1.0f; v[1].tv = 0.0f;
	v[2].x = x1; v[2].y = y1; v[2].tu = 1.0f; v[2].tv = 1.0f;
	v[3].x = x0; v[3].y = y1; v[3].tu = 0.0f; v[3].tv = 1.0f;

	idx[0] = 0; idx[1] = 1; idx[2] = 2;
	idx[3] = 0; idx[4] = 2; idx[5] = 3;

	FSUnlockVertexBuffer( ro );
	FSUnlockIndexBuffer( ro );

	tex.id = m->tex;

	ro->numTextureGroups              = 1;
	ro->textureGroups[0].startVert    = 0;
	ro->textureGroups[0].startIndex   = 0;
	ro->textureGroups[0].numVerts     = 4;
	ro->textureGroups[0].numTriangles = 2;
	ro->textureGroups[0].colourkey    = false;
	ro->textureGroups[0].texture      = (LPTEXTURE) &tex;

	/* A pass must set its own state. The HUD's 2D draws sit inside
	   disable_zbuff()/reset_zbuff() in screenpolys.c, and without that this
	   quad was submitted with whatever depth state the last pass left behind
	   and never reached the screen: 296 frames "shown", a black window. */
	FSClearBlack();
	disable_zbuff();
	draw_2d_object( ro );
	reset_zbuff();

	if ( !logged )
	{
		GLint vp[4];
		logged = 1;
		glGetIntegerv( GL_VIEWPORT, vp );
		DebugPrintf( "movie: draw vp=%d %d %d %d depth=%d blend=%d cull=%d scissor=%d quad=(%.0f,%.0f)-(%.0f,%.0f) glerr=%d\n",
			vp[0], vp[1], vp[2], vp[3],
			(int) glIsEnabled( GL_DEPTH_TEST ), (int) glIsEnabled( GL_BLEND ),
			(int) glIsEnabled( GL_CULL_FACE ), (int) glIsEnabled( GL_SCISSOR_TEST ),
			x0, y0, x1, y1, (int) glGetError() );
	}

	ro->numTextureGroups = 0;
}

/* ----------------------------------------------------------------- audio */

static void movie_queue_audio( movie_t * m )
{
	ALint processed = 0, queued = 0, state = 0;
	float ** pcm;
	int filled = 0;

	if ( !m->v_ready || !m->source )
		return;

	/* reclaim anything the device has finished with */
	alGetSourcei( m->source, AL_BUFFERS_PROCESSED, &processed );
	while ( processed-- > 0 && m->free_count < MOVIE_AL_BUFFERS )
	{
		ALuint b = 0;
		ALint  sz = 0, bits = 16, ch = 1;

		alSourceUnqueueBuffers( m->source, 1, &b );
		alGetBufferi( b, AL_SIZE, &sz );
		alGetBufferi( b, AL_BITS, &bits );
		alGetBufferi( b, AL_CHANNELS, &ch );
		if ( bits > 0 && ch > 0 )
			m->samples_done += (double) sz / (double) ( ( bits / 8 ) * ch );

		m->freebuf[ m->free_count++ ] = b;
	}

	while ( m->free_count > 0 )
	{
		int samples = vorbis_synthesis_pcmout( &m->v_dsp, &pcm );
		int chans   = m->v_info.channels > 2 ? 2 : m->v_info.channels;
		int n, i, c;
		ALuint b;

		if ( samples <= 0 )
			break;

		n = samples > MOVIE_PCM_FRAMES ? MOVIE_PCM_FRAMES : samples;
		for ( i = 0; i < n; i++ )
			for ( c = 0; c < chans; c++ )
			{
				int val = (int) ( pcm[c][i] * 32767.0f );
				if ( val >  32767 ) val =  32767;
				if ( val < -32768 ) val = -32768;
				m->pcm[ i * chans + c ] = (short) val;
			}

		b = m->freebuf[ --m->free_count ];
		alBufferData( b, m->alfmt, m->pcm,
			(ALsizei) ( n * chans * (int) sizeof(short) ),
			(ALsizei) m->v_info.rate );
		alSourceQueueBuffers( m->source, 1, &b );
		filled++;
		vorbis_synthesis_read( &m->v_dsp, n );
	}

	/* Restart ONLY when fresh data was just queued.
	   alSourcePlay() on a STOPPED source rewinds the entire queue, which
	   resets AL_BUFFERS_PROCESSED to zero. Restarting unconditionally at the
	   end of the film therefore un-processes the very buffers just reclaimed,
	   the queue can never drain, and playback hangs forever on the last
	   fraction of a second. Measured: the player never returned. */
	alGetSourcei( m->source, AL_BUFFERS_QUEUED, &queued );
	alGetSourcei( m->source, AL_SOURCE_STATE, &state );
	if ( filled > 0 && queued > 0 && state != AL_PLAYING )
		alSourcePlay( m->source );

	m->al_queued = queued;
	m->al_state  = state;
}

/* Feed the vorbis decoder ONE packet at a time, draining it into OpenAL
   between packets.

   libvorbis keeps only a couple of blocks of decoded PCM internally.
   vorbis_synthesis_blockin() refuses data once that buffer is full and says so
   in its return value. The first version of this loop drained every available
   packet in a burst and ignored that return, which silently threw away 98.8%
   of the decoded audio: 676,000 samples a second went in and 8,000 came out.
   Everything else looked healthy (packets valid, vorbis_synthesis succeeding,
   OpenAL never erroring) because the only call that was failing was the one
   whose result was discarded.

   Decoding only while the device has somewhere to put the result also paces
   the demuxer: the read gate keys off free_count, so this stops the reader
   racing 14x ahead of playback as well. */
static void movie_feed_audio( movie_t * m )
{
	ogg_packet pkt;

	if ( !m->v_ready )
		return;

	for (;;)
	{
		movie_queue_audio( m );        /* reclaim finished buffers, push PCM */

		if ( m->free_count == 0 )
			break;                      /* device is full: decode no more */

		if ( ogg_stream_packetout( &m->v_stream, &pkt ) <= 0 )
			break;                      /* need more demuxed data */

		if ( vorbis_synthesis( &m->v_block, &pkt ) == 0 )
		{
			if ( vorbis_synthesis_blockin( &m->v_dsp, &m->v_block ) != 0 )
				m->blockin_refused++;   /* must never happen now; watched */
		}
	}
}

/* Seconds of audio the device has actually played: the master clock. */
static double movie_audio_clock( movie_t * m )
{
	ALint off = 0;

	if ( !m->v_ready || !m->source )
		return -1.0;

	alGetSourcei( m->source, AL_SAMPLE_OFFSET, &off );
	return ( m->samples_done + (double) off ) / (double) m->v_info.rate;
}

/* ------------------------------------------------------------------ main */

static void movie_cleanup( movie_t * m )
{
	if ( m->source )
	{
		alSourceStop( m->source );
		alSourcei( m->source, AL_BUFFER, 0 );
		alDeleteSources( 1, &m->source );
		alDeleteBuffers( MOVIE_AL_BUFFERS, m->albuf );
	}
	if ( m->tex )
		glDeleteTextures( 1, &m->tex );
	if ( m->rgb )
		free( m->rgb );

	if ( m->t_dec )   th_decode_free( m->t_dec );
	if ( m->t_setup ) th_setup_free( m->t_setup );
	th_comment_clear( &m->t_comment );
	th_info_clear( &m->t_info );

	if ( m->v_ready )
	{
		vorbis_block_clear( &m->v_block );
		vorbis_dsp_clear( &m->v_dsp );
	}
	vorbis_comment_clear( &m->v_comment );
	vorbis_info_clear( &m->v_info );

	if ( m->have_theora ) ogg_stream_clear( &m->t_stream );
	if ( m->have_vorbis ) ogg_stream_clear( &m->v_stream );
	ogg_sync_clear( &m->sync );

	if ( m->f )
		fclose( m->f );
}

static bool movie_skipped( void )
{
	SDL_Event e;
	bool skip = false;
	static bool vr_held_before = true;   /* a button held as the film starts does not skip it */

	while ( SDL_PollEvent( &e ) )
	{
		switch ( e.type )
		{
		case SDL_QUIT:
			QuitRequested = true;
			skip = true;
			break;
		case SDL_KEYDOWN:
		case SDL_MOUSEBUTTONDOWN:
		case SDL_CONTROLLERBUTTONDOWN:
		case SDL_JOYBUTTONDOWN:
			skip = true;
			break;
		default:
			break;
		}
	}

	/* In a headset the controllers are not SDL devices: read them here,
	   or nothing in the player's hands can skip the film. */
	if ( vr_enabled )
	{
		bool held;
		int i;
		vr_openxr_sync_input();
		held = vr_input.fire1 > 0.5f || vr_input.fire2 > 0.5f || vr_input.menu_edge;
		for ( i = 1; i < VRB_PAD_COUNT; i++ )
			held = held || vr_input.pad[i];
		if ( held && !vr_held_before )
			skip = true;
		vr_held_before = held;
	}
	return skip;
}

/* Test-time evidence, only ever reached under -introsecs. Writes the decoded
   frame and the actual window, so a decode fault and a draw fault cannot be
   mistaken for each other. */
static void movie_dump( movie_t * m )
{
	FILE * f;
	GLint vp[4];
	unsigned char * shot;
	int x, y;

	f = fopen( "logs/movie_decoded.ppm", "wb" );
	if ( f )
	{
		fprintf( f, "P6\n%d %d\n255\n", m->tex_w, m->tex_h );
		for ( y = 0; y < m->tex_h; y++ )
			for ( x = 0; x < m->tex_w; x++ )
				fwrite( m->rgb + ( (size_t) y * m->tex_w + x ) * 4, 1, 3, f );
		fclose( f );
	}

	glGetIntegerv( GL_VIEWPORT, vp );
	shot = (unsigned char *) malloc( (size_t) vp[2] * vp[3] * 3 );
	if ( !shot )
		return;
	glPixelStorei( GL_PACK_ALIGNMENT, 1 );
	glReadPixels( vp[0], vp[1], vp[2], vp[3], GL_RGB, GL_UNSIGNED_BYTE, shot );
	f = fopen( "logs/movie_screen.ppm", "wb" );
	if ( f )
	{
		fprintf( f, "P6\n%d %d\n255\n", vp[2], vp[3] );
		for ( y = vp[3] - 1; y >= 0; y-- )
			fwrite( shot + (size_t) y * vp[2] * 3, 1, (size_t) vp[2] * 3, f );
		fclose( f );
	}
	free( shot );
	DebugPrintf( "movie: dumped decoded %dx%d and screen %dx%d\n",
		m->tex_w, m->tex_h, vp[2], vp[3] );
}

bool movie_play( const char * path )
{
	movie_t m;
	ogg_page   pg;
	ogg_packet pkt;
	int    t_headers = 0, v_headers = 0;
	int    i;
	bool   done = false;
	double first_wall = 0.0;
	double frame_time = 0.0;
	int    have_frame = 0;
	int    at_eof = 0;
	int    need_data = 0;
	int    dump_pending = 0;
	double last_eof_report = 0.0;
	double last_progress = 0.0;
	int    audio_done = 0;
	double clock_at_audio_end = 0.0;
	double wall_at_audio_end = 0.0;

	if ( !movie_enabled )
		return false;

	memset( &m, 0, sizeof(m) );
	movie_worst_drift_ms = 0.0;

	m.f = fopen( path, "rb" );
	if ( !m.f )
	{
		DebugPrintf( "movie: no %s, skipping the intro\n", path );
		return false;
	}

	ogg_sync_init( &m.sync );
	th_info_init( &m.t_info );
	th_comment_init( &m.t_comment );
	vorbis_info_init( &m.v_info );
	vorbis_comment_init( &m.v_comment );

	/* ---- headers ---- */
	while ( !( t_headers && ( v_headers || !m.have_vorbis ) ) )
	{
		if ( !movie_read( &m ) )
			break;

		while ( ogg_sync_pageout( &m.sync, &pg ) > 0 )
		{
			ogg_stream_state test;

			if ( !ogg_page_bos( &pg ) )
			{
				movie_page( &m, &pg );
				continue;
			}

			ogg_stream_init( &test, ogg_page_serialno( &pg ) );
			ogg_stream_pagein( &test, &pg );
			if ( ogg_stream_packetpeek( &test, &pkt ) != 1 )
			{
				ogg_stream_clear( &test );
				continue;
			}

			if ( !m.have_theora &&
			     th_decode_headerin( &m.t_info, &m.t_comment, &m.t_setup, &pkt ) >= 0 )
			{
				memcpy( &m.t_stream, &test, sizeof(test) );
				m.have_theora = 1;
				ogg_stream_packetout( &m.t_stream, NULL );
			}
			else if ( !m.have_vorbis &&
			          vorbis_synthesis_headerin( &m.v_info, &m.v_comment, &pkt ) >= 0 )
			{
				memcpy( &m.v_stream, &test, sizeof(test) );
				m.have_vorbis = 1;
				ogg_stream_packetout( &m.v_stream, NULL );
			}
			else
			{
				ogg_stream_clear( &test );
			}
		}

		while ( m.have_theora && t_headers < 2 &&
		        ogg_stream_packetout( &m.t_stream, &pkt ) > 0 )
		{
			if ( th_decode_headerin( &m.t_info, &m.t_comment, &m.t_setup, &pkt ) <= 0 )
				t_headers = 2;
			else
				t_headers++;
		}
		while ( m.have_vorbis && v_headers < 2 &&
		        ogg_stream_packetout( &m.v_stream, &pkt ) > 0 )
		{
			if ( vorbis_synthesis_headerin( &m.v_info, &m.v_comment, &pkt ) < 0 )
				v_headers = 2;
			else
				v_headers++;
		}

		if ( m.have_theora && t_headers >= 2 ) t_headers = 1;
		if ( m.have_vorbis && v_headers >= 2 ) v_headers = 1;
		if ( feof( m.f ) )
			break;
	}

	if ( !m.have_theora )
	{
		DebugPrintf( "movie: %s has no theora stream\n", path );
		movie_cleanup( &m );
		return false;
	}

	m.t_dec = th_decode_alloc( &m.t_info, m.t_setup );
	if ( !m.t_dec )
	{
		DebugPrintf( "movie: theora decoder would not start\n" );
		movie_cleanup( &m );
		return false;
	}

	m.tex_w = m.t_info.pic_width;
	m.tex_h = m.t_info.pic_height;
	m.rgb = (unsigned char *) malloc( (size_t) m.tex_w * m.tex_h * 4 );
	if ( !m.rgb )
	{
		movie_cleanup( &m );
		return false;
	}

	DebugPrintf( "movie: %s  %dx%d  %.3f fps  vorbis=%d\n", path,
		m.tex_w, m.tex_h,
		(double) m.t_info.fps_numerator / (double) m.t_info.fps_denominator,
		m.have_vorbis );

	if ( m.have_vorbis && alcGetCurrentContext() )
	{
		if ( vorbis_synthesis_init( &m.v_dsp, &m.v_info ) == 0 )
		{
			vorbis_block_init( &m.v_dsp, &m.v_block );
			m.v_ready = 1;
			m.alfmt = ( m.v_info.channels >= 2 ) ? AL_FORMAT_STEREO16
			                                     : AL_FORMAT_MONO16;
			alGenSources( 1, &m.source );
			alGenBuffers( MOVIE_AL_BUFFERS, m.albuf );
			alSourcei( m.source, AL_SOURCE_RELATIVE, AL_TRUE );
			alSource3f( m.source, AL_POSITION, 0.0f, 0.0f, 0.0f );
			alSourcef( m.source, AL_ROLLOFF_FACTOR, 0.0f );

			/* All buffers start free. Queueing them empty, as a first attempt
			   did, makes OpenAL report them processed immediately, so the
			   sample clock never advances, which silently turns the audio
			   master clock into a constant zero and lets the video run at
			   decode speed: 3.3 minutes of film in 17 seconds, measured. */
			for ( i = 0; i < MOVIE_AL_BUFFERS; i++ )
				m.freebuf[i] = m.albuf[i];
			m.free_count = MOVIE_AL_BUFFERS;
		}
	}

	glGenTextures( 1, &m.tex );
	glBindTexture( GL_TEXTURE_2D, m.tex );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, m.tex_w, m.tex_h, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, NULL );

	first_wall = (double) SDL_GetTicks() / 1000.0;
	last_progress = first_wall;

	/* ---- playback ----
	   One frame is decoded and held with its presentation time, and drawn only
	   once the clock reaches it. Without the hold, a frame whose time is still
	   in the future is drawn immediately and the whole film plays at decode
	   speed. */
	while ( !done )
	{
		ogg_int64_t gp;
		double      clock;

		if ( movie_skipped() )
			break;

		if ( movie_max_seconds > 0 &&
		     (double) SDL_GetTicks() / 1000.0 - first_wall > (double) movie_max_seconds )
		{
			/* Dump the NEXT drawn frame rather than this instant: after
			   render_flip() the back buffer is undefined, so reading here
			   captures black and looks like a drawing fault that is not
			   there. Ask for the dump, let one more frame be drawn, and
			   read it before the swap. */
			if ( !dump_pending )
				DebugPrintf( "movie: -introsecs cap reached\n" );
			dump_pending = 1;
			if ( (double) SDL_GetTicks() / 1000.0 - first_wall
			     > (double) movie_max_seconds + 3.0 )
				break;   /* safety: no frame arrived to dump */
		}

		/* Read only when something wants data. Reading unconditionally races
		   to EOF in 25 seconds on a 3.3 minute film. Note this must also run
		   while a frame is held: an earlier version stopped reading until the
		   held frame was shown, which starved the audio device, stalled the
		   clock that frame was waiting on, and deadlocked the player. */
		need_data = 0;
		if ( !have_frame )
			need_data = 1;
		if ( m.v_ready && m.free_count > 0 )
			need_data = 1;

		if ( need_data )
		{
			if ( !movie_read( &m ) && feof( m.f ) )
				at_eof = 1;
			while ( ogg_sync_pageout( &m.sync, &pg ) > 0 )
				movie_page( &m, &pg );
		}

		movie_feed_audio( &m );

		while ( !have_frame && ogg_stream_packetout( &m.t_stream, &pkt ) > 0 )
		{
			if ( th_decode_packetin( m.t_dec, &pkt, &gp ) == 0 )
			{
				frame_time = th_granule_time( m.t_dec, gp );
				have_frame = 1;
			}
		}

		/* Finished only when the file is done AND nothing is left to show or
		   hear. Ending at EOF alone truncates playback by the whole buffer. */
		/* When the audio ends the clock STOPS, because the clock is the
		   device's playback position. Any video frame still held is then
		   waiting on a clock that will never advance again: measured:
		   at_eof, queue empty, source AL_STOPPED, clock frozen at 8.06 s and
		   one frame held forever. So hand timing over to the wall clock,
		   continuing from where the audio left off, and let the tail drain. */
		if ( at_eof && m.v_ready && !audio_done &&
		     ( m.al_queued == 0 || m.al_state == AL_STOPPED ) )
		{
			audio_done         = 1;
			clock_at_audio_end = movie_audio_clock( &m );
			wall_at_audio_end  = (double) SDL_GetTicks() / 1000.0;
		}

		if ( at_eof && !have_frame && ( !m.v_ready || audio_done ) )
			done = true;

		/* Once a second while shutting down, say why we are still here. */
		if ( at_eof && (double) SDL_GetTicks() / 1000.0 - last_eof_report >= 1.0 )
		{
			last_eof_report = (double) SDL_GetTicks() / 1000.0;
			DebugPrintf( "movie: at_eof frames=%d have_frame=%d queued=%d state=%d free=%d clock=%.2f\n",
				m.frames_shown, have_frame, (int) m.al_queued, (int) m.al_state,
				m.free_count, movie_audio_clock( &m ) );
		}

		/* A malformed or truncated file must never be able to hang the game.
		   Whatever the cause, stop showing the intro and get to the menu. */
		if ( m.frames_shown > 0 &&
		     (double) SDL_GetTicks() / 1000.0 - last_progress > 5.0 )
		{
			DebugPrintf( "movie: no progress for 5s (at_eof=%d queued=%d state=%d), giving up\n",
				at_eof, (int) m.al_queued, (int) m.al_state );
			break;
		}

		if ( audio_done )
			clock = clock_at_audio_end
			      + ( (double) SDL_GetTicks() / 1000.0 - wall_at_audio_end );
		else
			clock = movie_audio_clock( &m );
		if ( clock < 0.0 )
			clock = (double) SDL_GetTicks() / 1000.0 - first_wall;

		if ( have_frame && frame_time <= clock )
		{
			th_ycbcr_buffer yuv;

			if ( th_decode_ycbcr_out( m.t_dec, yuv ) == 0 )
			{
				double drift = clock - frame_time;

				movie_yuv_to_rgb( &m, yuv );
				glBindTexture( GL_TEXTURE_2D, m.tex );
				glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, m.tex_w, m.tex_h,
					GL_RGBA, GL_UNSIGNED_BYTE, m.rgb );

				/* Wrap the frame the way RenderLoop() does, or none of this
				   reaches the headset: movie_play() runs its own loop before
				   the main one, so without this it draws into the window with
				   the eye buffer's viewport still set and the compositor never
				   sees a thing. Menus already ride the floating quad panel via
				   submit_panel(), and a flat film wants exactly that. */
				vr_openxr_begin_frame();
				vr_target_begin_frame();

				movie_draw( &m );

				if ( dump_pending )
				{
					movie_dump( &m );
					dump_pending = 0;
					done = true;
				}

				if ( vr_views_valid )
				{
					vr_panel_frame = true;
					vr_openxr_submit_panel();
					vr_panel_frame = false;
				}
				vr_openxr_end_frame();
				vr_target_end_frame();

				render_flip( &render_info );
				m.frames_shown++;
				last_progress = (double) SDL_GetTicks() / 1000.0;

				if ( drift < 0.0 ) drift = -drift;
				drift *= 1000.0;
				if ( drift > movie_worst_drift_ms )
					movie_worst_drift_ms = drift;
			}
			have_frame = 0;
		}
		else if ( have_frame )
		{
			SDL_Delay( 1 );
		}
	}

	DebugPrintf( "movie: %d frames shown, worst A/V drift %.1f ms, blockin refused %ld\n",
		m.frames_shown, movie_worst_drift_ms, m.blockin_refused );

	movie_cleanup( &m );
	return m.frames_shown > 0;
}

#else /* !MOVIE_THEORA */

bool movie_play( const char * path )
{
	(void) path;
	return false;
}

#endif
