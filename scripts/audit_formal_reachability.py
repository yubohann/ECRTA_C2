#!/usr/bin/env python3
import json
import math
import statistics
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path('/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal')
OUT = Path('/home/c2dev/c2_explorer_reproduction/PRCT_C2_REACHABILITY_AUDIT.json')

def pct(values, fraction):
    if not values:
        return None
    ordered = sorted(values)
    idx = (len(ordered) - 1) * fraction
    lo = math.floor(idx)
    hi = math.ceil(idx)
    if lo == hi:
        return ordered[lo]
    return ordered[lo] + (ordered[hi] - ordered[lo]) * (idx - lo)

def run_key(path):
    parts = path.parts
    idx = parts.index('formal')
    return path.parent.name, parts[idx+1], parts[idx+2]

by_run = defaultdict(lambda: {
    'response_count': 0, 'success_count': 0, 'failure_count': 0,
    'terminations': Counter(), 'durations': [], 'state_ages': [], 'queries': 0,
    'shadows': 0,
})

for path in sorted(ROOT.rglob('telemetry_drone_*.jsonl')):
    name = path.parent.name
    if not (name.startswith('b2_') or name.startswith('b3_')):
        continue
    scene, uav, _ = run_key(path)
    rec = by_run[(scene, uav, name)]
    for line in path.open(encoding='utf-8', errors='replace'):
        line = line.strip()
        if not line:
            continue
        try:
            item = json.loads(line)
        except Exception:
            continue
        event = item.get('event', '')
        if event == 'peer_local_map_reachability_response':
            rec['response_count'] += 1
            rec['durations'].append(item.get('duration_wall_s'))
            rec['state_ages'].append(item.get('peer_state_age_s'))
            if item.get('success'):
                rec['success_count'] += 1
            else:
                rec['failure_count'] += 1
            rec['terminations'][item.get('termination', 'unknown')] += 1
        elif event in ('peer_local_map_reachability_query', 'peer_local_map_reachability_probe'):
            rec['queries'] += 1
        elif event in ('peer_reachability_shadow_probe',):
            rec['shadows'] += 1

rows = []
for (scene, uav, name), rec in sorted(by_run.items()):
    total = rec['response_count']
    rows.append({
        'scene': scene,
        'uav_num': uav,
        'run_id': name,
        'method': name.split('_', 1)[0],
        'response_count': total,
        'success_count': rec['success_count'],
        'failure_count': rec['failure_count'],
        'success_rate': rec['success_count'] / total if total else None,
        'termination_counts': dict(rec['terminations']),
        'duration_p50_wall_s': pct(rec['durations'], 0.5),
        'duration_p95_wall_s': pct(rec['durations'], 0.95),
        'state_age_p50_s': pct(rec['state_ages'], 0.5),
        'state_age_p95_s': pct(rec['state_ages'], 0.95),
        'query_count': rec['queries'],
        'shadow_probe_count': rec['shadows'],
    })

summary = {}
for method in ('b2', 'b3'):
    rs = [r for r in rows if r['method'] == method]
    total_resp = sum(r['response_count'] for r in rs)
    success = sum(r['success_count'] for r in rs)
    summary[method] = {
        'runs_with_telemetry': len(rs),
        'response_count': total_resp,
        'success_count': success,
        'failure_count': sum(r['failure_count'] for r in rs),
        'success_rate': success / total_resp if total_resp else None,
        'query_count': sum(r['query_count'] for r in rs),
        'shadow_probe_count': sum(r['shadow_probe_count'] for r in rs),
    }

payload = {'rows': rows, 'summary': summary}
OUT.write_text(json.dumps(payload, indent=2, ensure_ascii=False), encoding='utf-8')
print('WRITTEN', OUT)
for method in ('b2', 'b3'):
    print(method, summary[method])
for r in rows:
    print(r['scene'], r['uav_num'], r['run_id'], 'resp', r['response_count'], 'succ', r['success_count'], 'fail', r['failure_count'], 'q', r['query_count'])
