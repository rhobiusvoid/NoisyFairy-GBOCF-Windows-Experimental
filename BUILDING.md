# Building NoisyFairy from source

You do not have to trust the executables from a release: they are built from the source in this repository by the script described here, and you can build them yourself.

The Windows binaries are **cross-compiled on Linux** (clang + MinGW-w64). The script does the whole
job: it downloads and builds every dependency at a pinned version, builds the game library and the
four executables, and assembles a ready-to-run folder.

## What you need

A 64-bit Linux machine (tested on Ubuntu 24.04), about 10 GB of free disk and an internet
connection. On Windows, use WSL2 with an Ubuntu 24.04 distribution; nothing else is required.

```sh
sudo apt install clang lld cmake ninja-build pkg-config git curl unzip make \
     python3 python3-pil mingw-w64 gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix \
     libboost-dev libvulkan-memory-allocator-dev glslang-tools
```

## Build

```sh
git clone <this repository>
cd <this repository>
scripts/build-windows.sh
```

The first run takes a while (the game library alone is about 1,500 source files). It can be
resumed: finished steps are skipped. Options:

| Variable | Meaning |
|---|---|
| `WORK=/path` | Where dependencies, objects and output go (default `./build-win`) |
| `JOBS=4` | Limit parallel compile jobs (default: all cores) |
| `DIAGNOSTICS=1` | Enable upstream's `PS4RT_DIAGNOSTICS` tooling (delete `obj/gpu.done` first). The hang dump, stick sweep and warm-up switches are always built in the experimental repository |

Output:

| Path | Contents |
|---|---|
| `build-win/out/` | `ps4rt.exe`, `ps4rt-launcher.exe`, `ps4rt-setup.exe`, `ps4rt-pkg.exe` |
| `build-win/package/NoisyFairy/` | A complete folder (executables, DLLs, shaders, launcher files, docs) laid out like the release zip |

Copy `build-win/package/NoisyFairy` to Windows and double-click `NoisyFairy.bat`.

## What the script does

1. Checks the tools above.
2. Writes clang wrappers and a CMake toolchain file for `x86_64-w64-mingw32` (posix threads,
   libstdc++, emulated TLS; the runtime needs `-femulated-tls` and the shared libgcc so that
   the executable and `libstdc++-6.dll` share one TLS implementation).
3. Builds these dependencies from source at pinned tags:

   | Library | Version |
   |---|---|
   | SDL3 | release-3.2.24 |
   | {fmt} | 11.0.2 |
   | xxHash | v0.8.3 |
   | zlib | v1.3.1 |
   | Zydis (with Zycore) | v4.1.1 |
   | Crypto++ | 8.9.0 |
   | robin-map | v1.3.0 |
   | Vulkan-Headers | v1.4.368 |
   | Vulkan-Loader | v1.3.296 (only its export list, to make an import library for the system `vulkan-1.dll`) |

   Boost headers and the Vulkan Memory Allocator header come from the Ubuntu packages above.
4. Downloads the **FFmpeg** Windows shared build (LGPL) from
   [BtbN/FFmpeg-Builds](https://github.com/BtbN/FFmpeg-Builds). It is used for movie and audio
   decoding and its DLLs ship with the game. The script expects the DLL versions
   `avcodec-63`, `avformat-63`, `avutil-61`, `swresample-7` and `swscale-10`; if the "latest"
   build has moved on, it stops with a message. You can then point `FFMPEG_URL` in
   `scripts/build-windows.sh` at a matching build.
5. Builds `gpu/` (the Vulkan renderer, shader recompiler and Gnm/VideoOut drivers) as a static library.
6. Builds `ps4rt.exe` (the runtime), `ps4rt-launcher.exe`, `ps4rt-setup.exe` and `ps4rt-pkg.exe`.
   All four embed the NoisyFairy icon (`res/`).
7. Assembles the package folder. The launcher needs an embedded Python for the helper scripts;
   the script downloads the official Windows embeddable Python 3.12 from python.org for that.

## Verifying a binary

Builds are not bit-for-bit reproducible across toolchain versions (compiler and linker embed
paths and timestamps), so compare behavior and provenance rather than hashes: build the tag the
release was made from with the toolchain versions listed in the release notes, check that the
same dependencies were used, and run the unit tests below. Anyone can read what the executables do
because every line of code in them is in this repository.

## Tests

The `tests/` directory holds the unit tests (Python and C/C++). The Python ones run on any machine:

```sh
python3 -m unittest discover -s tests -p 'test_*.py'
```

## Troubleshooting

- *"MinGW-w64 posix-threads toolchain not found"*: install `g++-mingw-w64-x86-64-posix`; if your
  distribution also has the "win32" variant, select posix with
  `sudo update-alternatives --config x86_64-w64-mingw32-g++`.
- A dependency fails to build: its log is `build-win/obj/<name>.log`. The game library log is
  `build-win/obj/gpu.log`.
- Delete `build-win/obj/<name>.done` to rebuild a single step.
- Missing `libgcc_s_seh-1.dll` / `libstdc++-6.dll` / `libwinpthread-1.dll` warning: copy them from
  `/usr/lib/gcc/x86_64-w64-mingw32/*-posix/` and `/usr/x86_64-w64-mingw32/lib/` next to the executables.

## Licenses of what ships in the package

The project is GPL-2.0-or-later (see `LICENSE`, and `docs/UPSTREAM_CREDITS.md` for the full lineage).
The binary package also contains SDL3 (zlib license), FFmpeg (LGPL, shared build; source at
the BtbN link above), the MinGW-w64 runtime DLLs (GCC runtime exception), Python (PSF license),
Dear ImGui (MIT), Crypto++ (Boost Software License 1.0) and DejaVu Sans (Bitstream Vera license,
free to redistribute).
