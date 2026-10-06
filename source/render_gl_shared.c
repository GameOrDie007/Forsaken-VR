#ifdef GL
#include "render_gl_shared.h"
#include "vr_target.h"
#include "vr_openxr.h"
#include <math.h>

// windows needs explicit retrieval of newer GL functions...
/*
#ifdef WIN32

static PFNGLBLENDCOLOREXTPROC glBlendColor = NULL;

void no_glBlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha){}

static void bind_glBlendColor(void)
{
        if( !glBlendColor ) glBlendColor = SDL_GL_GetProcAddress( "glBlendColor" );
        if( !glBlendColor ) glBlendColor = SDL_GL_GetProcAddress( "glBlendColorEXT" );
        if( !glBlendColor )
        {
                glBlendColor = (PFNGLBLENDCOLOREXTPROC) no_glBlendColor;
                DebugPrintf("bind_glBlendColor: failed to get proc address\n");
        }
}

static void bind_gl_funcs(void)
{
        bind_glBlendColor();
}

#endif // WIN32
*/

// CHECK_GL_ERRORS will set this when called
GLenum render_last_gl_error = GL_NO_ERROR;

// description of last render error
const char * render_error_description( int e )
{
	CHECK_GL_ERRORS;
	if(render_last_gl_error != GL_NO_ERROR)
		return (const char *) gluErrorString(render_last_gl_error);
	else
		return NULL;
}

// poly modes

void render_mode_wireframe(void)
{
	glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
}

void render_mode_points(void)
{
	glPolygonMode(GL_FRONT_AND_BACK, GL_POINT);
}

void render_mode_fill(void)
{
	glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// unused in opengl
bool FSBeginScene(){ return true; }
bool FSEndScene(){ return true; }

// prototypes
void reset_trans( void );

// TODO	- should get this from gl caps ?
bool bSquareOnly = true;

//
// Texture Routines
//

static u_int8_t gamma_table[256];

void build_gamma_table( double gamma )
{
	double k;
	int i;

	DebugPrintf("build_gamma_table( %f )\n",gamma);

#ifndef DEBUG_ON
	if (gamma <= 0)
		gamma = 1.0;
#endif

	k = 255.0/pow(255.0, 1.0/gamma);
	
	for (i = 0; i <= 255; i++)
	{
		gamma_table[i] = (u_int8_t)(k*(pow((double)i, 1.0/gamma)));
		if( i && !gamma_table[i] )
			gamma_table[i] = 1;
	}
}

void release_texture( LPTEXTURE texture )
{
	if(!texture) return;
	texture_t *texdata = (texture_t *) texture;
	glDeleteTextures( 1, &texdata->id );
	CHECK_GL_ERRORS;
	free(texture);
}

static bool create_texture(LPTEXTURE *t, const char *path, u_int16_t *width, u_int16_t *height, int numMips, bool * colorkey)
{
	texture_t *texdata;
	texture_image_t image;

	Change_Ext( path, image.path, ".PNG" );
	if( ! File_Exists( (char*) image.path ) )
	{
		DebugPrintf("Could not find texture file: %s\n",path);
		return true;
	}

	if(load_image( &image, numMips )!=0)
	{
		DebugPrintf("couldn't load image\n");
		return false;
	}

	// return values
	*width  = (u_int16_t) image.w;
	*height = (u_int16_t) image.h;
	(*colorkey) = (bool) image.colorkey;

	// employ colour key and gamma correction
	{
		int y, x;
		int size = 4;
		int pitch = size*image.w;
		for (y = 0; y < image.h; y++)
		{
			for (x = 0; x < image.w; x++)
			{
				// move to the correct offset in the data
				// y is the row and pitch is the size of a row
				// (x*size) is the length of each pixel data (column)
				DWORD index = (y*pitch)+(x*size);

				// image.data is packed in rgba
				image.data[index]   = (char) gamma_table[ (u_int8_t) image.data[index]];	   // red
				image.data[index+1] = (char) gamma_table[ (u_int8_t) image.data[index+1]];  // green
				image.data[index+2] = (char) gamma_table[ (u_int8_t) image.data[index+2]];  // blue
				image.data[index+3] = (char) gamma_table[ (u_int8_t) image.data[index+3]];  // alpha

				// colour key
				if( image.colorkey && (image.data[index] + image.data[index+1] + image.data[index+2]) == 0 )
					image.data[index+3] = 0; // alpha - pixel will not be rendered do to alpha value tests

			}
		}
	}

	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

	// create a new opengl texture
	if( ! *t )
	{
		texdata = malloc(sizeof(texture_t));
		glGenTextures(1, &texdata->id);
		glBindTexture(GL_TEXTURE_2D, texdata->id);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, image.w, image.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image.data);
		CHECK_GL_ERRORS;
	}
	// updates an existing texture
	else
	{
		texdata = (texture_t *) *t;
		glBindTexture(GL_TEXTURE_2D, texdata->id);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, image.w, image.h, GL_RGBA, GL_UNSIGNED_BYTE, image.data);
		CHECK_GL_ERRORS;
	}

	// When the texture area is small, filter between the two nearest mipmaps as well as
	// within each one. MIPMAP_NEAREST picks a single level, so the change between levels
	// is a visible band across a surface: easy to miss on a monitor, obvious in a
	// headset whose eye buffer is several times the window's resolution.
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
	// when texture area is large, bilinear filter the original
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	// the texture wraps over at the edges (repeat)
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
	glTexParameterf( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );

	// anisotropic settings
	if(caps.anisotropic)
		glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, caps.anisotropic);

