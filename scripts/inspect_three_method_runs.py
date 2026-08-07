import argparse
import collections
import json
import sys
from pathlib import Path

LOG_BASE = Path('/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal_three_method')
DEFAULT_SCENE = 'open_plan_office'
DEFAULT_UAV = 3
DEFAULT_BATCH = 'candidate_seed2_pilot_v3b'
DEFAULT_RUN = '001'
METHODS = ['b0', 'b1', 'reach', 'svr', 'steer']


def load_jsonl(path):
    out = []
    if not path.exists():
        return out
    for line in path.read_text(encoding='utf-8', errors='replace').splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            out.append(json.loads(line))
        except Exception:
            continue
    return out


def key_fields(ev):
    return (ev.get('frontier_id', -1), (ev.get('goal_x'), ev.get('goal_y'), ev.get('goal_z')))


def summarize_method(root, batch_prefix, run_index, scene, uav, method):
    run_id = f'{batch_prefix}_{method}_{scene}_uav{uav}_run_{run_index}'
    d = root / run_id
    print(f'===== {method}: {d.name} =====')
    if not d.exists():
        print('  MISSING RUN DIR')
        return
    manifest = d / 'run_manifest.txt'
    if manifest.exists():
        wanted = ('method_mode', 'prct_enable_retry_suppression', 'prct_repeat_threshold',
                  'prct_cooldown_s', 'prct_enable_peer_takeover', 'c3_enable_marginal_gate',
                  'reach_risk_weight', 'reach_risk_penalty', 'reach_center_match_radius_m',
                  'steer_goal_min_hold_s', 'steer_switch_margin', 'steer_load_bias',
                  'svr_reallocation_cost_m', 'svr_solver_cost_s', 'svr_reuse_match_radius_m',
                  'lkh_seed', 'duration_s', 'communication_threshold_m', 'ready',
                  'prct_check_failed', 'c3_check_failed', 'method_check_failed',
                  'communication_threshold_check_failed', 'finish_count_pre_shutdown')
        for line in manifest.read_text(encoding='utf-8').splitlines():
            if any(line.startswith(w + '=') for w in wanted):
                print('  ' + line)
    failures = load_jsonl(d / 'failures.jsonl')
    print(f'  failures.jsonl: {len(failures)}')
    if failures:
        counter = collections.Counter(key_fields(e) for e in failures)
        for (fid, goal), n in counter.most_common(5):
            print(f'    frontier={fid} goal={goal} count={n}')
    method_events = load_jsonl(d / 'method_events.jsonl')
    command_events = load_jsonl(d / 'command_events.jsonl')
    task_events = load_jsonl(d / 'task_events.jsonl')
    ev_count = collections.Counter(e.get('event') for e in method_events)
    cmd_count = collections.Counter(e.get('event') for e in command_events)
    print(f'  method_events.jsonl: {len(method_events)} -> {dict(ev_count)}')
    print(f'  command_events.jsonl: {len(command_events)} -> {dict(cmd_count)}')
    print(f'  task_events.jsonl: {len(task_events)}')

    reach_alloc = [e for e in method_events if e.get('event') == 'reach_allocation_cost_adjustment']
    reach_local = [e for e in method_events if e.get('event') == 'reach_cost_adjustment']
    for name, evs in (('reach_allocation_cost_adjustment', reach_alloc), ('reach_cost_adjustment', reach_local)):
        if evs:
            links = sum(e.get('risk_center_links', 0) for e in evs if isinstance(e.get('risk_center_links'), int))
            edges = sum(e.get('risk_adjusted_edges', 0) for e in evs if isinstance(e.get('risk_adjusted_edges'), int))
            unmatched = sum(e.get('unmatched_risk_frontiers', 0) for e in evs if isinstance(e.get('unmatched_risk_frontiers'), int))
            frontier_counts = [e.get('frontier_count') for e in evs if isinstance(e.get('frontier_count'), int)]
            print(f'  {name}: events={len(evs)} risk_center_links_sum={links} risk_adjusted_edges_sum={edges} unmatched_sum={unmatched} frontier_counts={frontier_counts[:5]}')
            if evs:
                print('    sample: ' + json.dumps(evs[0], ensure_ascii=False)[:800])

    svr_gates = [e for e in method_events if e.get('event') == 'svr_reallocation_gate']
    if svr_gates:
        reuse_reasons = collections.Counter(e.get('reuse_reason') for e in svr_gates)
        overlap = [e.get('overlap_matched') for e in svr_gates if isinstance(e.get('overlap_matched'), int)]
        print(f'  svr_reallocation_gate: events={len(svr_gates)} reuse_reasons={dict(reuse_reasons)} overlap_matched={overlap[:10]}')
        print('    sample: ' + json.dumps(svr_gates[0], ensure_ascii=False)[:800])

    goal_switches = [e for e in command_events if e.get('event') == 'goal_switch']
    steer_rejects = [e for e in command_events if e.get('event') == 'steer_switch_margin_rejected']
    no_alt = [e for e in command_events if e.get('event') == 'prct_eviction_marginal_gate_no_alternative']
    if goal_switches or steer_rejects or no_alt:
        print(f'  steer: goal_switch={len(goal_switches)} margin_rejected={len(steer_rejects)} no_alternative={len(no_alt)}')
        if goal_switches:
            print('    sample: ' + json.dumps(goal_switches[0], ensure_ascii=False)[:800])
        if steer_rejects:
            print('    sample: ' + json.dumps(steer_rejects[0], ensure_ascii=False)[:800])

    summary_path = d / 'telemetry_summary.json'
    if summary_path.exists():
        data = json.loads(summary_path.read_text(encoding='utf-8'))
        interesting = {k: data.get(k) for k in ('status', 'finish_drone_ids', 'local_finish_makespan_wall_s',
                                                'frontier_count', 'trajectory_failures', 'lkh_calls', 'lkh_failures',
                                                'a_star_failures', 'plan_events')}
        print(f'  summary: {json.dumps(interesting, ensure_ascii=False)}')
    print()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--scene', default=DEFAULT_SCENE)
    parser.add_argument('--uav', type=int, default=DEFAULT_UAV)
    parser.add_argument('--batch', default=DEFAULT_BATCH)
    parser.add_argument('--run', default=DEFAULT_RUN)
    parser.add_argument('methods', nargs='*', default=METHODS)
    args = parser.parse_args()
    root = LOG_BASE / args.scene / f'uav_{args.uav}'
    batch_prefix = f'three_{args.batch}'
    for method in (args.methods or METHODS):
        summarize_method(root, batch_prefix, args.run, args.scene, args.uav, method)


if __name__ == '__main__':
    main()

