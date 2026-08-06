#!/usr/bin/env python3
import json
from collections import defaultdict
from pathlib import Path

root = Path('/home/c2dev/c2_explorer_reproduction')
data = json.loads((root / 'PRCT_C2_STATS.json').read_text(encoding='utf-8'))
rows = data['rows']
print('ROWS', len(rows))
print()
print('CONFIG | METHOD | N | FINISH | FINISH_RATE | RMST | ASTAR | TRAJ | TAKEOVER_SENT | TAKEOVER_EXEC | WAIT_S')
for scene in ['open_plan_office', 'cubicle_office', 'octa_maze']:
    for uav in [3, 4]:
        for method in ['b0','b1','b2','b3']:
            rs = [r for r in rows if r['scene']==scene and r['uav_num']==uav and r['method']==method]
            if not rs:
                continue
            n=len(rs)
            allfin=sum(1 for r in rs if r['finish_count']>=r['uav_num'])
            fin_total=sum(r['finish_count'] or 0 for r in rs)
            import math
            effs=[(r['makespan_s_proxy'] if r['makespan_s_proxy'] is not None and r['finish_count']>=r['uav_num'] else 180.0) for r in rs]
            rm=math.sqrt(sum(x*x for x in effs)/len(effs))
            astar=sum(r['astar_open_set_exhausted_count'] or 0 for r in rs)
            traj=sum(r['trajectory_plan_failure_count'] or 0 for r in rs)
            sent=sum(r['takeover_sent'] or 0 for r in rs)
            exe=sum(r['takeover_executed'] or 0 for r in rs)
            wait=sum(r['wait_duration_total_wall_s'] or 0 for r in rs)
            print(f"{scene} | {uav} | {method} | {n} | {allfin} | {fin_total} | {rm if rm is not None else 'NA'} | {astar} | {traj} | {sent} | {exe} | {wait:.3f}")
print()
print('B3 AUDIT TOTALS')
sent=sum(r['takeover_sent'] or 0 for r in rows if r['method']=='b3')
recv=sum(r['takeover_received'] or 0 for r in rows if r['method']=='b3')
exe=sum(r['takeover_executed'] or 0 for r in rows if r['method']=='b3')
rej=sum(r['receipt_rejected'] or 0 for r in rows if r['method']=='b3')
ab=sum(r['receipt_aborted'] or 0 for r in rows if r['method']=='b3')
com=sum(r['receipt_completed'] or 0 for r in rows if r['method']=='b3')
acc=sum(r['receipt_accepted'] or 0 for r in rows if r['method']=='b3')
fallback=sum(r['handoff_fallback_count'] or 0 for r in rows if r['method']=='b3')
wait=sum(r['wait_duration_total_wall_s'] or 0 for r in rows if r['method']=='b3')
print('sent, received, executed', sent, recv, exe)
print('ACCEPTED', acc, 'COMPLETED', com, 'REJECTED', rej, 'ABORTED', ab, 'FALLBACK', fallback, 'WAIT_TOTAL', wait)
