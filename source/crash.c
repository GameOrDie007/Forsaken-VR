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

/* Crash reporting.
 *
 * Once a build leaves this machine the crash you have to explain happens
 * somewhere you cannot attach a debugger: a tester's PC, a headset session, a
 * machine with no toolchain. One file the player can send turns that into an
 * answer.
 *
 * Two rules shape what is written here:
 *
 * - RECORD ADDRESSES, NOT NAMES. Resolving symbols inside a process that is
 *   already dying is the least reliable moment to do it, and a handler that
 *   tries will sometimes die itself and leave a header with nothing under it.
 *   Frames in our own exe are written as the address addr2line wants (the
 *   linker's preferred image base plus the offset) so a line can be pasted
 *   straight into addr2line against a matching build. Other modules show the
 *   offset alone.
 *
 * - WRITE IT BESIDE THE EXECUTABLE. A report that lands wherever the game
 *   happened to be launched from is a report nobody finds, and its absence
 *   cannot be told apart from the handler never having run.
 *
 * Some faults bypass this entirely: the Windows fail-fast codes, stack
 * cookie violations, heap corruption caught by the allocator, stack overflow.
 * They terminate without running an unhandled-exception filter, so NO FILE AT
 * ALL is itself a diagnosis: it says look in the operating system's own crash
 * log, which records the faulting module and offset regardless.
 */

#include "crash.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef WIN32

#include <windows.h>
#include <stdio.h>
#include <time.h>

#include "version.h"

#define CRASH_MAX_FRAMES 48

static char crash_path[MAX_PATH * 2];
static char crash_exe_file[MAX_PATH];

/* <exe directory>\crash.txt */
static void crash_build_path( void )
{
	char  exe[MAX_PATH];
	DWORD n;
	char *slash;

	crash_path[0] = 0;

	n = GetModuleFileNameA( NULL, exe, MAX_PATH );
	if ( n == 0 || n >= MAX_PATH )
	{
		strcpy( crash_path, "crash.txt" );
		return;
	}

	slash = strrchr( exe, '\\' );
	if ( slash )
		*( slash + 1 ) = 0;
	else
		exe[0] = 0;

	sprintf( crash_path, "%scrash.txt", exe );
	GetModuleFileNameA( NULL, crash_exe_file, MAX_PATH );
}

static const char * crash_code_name( DWORD code )
{
	switch ( code )
	{
	case EXCEPTION_ACCESS_VIOLATION:      return "ACCESS_VIOLATION";
	case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
	case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
	case EXCEPTION_FLT_DIVIDE_BY_ZERO:    return "FLT_DIVIDE_BY_ZERO";
	case EXCEPTION_ILLEGAL_INSTRUCTION:   return "ILLEGAL_INSTRUCTION";
	case EXCEPTION_INT_DIVIDE_BY_ZERO:    return "INT_DIVIDE_BY_ZERO";
	case EXCEPTION_PRIV_INSTRUCTION:      return "PRIV_INSTRUCTION";
	case EXCEPTION_STACK_OVERFLOW:        return "STACK_OVERFLOW";
	case EXCEPTION_IN_PAGE_ERROR:         return "IN_PAGE_ERROR";
	default:                              return "UNKNOWN";
	}
}

/* Distance between where our exe WANTED to load and where it actually did.

   The obvious version of this reads OptionalHeader.ImageBase out of the mapped
   module, and that does not work, because the Windows loader rewrites that
   field in memory to the address it actually chose. It hands back the runtime
   base and the arithmetic cancels to nothing. Measured: it printed
   0x7ff6cd482dc5, which addr2line cannot resolve.

   So read the preferred base from the FILE, once, at startup: on disk the
   header still says what the linker asked for. Doing it at install time also
   keeps file parsing out of a handler running in a dying process. */
static long long crash_exe_delta = 0;