#if GL > 1
	glGenerateMipmap( GL_TEXTURE_2D );
#else
	// generates full range of mipmaps and scales to nearest power of 2
	if(gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGBA, image.w, image.h, GL_RGBA, GL_UNSIGNED_BYTE, image.data) != 0)
	{
		CHECK_GL_ERRORS;
		return false;
	}
#endif

	if ( render_error_description(0) )
		return false;

	*t = (LPTEXTURE) texdata;

	DebugPrintf( "Created texture: file=%s, width=%d, height=%d, colorkey=%s\n", 
		image.path, image.w, image.h, (image.colorkey ? "true" : "false") );

	destroy_image( &image );

	return true;
}

bool update_texture_from_file(LPTEXTURE dstTexture, const char *fileName, u_int16_t *width, u_int16_t *height, int numMips, bool * colorkey)
{
	create_texture(&dstTexture, fileName, width, height, numMips, colorkey);
	return true;
}

bool FSCreateTexture(LPTEXTURE *texture, const char *fileName, u_int16_t *width, u_int16_t *height, int numMips, bool * colourkey)
{	
	return create_texture(texture, fileName, width, height, numMips, colourkey);
}

static void print_info( void )
{
	GLboolean b;
	glGetBooleanv(GL_STEREO,&b);

	DebugPrintf( "gl vendor='%s', renderer='%s', version='%s', shader='%s', stereo='%s'\n",
		glGetString(GL_VENDOR),
		glGetString(GL_RENDERER),
		glGetString(GL_VERSION),
		glGetString(GL_SHADING_LANGUAGE_VERSION),
		(b)?"true":"false");

#if GL < 3
	DebugPrintf( "extensions='%s'\n", glGetString(GL_EXTENSIONS));
#else
  #ifndef MACOSX // TODO - Bug in mac drivers
	int i, max;
	glGetIntegerv(GL_NUM_EXTENSIONS,&max);
	DebugPrintf("render: number of extensions = %d\n",max);
	for(i = 0; i < max; i++ )
	{
		const GLubyte* extension = glGetStringi(GL_EXTENSIONS,i);
		if(!extension)
			DebugPrintf("render: glGetString(GL_EXTENSIONS) returned null for extension %d\n",i);
		else
			DebugPrintf("render: Extension %d = %s\n",i,extension);
	}
  #endif
#endif
}

#if GL > 1

// Fallback shaders that emulate original Forsaken rendering.

// Things a vertex shader must take into account:
// - orthographic a.k.a. pretransformed a.k.a. 2D mode:
//   - "orthographic" uniform var
//   - "ortho_proj" uniform matrix
//   - different vertex layout (LVERTEX vs TLVERTEX)
// - vertex colors are in BGRA format, not RGBA

#if   GL == 2
	#define GLSL_VERSION   "120"
	#define GLSL_VERT_IN   "attribute"
	#define GLSL_VERT_OUT  "varying"
	#define GLSL_FRAG_IN   "varying"
	#define GLSL_FRAG_OUT  ""
#elif GL >= 3
	#define GLSL_VERSION   "150"
	#define GLSL_VERT_IN   "in"
	#define GLSL_FRAG_IN   "in"
	#define GLSL_FRAG_OUT  "out"
	#define GLSL_VERT_OUT  "out"
#endif

static const char *default_vertex_shader =
	"#version " GLSL_VERSION "\n"
	"\n"
	"uniform bool orthographic;\n"
	"\n"
	"uniform mat4 mvp;\n"
	"uniform mat4 ortho_proj;\n"
	"\n"
	GLSL_VERT_IN " vec3 pos;\n"
	GLSL_VERT_IN " vec4 tlpos;\n"
	GLSL_VERT_IN " vec4 vcolor;\n"
	GLSL_VERT_IN " vec2 vtexc;\n"
	"\n"
	GLSL_VERT_OUT " vec4 color;\n"
	GLSL_VERT_OUT " vec2 texc;\n"
	"\n"
	"void main(void)\n"
	"{\n"
	"    if (orthographic)\n"
	"    {\n"
	// tlpos comes straight from TLVERTEX, which is the old Direct3D
	// "pretransformed" vertex: (sx, sy, sz, rhw) in *screen* coordinates,
	// where rhw is the RECIPROCAL homogeneous w, not 1.0. Feeding that
	// vec4 to the projection unchanged (as the original disabled line did)
	// divides everything by rhw and collapses the geometry, which is why
	// this branch was #if 0'd out as "broken in GL 2". The vertices are
	// already transformed, so take xyz and supply w = 1 ourselves.
	"        gl_Position = ortho_proj * vec4(tlpos.xyz, 1.0);\n"
	"    }\n"
	"    else\n"
	"    {\n"
	"        gl_Position = mvp * vec4(pos, 1.0);\n"
	"    }\n"
	"    color = vcolor.bgra;\n"
	"    texc = vtexc;\n"
	"}\n"
