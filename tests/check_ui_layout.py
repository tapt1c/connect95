"""Exercise the same portable layout functions used by the Win32 windows."""
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
for name in ("sidebar_layout", "main_layout"):
    executable = root / ("build/" + name + "-test")
    subprocess.run([os.environ.get("HOST_CC", "cc"), "-std=c99", "-Wall", "-Wextra", "-Werror", "-Isrc",
                    "tests/" + name + "_test.c", "-o", str(executable)], cwd=root, check=True)
    subprocess.run([str(executable)], cwd=root, check=True)
