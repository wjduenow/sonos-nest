#!/usr/bin/env python3
"""Regenerate both parts: shell.stl and lid.stl."""
import subprocess, sys, os
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for script in ("build_shell.py", "build_lid.py"):
    print(f"== {script} ==")
    subprocess.run([sys.executable, script], check=True)
print("All parts built.")