;

// Things a fragment shader must take into account:
// - color-keying (discard if alpha <= 100/255; don't ask)
//   - "colorkeying_enabled" uniform var
// - texturing (possibly disabled)
//   - "texturing_enabled" uniform var

static const char *default_fragment_shader =
	"#version " GLSL_VERSION "\n"
	"\n"
	"uniform bool colorkeying_enabled;\n"
	"uniform bool texturing_enabled;\n"
	"uniform sampler2D tex;\n"
	"\n"
	GLSL_FRAG_IN " vec4 color;\n"
	GLSL_FRAG_IN " vec2 texc;\n"
	"\n"
	GLSL_FRAG_OUT " vec4 fcolor;\n"
	"\n"
	"void main(void)\n"
	"{\n"
	"    vec2 dx;\n"
	"    vec2 dy;\n"
	"\n"
#if GL >= 3
	"    // This *may* be needed - see:\n"
	"    // http://www.opengl.org/wiki/GLSL_Sampler\n"
	"    // but I'm not sure because it's not really clear\n"
	"    // on just how 'uniform' the control flow has to be\n"
	"    dx = dFdx(texc);\n"
	"    dy = dFdy(texc);\n"
#endif
	"    if ( texturing_enabled )\n"
#if GL >= 3
	"        fcolor = textureGrad(tex, texc, dx, dy) * color;\n"
#else
	"        fcolor = texture2D(tex, vec2(texc.s,texc.t)) * color;\n"
#endif
	"    else\n"
	"        fcolor = color;\n"
	"    if ( colorkeying_enabled && fcolor.a <= (100.0/255.0) )\n"
	"        discard;\n"
#if GL < 3
        "    gl_FragColor = fcolor;\n"
#endif
	"}\n"
;

GLuint vertex_shader = 0;
GLuint fragment_shader = 0;
GLuint current_program = 0;

static GLuint new_shader( GLenum type, const char *src, char **log )
{
	GLuint shader;
	int compile_ok;
	GLsizei log_size;
	static char *info_log = NULL;

	if ( info_log )
		free( info_log );

	shader = glCreateShader( type );
	glShaderSource( shader, 1, &src, NULL );
	glCompileShader( shader );
	glGetShaderiv( shader, GL_COMPILE_STATUS, &compile_ok );
	if ( compile_ok == GL_FALSE )
	{
		if ( log )
		{
			glGetShaderiv( shader, GL_INFO_LOG_LENGTH, &log_size );
			info_log = malloc( log_size );
			glGetShaderInfoLog( shader, log_size, NULL, info_log );
			*log = info_log;
			return 0;
		}
	}

	CHECK_GL_ERRORS;

	if ( log )
		*log = NULL;

	return shader;
}

static bool update_shader_program( char **log )
{
	int link_ok;
	GLsizei log_size;
	static char *info_log = NULL;

	if ( info_log )
		free( info_log );

	if ( current_program )
	{
		glUseProgram( 0 );
		glDeleteProgram( current_program );
	}

	current_program = glCreateProgram();
	if(!current_program)
	{
		Msg("Failed to create shader program\n");
		return false;
	}

	glAttachShader( current_program, vertex_shader );
	glAttachShader( current_program, fragment_shader );
	glLinkProgram( current_program );
	glGetProgramiv( current_program, GL_LINK_STATUS, &link_ok );
	if ( link_ok == GL_FALSE )
	{
		if ( log )
		{
			glGetProgramiv( current_program, GL_INFO_LOG_LENGTH, &log_size );
			info_log = malloc( log_size );
			glGetProgramInfoLog( current_program, log_size, NULL, info_log );
			*log = info_log;
			return false;
		}
	}

	glUseProgram( current_program );
	CHECK_GL_ERRORS;

	if ( log )
		*log = NULL;

	return true;
}

