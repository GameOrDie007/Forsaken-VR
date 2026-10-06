#!/bin/bash
#
# Builds the one dependency MSYS2 does not package: luasocket.
#
# Everything else (SDL2, lua5.1, libpng, zlib, openal, enet, glew) comes from
# pacman: see BUILD-WINDOWS.md. luasocket is statically linked into
# projectx by lua_common.c (luaopen_socket_core), so it cannot be skipped.
#
# Upstream's libs/src/luasocket-2.0.2.build only handles linux and macosx.
# This is the Windows equivalent: it builds the Winsock backend (wsocket.c)
# instead of usocket.c/unix.c and links ws2_32.
#
# Run from the repo root, with /mingw64/bin on PATH:
#     bash build-luasocket.sh
#
set -e

cd "$(dirname -- "$0")"
ROOT=$PWD
LIBS=$ROOT/libs
SRC=$LIBS/src
BUILD=$LIBS/build
VER=2.0.2

: ${CC:=gcc}
: ${AR:=ar}
: ${RANLIB:=ranlib}

if [ ! -f "$SRC/luasocket-$VER.tar.gz" ]; then
	echo "ERROR: $SRC/luasocket-$VER.tar.gz not found."
	echo "Clone the libs repo first:"
	echo "    git clone --depth 1 https://github.com/ForsakenX/forsaken-libs.git libs"
	exit 1
fi

mkdir -p "$LIBS/lib/pkgconfig" "$LIBS/include" "$BUILD"

if [ -f "$LIBS/lib/liblua-socket.a" ] && [ -f "$LIBS/lib/liblua-mime.a" ]; then
	echo "luasocket already built -- delete libs/lib to force a rebuild"
	exit 0
fi

echo "=== extracting luasocket-$VER ==="
rm -rf "$BUILD/luasocket-$VER"
tar -xzf "$SRC/luasocket-$VER.tar.gz" -C "$BUILD"

cd "$BUILD/luasocket-$VER/src"

LUA_CFLAGS=$(pkg-config --cflags lua5.1 2>/dev/null || pkg-config --cflags lua)

# wsocket.c is the Winsock backend; usocket.c/unix.c are POSIX-only.
SOCKET_SRC="luasocket.c timeout.c buffer.c io.c auxiliar.c options.c inet.c tcp.c udp.c except.c select.c wsocket.c"

echo "=== compiling luasocket (Winsock backend) ==="
for f in $SOCKET_SRC mime.c; do
	echo "  CC $f"
	$CC -c -O2 -fPIC -DLUASOCKET_API= -DMIME_API= $LUA_CFLAGS -o "${f%.c}.o" "$f"
done

echo "=== archiving ==="
SOCKET_OBJ=$(echo $SOCKET_SRC | sed 's/\.c/.o/g')
$AR cr "$LIBS/lib/liblua-socket.a" $SOCKET_OBJ
$RANLIB "$LIBS/lib/liblua-socket.a"
$AR cr "$LIBS/lib/liblua-mime.a" mime.o
$RANLIB "$LIBS/lib/liblua-mime.a"

cp luasocket.h mime.h "$LIBS/include/"

echo "=== writing pkg-config files ==="

# liblua-socket.a / liblua-mime.a are static and call back into lua
# (luaL_optlstring, luaL_openlib, ...), so lua has to appear AFTER them on the
# link line. The Makefile asks pkg-config for lua before lua-socket, which puts
# them in the wrong order, so we repeat lua's libs here as a trailing group.
LUA_LIBS=$(pkg-config --libs lua5.1 2>/dev/null || pkg-config --libs lua)

# The prefix is relative to the repository root, where make runs: the Makefile
# expands pkg-config in backticks, which splits an absolute path that has a
# space in it (C:\Users\Some Name\...) into two arguments.
write_pc() {
	cat > "$LIBS/lib/pkgconfig/$1" <<EOF
prefix=libs
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: LuaSocket
Description: Network libraries for lua (static, Windows build)
Version: $VER
Libs: -L\${libdir} -llua-mime -llua-socket $LUA_LIBS -lws2_32
Cflags: -I\${includedir}
EOF
}
write_pc lua-socket.pc
write_pc lua5.1-socket.pc

echo
echo "Built: luasocket -> $LIBS/lib/liblua-socket.a"
