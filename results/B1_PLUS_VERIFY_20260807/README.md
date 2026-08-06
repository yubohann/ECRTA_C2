# B1+ 参数验证 pilot：open_plan_office / 3 UAV / 120 s

- run_id: b1plus_verify_20260807
- B1+ 参数：prct_backoff_initial_s=5.0, prct_backoff_max_s=30.0, prct_backoff_factor=2.0
- 结果：3/3 FINISH，makespan proxy=66.86 s，A* failure=1，prct_retry_suppression_register=1
- 遥测 backoff_s=5，peer takeover=0，traj failure=1
- 用途：只验证参数进入 ROS、代码可运行、遥测可审计；不是论文级性能结论。

原始文件：telemetry_summary.json、peer_takeover_audit.json、run_manifest.txt、prct_check.tsv。
运行目录：/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal_b1plus/open_plan_office/uav_3/b1plus_verify_20260807
