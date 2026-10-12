# Forsaken VR for Quest and Steam Frame (standalone): the native part.
#
# Builds the libraries the game needs from their release sources, then the
# game itself as libmain.so, the library SDL's Android activity loads.
#
#   EXT_SRC   folder holding SDL2/, openal-soft-1.25.2/, lua-5.1.5/,
#             luasocket-2.0.2/, enet-1.3.18/, libpng-1.6.44/, libogg-1.3.5/,
#             libvorbis-1.3.7/, libtheora-1.2.0/ and openxr/ (the Khronos
#             loader for Android, from its AAR)

cmake_minimum_required(VERSION 3.18)
project(forsaken_vr_android C CXX)

get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
# the libraries' release sources; tools/build-quest.ps1 passes its own
set(EXT_SRC "${REPO}/../../Android/src" CACHE PATH "Third-party release sources")
set(BUILD_SHARED_LIBS OFF)

# --- SDL2 (shared: SDL's Java side loads it by name) ---------------------------
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
add_subdirectory("${EXT_SRC}/SDL2" SDL2)

# --- OpenAL Soft (shared: libopenal.so) ---------------------------------------
set(LIBTYPE SHARED CACHE STRING "" FORCE)
set(ALSOFT_UTILS OFF CACHE BOOL "" FORCE)
set(ALSOFT_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ALSOFT_TESTS OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_CONFIG OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_HRTF_DATA OFF CACHE BOOL "" FORCE)
set(ALSOFT_INSTALL_AMBDEC_PRESETS OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXT_SRC}/openal-soft-1.25.2" openal)

# --- Lua 5.1, static -----------------------------------------------------------
set(LUA "${EXT_SRC}/lua-5.1.5/src")
file(GLOB LUA_SRC "${LUA}/*.c")
list(REMOVE_ITEM LUA_SRC "${LUA}/lua.c" "${LUA}/luac.c" "${LUA}/print.c")
add_library(lua51 STATIC ${LUA_SRC})
target_compile_definitions(lua51 PUBLIC LUA_USE_POSIX LUA_USE_DLOPEN)
# The NDK turns on FORTIFY by default. Lua 5.1 keeps a string's characters
# right after its header and reads them through that header's pointer, which
# FORTIFY takes for a zero-length object and aborts on (strchr in the garbage
# collector). The PC build has no FORTIFY: build Lua, and the game, the same way.
target_compile_options(lua51 PRIVATE -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0)
target_include_directories(lua51 PUBLIC "${LUA}")

# --- LuaSocket 2.0.2, static (the game links luaopen_socket_core) --------------
set(LSOCK "${EXT_SRC}/luasocket-2.0.2/src")
add_library(luasocket STATIC
	${LSOCK}/luasocket.c ${LSOCK}/timeout.c ${LSOCK}/buffer.c ${LSOCK}/io.c
	${LSOCK}/auxiliar.c ${LSOCK}/options.c ${LSOCK}/inet.c ${LSOCK}/tcp.c
	${LSOCK}/udp.c ${LSOCK}/except.c ${LSOCK}/select.c ${LSOCK}/usocket.c
	${LSOCK}/mime.c)
target_compile_definitions(luasocket PRIVATE LUASOCKET_API= MIME_API=)
target_include_directories(luasocket PUBLIC "${LSOCK}")
target_link_libraries(luasocket PUBLIC lua51)

# --- ENet, static ----------------------------------------------------------------
add_subdirectory("${EXT_SRC}/enet-1.3.18" enet)

