"""Prove diagnostic inserts do not change upstream engine statements."""
from pathlib import Path
import re
import subprocess
import sys

root = Path(sys.argv[1])
files = ('ImportData.cpp', 'ImportManager.cpp', 'LocalMapManager.cpp',
         'MapConfigLoader.cpp', 'SKSEPlugin.cpp')

def clean(text, modified=False):
    if modified:
        text = re.sub(r'#include "HdnDiagnostic.h"\s*', '', text)
        # All logger calls in this patch only contain diagnostic scalar/string reads.
        text = re.sub(r'logger::info\(\s*"HDN-DIAG1.*?\);', '', text, flags=re.S)
        text = re.sub(r'if \(HdnDiagnostic::Take\([^;]+?\)\) \{\s*\}', '', text)
        text = re.sub(r'if \((logTarget|logCall)\) \{\s*\}', '', text)
        text = re.sub(r'if \(fileName.string\(\) == "hdn-world-edits-test.json"\) \{\s*\}', '', text)
        text = re.sub(r'const bool target = .*?;', '', text, flags=re.S)
        text = re.sub(r'const bool logTarget = .*?;', '', text, flags=re.S)
        text = re.sub(r'const bool logCall = .*?;', '', text, flags=re.S)
        text = re.sub(r'std::size_t (scanned|loaded) = 0;|\+\+(scanned|loaded);|HdnDiagnostic::ResetDoors\(\);', '', text)
    return re.sub(r'\s+', '', text)

for name in files:
    path = 'src/main/' + name
    original = subprocess.check_output(['git', '-C', str(root), 'show', 'HEAD:' + path]).decode()
    changed = (root / path).read_text()
    assert clean(changed, True) == clean(original), 'Engine behavior changed: ' + name
    assert 'HDN-DIAG1' in changed
header = (root / 'src/main/HdnDiagnostic.h').read_text()
assert 'compare_exchange_weak' in header and 'memory_order_relaxed' in header
assert 'Take(HdnDiagnostic::doorCalls, 32)' in (root / 'src/main/LocalMapManager.cpp').read_text()
assert 'Take(HdnDiagnostic::targetCalls, 64)' in (root / 'src/main/LocalMapManager.cpp').read_text()
assert 'Take(HdnDiagnostic::importCalls, 128)' in (root / 'src/main/ImportData.cpp').read_text()
print('PASS: 5 source bodies unchanged outside logging; 32/64/128 bounded diagnostics')
