#!/usr/bin/env python3
"""Regenerate both parts: wall_plate.stl and cradle.stl."""
import subprocess, sys, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for script in ("build_wall_plate.py", "build_cradle_from_reference.py"):
    print(f"== {script} ==")
    subprocess.run([sys.executable, script], check=True)
print("All parts built.")
