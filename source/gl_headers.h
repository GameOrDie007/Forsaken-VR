/*
  Central include point for the OpenGL headers.

  Why this file exists:

  On Windows, opengl32.dll only ever exports the OpenGL 1.1 entry points.
  Everything above that: shaders, VBOs, FBOs, i.e. all of the GL 2/3 render
  paths: must be resolved at runtime through wglGetProcAddress. On Linux and
  macOS libGL exports those symbols directly, which is why the upstream tree
  links fine there and fails at link time here with a wall of
  "undefined reference to glCreateShader" style errors.

  GLEW fills that gap. It must be included *before* any gl.h or it deliberately
  errors out, so every translation unit that wants GL includes this header
  rather than SDL_opengl.h directly.

  Call fs_gl_loader_init() once, after the GL context exists, before issuing
  any GL >= 2 call. On non-Windows builds it is a no-op that returns true.
*/
#ifndef FS_GL_HEADERS_INCLUDED
#define FS_GL_HEADERS_INCLUDED

#ifdef GLES
  /* Quest and Steam Frame standalone: OpenGL ES 3. The few desktop calls the
     game makes are given their ES form here (fs_gles_map_buffer is in
     render_gl_shared.c); the wireframe and point debug views, and two
     desk-probe readbacks, do nothing. */
  #include <GLES3/gl3.h>
  #include <GLES2/gl2ext.h>
  #define glClearDepth   glClearDepthf
  #define glDepthRange   glDepthRangef
  void * fs_gles_map_buffer( GLenum target, GLenum access );
  #define glMapBuffer    fs_gles_map_buffer
  #define glPolygonMode( face, mode )                         ((void) 0)
  #define glGetTexImage( target, level, format, type, pixels ) ((void) 0)
  #define glGetBufferSubData( target, offset, size, data )     ((void) 0)
  #ifndef GL_WRITE_ONLY
    #define GL_WRITE_ONLY  0x88B9
  #endif
  #ifndef GL_FRAMEBUFFER_SRGB
    /* GL_EXT_sRGB_write_control's switch: the blits turn it off, as on the PC */
    #define GL_FRAMEBUFFER_SRGB  0x8DB9
  #endif
  /* texture size queries are OpenGL ES 3.1; the desk probe that asks just stops */
  #define glGetTexLevelParameteriv( target, level, pname, params ) ( *(params) = 0 )
  #ifndef GL_TEXTURE_WIDTH
    #define GL_TEXTURE_WIDTH   0x1000
    #define GL_TEXTURE_HEIGHT  0x1001
  #endif
  #ifndef GL_POINT
    #define GL_POINT  0x1B00
    #define GL_LINE   0x1B01
    #define GL_FILL   0x1B02
  #endif
#else

#if defined(GL) && (GL > 1) && (defined(MINGW) || defined(_WIN32))
  #define FS_USE_GLEW 1
  #include <GL/glew.h>
#endif

#include "SDL_opengl.h"

#endif /* GLES */

/* returns non-zero on success; logs its own failure */
int fs_gl_loader_init( void );

#endif /* FS_GL_HEADERS_INCLUDED */
