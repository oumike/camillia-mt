# Makes RadioLib's LR11x0::config() tolerate old LR1110 transceiver firmware.
#
# RadioLib sends DriveDiosInSleepMode (opcode 0x012A) during begin(). That
# opcode only exists in LR11x0 transceiver firmware 0x0308 and later; older
# chips answer CMD_PERR, which RadioLib surfaces as RADIOLIB_ERR_SPI_CMD_INVALID
# (-706) and which aborts init on a radio that is otherwise perfectly healthy.
# Preproduction Elecrow ThinkNode M9 units ship with such firmware. (Meshtastic
# runs on them only because it pins an older RadioLib that never sends the
# command.)
#
# RadioLib 7.8 added its own guard, but it only skips the command on LR1110
# firmware below 0x0306, so a 0x0306/0x0307 part still fails. Rather than move
# that threshold to a number nobody has confirmed on hardware, the patch keeps
# upstream's guard and additionally treats a CMD_INVALID answer as "not
# supported" -- the same tolerance this script applied before 7.8.
#
# The command is an optional nicety -- it suppresses DIO glitches while the chip
# sleeps -- so skipping it on old firmware costs nothing that matters here.
#
# The pattern targets RadioLib 7.8.1 exactly, which platformio.ini pins for m9.
# A RadioLib bump has to update OLD below in the same change.
#
# Scope: PlatformIO gives each environment its own libdeps copy, so this only
# touches the env that lists the script in extra_scripts. Nothing outside the
# m9 build sees a modified RadioLib.
#
# Idempotent. Dependencies are installed before SCons runs, so for a build the
# library is always present here; anything short of a patched source fails the
# build rather than shipping a radio that cannot start: when this only warned,
# m9 builds silently went out unpatched. The one exception is the clean
# pass, which PlatformIO runs before installing anything.
Import("env")
import os

from SCons.Script import COMMAND_LINE_TARGETS

MARKER = "camillia-lr1110-oldfw-patch"
TAG = "[patch_radiolib_lr11x0]"

OLD = """  if(!((this->versionDevice == RADIOLIB_LR11X0_DEVICE_LR1110) && (this->versionCombined < 0x0306))) {
    state = this->driveDiosInSleepMode(true);
    RADIOLIB_ASSERT(state);
  }"""

NEW = """  if(!((this->versionDevice == RADIOLIB_LR11X0_DEVICE_LR1110) && (this->versionCombined < 0x0306))) {
    state = this->driveDiosInSleepMode(true);
    // camillia-lr1110-oldfw-patch: LR11x0 transceiver firmware older than 0x0308
    // does not implement DriveDiosInSleepMode (0x012A) and answers CMD_PERR,
    // which would abort begin() with -706 on a healthy radio. Upstream's guard
    // above only covers LR1110 firmware below 0x0306. The command only
    // suppresses DIO glitches in sleep mode, so skipping it is safe.
    if(state == RADIOLIB_ERR_SPI_CMD_INVALID) {
      RADIOLIB_DEBUG_BASIC_PRINTLN("DriveDiosInSleepMode unsupported (old LR11x0 FW), skipping");
      state = RADIOLIB_ERR_NONE;
    }
    RADIOLIB_ASSERT(state);
  }"""

path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                    "RadioLib", "src", "modules", "LR11x0", "LR11x0.cpp")

# PlatformIO runs a clean target in its own SCons pass, before it installs
# dependencies; the build pass that follows does not carry the clean target.
# The clean arrives as SCons' --clean plus FULLCLEAN=0/1, not as a target name
# (see patch_pubsubclient_topiclen.py), so ask the builder.
clean_only = env.IsCleanTarget() or any(
    t in ("clean", "cleanall", "fullclean") for t in COMMAND_LINE_TARGETS)

if not os.path.isfile(path):
    if clean_only:
        print("%s RadioLib not installed (clean run), nothing to patch" % TAG)
    else:
        raise RuntimeError("%s %s not found - refusing to build an unpatched "
                           "LR11x0 driver" % (TAG, path))
else:
    with open(path, encoding="utf-8") as f:
        src = f.read()
    if MARKER in src:
        print("%s already patched" % TAG)
    elif src.count(OLD) == 1:
        with open(path, "w", encoding="utf-8") as f:
            f.write(src.replace(OLD, NEW, 1))
        print("%s patched LR11x0::config() for old-firmware tolerance" % TAG)
    else:
        raise RuntimeError(
            "%s pattern not found in %s - RadioLib version drift? Check "
            "LR11x0::config() by hand and update this script; refusing to build "
            "an unpatched radio" % (TAG, path))
