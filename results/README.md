# results

本目录只保存聚合结果，不保存原始 rosbag、大日志或完整实验目录。

## 当前状态

- 当前协议为 B0/B1/REACH-C2/SVR-C2/STEER-C2 五方法成对对比。
- 固定 seed 搜索候选：`open_plan_office / 3 UAV / 5 m / LKH_SEED=2`，B0 为 2/3 FINISH、407 次 A* 失败。
- 五方法 pilot 结果会写入 `formal_three_method/` 的原始运行目录和本目录的聚合文件。
- 旧 PRCT/ECRTA/B1+ 结果只作为历史负结果保留在 `archive/`，不再进入当前统计口径。

## 当前交付物

- `ALL_METHODS_RANKING.md`：每轮 B0/B1/REACH/SVR/STEER 的排名和机制审计结论。
- `THREE_METHODS_STATS.csv`：逐实例聚合表。
- `THREE_METHODS_STATS.json`：逐实例、配置统计、成对比较和 bootstrap CI。
- `METHOD_GATE.tsv`：每轮批量前的 `verify_three_method_gate.sh` 与 `method_check.tsv` 摘要。

旧 PRCT/ECRTA 聚合文件不再作为当前结果，原始备份保留在 `archive/results_raw_20260806/`。

## 口径

- RMST 使用修正口径：未全机 FINISH 的实例按 180s 截断。
- FINISH 是本地 FSM 观测规则，不是论文定义的全局覆盖完成指标。
- 固定 `LKH_SEED` 后，成对实例必须使用同一 seed；没有 seed 的旧日志不能进入公平比较。
- 当前结果不能直接与论文表格比较。
