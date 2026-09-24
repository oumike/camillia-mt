from pathlib import Path

Import("env")  # noqa: F821 - injected by PlatformIO/SCons

from platformio.project.config import ProjectConfig


compiler_dirs = []
package_dir = env.PioPlatform().get_package_dir("toolchain-riscv32-esp")
if package_dir:
    package_path = Path(package_dir)
    compiler_dirs.extend((
        package_path / "bin",
        package_path / "riscv32-esp-elf" / "bin",
    ))

core_dir = Path(ProjectConfig.get_instance().get("platformio", "core_dir"))
tool_cache = core_dir / "tools" / "toolchain-riscv32-esp"
compiler_dirs.extend((
    tool_cache / "bin",
    tool_cache / "riscv32-esp-elf" / "bin",
))

for compiler_dir in compiler_dirs:
    if compiler_dir.joinpath("riscv32-esp-elf-g++").is_file():
        env.PrependENVPath("PATH", str(compiler_dir))
        print(f"[p4_toolchain_path] compiler={compiler_dir}")
        break
else:
    raise RuntimeError(
        "riscv32-esp-elf-g++ not found in the installed package or "
        f"pioarduino tool cache under {core_dir}"
    )