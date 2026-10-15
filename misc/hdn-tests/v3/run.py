#!/usr/bin/env python3
"""Compile current native sources against narrow mocks; leave no artifacts."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[3])
args = parser.parse_args()
repo = args.repo.resolve()
source = repo / 'skyrim-platform/src/platform_se/skyrim_platform'
if not (source / 'RemoteFaceRegen.cpp').is_file():
    parser.error(f'No current v3 native source at {source}')
own = Path(__file__).resolve().parent
environment = os.environ.copy()
environment['HDN_V3_REPO'] = str(repo)
with tempfile.TemporaryDirectory(prefix='hdn-faces-v3-tests-') as directory:
    work = Path(directory)
    shutil.copytree(own / 'cases', work, dirs_exist_ok=True)
    first = ['compile-queue.py', 'compile-native.py', 'compile-ownership.py']
    remaining = sorted(path.name for path in work.glob('compile-*.py')
                       if path.name not in first)
    for script in first + remaining:
        subprocess.run([sys.executable, str(work / script)], cwd=work,
                       env=environment, check=True)
print('PASS all current-source v3 contracts; temporary build artifacts removed')