static bool set_default_shaders( void )
{
	char *info_log;

	// Clean up the existing shaders if any
	if ( vertex_shader )
	{
		glDeleteShader( vertex_shader );
		CHECK_GL_ERRORS;
		vertex_shader = 0;
	}
	if ( fragment_shader )
	{
		glDeleteShader( fragment_shader );
		CHECK_GL_ERRORS;
		fragment_shader = 0;
	}

	DebugPrintf("Using default vertex shader:\n\n%s\n",default_vertex_shader);
	vertex_shader = new_shader( GL_VERTEX_SHADER, default_vertex_shader, &info_log );
	if ( ! vertex_shader )
	{
		DebugPrintf( "Failed to compile vertex shader! Info log:\n-----\n%s\n-----\n", info_log );
		return false;
	}
	DebugPrintf("Using default fragment shader:\n\n%s\n",default_fragment_shader);
	fragment_shader = new_shader( GL_FRAGMENT_SHADER, default_fragment_shader, &info_log );
	if ( ! fragment_shader )
	{
		DebugPrintf( "Failed to compile fragment shader! Info log:\n-----\n%s\n-----\n", info_log );
		return false;
	}
	if ( update_shader_program( &info_log ) == false )
	{
		DebugPrintf( "Failed to build shader program! Info log:\n-----\n%s\n-----\n", info_log );
		return false;
	}

	return true;
}

#endif // GL > 1

static bool set_defaults( void )
{
	build_gamma_table(1.0f); // 1.0f means no gamma change
#if GL == 1
	glShadeModel(GL_SMOOTH); // TODO - is there gouraud ?
	glDisable(GL_LIGHTING); // we light our own verts
	// TODO invalid enumerant ?
	//glPolygonMode(GL_BACK, GL_NONE); // don't draw back faces
	//CHECK_GL_ERRORS;
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
#else
	if(!set_default_shaders()) return false;
#endif
	reset_cull();
	reset_trans();
	set_normal_states(); // default preset render mode
	CHECK_GL_ERRORS;
	return true;
}

// Physical render surface. Everything that talks to glViewport or flips a Y
// coordinate has to use these rather than render_info.ThisMode, which is the
// *window*. They are the same in flat mode; under -vr the surface is the eye
// buffer. Falls back to the window until vr_surface_set() has run at startup.

extern bool MenuIsActive( void );

static int surface_w( void )
{
	return vr_surface_width  > 0 ? vr_surface_width  : render_info.ThisMode.w;
}

static int surface_h( void )
{
	return vr_surface_height > 0 ? vr_surface_height : render_info.ThisMode.h;
}

static void resize_viewport( int width, int height )
{
	render_viewport_t viewport;
	viewport.X = 0;
	viewport.Y = 0;
	viewport.Width = width;
	viewport.Height = height;
	viewport.MinZ = 0.0f;
	viewport.MaxZ = 1.0f;
	FSSetViewPort(&viewport);
}

// capabilities of the opengl system
gl_caps_t caps;

static void detect_caps( void )
{
	int i, max;

	//check whether anisotropic filtering extension is supported
	caps.anisotropic = 0.0f;

#if GL < 3
	if(strstr((char*)glGetString(GL_EXTENSIONS), "GL_EXT_texture_filter_anisotropic"))
		glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &caps.anisotropic );
#else
  #ifdef MACOSX // TODO - Bug in mac drivers.. hopefully everyone also gets a value of 16f
	// OSX 10.8.2, AMD Radeon HD 6750M, GL 2.1 ATI-1.0.29, shader 1.20
	caps.anisotropic = 16.0f;
  #else
	glGetIntegerv(GL_NUM_EXTENSIONS,&max);
	for(i = 0; i < max; i++ )
	{
		const GLubyte* extension = glGetStringi(GL_EXTENSIONS,i);
		if(extension && strstr(extension, "GL_EXT_texture_filter_anisotropic"))
			glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &caps.anisotropic );
	}
  #endif
#endif

	DebugPrintf("render: anisotropic filtering support = %s\n",
		caps.anisotropic?"true":"false");
}

// Resolves the GL >= 2 entry points. See gl_headers.h for why this is needed
// on Windows and why it is a no-op everywhere else.
int fs_gl_loader_init( void )
{
#ifdef FS_USE_GLEW
	GLenum err;

	// our GL3 path asks for a core profile; glewInit() uses glGetString(
	// GL_EXTENSIONS ) which is removed in core, so it needs this to use
	// glGetStringi instead. harmless on compatibility profiles.
	glewExperimental = GL_TRUE;

	err = glewInit();
	if( err != GLEW_OK )
	{
		Msg( "render: glewInit() failed: %s\n", glewGetErrorString( err ) );
		return 0;
	}

	// glewExperimental provokes a bogus GL_INVALID_ENUM on some drivers;
	// swallow it so CHECK_GL_ERRORS does not report it later as ours.
	glGetError();

	DebugPrintf( "render: GLEW %s initialised\n", glewGetString( GLEW_VERSION ) );
#endif
	return 1;
}

