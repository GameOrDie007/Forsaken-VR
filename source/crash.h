/* Forsaken VR: crash reporting
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

#ifndef CRASH_H
#define CRASH_H

/* Top-level crash reporting: see crash.c for what it writes and why.
 *
 * crash_install() as early as possible; it also deletes any stale report so
 * that a file present afterwards is unambiguously from this run.
 * -crashtest faults on purpose, which is the only way to know the handler
 * works. */

void         crash_install( void );
void         crash_test( void );
const char * crash_report_path( void );

#endif
