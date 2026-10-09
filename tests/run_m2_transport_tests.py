#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build_dir',type=Path);a=p.parse_args()
with tempfile.TemporaryDirectory() as tmp:
    for active in (0,1):
        binary=Path(tmp)/('parallel-'+str(active))
        subprocess.run([os.environ.get('CC','cc'),'-std=c11','-Wall','-Wextra','-Werror',
                        '-Wconversion','-Wshadow','-g','-fno-omit-frame-pointer',
                        '-fsanitize=address,undefined','-DFUNKOTTO_M2_ACTIVE='+str(active),
                        '-I'+str(ROOT/'tests/parallel_fakes'),'-I'+str(ROOT/'firmware/include'),
                        '-I'+str(ROOT/'protocol'),'-I'+str(a.build_dir.resolve()),
                        str(ROOT/'tests/test_m2_parallel.c'),str(ROOT/'firmware/src/board.c'),
                        str(ROOT/'protocol/m2.c'),'-o',str(binary)],check=True)
        subprocess.run([str(binary)],check=True)
subprocess.run(['python3',str(ROOT/'tests/check_m2_pio.py'),str(a.build_dir/'parallel.pio.h')],check=True)
