#!/usr/bin/env python3
"""Emit the firmware manifest.json for a Release (see plans/06-scalable-ota.md § Manifest schema).

Usage:  build_manifest.py <version> <base_url> <bins_dir>  > manifest.json

  <version>   the release tag, e.g. "v0.6.0" — becomes manifest.version and is what a device
              string-compares against its own FW_VERSION to decide "update available".
  <base_url>  the Release asset download prefix, e.g.
              https://github.com/<owner>/<repo>/releases/download/<version>
              Each unit's `url` is <base_url>/<bin>.
  <bins_dir>  a directory holding the built binaries named firmware-<unit>.bin. The unit id is
              parsed straight from the filename (nest/sleep/button — the ids registrationJson()
              reports), so this stays in sync with whatever the CI matrix built without a
              hardcoded unit list here.

Advisory-only fields (sha256, size) let a later firmware add image verification without a schema
change; HTTPUpdate already validates the ESP image header on its own in v1.
"""
import hashlib
import json
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 4:
        sys.stderr.write("usage: build_manifest.py <version> <base_url> <bins_dir>\n")
        return 2
    version, base_url, bins_dir = sys.argv[1], sys.argv[2].rstrip("/"), Path(sys.argv[3])

    units = {}
    for bin_path in sorted(bins_dir.glob("firmware-*.bin")):
        unit = bin_path.stem[len("firmware-"):]  # firmware-nest.bin -> "nest"
        data = bin_path.read_bytes()
        units[unit] = {
            "bin": bin_path.name,
            "url": f"{base_url}/{bin_path.name}",
            "sha256": hashlib.sha256(data).hexdigest(),
            "size": len(data),
        }

    if not units:
        sys.stderr.write(f"error: no firmware-*.bin found in {bins_dir}\n")
        return 1

    json.dump({"version": version, "units": units}, sys.stdout, indent=2)
    sys.stdout.write("\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
