# Trims arduino-audio-driver down to the one codec this firmware actually uses.
#
# The library declares 30-odd codec drivers as file-scope `static` objects in
# AudioDriver.h, and AudioBoard.h then includes AudioBoards/AudioBoards.h, which
# constructs eight prebuilt AudioKit/Lyrat/M5Stack boards on top of them. Every
# translation unit including AudioBoard.h therefore emits all of them, their
# constructors run from .init_array and reference their vtables, and no amount of
# -ffunction-sections/--gc-sections can drop them: the linker cannot prove a
# constructor with side effects is dead.
#
# The cost, measured on the v4.9.2 wio-tracker-l2 image with
# xtensa-esp32s3-elf-nm, is 28,301 bytes of drivers for hardware that is either
# not on the board at all (WM8960/WM8978/WM8994, ES8388 and its four bank
# variants, CS43l22/CS42L51/CS42448, ES8156/ES8374, ES7210, AC101, PCM3168,
# NAU8325, and the Zephyr-ported set) or present but never opened (the ES7243
# mic -- see cfg.input_device = ADC_INPUT_NONE in pagerAudioBegin()).
#
# That mattered because wio-tracker-l2 had 3,547 bytes of headroom in its
# 0x320000 app slot and had already failed an alpha release by 2,857 bytes; see
# issue #72.
#
# What survives: AudioDriverES8311, the DAC both boards using this library
# actually drive, and NoDriverClass NoDriver, which AudioBoard.h's own NoBoard
# instance needs. Nothing this firmware calls changes -- the Wio Tracker L2 and
# the T-Lora Pager both keep their notification beep, which on the Wio is the
# only sound path it has (BOARD_BUZZER is -1 there).
#
# Scope: PlatformIO gives each environment its own libdeps copy, so this only
# touches the envs that list the script in extra_scripts. Nothing outside them
# sees a modified audio-driver.
#
# Idempotent, and safe to run before libdeps exist: on a fresh checkout the first
# build may run this before the library has been downloaded, in which case it
# warns and the patch lands on the next build. If you see that warning, run the
# build once more.
Import("env")
import os

MARKER = "camillia-audio-codec-trim"

# ── AudioDriver.h: fence off every instance, then re-declare the two needed ───
# Anchored on the first instance rather than the "-- Drivers" comment alone, so
# a library that reorders or renames this block fails the match loudly instead
# of being fenced off at the wrong place.
DRIVERS_OLD = """// -- Drivers
/// @ingroup audio_driver
static AudioDriverAC101Class AudioDriverAC101;"""

DRIVERS_NEW = """// -- Drivers
// camillia-audio-codec-trim: everything from here to the end of the namespace
// is a file-scope static instance. Each one that survives is constructed at
// startup and drags its whole driver class into the image, so the block is
// fenced off wholesale and only the instances this firmware names are declared
// again below the #endif.
#if 0
/// @ingroup audio_driver
static AudioDriverAC101Class AudioDriverAC101;"""

# The AD1938 block is the last thing before the namespace closes, which makes it
# the anchor for the other end of the fence.
TAIL_OLD = """#ifdef ARDUINO
/// @ingroup audio_driver
static AudioDriverAD1938Class AudioDriverAD1938;
#endif

}  // namespace audio_driver"""

TAIL_NEW = """#ifdef ARDUINO
/// @ingroup audio_driver
static AudioDriverAD1938Class AudioDriverAD1938;
#endif
#endif  // camillia-audio-codec-trim

// camillia-audio-codec-trim: the only two instances this firmware links.
//   ES8311  -- the DAC driven by pagerAudioBegin() on both audio boards.
//   NoDriver -- referenced by this library's own NoBoard, just below.
/// @ingroup audio_driver
static AudioDriverES8311Class AudioDriverES8311;
/// @ingroup audio_driver
static NoDriverClass NoDriver;

}  // namespace audio_driver"""

# ── AudioBoard.h: drop the presets that name codecs no longer instantiated ────
BOARDS_OLD = """/// @ingroup audio_driver
static AudioBoard GenericWM8960{AudioDriverWM8960, NoPins};
/// @ingroup audio_driver
static AudioBoard GenericCS43l22{AudioDriverCS43l22, NoPins};"""

BOARDS_NEW = """// camillia-audio-codec-trim: both name codec instances that are no longer
// declared, and nothing in this firmware constructs either board. NoBoard above
// stays -- it uses NoDriver, which is kept.
#if 0
static AudioBoard GenericWM8960{AudioDriverWM8960, NoPins};
static AudioBoard GenericCS43l22{AudioDriverCS43l22, NoPins};
#endif"""

INCLUDE_OLD = """// we automatically include all baords using gpios as ints
#if !defined(__zephyr__)
#include "AudioBoards/AudioBoards.h"
#endif"""

INCLUDE_NEW = """// camillia-audio-codec-trim: the eight prebuilt AudioKit/Lyrat/M5Stack board
// definitions in here each construct a codec instance (AC101, ES8388 twice, and
// so on), which is what forced every driver into the image. This firmware builds
// its own AudioBoard from explicit pins and uses none of them.
#if 0
#include "AudioBoards/AudioBoards.h"
#endif"""

libdir = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                      "audio-driver", "src")

edits = [
    ("AudioDriver.h", [(DRIVERS_OLD, DRIVERS_NEW), (TAIL_OLD, TAIL_NEW)]),
    ("AudioBoard.h", [(BOARDS_OLD, BOARDS_NEW), (INCLUDE_OLD, INCLUDE_NEW)]),
]

for name, pairs in edits:
    path = os.path.join(libdir, name)
    if not os.path.isfile(path):
        print("[patch_audio_driver_codecs] audio-driver not fetched yet: %s" % path)
        print("[patch_audio_driver_codecs] NOT patched - run the build once more")
        break

    with open(path) as f:
        src = f.read()

    if MARKER in src:
        print("[patch_audio_driver_codecs] %s already patched" % name)
        continue

    # Both replacements in a file have to land, or neither should: half a fence
    # is a file that does not compile, which is a far worse failure than an
    # untrimmed build.
    if not all(old in src for old, _ in pairs):
        missing = [i for i, (old, _) in enumerate(pairs) if old not in src]
        print("[patch_audio_driver_codecs] WARNING: %s pattern(s) %s not found "
              "- audio-driver version drift?" % (name, missing))
        print("[patch_audio_driver_codecs] WARNING: NOT patched, check by hand")
        break

    for old, new in pairs:
        src = src.replace(old, new, 1)
    with open(path, "w") as f:
        f.write(src)
    print("[patch_audio_driver_codecs] trimmed %s to the ES8311 path" % name)