bool render_init( render_info_t * info )
{
	if( !fs_gl_loader_init() )
		return false;

#if defined(GL) && (GL >= 3)
	// A GL 3.2 *core* profile has no usable default vertex array object:
	// VAO 0 is not an object there, so every glVertexAttribPointer() and
	// every draw call raises GL_INVALID_OPERATION. (Compatibility profiles
	// and Mesa are lenient about this, which is why it went unnoticed.)
	// The renderer re-specifies its attribute pointers on every draw and
	// keeps no other array state, so a single VAO bound for the lifetime
	// of the context is all that is required.
	{
		static GLuint global_vao = 0;
		if( !global_vao )
		{
			glGenVertexArrays( 1, &global_vao );
			glBindVertexArray( global_vao );
			DebugPrintf( "render: bound global VAO %u (core profile)\n", global_vao );
		}
	}
#endif

	print_info();
	detect_caps();
	if(!set_defaults()) return false;
	resize_viewport(surface_w(), surface_h());
	if(info->wireframe)
		render_mode_wireframe();
	info->ok_to_render = true;
	return true;
}

void render_cleanup( render_info_t * info )
{
	info->ok_to_render = false;
	// ???
}

bool render_mode_select( render_info_t * info )
{
	// Not allowed in VR. This calls sdl_init_video() again, which creates a
	// SECOND window with a SECOND GL context and leaves the first orphaned.
	// The OpenXR session's graphics binding refers to the original context, so
	// the moment that happens the session is pointing at a dead context: the
	// headset and both windows go black and the game locks up.
	//
	// Blocking it is also the right behaviour rather than just a guard: in VR
	// the render resolution is dictated by the runtime's per-eye buffers, not
	// by a desktop window size, so the video mode menu has nothing useful to
	// say. Resolution scaling for VR belongs in its own option.
	if( vr_enabled )
	{
		DebugPrintf( "render: ignoring video mode change while VR is active\n" );
		return true;
	}

	render_cleanup( info );
	if(!sdl_init_video())
		return false;
	//if(!render_init( info ))
	//	return false;
	return true;
}

// TODO - in d3d9 render_flip would detect a lost device
//		lost as in alt+tab (etc) which caused video memory to dump
//		at this point we should set needs_reset = true

static bool needs_reset = false;

bool render_reset( render_info_t * info )
{
	if(!needs_reset)
		return false;
	if(!render_mode_select( info ))
		return false;
	needs_reset = false;
	return true;
}

void render_set_filter( bool red, bool green, bool blue )
{
	glColorMask(red?1:0, green?1:0, blue?1:0, 1);
}

bool render_flip( render_info_t * info )
{
	sdl_render_present(info);
	CHECK_GL_ERRORS;
	return true;
}

void reset_trans( void )
{
	glDisable(GL_BLEND);
	glBlendFunc(GL_ONE,GL_ZERO); // src, dest
}

void reset_zbuff( void )
{
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE); // depth write
}

void disable_zbuff_write( void )
{
	glDepthMask(GL_FALSE); // depth write
}

void disable_zbuff( void )
{
	glDisable(GL_DEPTH_TEST);
}

void cull_none( void )
{
	glDisable(GL_CULL_FACE);
}

void cull_cw( void )
{
	glCullFace(GL_FRONT); // cw is the front for us
}

void reset_cull( void )
{	
	glEnable(GL_CULL_FACE);
	glFrontFace(GL_CW);
	glCullFace(GL_BACK);
}

void set_normal_states( void )
{
	reset_zbuff();
	reset_trans();
}

static void set_trans_state_9()
{
	glBlendFunc(GL_SRC_ALPHA,GL_ONE); // src, dest
}

void set_alpha_states( void )
{
	disable_zbuff_write();
	glEnable(GL_BLEND);
	set_trans_state_9();
}

/* ordinary "over" blending, for the comfort vignette: set_alpha_states is
   additive, and adding black to a picture changes nothing */
void set_blend_over_states( bool on )
{
	if( on )
	{
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
	}
	else
		glDisable(GL_BLEND);
}

extern float framelag;

// 16.7 ~ 1/85 * 71 / 0.05;
// 85 is my fps, 71 is the framelag multiplier, 0.05 is a suitable
// alpha for that fps.

void set_whiteout_state( void )
{
// was going really slow in gl1 for some reason so using this instead
#if GL == 1
	disable_zbuff_write();
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA,GL_ONE); // src, dest
#else
	// higher = more white; < 1.0 makes it darker
	float whiteness = 5.0f;

	float src_a = framelag / 16.7f;
	float dst_a = src_a / whiteness;

	glEnable(GL_BLEND);
	glBlendFunc(GL_CONSTANT_ALPHA,GL_ONE_MINUS_CONSTANT_COLOR); // src, dest
	glBlendColor(dst_a, dst_a, dst_a, src_a); // src, dest
#endif // GL == 1
}

// TODO - is the stencil buffer ever cleared ?
// TODO - do we even use the stencil buffer ?
// TODO - FSClear is meant to clear current viewport
//        perhaps we can automate and remove need for rect arg ?

