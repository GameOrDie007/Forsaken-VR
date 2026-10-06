/* Forsaken VR: controller vibration
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

#ifndef VR_HAPTICS_H
#define VR_HAPTICS_H

#include <stdbool.h>

extern int  vr_vibration;       /* VRVibration: 0 off, 1 on (default) */
extern bool vr_haptics_log;     /* -vrhapticslog: log every pulse with its reason */
extern int  vr_haptics_pulses;

/* Once a frame from the main loop. Inert unless VRVibration is on. */
void vr_haptics_update( void );

#endif
