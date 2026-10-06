/*
  Streaming Ogg Vorbis music.

  Retail Forsaken played its soundtrack from CD redbook audio, which the port
  never reimplemented: there is no music playback path at all in upstream
  ForsakenX. This adds one, streaming the Ogg Vorbis rips in Music/OGG through
  a dedicated OpenAL source.

  It is deliberately self-contained: nothing here touches the renderer, the
  engine's matrices, the 2D pass, or the existing sfx code. The only calls into
  the rest of the game are music_init/shutdown/update from main.c, and reads of
  MyGameStatus and ShortLevelNames[LevelNum] to decide what should be playing.

  Which track goes with which level is data, not code: see Music/playlist.txt.
  Edit that file and restart; no rebuild needed.

  Everything degrades quietly. A missing Music folder, a missing playlist, an
  unreadable track or a build without libvorbis all leave the game running
  exactly as it did before, with a line in the log saying why.
*/
#ifndef MUSIC_INCLUDED
#define MUSIC_INCLUDED

#include "main.h"

/* Opens the OpenAL streaming source and reads Music/playlist.txt. Call after
   sound_init(). Safe to call when music is unavailable; returns false having
   logged the reason, and every other entry point then does nothing. */
bool music_init( void );

void music_shutdown( void );

/* Pumps the stream and picks the track that matches the current game state.
   Call once per frame from the main loop. Cheap when nothing has changed. */
void music_update( void );

/* Play a specific file under Music/OGG. Pass loop=true for level tracks.
   Used by the state machine above, and directly by the intro player. */
bool music_play_file( const char * filename, bool loop );

/* Fade out and stop. The fade runs in music_update(). */
void music_stop( void );

/* 0.0 .. 1.0. Persisted by the caller; -musicvol sets the startup value. */
void  music_set_volume( float v );
float music_get_volume( void );

/* True while a track is open and not fully faded out. */
bool music_is_playing( void );

/* Suspends the state machine so an explicit music_play_file() is not
   immediately overridden: the intro movie uses this. */
void music_set_auto( bool on );

/* -nomusic on the command line. */
extern bool music_disabled;

#endif /* MUSIC_INCLUDED */