// clears color/zbuff same time to opaque black
bool FSClear(XYRECT * rect)
{
	int width = rect->x2 - rect->x1;
	int height = rect->y2 - rect->y1;
	int x = rect->x1;
	int y = surface_h() - rect->y1 - height;
	// here we employ a stencil buffer so that we
	// only clear the desired part of the screen
	glEnable(GL_SCISSOR_TEST);
	glScissor(x, y, width, height);
	//
	glClearDepth(1.0f);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
	//
	glDisable(GL_SCISSOR_TEST);
	return true;
}

bool FSClearBlack(void)
{
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	return true;
}

bool FSClearDepth(XYRECT * rect)
{
	glClearDepth(1.0f);
	glClear(GL_DEPTH_BUFFER_BIT);
	return true;
}

bool FSGetViewPort(render_viewport_t *view)
{
	GLint i[4];
	GLfloat f[2];
	// scalex/y are not modified here
	// xywh
	glGetIntegerv( GL_VIEWPORT, i );
	view->X	= i[0];
	view->Y	= surface_h() - (i[1] + i[3]);
	view->Width	= i[2];
	view->Height = i[3];
	// near,far
	glGetFloatv( GL_DEPTH_RANGE, f );
	view->MinZ = f[0];
	view->MaxZ = f[1];
	return true;
}

// The orthographic projection matrix depends on the viewport
// dimensions. We don't update it right away but mark it `dirty'
// and have the drawing routine update it when it's needed.

static bool ortho_matrix_needs_update = true;

/* Where the ship's forward axis sits in NDC this eye, or 0,0 for "don't move
   the 2D layer". Set per eye by the VR render loop; see vr_hud_anchor. */
float vr_2d_off_x = 0.0f;
float vr_2d_off_y = 0.0f;

/* True only while the VR per-eye loop is drawing an eye. That is the only time
   2D content is SEEN at the render target's aspect. Everything else drawn into
   the target in VR (menus, the mono fallback, the intro) is an intermediate
   that submit_panel() blits onto a 4:3 quad, and the blit itself reshapes it.
   Correcting the aspect there as well squashed every panel by 0.706: measured,
   the pause menu spanned 202 px of 600 on the panel against 290 flat. */
bool  vr_2d_eye_pass = false;

/* The HUD screen (oct2.c): the HUD's box, in the logical 2D space, stretched
   to fill the whole target, so the screen holds just the HUD at full
   resolution. Off for everything else. */
bool  vr_2d_crop = false;
float vr_2d_crop_box[4];   /* x0, y0, x1, y1 */

bool FSSetViewPort(render_viewport_t *view)
{
	// render_viewport_t x/y starts top/left
	// but glViewport starts from bottom/left
	int bottom = surface_h() - (view->Y + view->Height);
	glViewport(	view->X, bottom, (GLint) view->Width, (GLint) view->Height	);
	// sets the min/max depth values to render
	// default is max 1.0f and min 0.0f
	// this is here for compatibility with d3d9
	glDepthRange(view->MinZ,view->MaxZ);
	// i want to know if this is ever changed
	// as most likely we don't need this info in render_viewport_t
	if(view->MaxZ!=1.0f || view->MinZ!=0.0f)
	{
		DebugPrintf("-------------------------------\n");
		DebugPrintf("max/min z used: max=%d min=%d\n",
			view->MaxZ, view->MinZ);
		DebugPrintf("-------------------------------\n");
	}
	// ScaleX|Y are not even part of the d3d9 struct anymore
	// they were part of the old d3d6 viewport settings
	// from testing d3d9 passes the values along untouched
	// probably need some testing here to see how d3d6 worked
	// forsaken still uses these values so we need them

	ortho_matrix_needs_update = true; // TODO could enforce this in GL1 too ?
	return true;
}

