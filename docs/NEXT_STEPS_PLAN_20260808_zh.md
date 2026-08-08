# 下一步执行计划（2026-08-08）——审计收尾、双类失败分析、HOP 定论、论文定稿

## 背景（三次审计后的状态）
- 聚合器已修正：infra 排除、按 index 配对、置换检验（wilcoxon/MWU）、部分完成截断 180s、轨迹失败从原始遥测统计、SVR 复用按 gate decision 统计。
- 结论（当前）：无统计显著改善；REACH 在 open/2 方向一致占优（非显著）；SVR 复用真实发生（80 次）无收益；STEER 无收益；B1 轨迹失败高企（R2 cubicle 383 vs B0 74）——**待分析的新现象**。
- 方法状态：REACH/SVR/STEER 定论；ETA 定论（分配层无空间）；HOP 中期（n=2-3，跳数降 10-25%，makespan 效应未定）。

## 第 1 步：剩余审计项（~1.5h，前台执行）
1.1 完成率指标定义验证
- 检查 `finish_drone_ids` vs `all_expected_finished_by_local_rule` 一致性（抽查 R1/R2 cubicle 各 run）
- 记录"本地 FINISH 规则"与覆盖率完成的偏差证据（trajectory_end_reasons）
1.2 LKH 耗时口径
- 验证 summary `lkh` 字段的 p50/p95 提取；ACVRP/ATSP 失败次数；确认"LKH 不是瓶颈"主张的数字来源
1.3 覆盖率代理
- 提取各方法 trajectory_end_reasons 的 frontier_covered 分布（论文覆盖代理指标）
1.4 R3 完整性（后台）
- 补齐 cubicle R3 缺失 run（45/50 → 50/50），追加到原 batch status.tsv
- 判定：R3 完整后重聚合，确认无显著差异结论不变

## 第 2 步：B1"轨迹失败高企"分析（~2-3h，前台）
2.1 提取 R2 cubicle 各方法 traj_result 失败原因分布与时间线
2.2 对比 B0/B1/REACH/SVR/STEER
2.3 机制判断：冷却→目标切换→反复规划不可行轨迹？
2.4 判定：成立 → 论文新增"双类执行层失败"分析（A* 失败链 + 轨迹规划失败）；不成立 → 记录为观测

## 第 3 步：HOP 定论（~2-3h，后台跑 + 前台判）
3.1 补齐 cubicle/4：B0 n=8、HOP n=8（已跑 3+2，补 5+6）
3.2 判定标准：方向一致（≥6/8 胜）→ 论文补充实验；不稳 → "跳数下降确定、makespan 效应被方差淹没"如实记录
3.3 open/2 的 HOP "提前完成→no_grid 空转"现象记录在案

## 第 4 步：论文定稿（~2-3h）
4.1 用修正数字重写实验章节（Tables A/B、机制表、显著性表）
4.2 统计口径写入方法节（截断规则、traj 失败来源、SVR 复用统计、双检验）
4.3 结论：失败链审计 + REACH 趋势 + 双类失败分析 + 负结果
4.4 最终投稿判断（路径 B）

## 命令/脚本索引
- 聚合：`python3 scripts/aggregate_formal_batch.py <batch_dir>`
- 测试：`python3 scripts/test_aggregate_formal_batch.py`
- 失败链：`python3 scripts/analyze_failure_chains.py <run_dir>`
- HOP pilot：`scripts/wsl_hop_pilots.sh`（模式：METHOD_MODE=hop）
- 后台调度：`scripts/round3_scheduler.sh`（已停）；补跑用单独脚本

## 判定标准汇总
| 项 | 通过 | 不通过 |
|---|---|---|
| HOP makespan | ≥6/8 胜 | 记录为"机制验证" |
| 双类失败分析 | B1 轨迹失败可归因 | 记录为观测 |
| R3 完整 | 50/50，结论不变 | 单独报告 |
