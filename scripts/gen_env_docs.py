#!/usr/bin/env python3
"""Generates docs/ENVIRONMENT.md: every PS4RT_* environment variable the code reads, where it is
read, and the nearest explanatory comment. Run without arguments to rewrite the file, with
--check to fail when it is out of date (used by CI)."""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'docs' / 'ENVIRONMENT.md'
SCAN = ['src', 'gpu/shim', 'gpu/shadps4', 'scripts']
EXT = {'.c', '.cpp', '.h', '.py'}
READ = re.compile(r'(?:getenv|GetEnvironmentVariable[AW]?|SetEnvironmentVariable[AW]?|environ(?:\.get)?|_wgetenv)\s*[\(\[]\s*L?"(PS4RT_[A-Z0-9_]+)"')
READ_PY = re.compile(r"""(?:environ(?:\.get)?|getenv)\s*[\(\[]\s*['"](PS4RT_[A-Z0-9_]+)['"]""")


# Hand-written descriptions where the source has no usable comment.
OVERRIDES = {
    'PS4RT_BOOT_MARKER': 'File the runtime creates shortly after a healthy boot; the launcher restarts the game if it never appears.',
    'PS4RT_BOOT_RETRIES': 'How many automatic restarts the launcher attempts after a boot stall (0 turns it off).',
    'PS4RT_CONFIG': 'Path of ps4rt.ini; ps4rt_mouse.ini (keyboard and mouse settings) is looked up next to it.',
    'PS4RT_KEY_MAP': 'Remaps keyboard buttons (see apply_button_map); the launcher Controls tab is the normal way.',
    'PS4RT_PAD_MAP': 'Remaps gamepad buttons (see apply_button_map).',
    'PS4RT_PAD_FILE': 'Reads the pad state from this file while the game runs (test input).',
    'PS4RT_PAD_RECORD': 'F9 starts and stops recording the pad state to this file.',
    'PS4RT_PAD_REPLAY': 'Plays back a recording made with PS4RT_PAD_RECORD.',
    'PS4RT_SAVE_WATCH': 'Diagnostic (Windows): 1 enables F10, which starts and stops logging which bytes of the loaded save the game reads; the report goes to `user\\savewatch.txt`.',
    'PS4RT_ACHIEVEMENTS_DIR': 'Folder for the local achievement (trophy) progress files. Empty: the default location is used.',
    'PS4RT_ASYNC_WRITE_FAULT': 'GPU buffer write faults are handled asynchronously by default; 0 turns that off.',
    'PS4RT_AUDIO': '`none` disables audio output and uses a silent timer instead of SDL audio.',
    'PS4RT_BACKGROUND_WARMUP': '1 forces the background pipeline warm-up even where on-demand pipeline compilation would be chosen.',
    'PS4RT_BC6_FLOAT': 'Diagnostic: `unsigned` or `signed` makes BC6 float textures decode that way instead of being rejected.',
    'PS4RT_CF_RESOLUTION': 'Render resolution as `WIDTHxHEIGHT` (640x360 to 8192x8192); the runtime applies it to the game image and the overlay uses it for its layout.',
    'PS4RT_CF_SHADOW_MAP': 'Shadow map size (1024, 2048 or the game size). Empty keeps the game default.',
    'PS4RT_CJK_FONT': 'Path of a font file for Japanese/Chinese/Korean text in the overlay; Linux system fonts are tried otherwise.',
    'PS4RT_COPY_THREADS': 'Number of GPU copy worker threads (0-16). Default: a quarter of the hardware threads, 1 to 4.',
    'PS4RT_CPU_CHECK': '0 skips the check that the CPU has the instruction sets the game needs.',
    'PS4RT_DEFERRED_READBACK': '1 defers GPU readbacks instead of waiting for each one (also a menu/ini option).',
    'PS4RT_DLC_INSTALL_SCRIPT': 'Linux only: script the overlay\'s "Install DLC..." button runs (needs PS4RT_ORIGINAL_GAME_DIR).',
    'PS4RT_DLSS': '0 disables DLSS (it is not available in the Windows port).',
    'PS4RT_DRAW_PIPE': '1 enables draw-pipeline threading; off by default because some games can lose UI/fade draws.',
    'PS4RT_ENGINE_FPS': 'Engine frame rate this run was started with; the menu compares it with the chosen value to tell when a restart is needed.',
    'PS4RT_FAULT_WINDOW': 'Size in KiB (4-1024, default 256) of the range one buffer write fault covers; rounded up to a power of two.',
    'PS4RT_FSR411_DIR': 'Folder with the FSR 4.1 shader assets (default `fsr4_411`).',
    'PS4RT_FSR4_DIR': 'Folder with the FSR 4 shader assets (default `fsr4_shaders`); the Windows launcher sets it.',
    'PS4RT_FSR4_PROFILE': '1 records GPU timestamps for the FSR 4.1 passes.',
    'PS4RT_FULL_BARRIERS': '1 uses full pipeline barriers (a compatibility switch; the mode is logged to last_run.log).',
    'PS4RT_GPU_LOG': 'GPU log level: trace, debug, info, warning (default), error or critical.',
    'PS4RT_IME_TEXT': "Test input: this text is returned immediately for the game's text-entry dialogs.",
    'PS4RT_LANGUAGE': 'PS4 system language number reported to the game (default 1, English US).',
    'PS4RT_MENU_SOUNDS': '`0` or `off` turns the overlay menu sounds off.',
    'PS4RT_MENU_SOUND_DIR': 'Folder with replacement menu sound files.',
    'PS4RT_MENU_SOUND_VOLUME': 'Menu sound volume, 0-100 (default 65).',
    'PS4RT_OBJECT_MOTION_ALL': '1 gives every object motion vectors for the upscaler, not only the usual ones.',
    'PS4RT_ORIGINAL_GAME_DIR': 'Linux only: game folder passed to the DLC install script.',
    'PS4RT_PARALLEL_WARMUP': 'Number of threads for pipeline warm-up compilation (0 or unset: off).',
    'PS4RT_PIPE_VERIFY': 'Developer check: verify the draw pipeline every N draws (0 = off).',
    'PS4RT_PKG_TOOL': 'Developer override: path of the PKG extractor used by the setup script.',
    'PS4RT_PREP_WORKERS': 'Number of idle-priority draw preparation workers (0-16).',
    'PS4RT_PRESENT_MODE': 'Vulkan present mode name (default `Mailbox`).',
    'PS4RT_PRESENT_THREAD': '0 presents on the same thread as rendering; by default a separate swap thread is used.',
    'PS4RT_SCENE_STENCIL_BITS': '1 forces the stencil-bits path when resolving scene depth targets, even where shader stencil export is supported.',
    'PS4RT_SETUP_START_DIR': 'Folder where the setup file browser starts (default: the home folder).',
    'PS4RT_SQUARE_PASS': '0 turns off the square-target pass (on by default).',
    'PS4RT_STAGING_KEEP_MB': 'Staging buffer memory kept between frames, in MB (default 512).',
    'PS4RT_STREAM_MEMORY': 'Where streamed GPU buffers are placed: `cached` (default) or `device`.',
    'PS4RT_STRICT_HANDLES': 'Set (any value): Windows raises an exception on invalid handle use, to find handle bugs.',
    'PS4RT_TOGGLE_FILE': 'Developer switch: file whose number is read every 250 ms as a bit mask of optimizations to disable.',
    'PS4RT_TRANSITION_SNAPSHOT': '1 preserves GPU transition snapshots (workaround switch).',
    'PS4RT_TROPHY_CATALOG': 'Path of the trophy catalog used by the achievements list.',
    'PS4RT_UFFD': 'Linux only: 1 tracks guest memory with userfaultfd.',
    'PS4RT_UI_TRIGGER_VS': 'Hash (hex) of the vertex shader that marks where UI drawing starts, for the upscaler.',
    'PS4RT_UPSCALE_BEFORE_CS': 'Hash (hex) of the compute shader the upscaler runs before.',
    'PS4RT_WORK_RETIRED': '0 stops using the GPU work-retired signal (on by default) when deciding that a submission is finished.',
    'PS4RT_WQM_HELPERS': '1 enables the WQM helper-lane workaround (diagnostic).',
}