#if GL > 1
void ortho_update ( GLuint current_program )
{
	MATRIX m;
	// NOTE: 'near' and 'far' are legacy macros in windows' windef.h, so the
	// clip plane locals have to be spelled differently to build on mingw.
	float left, right, bottom, top, z_near, z_far;
	// must be signed: glGetUniformLocation returns -1 when the uniform is
	// absent (or optimised out). As a GLuint that becomes 0xFFFFFFFF and the
	// ">= 0" test below is always true, so we would happily hand an invalid
	// location to glUniformMatrix4fv.
	GLint u_ortho_matrix;

	/* The cached matrix was fine when it only depended on the viewport. The
	   cockpit offset changes every frame and every eye, so a cache that only
	   refreshes on a viewport change would freeze the HUD at whatever the head
	   was doing on the first frame, and look exactly like the anchoring not
	   working. */
	{
		/* The cached matrix bakes in the eye-pass flag and the cockpit offset.
		   Rebuild whenever either differs from what was baked. Testing only
		   "offset is non-zero" missed the return to zero after the eye pass,
		   so the next 2D draw (a menu, the panel) reused a matrix that
		   still carried the last eye's cockpit shift. */
		static bool  baked_eye = false, baked_crop = false;
		static float baked_x = 0.0f, baked_y = 0.0f;
		if ( baked_eye != vr_2d_eye_pass || baked_x != vr_2d_off_x || baked_y != vr_2d_off_y ||
		     baked_crop != vr_2d_crop )
		{
			ortho_matrix_needs_update = true;
			baked_eye  = vr_2d_eye_pass;
			baked_x    = vr_2d_off_x;
			baked_y    = vr_2d_off_y;
			baked_crop = vr_2d_crop;
		}
	}

	if ( ortho_matrix_needs_update && ( u_ortho_matrix = glGetUniformLocation( current_program, "ortho_proj" ) ) >= 0 )
	{
		left = 0.0f;
		right = render_info.window_size.cx;  /* logical 2D space, not the surface */
		bottom = 0.0f;
		top = render_info.window_size.cy;    /* HUD keeps its proportions */
		z_near = -1.0f;
		z_far = 1.0f;
		memset(&m, 0, sizeof(MATRIX));
		m._11 = 2.0f/(right-left);
		// vertical flip ...
		m._22 = (2.0f/(top-bottom)) * -1.0f;
		m._33 = -2.0f/(z_far-z_near);
		m._14 = -(right+left)/(right-left);
		// ... and translate to make top left the origin
		m._24 = -(top+bottom)/(top-bottom) + 2.0f;
		m._34 = -(z_far+z_near)/(z_far-z_near);
		m._44 = 1.0f;

		/* Keep the 2D layer's aspect when the surface is not the window.
		   The logical space above is the window's (4:3 at 1600x1200), but in
		   VR the surface is the eye buffer, which is nearly square: 3072x3264
		   on a Quest 3 over VDXR. Mapping one onto the other stretches every
		   2D thing vertically by 1.417: measured, an 8x8 glyph came out 15.4
		   pixels wide and 21.8 tall. Scale the finished NDC so the logical
		   space keeps its shape and sits centred, letterboxed or pillarboxed
		   as needed.

		   In flat mode the surface IS the window, both scales are exactly 1,
		   and this changes nothing, which is what makes it safe to do here,
		   at the one place every 2D primitive already passes through, rather
		   than at sixty call sites. */
		{
			float surf_aspect = (float) surface_w() / (float) surface_h();
			float log_aspect  = (right - left) / (top - bottom);
			float sx = 1.0f, sy = 1.0f;

			if ( !vr_2d_eye_pass )
				;                                  /* seen via the panel: leave it */
			else if ( surf_aspect > log_aspect )
				sx = log_aspect / surf_aspect;     /* pillarbox */
			else if ( surf_aspect < log_aspect )
				sy = surf_aspect / log_aspect;     /* letterbox */

			/* scale about the centre of NDC: ndc' = s * (m*v) */
			m._11 *= sx;  m._14 *= sx;
			m._22 *= sy;  m._24 *= sy;

			/* Cockpit anchoring. The 2D layer is screen space, so by default it
			   rides the head: turn and the HUD comes with you, which is exactly
			   what a HUD painted on a helmet would do and not what one painted
			   on a cockpit does. vr_2d_off_* carries where the ship's forward
			   axis currently sits, in NDC, so adding it here slides the whole
			   2D layer to stay with the ship instead.

			   It is added AFTER the aspect scale, because it is already in
			   final NDC: the game layer computed it by projecting a world
			   point through this eye's own matrices. Zero in flat mode and
			   zero for menus, so both are untouched. */
			m._14 += vr_2d_off_x;
			m._24 += vr_2d_off_y;
		}

		/* The HUD screen: where the box's corners land in NDC, then scaled
		   and moved so they land on the target's corners. Worked from m
		   itself, so the flip and any scale above come along. */
		if ( vr_2d_crop )
		{
			float xa = m._11 * vr_2d_crop_box[0] + m._14, xb = m._11 * vr_2d_crop_box[2] + m._14;
			float ya = m._22 * vr_2d_crop_box[1] + m._24, yb = m._22 * vr_2d_crop_box[3] + m._24;
			float kx = 2.0f / fabsf( xb - xa ), ky = 2.0f / fabsf( yb - ya );
			m._11 *= kx;  m._14 = ( m._14 - ( xa + xb ) * 0.5f ) * kx;
			m._22 *= ky;  m._24 = ( m._24 - ( ya + yb ) * 0.5f ) * ky;
		}

		glUniformMatrix4fv( u_ortho_matrix, 1, GL_TRUE, &m );
		CHECK_GL_ERRORS;
		ortho_matrix_needs_update = false;
	}
}
#endif

MATRIX proj_matrix;
MATRIX view_matrix;
MATRIX world_matrix;

bool mvp_needs_update;

