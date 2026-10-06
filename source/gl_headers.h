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

#if defined(GL) && (GL > 1) && (defined(MINGW) || defined(_WIN32))
  #define FS_USE_GLEW 1
  #include <GL/glew.h>
#endif

#include "SDL_opengl.h"

/* returns non-zero on success; logs its own failure */
int fs_gl_loader_init( void );

#endif /* FS_GL_HEADERS_INCLUDED */
