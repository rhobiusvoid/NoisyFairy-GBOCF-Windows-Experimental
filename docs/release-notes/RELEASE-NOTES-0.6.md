# NoisyFairy 0.6 Beta

Windows port of the PS4 runtime for Mobile Suit Gundam Battle Operation Code Fairy (CUSA28763 v01.03). Based on HekamiAeon's Linux project.

## Read this first
The game should reach the Bandai Namco logo **within a few seconds** of pressing Play. A rare random stall at boot can still happen (black screen, no logo). **0.6 handles it automatically:** if the game makes no boot progress for 60 seconds, the launcher restarts it (up to 2 times). If it still stalls, close the game and start it again; if it keeps happening, report it on the Issues tab with the newest file from `out\CUSA28763\user\log` and the `ps4rt_stall_dump.txt` next to it, plus your GPU and driver version. Set `PS4RT_BOOT_RETRIES=0` to turn the auto-restart off. Crashes and normal exits are never restarted.

## What's new
- **Video memory fix (4 GB cards).** The texture cache no longer runs out of VRAM during long sessions. Verified on a GTX 1050 Ti and a GTX 1650 Ti over extended play (many chapters and simulators). Under extreme memory pressure the oldest idle GPU-written textures may be dropped and rebuilt; the visible "rainbow noise" this briefly caused on MS colours is fixed.
- **Boot-stall auto-restart** (see above).
- **Boot stall mitigation:** the thread list lock uses a Windows SRWLOCK instead of the pthread mutex (one stall location addressed).
- **Crash-safe hang dump.** The 10/20/40/80 s thread dump (only when "Write log file" is on) no longer crashes the game itself, and now records more detail (guest thread state, internal queue state).
- **Vertex explosion fix** and other rendering fixes from the 0.5 test cycle.
- Launcher shows 0.6 Beta; volume-key installer survives a locked status file.

## Compatibility
- Confirmed booting and playing: GTX 1050 Ti, GTX 1650 Ti, RTX 3060 Mobile, RTX 5070 Ti, Radeon 780M and other AMD GPUs.
- Known issue: GTX 10-series (Pascal) shows weapon and shoulder-part glitches until the camera is close.
- Known issue: rare boot stall (auto-restart usually recovers).
- Note: overlay hooks (e.g. OBS game capture) can change timing and may make stalls more likely.
- This release had no final full test pass; please report anything unusual.

## Install
1. Download all four `NoisyFairy-0.6-Beta.zip.partNN.bin` files into one folder.
2. In cmd: `copy /b NoisyFairy-0.6-Beta.zip.part00.bin+NoisyFairy-0.6-Beta.zip.part01.bin+NoisyFairy-0.6-Beta.zip.part02.bin+NoisyFairy-0.6-Beta.zip.part03.bin NoisyFairy-0.6-Beta.zip`
3. Verify: 93,032,269 bytes, SHA-256 227c7ecc2428912731bfcf95f01e30d37804f2cd90e67acb13bd03c5926fb010
4. Unzip to a short writable path, run `NoisyFairy.bat`, pick your extracted `CUSA28763` folder, press Play.

You need your own extracted CUSA28763 game, the v01.03 update (if your copy is older) and the three volume-key PKGs. No game files, keys or firmware are included. Requires Windows 10 1803+ and a Vulkan 1.3 GPU.

**Upgrading from 0.5:** extract over the old folder; your `out\` folder (saves, settings) is kept.
