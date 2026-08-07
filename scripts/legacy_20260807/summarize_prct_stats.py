import json
p = '/home/c2dev/c2_explorer_reproduction/PRCT_C2_STATS.json'
d = json.load(open(p, encoding='utf-8'))
print('ROW_COUNT', d['row_count'])
print('CONFIG_STATS')
for x in d['config_stats']:
    print(x['scene'], x['uav_num'], x['method'], 'n='+str(x['n']), 'finish='+str(x['finish_count']), 'rmst='+str(x['rmst_s']), 'astar='+str(x['astar_failure_total']), 'traj='+str(x['traj_failure_total']), 'wait='+str(x['wait_total_s']), 'takeover='+str(x['takeover_sent_total']))
print('PAIRED')
for x in d['paired_comparisons']:
    print(x['scene'], x['uav_num'], x['comparison'], 'n='+str(x['n_pairs']), 'wins='+str(x['b3_wins']), 'losses='+str(x['b3_losses']), 'median_diff='+str(x['median_diff_s']), 'pct='+str(x['median_pct_improvement']), 'rmst_b3='+str(x['b3_rmst_s']), 'rmst_base='+str(x['base_rmst_s']))