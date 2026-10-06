# Building Forsaken (ProjectX) on Windows

Reproducible build steps for the `vr` branch, verified on Windows 11 with
gcc 16.1 / SDL2 2.32.10 on an NVIDIA RTX 4070 Ti SUPER.

Upstream's documented Windows path (`libs/src/build.sh`) compiles a set of
2010-era source tarballs. We don't use it. MSYS2 packages every dependency
except one, so we take those from `pacman` and build only `luasocket` locally.

---

## 1. Install MSYS2

```bash
winget install --id MSYS2.MSYS2 -e
```

Everything below runs in the **MSYS2 shell** (`C:\msys64\usr\bin\bash.exe`),
not cmd or PowerShell.

## 2. Install the toolchain and dependencies

```bash
pacman -S --needed mingw-w64-x86_64-gcc mingw-w64-x86_64-pkgconf make git mingw-w64-x86_64-SDL2 mingw-w64-x86_64-lua51 mingw-w64-x86_64-enet mingw-w64-x86_64-libpng mingw-w64-x86_64-zlib mingw-w64-x86_64-openal mingw-w64-x86_64-glew
```

| Package | Why |
|---|---|
| `SDL2` | window, input, GL context. We build `SDL=2`, not 1.2. |
| `lua51` | the game embeds Lua 5.1 for scripting |
| `enet` | networking (1.3.x; see the compat shim in `net_enet_2.c`) |
| `libpng`, `zlib` | texture loading |
| `openal` | sound |
| `glew` | **Windows only.** See "Why GLEW" below. |

## 3. Get the source, data and libs

```bash
git clone https://github.com/GameOrDie007/Forsaken-VR.git
cd Forsaken-VR
git clone --depth 1 https://github.com/ForsakenX/forsaken-data.git data
git clone --depth 1 https://github.com/ForsakenX/forsaken-libs.git libs
mkdir -p savegame logs configs pilots
```

`data/` is ~309 MB and contains all of the game's assets (levels, models,
textures, sound). You do **not** need an original retail Forsaken install:
the ForsakenX data repo is self-contained.

## 4. Build luasocket

MSYS2 has no `luasocket` package, and it cannot be skipped: `lua_common.c`
statically links `luaopen_socket_core`. Upstream's build script only handles
Linux and macOS, so this repo adds a Windows equivalent:

```bash
bash build-luasocket.sh
```

It compiles the Winsock backend (`wsocket.c`) into `libs/lib/liblua-socket.a`
and writes matching pkg-config files. Delete `libs/lib` to force a rebuild.

## 5. Build the game

```bash
export PATH=/mingw64/bin:$PATH
export PKG_CONFIG_PATH=$PWD/libs/lib/pkgconfig:$PKG_CONFIG_PATH
make SDL=2 GL=3 MINGW=1 -j8
```

Produces `projectx.exe` (~10 MB) in the top folder; the code it builds from
is in `source/`. Or just run `build-windows.bat` from Explorer, which does all
of the above.

## 6. Run

```bash
export PATH=/mingw64/bin:$PATH
./projectx.exe -window -debug -log
```

`/mingw64/bin` **must** be on `PATH`: that is where the SDL2, Lua, OpenAL,
GLEW, libpng and zlib DLLs live. `run-windows.bat` sets this for you.

### Useful command line flags

| Flag | Effect |
|---|---|
| `-window` | windowed instead of fullscreen |
| `-fullscreen` | fullscreen |
| `-debug` | enable debug logging |
| `-log` | write that log to `logs/<date>.txt` |
| `-vsync` | enable vsync |
| `-forceaccel` | force an accelerated GL visual |

**Note on logging:** SDL2's pkg-config adds `-mwindows`, so `projectx.exe` is
a GUI-subsystem binary and prints nothing to a console. On Windows
`DebugPuts()` calls `OutputDebugString()`. **Always pass `-debug -log`** and
read `logs/<date>.txt`: that is the only way to see what the game is doing.

---

## Changes required to build on modern Windows

All of these are on the `vr` branch. None change gameplay.

### Why GLEW (`gl_headers.h`, `render_gl_shared.c`, `Makefile`)

`opengl32.dll` only ever exports OpenGL 1.1. Every entry point above that
(shaders, VBOs, FBOs, i.e. all of the GL2/GL3 render paths) must be resolved
at runtime via `wglGetProcAddress`. On Linux and macOS `libGL` exports them
directly, which is why upstream links fine there and fails here with a wall of
`undefined reference to glCreateShader`.

`gl_headers.h` centralises the include order (GLEW must precede any `gl.h`)
and `fs_gl_loader_init()` calls `glewInit()` at the top of `render_init()`.
On non-Windows builds it compiles to a no-op.