static void crash_find_delta( const char * exe_path )
{
	FILE *        fp;
	unsigned char dos[64];
	unsigned char nt[256];
	long          lfanew;
	unsigned long long preferred = 0;
	HMODULE       self = GetModuleHandleA( NULL );

	crash_exe_delta = 0;
	if ( !self )
		return;

	fp = fopen( exe_path, "rb" );
	if ( !fp )
		return;

	if ( fread( dos, 1, sizeof(dos), fp ) == sizeof(dos) &&
	     dos[0] == 'M' && dos[1] == 'Z' )
	{
		lfanew = (long) ( dos[60] | ( dos[61] << 8 ) |
		                  ( dos[62] << 16 ) | ( (long) dos[63] << 24 ) );
		if ( lfanew > 0 && fseek( fp, lfanew, SEEK_SET ) == 0 &&
		     fread( nt, 1, sizeof(nt), fp ) == sizeof(nt) &&
		     nt[0] == 'P' && nt[1] == 'E' )
		{
			/* PE signature 4 + COFF header 20 = 24; ImageBase is at offset 24
			   of the optional header for PE32+ (magic 0x20b). */
			int magic = nt[24] | ( nt[25] << 8 );
			int i;
			if ( magic == 0x20b )
			{
				for ( i = 7; i >= 0; i-- )
					preferred = ( preferred << 8 ) | nt[24 + 24 + i];
			}
			else if ( magic == 0x10b )
			{
				for ( i = 3; i >= 0; i-- )
					preferred = ( preferred << 8 ) | nt[24 + 28 + i];
			}
		}
	}
	fclose( fp );

	if ( preferred )
		crash_exe_delta = (long long) preferred - (long long) (size_t) self;
}

/* One address as module + resolvable location. */
static void crash_write_addr( FILE * f, const char * label, void * addr )
{
	HMODULE mod = NULL;
	char    name[MAX_PATH];
	char *  leaf;

	/* Report the address addr2line actually wants: the module's PREFERRED
	   image base plus the offset. ASLR moves the module, so a runtime address
	   means nothing on another machine, and a bare offset makes addr2line
	   answer "??", which reads as "this build has no symbols" and sends you
	   off rebuilding a configuration you did not need. Measured: 0x42d38 gave
	   "??", 0x140042d38 gave crash_test at crash.c:202. */

	if ( !addr )
	{
		fprintf( f, "  %-10s (null)\n", label );
		return;
	}

	if ( GetModuleHandleExA(
	         GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
	         GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
	         (LPCSTR) addr, &mod ) && mod &&
	     GetModuleFileNameA( mod, name, MAX_PATH ) )
	{
		leaf = strrchr( name, '\\' );
		leaf = leaf ? leaf + 1 : name;

		if ( mod == GetModuleHandleA( NULL ) && crash_exe_delta )
			fprintf( f, "  %-10s %s  0x%llx   (+0x%llx)\n", label, leaf,
			         (unsigned long long) ( (long long) (size_t) addr + crash_exe_delta ),
			         (unsigned long long) ( (char *) addr - (char *) mod ) );
		else
			fprintf( f, "  %-10s %s  +0x%llx\n", label, leaf,
			         (unsigned long long) ( (char *) addr - (char *) mod ) );
	}
	else
	{
		fprintf( f, "  %-10s <no module>  (abs 0x%llx)\n", label,
		         (unsigned long long) (size_t) addr );
	}
}

