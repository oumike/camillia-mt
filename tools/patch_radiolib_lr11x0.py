# Makes RadioLib's LR11x0::config() tolerate old LR1110 transceiver firmware.
#
# RadioLib 7.x sends DriveDiosInSleepMode (opcode 0x012A) unconditionally during
# begin(). That opcode only exists in LR11x0 transceiver firmware 0x0308 and
# later; older chips answer CMD_PERR, which RadioLib surfaces as
# RADIOLIB_ERR_SPI_CMD_INVALID (-706) and which aborts init on a radio that is
# otherwise perfectly healthy. Preproduction Elecrow ThinkNode M9 units ship
# with such firmware. (Meshtastic runs on them only because it pins an older
# RadioLib that never sends the command.)
#
# The command is an optional nicety — it suppresses DIO glitches while the chip
# sleeps — so skipping it on old firmware costs nothing that matters here.
#
# Scope: PlatformIO gives each environment its own libdeps copy, so this only
# touches the env that lists the script in extra_scripts. Nothing outside the
# m9 build sees a modified RadioLib.
#
# Idempotent, and safe to run before libdeps exist: on a fresh checkout the
# first build may run this before RadioLib has been downloaded, in which case it
# warns and the patch lands on the next build. If you see that warning, run the
# build once more.
Import("env")
import os

MARKER = "camillia-lr1110-oldfw-patch"

OLD = """  state = this->driveDiosInSleepMode(true);
  RADIOLIB_ASSERT(state);"""

NEW = """  state = this->driveDiosInSleepMode(true);
  // camillia-lr1110-oldfw-patch: LR11x0 transceiver firmware older than 0x0308
  // does not implement DriveDiosInSleepMode (0x012A) and answers CMD_PERR,
  // which would abort begin() with -706 on a healthy radio. The command only
  // suppresses DIO glitches in sleep mode, so skipping it is safe.
  if(state == RADIOLIB_ERR_SPI_CMD_INVALID) {
    RADIOLIB_DEBUG_BASIC_PRINTLN("DriveDiosInSleepMode unsupported (old LR11x0 FW), skipping");
    state = RADIOLIB_ERR_NONE;
  }
  RADIOLIB_ASSERT(state);"""

path = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"),
                    "RadioLib", "src", "modules", "LR11x0", "LR11x0.cpp")

if not os.path.isfile(path):
    print("[patch_radiolib_lr11x0] RadioLib not fetched yet: %s" % path)
    print("[patch_radiolib_lr11x0] NOT patched - run the build once more")
else:
    with open(path) as f:
        src = f.read()
    if MARKER in src:
        print("[patch_radiolib_lr11x0] already patched")
    elif OLD in src:
        with open(path, "w") as f:
            f.write(src.replace(OLD, NEW, 1))
        print("[patch_radiolib_lr11x0] patched LR11x0::config() for old-firmware tolerance")
    else:
        # Better to build an unpatched radio and see -706 again than to silently
        # believe a patch landed that did not.
        print("[patch_radiolib_lr11x0] WARNING: pattern not found - RadioLib version drift?")
        print("[patch_radiolib_lr11x0] WARNING: check LR11x0::config() by hand, NOT patched")
