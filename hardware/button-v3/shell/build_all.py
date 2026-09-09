"""Build all three parts. conda run -n img23d python build_all.py"""
import os, subprocess, sys
os.chdir(os.path.dirname(os.path.abspath(__file__)))
for script in ("build_body.py", "build_bezel.py", "build_back.py"):
    print(f"== {script}")
    subprocess.run([sys.executable, script], check=True)