// This kinda sucks: MATRIX and RENDERMATRIX are supposed to be stored
// in row-major order (see include/new3d.h and render.h respectively),
// but the above matrices appear to be delivered to us already in
// column-major order, which is in fact the way gl{Load,Mult}Matrixf
// expect them (see render_opengl.c).
//
// To compensate for this, we need to multiply matrices in *reverse*
// order. The resulting matrix will also be in column-major order,
// hence the transpose parameter should be GL_FALSE.
//
// See also: http://en.wikipedia.org/wiki/Transpose#Properties

#if GL > 1
void mvp_update( GLuint current_program )
{
	MATRIX mvp;
	GLint u_mvp; // signed: see the note in ortho_update()

	if ( mvp_needs_update && ( u_mvp = glGetUniformLocation( current_program, "mvp" ) ) >= 0 )
	{
		MatrixMultiply( &world_matrix, &view_matrix, &mvp );
		MatrixMultiply( &mvp,          &proj_matrix, &mvp );
		glUniformMatrix4fv( u_mvp, 1, GL_FALSE, &mvp );
		CHECK_GL_ERRORS;
		mvp_needs_update = false;
	}
}
#endif

static void reset_modelview( void )
{
#if GL == 1
	MATRIX mv_matrix;
	glMatrixMode(GL_MODELVIEW);
	MatrixMultiply( &world_matrix, &view_matrix, &mv_matrix );
	glLoadMatrixf((GLfloat*)&mv_matrix);
#else
	mvp_needs_update = true;
#endif
}

bool FSSetView( RENDERMATRIX *matrix )
{
	memmove(&view_matrix,&matrix->m,sizeof(view_matrix));//memcpy
	reset_modelview();
	return true;
}

bool FSSetWorld( RENDERMATRIX *matrix )
{	
	memmove(&world_matrix,&matrix->m,sizeof(world_matrix));//memcpy
	reset_modelview();
	return true;
}

bool FSGetWorld(RENDERMATRIX *matrix)
{
	memmove(&matrix->m,&world_matrix,sizeof(matrix->m));//memcpy
	return true;
}

bool FSSetProjection( RENDERMATRIX *matrix )
{
	memmove(&proj_matrix,&matrix->m,sizeof(proj_matrix));//memcpy
#if GL == 1
	glMatrixMode(GL_PROJECTION);
	glLoadMatrixf((GLfloat*)&matrix->m);
#else
	// These matrices are eventually combined (multiplied together) at
	// render time. We update them locally and mark the combined
	// model-view-projection (MVP) matrix as dirty.
	//
	// The drawing routine should call mvp_update which will update the
	// MVP matrix if needed.
	mvp_needs_update = true;
#endif
	return true;
}

//
// These functions map nicely to OpenGL's Vertex Buffer Objects (VBOs).
//

#if GL > 1
LPVERTEXBUFFER _create_buffer( int size, GLenum type, GLenum gettype, GLenum usage )
{
	// Need to bind the new buffer in order to specify its parameters,
	// so temporarily store the old binding in a variable.
	GLuint oldvbo;
	GLuint vbo;

	glGenBuffers( 1, &vbo );
	glGetIntegerv( gettype, &oldvbo );
	glBindBuffer( type, vbo );
	glBufferData( type, size, NULL, usage );
	// Restore old binding
	glBindBuffer( type, oldvbo );

	CHECK_GL_ERRORS;

	return (LPVERTEXBUFFER) vbo;
}
#endif

bool draw_object(RENDEROBJECT *renderObject){return draw_render_object(renderObject,GL_TRIANGLES,false);}
bool draw_2d_object(RENDEROBJECT *renderObject){return draw_render_object(renderObject,GL_TRIANGLES,true);}
bool draw_line_object(RENDEROBJECT *renderObject){return draw_render_object(renderObject,GL_LINES,false);}

#if GL == 1
	#define delete_buffer(b) free( b )
#else
	#define delete_buffer(b) glDeleteBuffers( 1, b )
#endif

void FSReleaseRenderObject(RENDEROBJECT *renderObject)
{
	int i;
	if (renderObject->lpVertexBuffer)
	{
		delete_buffer( &renderObject->lpVertexBuffer );
		renderObject->lpVertexBuffer = NULL;
	}
	if (renderObject->lpNormalBuffer)
	{
		delete_buffer( &renderObject->lpNormalBuffer );
		renderObject->lpNormalBuffer = NULL;
	}
	if (renderObject->lpIndexBuffer)
	{
		delete_buffer( &renderObject->lpIndexBuffer );
		renderObject->lpIndexBuffer = NULL;
	}
	for (i = 0; i < renderObject->numTextureGroups; i++)
	{
		renderObject->textureGroups[i].numVerts = 0;
		renderObject->textureGroups[i].startVert = 0;

		if (renderObject->textureGroups[i].texture)
		{
			// this is just a pointer to Tloadheader
			// we do not need to worry about releasing it
			renderObject->textureGroups[i].texture = NULL;
		}
	}
	renderObject->numTextureGroups = 0;
}

#endif // GL
