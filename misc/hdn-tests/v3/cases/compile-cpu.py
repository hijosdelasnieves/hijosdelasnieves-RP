#!/usr/bin/env python3
"""Test the actual CPU component creator/deleter and snapshot append functions.

The copied production TintMask header is additionally instrumented with gcov so
its empty destructor is counted without replacing it with a test implementation.
TESTexture, the game allocator and BSFixedString's pool remain explicit mocks.
"""
import gzip
import hashlib
import os
import json
from pathlib import Path
import re
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
header = (source_dir / "game/classes/TintMask.h").read_text()
source_hash = hashlib.sha256(source.encode()).hexdigest()
header_hash = hashlib.sha256(header.encode()).hexdigest()

def function(signature):
    start = source.index(signature)
    _, end = balanced_body(source, start)
    return source[start:end]

share_start = source.index("struct\n{\n  // index is formID-0xff000000")
share_end = source.index("} share2;", share_start) + len("} share2;")
extracted = ["namespace TESModPlatform {\n" + source[share_start:share_end], "namespace {"]
for signature in ["bool DynamicFaceId(", "bool ValidFaceOwner(", "void RetireFaceOwner(",
                  "std::shared_ptr<::TintMask> CreateOwnedFaceTint(", "void PushOwnedFaceTint("]:
    extracted.append(function(signature))
extracted.append("}\n}")
for signature in ["std::shared_ptr<RE::BSTArray<RE::TintMask*>> TESModPlatform::GetTintsFor(",
                  "void TESModPlatform::ClearRemoteFaceState()", "void TESModPlatform::ClearTintMasks("]:
    extracted.append(function(signature))
generated = "\n\n".join(extracted) + "\n"
(own / "candidate-cpu.inc").write_text(generated)
snapshot = own / "production-snapshot"
(snapshot / "TintMask.h").write_text(header)
for stale in own.glob("cpu.contract*.gcda"):
    stale.unlink()
command = [
    "g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-pedantic",
    "-fpermissive",  # Preserve the real MSVC-compatible anonymous-union header.
    "--coverage", "-fno-inline", "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
    "-I", str(own), "-I", str(own / "mock-include"), "-isystem", str(snapshot),
    str(own / "cpu.contract.cpp"), "-o", str(own / "cpu.contract"),
]
subprocess.run(command, check=True)
result = subprocess.run([str(own / "cpu.contract")], text=True, capture_output=True)
if result.returncode:
    print(result.stdout, end=""); print(result.stderr, end="")
    raise SystemExit(result.returncode)
expected = int(re.search(r"expected_mask_ctor_dtor=(\d+)", result.stdout)[1])
gcno = next(own.glob("cpu.contract*.gcno"))
subprocess.run(["gcov", "--json-format", str(gcno)], check=True, cwd=own, capture_output=True)
coverage_file = next(own.glob("cpu.contract*.gcov.json.gz"))
coverage = json.loads(gzip.decompress(coverage_file.read_bytes()))
functions = [function for file in coverage["files"] if file["file"].endswith("/TintMask.h")
             for function in file["functions"]]
counts = {entry["demangled_name"]: entry["execution_count"] for entry in functions}
assert counts["TintMask::TintMask()"] == expected, counts
assert counts["TintMask::~TintMask()"] == expected, counts
assert hashlib.sha256((source_dir / "PapyrusTESModPlatform.cpp").read_bytes()).hexdigest() == source_hash
assert hashlib.sha256((source_dir / "game/classes/TintMask.h").read_bytes()).hexdigest() == header_hash
report = f"PapyrusTESModPlatform.cpp sha256={source_hash}\nTintMask.h sha256={header_hash}\n"
report += result.stdout
report += f"PASS gcov: production TintMask constructor/destructor each executed exactly {expected} times\n"
report += "LIMIT: TESTexture ABI/vtable, actual game allocator and engine BSFixedString string pool are mocked.\n"
report += "BUILD: production TintMask header is byte-identical; GCC -fpermissive accepts its MSVC anonymous-union extension.\n"
(own / "cpu.resultado.txt").write_text(report)
print(report, end="")
