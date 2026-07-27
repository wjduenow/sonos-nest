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
_patch_esp_wifi_remote_kconfig(env)
env.AddPreAction("$PROGPATH", patch_esp32p4_linker_scripts)