# NoisyFairy 0.7 Beta

Windows port of the PS4 runtime for Mobile Suit Gundam Battle Operation Code Fairy (CUSA28763 v01.03). Based on HekamiAeon's Linux project.

## What's new
- **Keyboard and mouse play.** Mouse look is on by default (controller players are unaffected). It is tuned to the game's own camera response, with a pitch-limit estimate; F9 resets it, F8 toggles capture.
- **Rebindable keys.** Movement, camera keys, face buttons, shoulders/triggers, sticks, Options, touchpad and D-pad can all be rebound, along with the mouse buttons and wheel. Binding a key already in use clears it from the old action. Esc cancels, Delete unbinds.
- **Weapon slot keys 1-8.** Matches the official GBO2 PC layout (1 Square, 2 Cross, 3 Circle, 4 Triangle, 5 Square+Triangle, 6 Square+Cross, 7 Circle+Triangle, 8 Circle+Cross). Each key presses the switch button (R1 by default) together with the slot's buttons, so rebinding your controller layout in-game is supported: edit the `weaponN` / `weapon_switch` lines in `ps4rt_mouse.ini`.
- **Controls panel** in the launcher (with a controller diagram; click a key cap to rebind) and in the in-game Insert menu. Changes apply live.
- **New NoisyFairy icon** on every executable and on the taskbar.
- Defaults moved: L1 = Left Ctrl, R1 = G, so the number row is free for weapons.

## Known issues
- Pascal (GTX 10-series) weapon/shoulder-part glitches until the camera is close.
- Rare boot stall (0.6's auto-restart usually recovers; `PS4RT_BOOT_RETRIES=0` turns it off).
- Overlay hooks (e.g. OBS game capture) can change timing and may make stalls more likely.
- Fine mouse aim on diagonals and per-MS camera speeds are still being measured; please report anything that feels off.

## Install
1. Download `NoisyFairy-0.7-Beta.zip` from the Releases page.
2. Verify: 93651860 bytes, SHA-256 0e4630aff3feef5de2a01083bd7cf27ff81abf8c9226efeb8d65b99f74c23bcf
3. Unzip to a short writable path, run `NoisyFairy.bat`, pick your extracted `CUSA28763` folder, press Play.

You need your own extracted CUSA28763 game, the v01.03 update (if your copy is older) and the three volume-key PKGs. No game files, keys or firmware are included. Requires Windows 10 1803+ and a Vulkan 1.3 GPU.

**Upgrading from 0.6:** extract over the old folder; your `out\` folder (saves, settings) is kept. If you had an older `ps4rt_mouse.ini`, press **Reset keys** in the Controls tab (or delete the file) to pick up the new defaults.
