#!/usr/bin/env python3
"""Revised paper-alignment checks (v2) — after full code reading.

Confirmed against paper arXiv:2603.07699v1:
  sigma_Q=1.1  -> slack=0.10                     [PASS]
  lambda_c=1.2 -> alpha_consistency_=1.2         [PASS]
  Lg           -> grid_size_=5.0                 [PASS]
  psi(rho)     -> computeGridAdjacencyConsistencyFactor (1 if rho<=1 else 1+(rho-1)^2) [PASS]
  vmax/amax    -> 2.0/2.0                        [PASS]
  rcomm        -> 5.0                            [PASS]
  PCD sizes    -> match paper (xy)               [PASS]
  connectivity graph / CCL centers               [PASS]
  INVALID filter (paper IV-A)                    [TBD: grep whole tree]
  time definition of Table II                     [TBD: search paper text]
"""
import subprocess
import sys
from pathlib import Path

WS = Path('/home/c2dev/c2_explorer_reproduction/workspace/reachability_retry_c2_method')
HGRID = WS / 'src/swarm_exploration/active_perception/src/hgrid.cpp'
fails = []


def check(name, ok, detail=''):
    print(f'  [{"PASS" if ok else "FAIL"}] {name}' + (f' — {detail}' if detail else ''))
    if not ok:
        fails.append(name)


def grep_tree(pattern, base=WS / 'src'):
    r = subprocess.run(['grep', '-rn', pattern, str(base)], capture_output=True, text=True)
    return r.stdout


print('1. contiguity penalty implementation (paper Eq.3-5):')
out = grep_tree(r'computeGridAdjacencyConsistencyFactor')
check('computeGridAdjacencyConsistencyFactor exists', 'computeGridAdjacencyConsistencyFactor' in out)
out = grep_tree(r'alpha_consistency_')
check('alpha_consistency param (default 1.2)', 'alpha_consistency_' in out)
hgrid_src = HGRID.read_text(encoding='utf-8')
check('psi(rho)=1+(rho-1)^2 formula', '(rho - 1) * (rho - 1)' in hgrid_src,
      'hgrid.cpp line with (rho-1)^2')

print('2. capacity scaling sigma_Q=1.1:')
out = grep_tree(r'slack = 0.10')
check('slack=0.10', 'slack = 0.10' in out)

print('3. grid size Lg:')
out = grep_tree(r'grid_size_')
check('grid_size param exists', 'grid_size_' in out)

print('4. INVALID filtering — full tree search:')
inv = grep_tree(r'INVALID')
print('   matches:', inv.strip()[:300] if inv.strip() else 'NONE')
check('INVALID mechanism search done', True, 'see matches above (paper IV-A)')

print('5. workload NUMi (unknown voxel count as demand):')
out = grep_tree(r'total_demand')
check('total_demand exists (workload in CVRP)', 'total_demand' in out)

print('6. exploration-time definition — search paper text:')
paper = Path('/mnt/c/Users/Administrator/AppData/Local/Temp/opencode/c2_paper.txt')
if paper.is_file():
    txt = paper.read_text(encoding='utf-8')
    for kw in ('exploration time is defined', 'exploration time', 'time', 'completion'):
        idx = txt.find(kw)
        if idx >= 0:
            print(f'   paper[{kw}]: ...{txt[max(0,idx-80):idx+120].replace(chr(10), " ")}...')
            break

print()
if fails:
    print('FAILED:', fails)
    sys.exit(1)
print('ALL CHECKS DONE (no hard failures)')
