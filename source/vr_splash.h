/* Forsaken VR: the splash on the menu screen at start
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

#ifndef VR_SPLASH_H
#define VR_SPLASH_H

/* Shows path (a PNG) on the menu screen for ms milliseconds or until any
   button, in VR only. No file, nothing shown. Blocking, like the intro. */
void vr_splash_show( const char * path, int ms );

#endif