### Explicit GL context (`main_sdl.c`, `render.h`)

The SDL2 path called `SDL_CreateRenderer()`, SDL's **2D** drawing API. It
does create a GL context internally but never hands it back and does not
guarantee it is current on our thread. The result was
`glewInit() failed: Missing GL version` and an immediate exit.

Replaced with an explicit `SDL_GL_CreateContext()` +
`SDL_GL_MakeCurrent()`, presenting via `SDL_GL_SwapWindow()` instead of
`SDL_RenderPresent()`, with vsync moved to `SDL_GL_SetSwapInterval()`.
`render_info.renderer` became `render_info.gl_context`.

This was going to be necessary anyway: OpenXR's OpenGL graphics binding needs
a context handle, which the SDL_Renderer path could never provide.

Also added `SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24)`, which was never set:
the depth buffer size was whatever the driver defaulted to.

### Global VAO for core profile (`render_gl_shared.c`)

`GL=3` asks for a 3.2 **core** profile. In core profile the default vertex
array object (VAO 0) is not a valid object, so every `glVertexAttribPointer()`
and every draw call raises `GL_INVALID_OPERATION`. Compatibility profiles and
Mesa are lenient about this, which is presumably why it went unnoticed.

The renderer re-specifies its attribute pointers on every draw, so one VAO
generated and bound at init is sufficient. Without it the game ran but logged
two GL errors per draw call.

### SDL2 input path (`input_sdl.c`)

The SDL2 branch had bit-rotted and did not compile:

- the event dispatch passed `&_event` to handlers taking `SDL_KeyboardEvent*`
  etc., on the mistaken comment that "the newer sdl now uses a union instead
  of sub structures". SDL2 kept the sub-structs; both versions name them
  identically, so the duplicated SDL1/SDL2 dispatch collapsed into one.
- `keysym.unicode` does not exist in SDL2 (text arrives via `SDL_TEXTINPUT`).
- `SDL_JoystickName(int)` became `SDL_JoystickNameForIndex(int)` in SDL2;
  wrapped in `joystick_name_for_index()`.
- added `SDL_TEXTINPUT`/`SDL_TEXTEDITING` cases so they stop hitting the
  "Unknown event type" default.

### enet 1.2 → 1.3 (`net_enet_2.c`)

enet 1.3 added a `channelLimit` argument to `enet_host_create()` and a user
data argument to `enet_host_connect()`. The file was written against 1.2 (the
version bundled in `libs/src`). Added `FS_HOST_CREATE`/`FS_HOST_CONNECT`
macros that switch on `ENET_VERSION_MAJOR`, so both versions still build.

### Modern gcc warnings-as-errors (`Makefile`)

gcc 14 promoted several long-standing warnings to hard errors. A 1998 codebase
trips them in a few hundred places, nearly all benign: for example
`lua_bullets.c` passes `VERT*`/`NORMAL*` to a `VECTOR*` parameter, and all
three are layout-identical structs of three floats (`new3d.h`).

Demoted back to warnings rather than churning gameplay files:
`-Wno-incompatible-pointer-types -Wno-int-conversion
-Wno-implicit-function-declaration -Wno-return-mismatch`.

Cleaning these up properly is worthwhile but is not this project's job.

### Link fixes (`Makefile`, `build-luasocket.sh`)

- dropped `-lsocket` and `-lOpenAL32` from the MINGW block. Those names came
  from a prebuilt dependency bundle in `./mingw/bin` that no longer exists;
  on MSYS2, sockets are `-lws2_32`/`-lwsock32` (already listed) and OpenAL
  comes from pkg-config as `-lopenal`.
- `liblua-socket.a`/`liblua-mime.a` are static and call back into Lua, so Lua
  must appear *after* them on the link line. The Makefile asks pkg-config for
  Lua first, so `libs/build-windows.sh` repeats Lua's libs as a trailing group
  in the generated `.pc`.

### Miscellaneous (`render_gl_shared.c`)

- `static resize_viewport(...)` had an implicit `int` return type; now `void`.
- the ortho matrix used locals named `near`/`far`, which are legacy macros in
  Windows' `windef.h`; renamed to `z_near`/`z_far`.

---

## Known issues

- **`GL=1` and `GL=2` are untested on Windows.** Only `GL=3` has been run.
  The GLEW wiring is active for `GL != 1`.
- **`SDL=1` (SDL 1.2) is untested** and not planned: the VR work requires
  SDL2.
- The build is a debug build with profiling on (`DEBUG=1 PROFILE=1` are the
  Makefile defaults, and upstream ships debug builds as releases). Performance
  numbers taken now are not representative.
