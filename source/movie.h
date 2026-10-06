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

#ifndef MOVIE_H
#define MOVIE_H

/* Ogg Theora / Vorbis playback.
 *
 * Retail Forsaken opened with a full motion intro. This port had no video
 * playback code at all: setup.ps1 copies intro.ogv out of the player's own
 * Forsaken Remastered install and, until now, nothing ever opened it. The
 * hooks left in music.c for "the intro player" had no caller.
 *
 * movie_play() is blocking and runs its own frame loop, the way a 1998 FMV
 * did. It returns when the file ends, when the player presses a key or a
 * controller button, or immediately if anything at all goes wrong: a
 * missing file, a stream it cannot decode, no audio device. Failure is
 * always silent and always degrades to "no intro", which is exactly the
 * behaviour this port had before, so it cannot make anything worse. A
 * malformed file cannot hang the game either: playback gives up if no frame
 * has been shown for five seconds.
 *
 * Build with MOVIE=0 to compile the whole thing out to a stub.
 */

#include <stdbool.h>

/* Plays path to completion. Returns true if any frame was shown. */
bool movie_play( const char * path );

/* ON by default, flat and in VR; -nointro turns it off. */
extern bool movie_enabled;

/* -introsecs:N stops playback after N seconds. 0 means play to the end.
   For testing without sitting through 3.3 minutes, and without having to
   kill the process to get out of it. */
extern int movie_max_seconds;

/* Measured A/V drift, in milliseconds, worst case over the last playback.
 * Video is scheduled against the audio clock, so this is the honest check
 * that sync held: it does not need ears. */
extern double movie_worst_drift_ms;

#endif
