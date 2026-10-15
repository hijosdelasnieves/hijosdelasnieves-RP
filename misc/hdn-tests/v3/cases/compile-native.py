#!/usr/bin/env python3
"""Build production scope/regenerator with identity/entry-point mocks only."""
import hashlib
import os
from pathlib import Path
import subprocess

own = Path(__file__).resolve().parent
source = Path(os.environ["HDN_V3_REPO"]) / "skyrim-platform/src/platform_se/skyrim_platform"
mock_include = own / "mock-include"
for include in [
    "RE/B/BSTArray.h", "RE/T/TESNPC.h", "RE/A/Actor.h", "RE/B/BGSHeadPart.h",
    "RE/B/BSFaceGenManager.h", "RE/B/BSGeometry.h", "RE/B/BSLightingShaderMaterialFacegen.h",
    "RE/B/BSLightingShaderProperty.h", "RE/N/NiRTTI.h", "REL/Relocation.h",
]:
    output = mock_include / include
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('#pragma once\n#include "MockEngine.h"\n')

names = ["RemoteFaceRegenQueue.h", "ScopedFaceTintContext.h", "RemoteFaceRegen.h", "RemoteFaceRegen.cpp"]
hashes = {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in names}
snapshot = own / "production-snapshot"
snapshot.mkdir(exist_ok=True)
for name in names:
    (snapshot / name).write_bytes((source / name).read_bytes())
(snapshot / "ConsoleApi.h").write_text('''#pragma once
#include <string>
#include <string_view>
namespace fmt {
template <class... Args> std::string format(const char* pattern, Args...) { return pattern; }
}
namespace ConsoleApi {
inline void WriteNativeDiagnosticLine(std::string_view line) noexcept { Test::diagnosticLines.emplace_back(line); }
}
''')
common = ["g++", "-std=c++20", "-Wall", "-Wextra", "-Werror", "-pedantic", "-pthread",
          "-fsanitize=address,undefined", "-fno-omit-frame-pointer", "-g",
          "-I", str(own), "-I", str(mock_include), "-I", str(snapshot)]
report = "\n".join(f"{name} sha256={digest}" for name, digest in hashes.items()) + "\n"
for name, extra in [("scope", []), ("regenerator", [str(snapshot / "RemoteFaceRegen.cpp")])]:
    executable = own / f"{name}.contract"
    command = common + ["-include", str(own / "MockEngine.h"),
                        str(own / f"{name}.contract.cpp"), *extra, "-o", str(executable)]
    subprocess.run(command, check=True)
    result = subprocess.run([str(executable)], check=True, text=True, capture_output=True)
    report += result.stdout
assert hashes == {name: hashlib.sha256((source / name).read_bytes()).hexdigest() for name in names}, "Candidate changed during test"
(own / "native.resultado.txt").write_text(report)
print(report, end="")
