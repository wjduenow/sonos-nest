#!/usr/bin/env python3
"""Regenerate all printed parts: shell.stl, face.stl, knob_cap.stl."""
import subprocess, sys, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for script in ("build_shell.py", "build_face.py", "build_knob_cap.py"):
    print(f"== {script} ==")
    subprocess.run([sys.executable, script], check=True)
print("All parts built.")
