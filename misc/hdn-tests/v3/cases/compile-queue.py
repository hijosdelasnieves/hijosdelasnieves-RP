#!/usr/bin/env python3
"""Compile the real worktree queue; all artifacts remain in this tests folder."""
import hashlib
import os
from pathlib import Path
import subprocess

own = Path(__file__).resolve().parent
repo = Path(os.environ["HDN_V3_REPO"])
source = repo / "skyrim-platform/src/platform_se/skyrim_platform"
header = source / "RemoteFaceRegenQueue.h"
digest = hashlib.sha256(header.read_bytes()).hexdigest()
command = [
    "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic",
    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
    "-I", str(source), str(own / "regen-queue.contract.cpp"), "-o", str(own / "queue.contract"),
]
subprocess.run(command, check=True)
result = subprocess.run([str(own / "queue.contract")], check=True, text=True, capture_output=True)
assert hashlib.sha256(header.read_bytes()).hexdigest() == digest, "Queue changed during test"
report = f"RemoteFaceRegenQueue.h sha256={digest}\n" + result.stdout
(own / "queue.resultado.txt").write_text(report)
print(report, end="")
