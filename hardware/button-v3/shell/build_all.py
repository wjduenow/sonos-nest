"""Build both parts. conda run -n img23d python build_all.py"""
import os, subprocess, sys
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for script in ("build_shell.py", "build_carrier.py", "build_lid.py"):
    print(f"== {script}")
    subprocess.run([sys.executable, script], check=True)
