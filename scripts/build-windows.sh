#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Builds the NoisyFairy Windows port from source on a Linux machine (cross-compile with
# clang + MinGW-w64). Fetches and builds every dependency at a pinned version, then builds
# the game library and the four executables.
#
#   scripts/build-windows.sh            build into ./build-win
#   WORK=/some/dir scripts/build-windows.sh
#   JOBS=4 scripts/build-windows.sh     limit parallel jobs
#   DIAGNOSTICS=1 scripts/build-windows.sh   enable PS4RT_DIAGNOSTICS
#
# Result: $WORK/package/NoisyFairy  (a ready-to-run folder, same layout as the release zip)
# See BUILDING.md for the prerequisites and the full explanation.
set -euo pipefail

ROOT=$(cd -- "$(dirname -- "$0")/.." && pwd)
WORK=${WORK:-$ROOT/build-win}
JOBS=${JOBS:-$(nproc)}
DIAG=${DIAGNOSTICS:-0}   # DIAGNOSTICS=1 enables upstream's PS4RT_DIAGNOSTICS tooling (delete obj/gpu.done after changing it)
PREFIX=$WORK/prefix
SRC=$WORK/src
BLD=$WORK/obj
OUT=$WORK/out
PKG=$WORK/package/NoisyFairy

# Pinned dependency versions.
SDL_TAG=release-3.2.24
FMT_TAG=11.0.2
XXHASH_TAG=v0.8.3
ZLIB_TAG=v1.3.1
ZYDIS_TAG=v4.1.1
CRYPTOPP_TAG=CRYPTOPP_8_9_0
ROBIN_TAG=v1.3.0
VKHEADERS_TAG=v1.4.368
VKLOADER_TAG=v1.3.296
FFMPEG_URL=https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl-shared.zip

step() { printf '\n==== %s\n' "$*"; }
die() { echo "error: $*" >&2; exit 1; }

# ---------------------------------------------------------------- prerequisites
step "Checking build tools"
missing=()
for t in clang clang++ ld.lld cmake ninja pkg-config git curl unzip python3 make \
         glslangValidator x86_64-w64-mingw32-gcc-posix x86_64-w64-mingw32-windres x86_64-w64-mingw32-dlltool; do
    command -v "$t" >/dev/null 2>&1 || missing+=("$t")
