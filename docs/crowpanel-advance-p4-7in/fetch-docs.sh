#!/usr/bin/env bash
# Download the Elecrow CrowPanel Advance 7" ESP32-P4 (DHE04107D) manuals.
# They are gitignored (~78 MB total) — see README.md.
set -euo pipefail
cd "$(dirname "$0")"
base="https://www.elecrow.com/download/product/DHE04107D"
for f in \
  "User_Manual(HMI_Advance_ESP32-P4).pdf" \
  "Arduino_Lessons_for_CrowPanel_Advanced_7inch_ESP32-P4_HMI.pdf" \
  "Advance_HMI_P4_7inch_Course.pdf" \
  "esp32-p4_datasheet_en.pdf" ; do
  if [ -s "$f" ]; then echo "have  $f"; else echo "fetch $f"; curl -fsSL -o "$f" "$base/$f"; fi
done
echo "done."
