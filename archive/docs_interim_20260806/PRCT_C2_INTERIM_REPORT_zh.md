# PRCT-C2 中间实验结果

日期：2026-08-06
状态：Octa/4 UAV 仍在补样本；本文只用于记录中间快照，不作为投稿结论。

## 1. 当前正式样本

聚合路径：/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal
聚合脚本：aggregate_prct_formal.py / summarize_prct_stats.py / summarize_gates.py / audit_formal_reachability.py

### 主表

| 场景 | UAV | 方法 | n | 全机FINISH | A*失败 | RMST(s) | takeover sent | takeover executed | 等待总时长(s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| open_plan_office | 3 | B0 | 13 | 7/13 | 4081 | 130.70 | 0 | 0 | 0.00 |
| open_plan_office | 3 | B1 | 13 | 13/13 | 61 | 57.57 | 0 | 0 | 0.00 |
| open_plan_office | 3 | B2 | 13 | 13/13 | 41 | 65.94 | 0 | 0 | 0.00 |
| open_plan_office | 3 | B3 | 13 | 13/13 | 49 | 58.17 | 18 | 18 | 5.76 |
| cubicle_office | 4 | B0 | 12 | 12/12 | 45 | 71.18 | 0 | 0 | 0.00 |
| cubicle_office | 4 | B1 | 13 | 12/13 | 37 | 83.73 | 0 | 0 | 0.00 |
| cubicle_office | 4 | B2 | 13 | 13/13 | 33 | 76.21 | 0 | 0 | 0.00 |
| cubicle_office | 4 | B3 | 12 | 12/12 | 21 | 81.28 | 11 | 11 | 3.30 |
| octa_maze | 4 | B0 | 13 | 13/13 | 46 | 85.13 | 0 | 0 | 0.00 |
| octa_maze | 4 | B1 | 5 | 5/5 | 0 | 84.00 | 0 | 0 | 0.00 |
| octa_maze | 4 | B2 | 3 | 3/3 | 3 | 91.82 | 0 | 0 | 0.00 |
| octa_maze | 4 | B3 | 3 | 3/3 | 0 | 76.57 | 0 | 0 | 0.00 |

RMST 使用修正口径：未全机 FINISH 的实例按 180s 截断；仍不是论文全局覆盖完成指标。

### 成对差异

| 场景 | 对比 | n | 胜/负 | 成对中位差(s) | 说明 |
|---|---|---:|---:|---:|---|
| open_plan_office | B3 vs B0 | 13 | 10/3 | -16.44 | 约改善 24%，主要来自 B1 |
| open_plan_office | B3 vs B1 | 13 | 6/7 | +1.60 | B3 无增量 |
| open_plan_office | B1 vs B0 | 13 | 11/2 | -3.92 | B1 收益明确 |
| cubicle_office | B3 vs B0 | 12 | 5/7 | +1.63 | B3 总体 RMST 更高 |
| cubicle_office | B3 vs B1 | 12 | 7/5 | -1.55 | 成对中位略好，总体 RMST 更高 |
| octa_maze | B3 vs B1 | 3 | 2/1 | -12.60 | 样本不足，不能定论 |

## 2. 门槛审计

| 门槛 | 判定 | 证据 |
|---|---|---|
| G0 | 通过 | 三图可启动，日志、配置、审计文件可生成 |
| G1 | 通过 | open_plan B0 出现 4081 次 A* 失败 |
| G2 | 通过 | B2 190/190 响应成功；B3 73/75 响应成功 |
| G3 | 机制层通过 | 2 次 B3 查询失败均为 peer 侧 open_set_exhausted，不是假可达 |
| G4 | 未通过 | B3 相对 B1 无稳定收益，Cubicle 总体退化 |
| G5 | 未通过 | Cubicle B3 RMST 明显高于 B0；覆盖率、碰撞、通信断连尚未完整提取 |

## 3. 初步判断

1. B1 重复失败抑制是当前唯一有明确收益的机制。
2. B3 peer takeover 能闭环，但当前数据不支撑“接管降低 makespan”的主张。
3. 论文主方法不能继续默认 PRCT takeover；Octa 完成后若仍不优于 B1，应改为重复失败抑制或负结果。
4. 当前不是完整主矩阵，只有三图各一个 UAV 配置、5m 通信；2/3/4 UAV 网格和通信范围实验尚未完成。

## 4. 主要威胁

- 无官方随机种子协议，成对实例只保证同图、同 UAV、同起点，不能保证严格同一随机过程。
- RMST 是本地 makespan proxy，不是论文指标。
- OpenGL 4.6 到 3.3 的 WSLg 兼容补丁必须披露。
- 当前结论不能写进论文，直到 Octa 完成并重新聚合。
