# results

本目录只保存聚合结果，不保存原始 rosbag、大日志或完整实验目录。

## 文件

- PRCT_C2_STATS.csv：逐实例聚合表。
- PRCT_C2_STATS.json：逐实例、配置统计、成对比较和 bootstrap CI。
- PRCT_C2_REACHABILITY_AUDIT.json：peer 可达性证书审计。

修正截断口径前的原始备份在 archive/results_raw_20260806/。

## 口径

- RMST 使用修正口径：未全机 FINISH 的实例按 180s 截断。
- FINISH 是本地 FSM 观测规则，不是论文定义的全局覆盖完成指标。
- 无官方随机种子协议；run_001/002/003 是实例标签，不是上游 seed。
- 当前结果不能直接与论文表格比较。
