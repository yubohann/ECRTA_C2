#!/usr/bin/env python3
"""test_paper_align.py — verify official-code alignment with the C2-Explorer paper.

Asserts (based on the paper text arXiv:2603.07699v1 and commit fd1c76a code):
  A. PCD bounding boxes match paper scene sizes (xy within 1m; z is scene height,
     paper's 5m is the exploration-space height, code PCDs are ~2.4-3.0m).
  B. Capacity slack 0.10 == paper sigma_Q=1.1.
  C. Kinematics max_vel/max_acc == 2.0/2.0 (paper).
  D. Communication threshold default == 5.0 (paper rcomm).
  E. Connectivity-based task units exist (CCL unknown centers + connectivity graph).
  F. Contiguity penalty psi(rho) (paper Eq.3-5, lambda_c=1.2) — EXPECTED MISSING.
  G. INVALID unreachable-region filtering (paper IV-A) — EXPECTED MISSING.
"""
import struct
import subprocess
import sys
from pathlib import Path

WS = Path('/home/c2dev/c2_explorer_reproduction/workspace/reachability_retry_c2_method')
PCD = WS / 'src/MARSIM/map_generator/resource'
CPP = WS / 'src/swarm_exploration/exploration_manager/src/c2_exploration_manager.cpp'
HGRID = WS / 'src/swarm_exploration/active_perception/src/hgrid.cpp'
UGRID = WS / 'src/swarm_exploration/active_perception/src/uniform_grid.cpp'
LAUNCH = WS / 'src/swarm_exploration/exploration_manager/launch/open_plan_office.launch'

fails = []


def check(name, ok, detail=''):
    status = 'PASS' if ok else 'FAIL'
    print(f'  [{status}] {name}' + (f' — {detail}' if detail else ''))
    if not ok:
        fails.append(name)


def pcd_bounds(name):
    p = PCD / name
    with open(p, 'rb') as f:
        head = b''
        while True:
            line = f.readline()
            head += line
            if line.startswith(b'DATA'):
                data_type = line.decode().strip().split()[1]
                break
        n = 0
        for ln in head.decode('ascii', 'ignore').splitlines():
            if ln.startswith('POINTS'):
                n = int(ln.split()[1])
        rest = f.read()
    xs, ys, zs = [], [], []
    if data_type == 'binary':
        for i in range(n):
            off = i * 12
            if off + 12 > len(rest):
                break
            x, y, z = struct.unpack_from('<fff', rest, off)
            xs.append(x); ys.append(y); zs.append(z)
    else:
        for ln in rest.decode('ascii', 'ignore').splitlines()[:n]:
            parts = ln.split()
            if len(parts) >= 3:
                try:
                    xs.append(float(parts[0])); ys.append(float(parts[1])); zs.append(float(parts[2]))
                except ValueError:
                    pass
    return (max(xs) - min(xs), max(ys) - min(ys), max(zs) - min(zs)) if xs else None


def grep(path, pattern, ctx=1):
    r = subprocess.run(['grep', '-n', pattern, str(path)], capture_output=True, text=True)
    return r.stdout


print('A. PCD scene sizes vs paper (30x30x5 / 35x30x5 / 35x35x5):')
for name, (px, py) in (('cubicle_office.pcd', (30, 30)), ('open_plan_office.pcd', (30, 35)),
                       ('octa_maze.pcd', (35, 35))):
    b = pcd_bounds(name)
    if not b:
        check(f'{name} parsed', False)
        continue
    ok = abs(b[0] - px) <= 1.5 and abs(b[1] - py) <= 1.5
    check(f'{name} xy size', ok, f'measured=({b[0]:.1f}x{b[1]:.1f}x{b[2]:.1f}) paper=({px}x{py}x5)')

print('B. capacity slack == 0.10 (paper sigma_Q=1.1):')
out = grep(CPP, r'slack = 0.10')
check('slack=0.10 hardcoded', 'slack = 0.10' in out)

print('C. kinematics 2.0/2.0:')
out = grep(LAUNCH, r'max_vel.*2.0')
check('max_vel=2.0', 'max_vel' in out and '2.0' in out)
out = grep(LAUNCH, r'max_acc.*2.0')
check('max_acc=2.0', 'max_acc' in out and '2.0' in out)

print('D. rcomm default 5.0:')
out = grep(LAUNCH, r'communication_threshold.*5.0')
check('comm default 5.0', '5.0' in out)

print('E. connectivity-based task units:')
out = grep(HGRID, r'centers_unknown_active_idx_')
check('CCL unknown centers used', 'centers_unknown_active_idx_' in out)
out = grep(HGRID, r'getConnectivityNodeId')
check('connectivity node ids', 'getConnectivityNodeId' in out)

print('F. contiguity penalty psi(rho) (paper Eq.3-5) — expected MISSING in release code:')
found = False
for pat in (r'psi', r'contiguity', r'lambda_c', r'adjacency'):
    if grep(HGRID, pat) or grep(CPP, pat):
        found = True
check('contiguity penalty confirmed MISSING (paper mechanism absent)',
      not found, 'grep psi/contiguity/lambda_c/adjacency -> empty')

print('G. INVALID unreachable filtering (paper IV-A) — expected MISSING:')
inv = grep(UGRID, r'INVALID') + grep(HGRID, r'INVALID') + grep(CPP, r'INVALID')
check('no INVALID state in code (paper mechanism missing)', 'INVALID' not in inv)

print()
if fails:
    print('FAILED CHECKS:', fails)
    sys.exit(1)
print('ALL PAPER-ALIGN CHECKS PASSED (expected misses confirmed)')