def usable(text):
    return len(text) > 25 and not re.match(r'(ifn?def|if|endif|define)\b', text)


def comment_near(lines, i, var):
    """Best-effort description: a comment line mentioning the variable, else the comment just above."""
    for j in range(max(0, i - 6), min(len(lines), i + 2)):
        t = lines[j].strip()
        if var in t and t.lstrip().startswith(('//', '/*', '*', '#')):
            text = re.sub(r'^(//+|/\*+|\*+|#+)\s*', '', t).rstrip('*/ ').strip()
            if usable(text):
                return text
    j = i - 1
    while j >= 0 and lines[j].strip().startswith(('//', '*', '/*', '#')):
        text = re.sub(r'^(//+|/\*+|\*+|#+)\s*', '', lines[j].strip()).rstrip('*/ ').strip()
        if usable(text):
            return text
        j -= 1
    return ''


def collect():
    found = {}
    for base in SCAN:
        for path in sorted((ROOT / base).rglob('*')):
            if path.suffix not in EXT or 'third_party' in path.parts:
                continue
            lines = path.read_text(errors='replace').splitlines()
            for i, line in enumerate(lines):
                for rx in (READ, READ_PY):
                    for m in rx.finditer(line):
                        entry = found.setdefault(m.group(1), {'files': set(), 'doc': ''})
                        entry['files'].add(str(path.relative_to(ROOT)))
                        if not entry['doc']:
                            entry['doc'] = OVERRIDES.get(m.group(1)) or comment_near(lines, i, m.group(1))
    return found


def render(found):
    out = ['# Environment variables', '',
           'Generated by `scripts/gen_env_docs.py` from the source: do not edit by hand.',
           'Most of these are developer switches; players normally use the launcher or `ps4rt.ini`.',
           'The "purpose" column is the nearest source comment, so it may be terse or empty;',
           'the file column says where to read the real behaviour.', '',
           f'{len(found)} variables.', '',
           '| Variable | Purpose (from the source) | Read in |', '|---|---|---|']
    for name in sorted(found):
        e = found[name]
        doc = e['doc'].replace('|', '\\|')
        files = ', '.join(f'`{f}`' for f in sorted(e['files'])[:3])
        if len(e['files']) > 3:
            files += f' (+{len(e["files"]) - 3})'
        out.append(f'| `{name}` | {doc} | {files} |')
    return '\n'.join(out) + '\n'


if __name__ == '__main__':
    text = render(collect())
    if '--check' in sys.argv:
        if not OUT.exists() or OUT.read_text() != text:
            sys.exit('docs/ENVIRONMENT.md is out of date: run scripts/gen_env_docs.py')
    else:
        OUT.write_text(text)
        print(f'wrote {OUT} ({text.count(chr(10))} lines)')
