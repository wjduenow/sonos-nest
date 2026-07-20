# PlatformIO pre-script: inject FW_VERSION from `git describe` into every build.
#
# Wired in via `extra_scripts = pre:tools/git_version.py` in [env] (platformio.ini), so all envs
# get it. The firmware reports this string to the sonos-portal dashboard (registrationJson()),
# and it's the one place a build's provenance is visible over the network. `--always` falls back
# to the short commit hash when there are no tags; `--dirty` marks an uncommitted working tree.
import subprocess

Import("env")  # noqa: F821 — provided by PlatformIO's SCons environment

try:
    rev = (
        subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            cwd=env["PROJECT_DIR"],
            stderr=subprocess.DEVNULL,
        )
        .decode()
        .strip()
    )
except Exception:
    rev = "unknown"

# StringifyMacro wraps + escapes it into a C string literal, so FW_VERSION arrives as "v1.2-3-gabc".
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(rev))])
print("[git_version] FW_VERSION = %s" % rev)
