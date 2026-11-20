# NoisyFairy: Windows port (0.8 Beta)

Unofficial Windows port of https://github.com/HekamiAeon/GBO-Code-Fairy-PS4RT by HekamiAeon (GPL-2.0-or-later; upstream
history is not included here, this is a snapshot of upstream plus the port changes). All credit for the original runtime goes to Hekami.

- Cross-compiled on Linux with clang + MinGW-w64 (UCRT/msvcrt, libstdc++, `-femulated-tls`).
- New: `src/compat_win.[ch]`, `src/compat_win_mem.h` (POSIX shims, runtime-resolved VirtualAlloc2/MapViewOfFile3, setjmp/longjmp, 1 ms timer).
- Launcher scripts ported (`Play-Game.bat`, `scripts/*.py`).
- DLSS disabled on Windows (bridge header missing upstream); FSR 1/3.1/4 kept.
- See `docs/WINDOWS-MUTEX-NOTES.md` and `docs/RUNNING_WINDOWS.md` for the deadlock-risk review.
- 0.8 Beta: the pitch limit in mouse look is gone (the camera pitch is now limited only by the game), shipped DLLs load at low addresses, documentation and from-source build instructions corrected, 4 GB card warning.
- 0.7 Beta: keyboard and mouse support (mouse look, rebindable keys, weapon slot keys 1-8, Controls panel in the launcher and the Insert menu), new NoisyFairy icon on all executables (see `RELEASE-NOTES-0.7.md`).
- 0.6 Beta: out-of-video-memory fix for 4 GB cards, boot-stall auto-restart, crash-safe hang dump (see `RELEASE-NOTES-0.6.md`).
- 0.5 Alpha: new graphical launcher (`NoisyFairy.bat`), log files, working volume-key installer, pipeline binary cache back on for AMD/Intel (see `RELEASE-NOTES-0.5.md`).
- 0.4 Alpha: fixed a boot crash on NVIDIA drivers that expose `VK_KHR_pipeline_binary` (RTX 30/50 cards). The pipeline binary cache is off by default on NVIDIA only (`PS4RT_PIPELINE_BINARIES=1` forces it on, `0` off).
- Prebuilt package: see Releases. Requires Windows 10 1803+, Vulkan 1.3 GPU. Bring your own game files and keys.

## Download and run
1. Download `NoisyFairy-0.8-Beta.zip` from the Releases page (the SHA-256 is in the release notes).
2. Unzip to a short writable path, then double-click `NoisyFairy.bat`, pick your **already extracted** `CUSA28763` folder (it must contain `eboot.bin` and `sce_sys\param.sfo`) and press Play.
3. Keep the v01.03 update PKG and the three volume-key PKGs as `.pkg` files; install them via the setup window or **Install Volume Keys...** on the launcher.

The game should start within a few seconds. A black screen that never shows the Bandai Namco logo is a stall: close the game and report it on the Issues tab with the newest log from `out\CUSA28763\user\log`.

When reporting a problem include: that log file, Windows version, GPU/driver.