static LONG WINAPI crash_filter( EXCEPTION_POINTERS * ep )
{
	FILE *  f;
	time_t  now;
	void *  frames[CRASH_MAX_FRAMES];
	USHORT  count, i;
	DWORD   code = ep && ep->ExceptionRecord
	             ? ep->ExceptionRecord->ExceptionCode : 0;

	f = fopen( crash_path, "w" );
	if ( !f )
		return EXCEPTION_EXECUTE_HANDLER;

	now = time( NULL );
	fprintf( f, "Forsaken VR crash report\n" );
	fprintf( f, "========================\n\n" );
	fprintf( f, "when      : %s", ctime( &now ) );
	fprintf( f, "build     : %s %s\n", __DATE__, __TIME__ );
	fprintf( f, "version   : %s.%s\n\n", PXV, PXMPV );

	fprintf( f, "exception : 0x%08lx  %s\n",
	         (unsigned long) code, crash_code_name( code ) );

	if ( ep && ep->ExceptionRecord )
	{
		crash_write_addr( f, "at", ep->ExceptionRecord->ExceptionAddress );

		if ( code == EXCEPTION_ACCESS_VIOLATION &&
		     ep->ExceptionRecord->NumberParameters >= 2 )
		{
			ULONG_PTR what = ep->ExceptionRecord->ExceptionInformation[0];
			fprintf( f, "  %-10s %s 0x%llx\n", "tried to",
			         what == 0 ? "read" : ( what == 1 ? "write" : "execute" ),
			         (unsigned long long)
			             ep->ExceptionRecord->ExceptionInformation[1] );
		}
	}

	fprintf( f, "\nstack (most recent first)\n" );
	count = RtlCaptureStackBackTrace( 0, CRASH_MAX_FRAMES, frames, NULL );
	if ( count == 0 )
	{
		fprintf( f, "  <none captured>\n" );
	}
	else
	{
		char label[16];
		for ( i = 0; i < count; i++ )
		{
			sprintf( label, "#%d", (int) i );
			crash_write_addr( f, label, frames[i] );
		}
	}

	fprintf( f,
	  "\nThe first frames are this handler and Windows' own; the game's start\n"
	  "below them. A projectx.exe line carries the address to resolve directly:\n"
	  "    addr2line -f -e projectx.exe 0x140043065\n"
	  "Other modules show only +offset, which needs that module's own base.\n"
	  "Resolve against a projectx.exe with the same bytes as the one that crashed.\n" );

	fclose( f );
	return EXCEPTION_EXECUTE_HANDLER;
}

void crash_install( void )
{
	crash_build_path();
	crash_find_delta( crash_exe_file );

	/* Keep the previous run's report beside this one, as crash.prev.txt.

	   The first version deleted crash.txt at startup, on the reasoning that a
	   stale report is indistinguishable from a fresh one. True, but a player
	   who crashes almost always starts the game again to get back in before
	   thinking to send anything, and deleting it there destroyed the only
	   evidence. Rotating keeps both properties: crash.txt is only ever from
	   THIS run, and the last crash survives one restart. */
	{
		char prev[MAX_PATH * 2];
		char * dot;

		strcpy( prev, crash_path );
		dot = strrchr( prev, '.' );
		if ( dot )
			strcpy( dot, ".prev.txt" );
		else
			strcat( prev, ".prev" );

		if ( GetFileAttributesA( crash_path ) != INVALID_FILE_ATTRIBUTES )
			MoveFileExA( crash_path, prev, MOVEFILE_REPLACE_EXISTING );
	}

	SetUnhandledExceptionFilter( crash_filter );
}

const char * crash_report_path( void )
{
	return crash_path;
}

/* Deliberate fault, so the handler can be proved to fire. A guard that has
   never refused anything has never been tested. */
void crash_test( void )
{
	volatile int * p = (volatile int *) 0;
	*p = 1;
}

/* Whether n bytes at p can be read, without touching them: for addresses that
   come from a file (a saved game's raw pointers). Not for every frame. */
bool mem_readable( const void * p, size_t n )
{
	MEMORY_BASIC_INFORMATION mbi;
	const char * a = (const char *) p;
	const char * end;

	if( !p )
		return false;
	if( !VirtualQuery( p, &mbi, sizeof( mbi ) ) || mbi.State != MEM_COMMIT )
		return false;
	if( mbi.Protect & ( PAGE_NOACCESS | PAGE_GUARD ) )
		return false;
	if( !( mbi.Protect & ( PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY |
	                       PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY ) ) )
		return false;
	end = (const char *) mbi.BaseAddress + mbi.RegionSize;
	return a + n <= end;
}

#else /* !WIN32 */

bool mem_readable( const void * p, size_t n ) { (void) n; return p != NULL; }
void crash_install( void ) {}
void crash_test( void ) {}
const char * crash_report_path( void ) { return ""; }

#endif
