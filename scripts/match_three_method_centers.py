import json
import math
import sys
from pathlib import Path

ROOT = Path('/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal_three_method/open_plan_office/uav_3')
BATCH_PREFIX = 'three_candidate_seed2_pilot_v3b'


def load_jsonl(path):
    out = []
    if not path.exists():
        return out
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        line = line.strip()
        if line:
            try:
                out.append(json.loads(line))
            except Exception:
                pass
    return out


def dist(a, b):
    return math.sqrt((a[0]-b[0])**2 + (a[1]-b[1])**2 + (a[2]-b[2])**2)


def main():
    methods = sys.argv[1:] or ['reach', 'svr', 'steer', 'b0']
    for method in methods:
        prefix = f'{BATCH_PREFIX}_{method}_open_plan_office_uav3_run_001'
        d = ROOT / prefix
        if not d.exists():
            print(f'== {method}: missing {d.name}')
            continue
        failures = load_jsonl(d / 'failures.jsonl')
        tasks = []
        for telemetry in sorted(d.glob('telemetry_drone_*.jsonl')):
            for ev in load_jsonl(telemetry):
                if ev.get('event') == 'allocation_task':
                    tasks.append(ev)
        print(f'== {method}: failures={len(failures)} tasks={len(tasks)}')
        if not failures or not tasks:
            continue
        for i, f in enumerate(failures[:6]):
            fp = (f.get('goal_x', 0), f.get('goal_y', 0), f.get('goal_z', 0))
            best = min(tasks, key=lambda t: dist(fp, (t.get('task_x', 0), t.get('task_y', 0), t.get('task_z', 0))))
            bd = dist(fp, (best.get('task_x', 0), best.get('task_y', 0), best.get('task_z', 0)))
            print(f'  failure {i}: fid={f.get("frontier_id")} goal={fp} nearest_task={best.get("task_center_id")} '
                  f'grid={best.get("task_grid_id")} d={bd:.3f} task_pos={best.get("task_x"), best.get("task_y"), best.get("task_z")}')


if __name__ == '__main__':
    main()
