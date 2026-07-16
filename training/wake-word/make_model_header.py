#!/usr/bin/env python3
"""Vendor a trained .tflite into the firmware as a C header.

    python3 make_model_header.py <name> [--desc "one-line provenance"]

Reads  src/boards/es3c28p/models/<name>.tflite
Writes src/boards/es3c28p/models/<name>_model.h  ->  const unsigned char <name>_model[]

`alignas(16)` is load-bearing: TFLM's flatbuffer accessors assume an aligned buffer, and an
unaligned model fails at GetModel()/version-check time on Xtensa rather than obviously.

Verify against an already-vendored model before trusting a change here:
    python3 make_model_header.py kinder_bedtime && git diff --exit-code \
        src/boards/es3c28p/models/kinder_bedtime_model.h   # must be byte-identical
"""
import argparse, pathlib, sys

MODELS = pathlib.Path(__file__).resolve().parents[2] / "src/boards/es3c28p/models"
DEFAULT_DESC = ("custom microWakeWord model trained locally\n"
                "// (Piper TTS positives + cross-phrase hard negatives, 10k steps).")

def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("name", help="model basename, e.g. kinder_rise")
    ap.add_argument("--desc", default=DEFAULT_DESC, help="provenance line for the header comment")
    a = ap.parse_args()

    src = MODELS / f"{a.name}.tflite"
    if not src.is_file():
        print(f"error: {src} not found — copy the trained .tflite here first", file=sys.stderr)
        return 1
    blob = src.read_bytes()

    lines = [f"// Auto-generated from {a.name}.tflite — {a.desc}",
             f"// Regenerate: python3 training/wake-word/make_model_header.py {a.name}",
             "#pragma once",
             "#include <cstdint>",
             "",
             f"alignas(16) const unsigned char {a.name}_model[] = {{"]
    for i in range(0, len(blob), 16):
        lines.append("  " + ",".join(f"0x{b:02x}" for b in blob[i:i + 16]) + ",")
    lines += ["};", f"const unsigned int {a.name}_model_len = {len(blob)};", ""]

    out = MODELS / f"{a.name}_model.h"
    out.write_text("\n".join(lines))
    print(f"wrote {out} ({len(blob)} bytes)")
    return 0

if __name__ == "__main__":
    sys.exit(main())
