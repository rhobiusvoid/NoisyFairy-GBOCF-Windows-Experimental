# NoisyFairy 0.8 Beta

Windows port of the PS4 runtime for Mobile Suit Gundam Battle Operation Code Fairy (CUSA28763 v01.03). Based on HekamiAeon's Linux project.

## What's new
- **Pitch limit removed from mouse look.** The camera pitch is now limited only by the game itself, like yaw. The pitch estimate and its reset key are gone: **F9 no longer does anything** in normal play, **F8** still toggles mouse capture.
- **Bundled DLLs load at low addresses.** The package now ships FFmpeg, SDL3 and the MinGW runtime DLLs with the high-entropy ASLR flag already cleared (the launcher still repeats this at start as a fallback), which keeps the runtime's guest address range free.
- **Documentation fixes** (thanks to Hekami's review): single-zip download instructions, and the base game must be an already extracted folder, since the bundled `ps4rt-pkg.exe` only handles update and add-on PKGs.
- **README:** tested-hardware notes and a clear warning that 4 GB graphics cards can show occasional rainbow noise on colour-changing suit textures when video memory runs out.
- **Build from source:** the build script verifies its embedded Python download, and the repository now has CI that cross-builds the Windows executables on every push.
- Internal clean-up with no change in behaviour: the mouse model moved to its own module with unit tests (output identical to 0.7 on a scripted test), and the overlay menu code was split into smaller functions.

## Install
1. Download `NoisyFairy-0.8-Beta.zip` and extract it to a short, writable path such as `C:\CodeFairy\`.
2. Run `NoisyFairy.bat`, pick your extracted `CUSA28763` folder, press **Play**.

You need your own extracted CUSA28763 game, the v01.03 update (if your copy is older) and the three volume-key PKGs. No game files, keys or firmware are included. Requires Windows 10 1803+ and a Vulkan 1.3 GPU.

**Upgrading from 0.7:** extract over the old folder; your `out\` folder (saves, settings) is kept. Your `ps4rt_mouse.ini` keeps working; any old `mouse_pitch_limit` lines are ignored.

SHA-256 of the zip: see the release page.

## Known issues
- GTX 10-series (Pascal) cards can show weapon/shoulder-part glitches until the camera is close; this will not be fixed, because a fix risks breaking newer GPUs.
- 4 GB graphics cards can show occasional rainbow noise on colour-changing textures when video memory runs out. There is no setting that avoids it on those cards.
- Rare boot stall: the game auto-restarts and usually recovers (`PS4RT_BOOT_RETRIES=0` turns this off). If you hit one, please open an issue with your GPU, driver and the newest log from `out\CUSA28763\user\log`.
- Overlay hooks (e.g. OBS game capture) can change timing and may make stalls more likely.
