#!/usr/bin/env python3
"""Exercise exact candidate owner functions and SKSE callbacks with deferred tasks."""
import hashlib
import os
from pathlib import Path
import subprocess


def balanced_body(source, start):
    opening = source.index("{", start)
    depth, state, i = 0, "code", opening
    while i < len(source):
        c, following = source[i], source[i:i + 2]
        if state == "line":
            if c == "\n": state = "code"
        elif state == "block":
            if following == "*/": state, i = "code", i + 1
        elif state in ('"', "'"):
            if c == "\\": i += 1
            elif c == state: state = "code"
        elif following in ("//", "/*"):
            state = "line" if following == "//" else "block"
            i += 1
        elif c in ('"', "'"): state = c
        elif c == "{": depth += 1
        elif c == "}":
            depth -= 1
            if not depth: return source[opening:i + 1], i + 1
        i += 1
    raise AssertionError("Unclosed function")


own = Path(__file__).resolve().parent
source_dir = Path(os.environ["HDN_V3_REPO"]) / "skyrim-platform/src/platform_se/skyrim_platform"
source = (source_dir / "PapyrusTESModPlatform.cpp").read_text()
source_hash = hashlib.sha256(source.encode()).hexdigest()

def function(signature):
    start = source.index(signature)
    _, end = balanced_body(source, start)
    return source[start:end]

share_start = source.index("struct\n{\n  // index is formID-0xff000000")
share_end = source.index("} share2;", share_start) + len("} share2;")
extracted = ["namespace TESModPlatform {\n" + source[share_start:share_end], "namespace {"]
for signature in ["bool DynamicFaceId(", "bool FaceRuntimeSupported()", "bool ValidFaceOwner(", "void RetireFaceOwner("]:
    extracted.append(function(signature))
extracted.append("}\n}")
for signature in [
    "std::shared_ptr<RE::BSTArray<RE::TintMask*>> TESModPlatform::GetTintsFor(",
    "void TESModPlatform::QueueRemoteFaceTintRefresh(",
    "void TESModPlatform::ClearRemoteFaceState()",
    "void TESModPlatform::ClearTintMasks(",
]:
    extracted.append(function(signature))
job_start = source.rfind("\nnamespace", 0, source.index("struct RemoteFaceJob"))
previous_namespace = source.rfind("\nnamespace", 0, job_start)
if source[previous_namespace:job_start].strip() == "namespace TESModPlatform {":
    job_start = previous_namespace
job_end = source.index("void TESModPlatform::UpdateRemoteFaceTints()", job_start)
extracted.append(source[job_start:job_end])
extracted.append(function("void TESModPlatform::UpdateRemoteFaceTints()"))
load_start = source.index("  RE::BSEventNotifyControl ProcessEvent(", source.index("class LoadGameEvent"))
load_body, _ = balanced_body(source, load_start)
extracted.append("RE::BSEventNotifyControl TESModPlatform::FireLoadEvent() " + load_body)
delete_start = source.index("  RE::BSEventNotifyControl ProcessEvent(", source.index("class RemoteFaceDeleteEvent"))
delete_body, _ = balanced_body(source, delete_start)
extracted.append("RE::BSEventNotifyControl TESModPlatform::FireDeleteEvent(const RE::TESFormDeleteEvent* event) " + delete_body)
generated = "\n\n".join(extracted) + "\n"
(own / "candidate-integration.inc").write_text(generated)

snapshot = own / "production-snapshot"
command = [
    "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pedantic",
    "-pthread", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
    "-I", str(own), "-I", str(own / "mock-include"), "-I", str(snapshot),
    "-include", str(own / "MockEngine.h"), str(own / "ownership.contract.cpp"),
    str(snapshot / "RemoteFaceRegen.cpp"), "-o", str(own / "ownership.contract"),
]
subprocess.run(command, check=True)
result = subprocess.run([str(own / "ownership.contract")], text=True, capture_output=True)
if result.returncode:
    print(result.stdout, end="")
    print(result.stderr, end="")
    raise SystemExit(result.returncode)
assert hashlib.sha256((source_dir / "PapyrusTESModPlatform.cpp").read_bytes()).hexdigest() == source_hash, "Integration changed during test"
report = f"PapyrusTESModPlatform.cpp sha256={source_hash}\nextracted sha256={hashlib.sha256(generated.encode()).hexdigest()}\n" + result.stdout
(own / "ownership.resultado.txt").write_text(report)
print(report, end="")
