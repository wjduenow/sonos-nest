import os
import shutil
from pathlib import Path

from SCons.Script import Import


Import("env")


def _check_littlefs_payload(env):
    project_dir = Path(env.subst("$PROJECT_DIR"))
    data_dir = project_dir / "data"
    if not any(data_dir.glob("*.bin")):
        print(
            "fix_p4_linker.py: WARNING - no .bin files in data/; "
            "run bin/download_c6_fw.sh before buildfs"
        )


ALIASES = """
PROVIDE(_data_start = _data_start_low);
PROVIDE(_bss_start = _bss_start_low);
PROVIDE(_bss_end = _bss_end_high);
PROVIDE(_heap_start = _heap_start_low);
PROVIDE(_rtc_p4_rev3_mspi_workaround_start = ORIGIN(rev3_mspi_workaround_seg));
PROVIDE(_rtc_p4_rev3_mspi_workaround_end = ORIGIN(rev3_mspi_workaround_seg) + LENGTH(rev3_mspi_workaround_seg));
""".strip()


def _patch_esp_wifi_remote_kconfig(env):
    project_dir = env.subst("$PROJECT_DIR")
    mc_dir = os.path.join(project_dir, "managed_components", "espressif__esp_wifi_remote")
    kconfig_path = os.path.join(mc_dir, "Kconfig")

    if not os.path.isfile(kconfig_path):
        return

    with open(kconfig_path, "r") as f:
        original = f.read()

    old_block = (
        "    if ESP_WIFI_REMOTE_ENABLED\n"
        '        orsource "./Kconfig.idf_v$ESP_IDF_VERSION.in"\n'
        '        orsource "./Kconfig.rpc.in"'
    )
    new_block = (
        "    # Slave target selection must be unconditionally available so that\n"
        "    # esp_hosted can select C6 even when ESP_WIFI_REMOTE_ENABLED=n.\n"
        '    orsource "./Kconfig.idf_v$ESP_IDF_VERSION.in"\n'
        "\n"
        "    if ESP_WIFI_REMOTE_ENABLED\n"
        '        orsource "./Kconfig.rpc.in"'
    )

    patched = original.replace(old_block, new_block)
    if patched != original:
        with open(kconfig_path, "w") as f:
            f.write(patched)
        print("fix_p4_linker.py: patched esp_wifi_remote Kconfig")
    else:
        if (
            '    orsource "./Kconfig.idf_v$ESP_IDF_VERSION.in"' not in patched
            or "# Slave target selection must be unconditionally available" not in patched
        ) and old_block not in patched:
            print("fix_p4_linker.py: WARNING - could not apply esp_wifi_remote Kconfig patch")

    idf_version = os.environ.get("IDF_VERSION", "").strip().lstrip("v")
    if not idf_version:
        # Prefer the real ESP-IDF package version in mixed Arduino+IDF builds.
        pio_platform = env.PioPlatform()
        espidf_pkg_dir = pio_platform.get_package_dir("framework-espidf")
        if espidf_pkg_dir:
            ver_file = os.path.join(espidf_pkg_dir, "version.txt")
            if os.path.isfile(ver_file):
                with open(ver_file) as vf:
                    idf_version = vf.read().strip().lstrip("v")

    if not idf_version:
        fw_dir = env.subst("$FRAMEWORK_DIR")
        ver_file = os.path.join(fw_dir, "version.txt") if fw_dir else ""
        if ver_file and os.path.isfile(ver_file):
            with open(ver_file) as vf:
                idf_version = vf.read().strip().lstrip("v")

    if idf_version:
        target_kconfig = os.path.join(mc_dir, f"Kconfig.idf_v{idf_version}.in")
        if not os.path.isfile(target_kconfig):
            major_minor = ".".join(idf_version.split(".")[:2])
            candidate = None
            for fname in sorted(os.listdir(mc_dir), reverse=True):
                if fname.startswith(f"Kconfig.idf_v{major_minor}") and fname.endswith(".in"):
                    candidate = os.path.join(mc_dir, fname)
                    break
            if candidate:
                shutil.copy2(candidate, target_kconfig)
                print(
                    "fix_p4_linker.py: created "
                    f"{os.path.basename(target_kconfig)} from {os.path.basename(candidate)}"
                )
            else:
                print(f"fix_p4_linker.py: WARNING - no donor Kconfig for IDF {idf_version}")