# --- libpng, static, on the NDK's zlib ------------------------------------------
set(PNG_SHARED OFF CACHE BOOL "" FORCE)
set(PNG_STATIC ON CACHE BOOL "" FORCE)
set(PNG_TESTS OFF CACHE BOOL "" FORCE)
set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
set(PNG_EXECUTABLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
add_subdirectory("${EXT_SRC}/libpng-1.6.44" libpng)

# --- Ogg, Vorbis and Theora (the soundtrack and the intro film), static ---------
set(INSTALL_DOCS OFF CACHE BOOL "" FORCE)
set(INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
add_subdirectory("${EXT_SRC}/libogg-1.3.5" ogg)
# libvorbis finds Ogg as an installed package: hand it this one
set(OGG_INCLUDE_DIR "${EXT_SRC}/libogg-1.3.5/include;${CMAKE_BINARY_DIR}/ogg/include" CACHE PATH "" FORCE)
set(OGG_LIBRARY ogg CACHE STRING "" FORCE)
add_subdirectory("${EXT_SRC}/libvorbis-1.3.7" vorbis)
set(THEORA "${EXT_SRC}/libtheora-1.2.0")
add_library(theoradec STATIC
	${THEORA}/lib/apiwrapper.c ${THEORA}/lib/bitpack.c ${THEORA}/lib/decapiwrapper.c
	${THEORA}/lib/decinfo.c ${THEORA}/lib/decode.c ${THEORA}/lib/dequant.c
	${THEORA}/lib/fragment.c ${THEORA}/lib/huffdec.c ${THEORA}/lib/idct.c
	${THEORA}/lib/info.c ${THEORA}/lib/internal.c ${THEORA}/lib/quant.c
	${THEORA}/lib/state.c)
target_include_directories(theoradec PUBLIC "${THEORA}/include")
target_link_libraries(theoradec PUBLIC ogg)

# --- the Khronos OpenXR loader for Android ------------------------------------------
set(OPENXR "${EXT_SRC}/openxr")
add_library(openxr_loader SHARED IMPORTED)
set_target_properties(openxr_loader PROPERTIES IMPORTED_LOCATION "${OPENXR}/aar/jni/${ANDROID_ABI}/libopenxr_loader.so")

# --- the game ----------------------------------------------------------------------
file(GLOB GAME_SRC "${REPO}/source/*.c")
# Windows-only parts (DirectSound, DirectInput) and the renderers the headset
# does not use (GL 1 fixed function, GL 2, the DevIL texture loader).
foreach(f sound_dsound.c input_dinput.c render_gl1.c render_gl2.c texture_devil.c render_disabled.c)
	list(REMOVE_ITEM GAME_SRC "${REPO}/source/${f}")
endforeach()
add_library(main SHARED ${GAME_SRC})
target_include_directories(main PRIVATE
	"${REPO}/source"
	"${EXT_SRC}/SDL2/include"
	"${EXT_SRC}/openal-soft-1.25.2/include"
	"${EXT_SRC}/openal-soft-1.25.2/include/AL"
	"${EXT_SRC}/enet-1.3.18/include"
	"${OPENXR}/include")
# The flags the PC build gets from its Makefile (SDL=2 GL=3 MINGW=1, debug
# on), plus GLES: OpenGL ES 3 in place of desktop GL 3.
target_compile_definitions(main PRIVATE
	GL=3 GLES VR_OPENXR MUSIC_OGG MOVIE_THEORA
	NET_ENET_2 BSP LUA_USE_APICHECK TEXTURE_PNG SOUND_SUPPORT SOUND_OPENAL
	DEBUG_ON DEBUG_COMP DEBUG_SPOTFX_SOUND DEBUG_VIEWPORT
	XR_USE_PLATFORM_ANDROID XR_USE_GRAPHICS_API_OPENGL_ES)
# The same warnings demoted as on the PC (gcc 14 made them errors; clang too).
target_compile_options(main PRIVATE -std=gnu99 -fcommon -g -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
	-Wno-incompatible-pointer-types -Wno-int-conversion -Wno-implicit-function-declaration
	-Wno-return-type -Wno-int-to-pointer-cast -Wno-pointer-to-int-cast
	-Wno-incompatible-function-pointer-types -Wno-format-security)
target_link_libraries(main SDL2 OpenAL lua51 luasocket enet png_static
	vorbisfile vorbis ogg theoradec openxr_loader GLESv3 EGL android log z m dl)
