# B1+ v3 pilots（2026-08-07）

这些 pilot 只用于参数、运行和遥测验证，不构成论文结果。

## open_plan_office / 3 UAV / 5 m / 120 s

run_dir：

logs/reachability_retry/formal_b1plus/open_plan_office/uav_3/b1plus_v3_pilot_open3_001

结果：

- 3/3 FINISH
- makespan_s_proxy：81.45 s
- A* 失败：0
- quarantine_enabled：1

## open_plan_office / 2 UAV / 5 m / 120 s

run_dir：

logs/reachability_retry/formal_b1plus/open_plan_office/uav_2/b1plus_v3_pilot_open2_001

结果：

- 2/2 FINISH
- A* 失败：4
- prct_retry_suppression_register：4
- quarantine_enabled：1，backoff_s=-1
- 未达到默认阈值 3，未产生 suppression_skip

## open_plan_office / 3 UAV / 5 m / 180 s 同构实例

run_dir：

logs/reachability_retry/formal_b1plus/open_plan_office/uav_3/b1plus_v3_open3_run003

结果：

- 3/3 FINISH
- A* 失败：0
- 该实例与上一轮 v2 run_003 不构成受控种子配对，只作运行验证

## 结论

- B1+ v3 编译和参数隔离通过。
- 隔离逻辑已经在遥测字段中生效。
- 端到端收益必须等待正式成对 batch，pilot 不能投稿。