# esp_hosted version GUARD. Read this before changing ESP_HOSTED_VERSION.
#
# custom_sdkconfig makes pioarduino rebuild the Arduino libs, so the IDF component manager resolves
# esp_hosted at BUILD time, from arduino-esp32's floating "^2.9.2", into the gitignored
# managed_components/. That copy is what links. The prebuilt one in framework-arduinoespressif32-libs
# never does, and neither does its header or dependencies.lock. The core's hostedGetHostVersion()
# still compiles against those stale package headers, so /api/config and the jukebox-c6 probe
# report the PACKAGE version, not the linked one (plans/13, 2026-09-14).
#
# This is enforcement, not a pin. pioarduino's pin mechanism (custom_component_remove + _add) is
# unusable for esp_hosted: removal rmtree's include/espressif__esp_hosted out of the SHARED package
# and strips it from pioarduino-build.py, and the Arduino core's esp32-hal-hosted.c needs those
# headers. So the build FAILS the moment the caret resolves to anything else. The C6 slave must run
# the same version: build it from managed_components/espressif__esp_hosted/slave (idf.py
# set-target esp32c6 && idf.py build, IDF 5.5.5). Arduino publishes slave images only up to 2.12.11.
# Runs in both HybridCompile passes. On the first pass of a fresh tree managed_components/ does not
# exist yet, and the child pass checks it.
ESP_HOSTED_VERSION = "2.12.13"


def _check_esp_hosted_version(env):
    manifest = Path(env.subst("$PROJECT_DIR")) / "managed_components" / "espressif__esp_hosted" / "idf_component.yml"
    if not manifest.is_file():
        print(f"p4_hosted_patch.py: esp_hosted not resolved yet; expecting {ESP_HOSTED_VERSION}")
        return
    resolved = next(
        (line.split(":", 1)[1].strip().strip("'\"") for line in manifest.read_text().splitlines()
         if line.startswith("version:")),
        "")
    if resolved != ESP_HOSTED_VERSION:
        print(
            "\n*** p4_hosted_patch.py: esp_hosted resolved to %r, but this build requires %r. ***\n"
            "*** The component manager follows arduino-esp32's floating caret, so the driver changed under\n"
            "*** you. Do NOT just bump ESP_HOSTED_VERSION: the C6 slave firmware must be rebuilt and flashed\n"
            "*** to the same version first (tools/p4_hosted_patch.py header, plans/13). ***\n"
            % (resolved or "<unreadable>", ESP_HOSTED_VERSION))
        env.Exit(1)
    print(f"p4_hosted_patch.py: esp_hosted {resolved} (matches the required version)")


def patch_esp32p4_linker_scripts(target, source, env):
    build_dir = Path(env.subst("$BUILD_DIR"))
    project_dir = Path(env.subst("$PROJECT_DIR"))

    # Linker scripts can be generated in transient locations depending on the
    # toolchain flow, so patch every discovered sections.ld candidate.
    candidates = {
        build_dir / "sections.ld",
        Path.cwd() / "sections.ld",
        project_dir / "sections.ld",
    }
    candidates.update(build_dir.rglob("sections.ld"))

    patched = 0
    for sections_path in sorted(candidates):
        if not sections_path.exists() or not sections_path.is_file():
            continue

        content = sections_path.read_text()
        updated = content

        # Some pioarduino/ESP-IDF combinations emit sections.ld entries that target
        # sram_seg while the memory script only declares split SRAM regions.
        # Remap those references to sram_low so ld does not treat them as an
        # undefined region and spill everything into tcm_idram_seg.
        if env.subst("$BOARD_MCU") == "esp32p4" and "> sram_seg" in updated:
            updated = updated.replace("> sram_seg", "> sram_low")
            updated = updated.replace("ORIGIN(sram_seg)", "ORIGIN(sram_low)")
            updated = updated.replace("LENGTH(sram_seg)", "LENGTH(sram_low)")

        if "_rtc_p4_rev3_mspi_workaround_start" not in updated:
            updated = f"{updated.rstrip()}\n\n{ALIASES}\n"

        if updated != content:
            sections_path.write_text(updated)
            patched += 1
            print(f"Patched linker script: {sections_path}")

    if patched == 0:
        print("No linker script candidates patched")


_check_littlefs_payload(env)
_check_esp_hosted_version(env)
_patch_esp_wifi_remote_kconfig(env)
env.AddPreAction("$PROGPATH", patch_esp32p4_linker_scripts)