done
[ -f /usr/include/boost/version.hpp ] || missing+=("boost headers (libboost-dev)")
[ -f /usr/include/vk_mem_alloc.h ] || missing+=("vk_mem_alloc.h (libvulkan-memory-allocator-dev)")
python3 -c 'import PIL' 2>/dev/null || missing+=("python3-pil")
if [ ${#missing[@]} -gt 0 ]; then
    echo "Missing: ${missing[*]}" >&2
    echo "On Ubuntu/Debian:" >&2
    echo "  sudo apt install clang lld cmake ninja-build pkg-config git curl unzip python3 python3-pil make \\" >&2
    echo "       mingw-w64 gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix \\" >&2
    echo "       libboost-dev libvulkan-memory-allocator-dev glslang-tools" >&2
    exit 1
fi
GCCDIR=$(ls -d /usr/lib/gcc/x86_64-w64-mingw32/*-posix 2>/dev/null | sort -V | tail -1)
[ -n "$GCCDIR" ] || die "MinGW-w64 posix-threads toolchain not found (g++-mingw-w64-x86-64-posix)"

mkdir -p "$WORK" "$PREFIX/include" "$PREFIX/lib" "$SRC" "$BLD" "$OUT" "$WORK/bin"

# ---------------------------------------------------------------- toolchain
step "Writing toolchain wrappers"
cat >"$WORK/bin/clang-mingw" <<EOF
#!/bin/sh
exec clang --target=x86_64-w64-mingw32 --sysroot=/usr/x86_64-w64-mingw32 -B$GCCDIR -L$GCCDIR -fuse-ld=lld -Wno-unused-command-line-argument "\$@"
EOF
cat >"$WORK/bin/clang++-mingw" <<EOF
#!/bin/sh
exec clang++ --target=x86_64-w64-mingw32 --sysroot=/usr/x86_64-w64-mingw32 -B$GCCDIR -L$GCCDIR -fuse-ld=lld -stdlib=libstdc++ -isystem $GCCDIR/include/c++ -isystem $GCCDIR/include/c++/x86_64-w64-mingw32 -Wno-unused-command-line-argument "\$@"
EOF
chmod +x "$WORK/bin/clang-mingw" "$WORK/bin/clang++-mingw"
CC=$WORK/bin/clang-mingw
CXX=$WORK/bin/clang++-mingw

# MinGW headers are lower case; the code includes a few with Windows capitalisation.
mkdir -p "$PREFIX/include-case"
echo '#include <windows.h>' >"$PREFIX/include-case/Windows.h"
echo '#include <shlobj.h>' >"$PREFIX/include-case/Shlobj.h"

cat >"$WORK/toolchain.cmake" <<EOF
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER $CC)
set(CMAKE_CXX_COMPILER $CXX)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_AR /usr/bin/llvm-ar)
set(CMAKE_RANLIB /usr/bin/llvm-ranlib)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32 $PREFIX)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
set(CMAKE_C_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000005 -idirafter $PREFIX/include-case")
set(CMAKE_CXX_FLAGS_INIT "-D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000005 -idirafter $PREFIX/include-case -include string.h -include wchar.h -include stdio.h -femulated-tls")
EOF
TOOLCHAIN=$WORK/toolchain.cmake

# ---------------------------------------------------------------- dependencies
fetch() {  # name url tag [recursive]
    local name=$1 url=$2 tag=$3 rec=${4:-}
    [ -d "$SRC/$name/.git" ] && return 0
    git clone --quiet --depth 1 --branch "$tag" ${rec:+--recursive --shallow-submodules} "$url" "$SRC/$name"
}
cmake_dep() {  # name [src subdir] -- extra cmake args...
    local name=$1 sub=$2; shift 2
    [ -f "$BLD/$name.done" ] && return 0
    step "Building $name"
    cmake -S "$SRC/$name/$sub" -B "$BLD/$name" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_PREFIX_PATH="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF "$@" >"$BLD/$name.log" 2>&1 || { tail -30 "$BLD/$name.log"; die "$name configure failed"; }
    cmake --build "$BLD/$name" -j"$JOBS" >>"$BLD/$name.log" 2>&1 || { tail -30 "$BLD/$name.log"; die "$name build failed"; }
    cmake --install "$BLD/$name" >>"$BLD/$name.log" 2>&1
    touch "$BLD/$name.done"
}

step "Fetching dependency sources"
fetch zlib      https://github.com/madler/zlib.git              "$ZLIB_TAG"
fetch fmt       https://github.com/fmtlib/fmt.git               "$FMT_TAG"
fetch xxhash    https://github.com/Cyan4973/xxHash.git          "$XXHASH_TAG"
fetch zydis     https://github.com/zyantific/zydis.git          "$ZYDIS_TAG" rec
fetch SDL       https://github.com/libsdl-org/SDL.git           "$SDL_TAG"
fetch cryptopp  https://github.com/weidai11/cryptopp.git        "$CRYPTOPP_TAG"
fetch robin-map https://github.com/Tessil/robin-map.git         "$ROBIN_TAG"
fetch vkheaders https://github.com/KhronosGroup/Vulkan-Headers.git "$VKHEADERS_TAG"
fetch vkloader  https://github.com/KhronosGroup/Vulkan-Loader.git "$VKLOADER_TAG"

cmake_dep zlib "" -DZLIB_BUILD_EXAMPLES=OFF
cmake_dep fmt "" -DFMT_TEST=OFF -DFMT_DOC=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
cmake_dep xxhash cmake_unofficial -DXXHASH_BUILD_XXHSUM=OFF
ln -sfn "$SRC/zydis/dependencies/zycore" "$SRC/zycore"
cmake_dep zycore ""
cmake_dep zydis "" -DZYDIS_BUILD_TOOLS=OFF -DZYDIS_BUILD_EXAMPLES=OFF -DZYDIS_BUILD_DOXYGEN=OFF
cmake_dep SDL "" -DBUILD_SHARED_LIBS=ON -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF
cmake_dep robin-map "" -DBUILD_TESTING=OFF
cmake_dep vkheaders ""

# Vulkan: only an import library for the system's vulkan-1.dll is needed.
if [ ! -f "$PREFIX/lib/libvulkan-1.dll.a" ]; then
    step "Creating the Vulkan import library"
    x86_64-w64-mingw32-dlltool -d "$SRC/vkloader/loader/vulkan-1.def" -D vulkan-1.dll -l "$PREFIX/lib/libvulkan-1.dll.a"
fi

# Crypto++ has no CMake build; the same defines must be used here and when compiling the PKG tool.
CRYPTOPP_DEFS="-DCRYPTOPP_DISABLE_ASM -DCRYPTOPP_DISABLE_SSE4 -DCRYPTOPP_DISABLE_AESNI -DCRYPTOPP_DISABLE_SHA"
if [ ! -f "$BLD/cryptopp.done" ]; then
    step "Building Crypto++"
    (cd "$SRC/cryptopp" && make -j"$JOBS" -f GNUmakefile libcryptopp.a CXX="$CXX" AR=llvm-ar RANLIB=llvm-ranlib \
        CXXFLAGS="-O2 -std=c++17 $CRYPTOPP_DEFS -DNDEBUG -DCRYPTOPP_DISABLE_ASM" IS_MINGW=1 \
        >"$BLD/cryptopp.log" 2>&1) || { tail -30 "$BLD/cryptopp.log"; die "Crypto++ build failed"; }
    "$CC" -O2 -c "$ROOT/scripts/cryptopp_x64_shim.c" -o "$SRC/cryptopp/x64_shim.o"
    llvm-ar rcs "$SRC/cryptopp/libcryptopp.a" "$SRC/cryptopp/x64_shim.o"
    mkdir -p "$PREFIX/include/cryptopp"
    cp "$SRC/cryptopp"/*.h "$PREFIX/include/cryptopp/"
    cp "$SRC/cryptopp/libcryptopp.a" "$PREFIX/lib/"
    touch "$BLD/cryptopp.done"
fi

# Header-only: Boost and the Vulkan Memory Allocator come from the distribution.
[ -e "$PREFIX/include/boost" ] || ln -s /usr/include/boost "$PREFIX/include/boost"
mkdir -p "$PREFIX/include/vma"; cp /usr/include/vk_mem_alloc.h "$PREFIX/include/vma/"

# FFmpeg (LGPL shared build): headers, import libraries and the DLLs that ship with the game.
if [ ! -d "$WORK/ffmpeg" ]; then
    step "Downloading FFmpeg"
    curl -fsSL "$FFMPEG_URL" -o "$WORK/ffmpeg.zip"
    unzip -q "$WORK/ffmpeg.zip" -d "$WORK/ffmpeg.tmp"
    mv "$WORK/ffmpeg.tmp"/* "$WORK/ffmpeg"; rmdir "$WORK/ffmpeg.tmp"
fi
FF=$WORK/ffmpeg
for dll in avcodec-63 avformat-63 avutil-61 swresample-7 swscale-10; do
    [ -f "$FF/bin/$dll.dll" ] || die "FFmpeg build has different DLL versions (expected $dll.dll); see BUILDING.md"
done
cp -r "$FF"/include/* "$PREFIX/include/"
cp "$FF"/lib/*.dll.a "$PREFIX/lib/"
mkdir -p "$PREFIX/lib/pkgconfig"
cp "$FF"/lib/pkgconfig/*.pc "$PREFIX/lib/pkgconfig/"
for pc in "$PREFIX"/lib/pkgconfig/libav*.pc; do sed -i "s|^prefix=.*|prefix=$PREFIX|" "$pc"; done
# libswscale/libswresample .pc files are not shipped by the build; write them.
for lib in swscale swresample; do
    [ -f "$PREFIX/lib/pkgconfig/lib$lib.pc" ] || cat >"$PREFIX/lib/pkgconfig/lib$lib.pc" <<EOF
prefix=$PREFIX
libdir=\${prefix}/lib
includedir=\${prefix}/include
Name: lib$lib
Description: FFmpeg $lib
Version: 1.0
Libs: -L\${libdir} -l$lib
Cflags: -I\${includedir}
EOF
done
# libxxhash / fmt / sdl3 pkg-config files are generated by their installs; make prefixes relative to ours.
export PKG_CONFIG_LIBDIR=$PREFIX/lib/pkgconfig:$PREFIX/share/pkgconfig
export PKG_CONFIG_SYSROOT_DIR=

# ---------------------------------------------------------------- game library (gpu/)
if [ ! -f "$BLD/gpu.done" ]; then
    step "Building the game library (this is the long step)"
    cmake -S "$ROOT/gpu" -B "$BLD/gpu" -G Ninja -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$PREFIX" -DPS4RT_DIAGNOSTICS=$([ "$DIAG" = 1 ] && echo ON || echo OFF) -DPS4RT_LTO=OFF -DPS4RT_PGO=off \
        -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF >"$BLD/gpu.log" 2>&1 || { tail -40 "$BLD/gpu.log"; die "game library configure failed"; }
    cmake --build "$BLD/gpu" -j"$JOBS" --target ps4rtgpu >>"$BLD/gpu.log" 2>&1 || { tail -40 "$BLD/gpu.log"; die "game library build failed"; }
    touch "$BLD/gpu.done"
fi

# ---------------------------------------------------------------- icon resource
step "Compiling the icon resource"
( cd "$ROOT/res" && x86_64-w64-mingw32-windres noisyfairy.rc -O coff -o "$BLD/noisyfairy_res.o" )

# ---------------------------------------------------------------- ps4rt.exe
step "Building ps4rt.exe"
mkdir -p "$BLD/atrac9"
for f in "$ROOT"/third_party/LibAtrac9/C/src/*.c; do
    "$CC" -std=c99 -O2 -w -c "$f" -o "$BLD/atrac9/$(basename "${f%.c}").o"
done
llvm-ar rcs "$BLD/libatrac9.a" "$BLD"/atrac9/*.o
"$CC" -std=c11 -O2 -Wall -Wextra -Werror -mgeneral-regs-only -c "$ROOT/src/isa_emulate.c" -o "$BLD/isa_emulate.o"
( cd "$ROOT" && "$CC" -std=gnu11 -O2 -femulated-tls -pthread -D_WIN32_WINNT=0x0A00 -DNTDDI_VERSION=0x0A000005 \
    -DPS4RT_RUNTIME -DPS4RT_DIAGNOSTICS=$DIAG -I"$PREFIX/include" -I. -Isrc \
    src/loader.c src/runtime*.c src/compat_win.c src/cpu_check.c "$BLD/isa_emulate.o" src/isa_rewrite.c "$BLD/libatrac9.a" -lm \
    -Wl,--start-group "$BLD/gpu/libps4rtgpu.a" "$BLD/gpu/third_party/sirit/src/libsirit.a" \
    "$BLD/gpu/third_party/miniz/libminiz.a" "$BLD"/gpu/third_party/fsr-vulkan/*.a \
    -lfmt -lxxhash -lZydis -lZycore -lzlibstatic -Wl,--end-group \
    -L"$PREFIX/lib" -lSDL3 -mconsole -Wl,--disable-high-entropy-va \
    -lavcodec -lavutil -lswscale -lavformat -lswresample -lvulkan-1 \
    -L"$GCCDIR" -lgcc_s -lstdc++ -lws2_32 -lbcrypt -lntdll -lsynchronization -lwinmm -lpdh -lole32 -lshell32 \
    -ladvapi32 -lgdi32 -lsetupapi -limm32 -lversion -loleaut32 -luuid \
    "$BLD/noisyfairy_res.o" -o "$OUT/ps4rt.exe" )

# ---------------------------------------------------------------- launcher, setup and PKG tool
IMGUI=$ROOT/gpu/third_party/imgui
IMFLAGS=(-std=c++20 -O2 -D_WIN32_WINNT=0x0A00 -I"$PREFIX/include" -I"$IMGUI" -I"$IMGUI/backends" -I"$ROOT/gpu/shim" -I"$ROOT/gpu/shim/imgui")
mkdir -p "$BLD/ui"
imgui_objs=()
for f in "$IMGUI"/imgui.cpp "$IMGUI"/imgui_draw.cpp "$IMGUI"/imgui_tables.cpp "$IMGUI"/imgui_widgets.cpp \
         "$IMGUI"/backends/imgui_impl_sdl3.cpp "$IMGUI"/backends/imgui_impl_sdlrenderer3.cpp "$ROOT"/gpu/shim/ps4rt_png.cpp; do
    o=$BLD/ui/$(basename "${f%.cpp}").o
    [ -f "$o" ] || "$CXX" "${IMFLAGS[@]}" -c "$f" -o "$o"
    imgui_objs+=("$o")
done
step "Building ps4rt-launcher.exe"
"$CXX" "${IMFLAGS[@]}" -c "$ROOT/scripts/ps4rt_launcher_native.cpp" -o "$BLD/ui/launcher.o"
"$CXX" "$BLD/ui/launcher.o" "${imgui_objs[@]}" -L"$PREFIX/lib" -lSDL3 -lavcodec -lavutil -lswscale -lzlibstatic -pthread \
    -mwindows -static-libgcc -static-libstdc++ -lshell32 -lole32 -lgdi32 -lwinmm "$BLD/noisyfairy_res.o" -o "$OUT/ps4rt-launcher.exe"
step "Building ps4rt-setup.exe"
"$CXX" "${IMFLAGS[@]}" -c "$ROOT/scripts/ps4rt_setup_native.cpp" -o "$BLD/ui/setup.o"
"$CXX" "$BLD/ui/setup.o" "${imgui_objs[@]}" -L"$PREFIX/lib" -lSDL3 -lavcodec -lavutil -lswscale -pthread \
    -mwindows -static-libgcc -static-libstdc++ -lshell32 -lole32 -lgdi32 -lwinmm "$BLD/noisyfairy_res.o" -o "$OUT/ps4rt-setup.exe"
step "Building ps4rt-pkg.exe"
( cd "$ROOT" && "$CXX" -std=c++23 -O2 -femulated-tls -pthread -D_WIN32_WINNT=0x0A00 $CRYPTOPP_DEFS \
    -I"$PREFIX/include" -Ithird_party/ps4pkg \
    scripts/ps4rt_pkg_native.cpp third_party/ps4pkg/core/file_format/pkg.cpp \
    third_party/ps4pkg/core/file_format/pkg_type.cpp third_party/ps4pkg/core/crypto/crypto.cpp \
    -L"$PREFIX/lib" -lcryptopp -lzlibstatic -ladvapi32 "$BLD/noisyfairy_res.o" -o "$OUT/ps4rt-pkg.exe" )

# ---------------------------------------------------------------- package folder
step "Assembling $PKG"
rm -rf "$PKG"; mkdir -p "$PKG/bin"
cp "$OUT"/*.exe "$PKG/bin/"
cp "$FF"/bin/{avcodec-63,avformat-63,avutil-61,swresample-7,swscale-10}.dll "$PKG/bin/"
cp "$PREFIX/bin/SDL3.dll" "$PKG/bin/"
for dll in libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
    p=$(find "$GCCDIR" /usr/x86_64-w64-mingw32/lib -name "$dll" 2>/dev/null | head -1)
    [ -n "$p" ] && cp "$p" "$PKG/bin/" || echo "warning: $dll not found; copy it from your MinGW-w64 install" >&2
done
# Embedded Python for the launcher's helper scripts (official python.org embeddable build).
PYZIP=$WORK/python-embed.zip
if [ ! -f "$PYZIP" ]; then
    curl -fsSL https://www.python.org/ftp/python/3.12.8/python-3.12.8-embed-amd64.zip -o "$PYZIP" \
        || { rm -f "$PYZIP"; echo "warning: could not download embedded Python; extract python-3.12.8-embed-amd64.zip into $PKG/python yourself" >&2; }
fi
if [ -f "$PYZIP" ]; then
    unzip -q -o "$PYZIP" -d "$PKG/python"
    # Without the ._pth file Python also searches the script's own folder, which the launcher scripts rely on.
    rm -f "$PKG"/python/python*._pth
fi
for item in LICENSE README.md WINDOWS-PORT.md BUILDING.md NoisyFairy.bat Play-Game.bat port_features.json \
            docs fsr4_shaders launcher; do
    cp -r "$ROOT/$item" "$PKG/"
done
mkdir -p "$PKG/scripts" "$PKG/gpu"
cp "$ROOT"/scripts/*.py "$PKG/scripts/"
cp -r "$ROOT/gpu/assets" "$PKG/gpu/"
echo
echo "Done. Executables: $OUT"
echo "Ready-to-run folder: $PKG"
