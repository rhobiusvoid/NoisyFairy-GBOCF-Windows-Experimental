# NoisyFairy 0.5 Alpha

Windows port of the PS4 runtime for Mobile Suit Gundam Battle Operation Code Fairy (CUSA28763 v01.03). Based on HekamiAeon's Linux project.

## Read this first
The game should reach the Bandai Namco logo **within a few seconds** of pressing Play. A rare, random stall at boot has been seen: the screen stays black and the logo never appears. If that happens, close the game and **report it on the Issues tab**, attaching the newest file from `out\CUSA28763\user\log` (the launcher writes one by default; it contains a "Hang watch" section that shows what the game was doing). Please include your GPU and driver version.

## What's new
- **New graphical launcher.** Double-click `NoisyFairy.bat` instead of using the command line.
  - **Launcher tab:** game folder picker (remembered), **Play**, and **Install Volume Keys...** (hidden once all three keys are installed).
  - **Options tab:** the same graphics settings as the in-game menu (window mode, frame rate limit, FPS counter, internal resolution, graphics quality, anisotropic filtering, FSR mode, FSR output size, sharpening), saved to `ps4rt.ini` immediately and applied on the next start.
  - **Write log file** (on by default): saves everything the game prints to `out\CUSA28763\user\log` (10 newest kept), so crash details survive after the window closes. The launcher shows the exit code if the game crashes.
- **Volume-key installer fixed on Windows.** The picker window now opens (it used to quit immediately with an error) and the setup/update windows are shown instead of falling back to the terminal. Paths with non-ASCII characters and stray output no longer break the file selection.
- **Pipeline binary cache back on for AMD and Intel GPUs.** 0.4 turned it off on every GPU to fix the NVIDIA boot crash; it is now off only on NVIDIA (`PS4RT_PIPELINE_BINARIES=1` forces it on, `0` off).
- **Hang diagnostics.** With logging on, the log gets a thread-state dump 10, 20, 40 and 80 seconds after start, to help track down boot stalls.
- Project name and branding updated to NoisyFairy.

## Compatibility
- Confirmed booting and playing: GTX 1050 Ti, RTX 3060 Mobile, RTX 5070 Ti, Radeon 780M and other AMD GPUs.
- Known issue: GTX 10-series (Pascal) shows weapon and shoulder-part glitches until the camera is close.
- Known issue: rare random black-screen stall at boot (see above).
- Chapters past the tutorial and Volumes 2/3 are untested on Windows.

## Install
1. Download all four `NoisyFairy-0.5-Alpha.zip.partNN.bin` files into one folder.
2. In cmd: `copy /b NoisyFairy-0.5-Alpha.zip.part00.bin+NoisyFairy-0.5-Alpha.zip.part01.bin+NoisyFairy-0.5-Alpha.zip.part02.bin+NoisyFairy-0.5-Alpha.zip.part03.bin NoisyFairy-0.5-Alpha.zip`
3. Verify: 93,015,186 bytes, SHA-256 899722f1aec541dd9042eb1e86acbe84f246f6d9c32bcde42f35d6cd3a328c39
4. Unzip to a short writable path, run `NoisyFairy.bat`, pick your extracted `CUSA28763` folder, press Play.

You need your own extracted CUSA28763 game, the v01.03 update (if your copy is older) and the three volume-key PKGs. No game files, keys or firmware are included. Requires Windows 10 1803+ and a Vulkan 1.3 GPU.

**Upgrading from 0.4:** extract over the old folder; your `out\` folder (saves, settings) is kept.
