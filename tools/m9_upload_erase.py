Import("env")

from SCons.Script import COMMAND_LINE_TARGETS


if "upload_erase" in COMMAND_LINE_TARGETS:
    upload_flags = list(env.get("UPLOADERFLAGS", []))
    try:
        write_flash_index = upload_flags.index("write_flash")
    except ValueError as exc:
        raise RuntimeError("M9 upload_erase requires esptool write_flash") from exc

    upload_flags.insert(write_flash_index + 1, "--erase-all")
    env.Replace(UPLOADERFLAGS=upload_flags)

env.AddCustomTarget(
    name="upload_erase",
    dependencies="upload",
    actions=None,
    title="Erase Flash and Upload",
    description="Erase and upload the M9 in one esptool session",
)