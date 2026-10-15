#!/usr/bin/env python3
"""Compile actual Frida hook bodies and regenerator with a minimal Gum context."""
import hashlib
import os
from pathlib import Path
import subprocess

own = Path(__file__).resolve().parent
source_dir = Path(os.environ["HDN_V3_REPO"]) / "skyrim-platform/src/platform_se/skyrim_platform"
source = (source_dir / "FridaHooks.cpp").read_text()
digest = hashlib.sha256(source.encode()).hexdigest()
queue_start = source.index("thread_local uint32_t g_queueNiNodeActorId = 0;")
queue_end = source.index("void InstallQueueNinodeUpdateHook()", queue_start)
apply_start = source.index("struct ApplyMasksInvocationData")
apply_end = source.index("void InstallApplyMasksToRenderTargetsHook()", apply_start)
generated = source[queue_start:queue_end] + "\n" + source[apply_start:apply_end]
(own / "candidate-hooks.inc").write_text(generated)
snapshot = own / "production-snapshot"
command = [
    "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic", "-pthread",
    "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
    "-I", str(own), "-I", str(own / "mock-include"), "-I", str(snapshot),
    "-include", str(own / "MockEngine.h"), str(own / "hooks.contract.cpp"),
    str(snapshot / "RemoteFaceRegen.cpp"), "-o", str(own / "hooks.contract"),
]
subprocess.run(command, check=True)
result = subprocess.run([str(own / "hooks.contract")], text=True, capture_output=True)
if result.returncode:
    print(result.stdout, end=""); print(result.stderr, end="")
    raise SystemExit(result.returncode)
assert hashlib.sha256((source_dir / "FridaHooks.cpp").read_bytes()).hexdigest() == digest, "Hooks changed during test"
report = f"FridaHooks.cpp sha256={digest}\nextracted sha256={hashlib.sha256(generated.encode()).hexdigest()}\n" + result.stdout
(own / "hooks.resultado.txt").write_text(report)
print(report, end="")
