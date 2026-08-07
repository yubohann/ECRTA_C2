# ROUND-1 正式结果（cubicle_office/4 UAV / 5m / 180s / LKH_SEED=1，n=10/方法）

批次：`batch_formal_r1_20260808`（2026-08-08 完成，50 runs，1 run 标记 infra_suspect 已排除）

## 主表

| method | finish | makespan_med (s) | makespan_mean | A*失败sum | 机制事件 |
|---|---|---|---|---|---|
| B0 | 8/10 | 74.70 | 83.65 | 285 | — |
| B1 | 9/10 | 76.05 | 80.48 | 380 | suppression |
| REACH | 9/10 | 75.59 | 81.07 | **502** | 2371 事件但 risk_adjusted_edges=0 |
| SVR | **10/10** | 80.27 | 80.19 | **9** | 149 gates, 0 reuse |
| STEER | 9/10 | 75.75 | 75.54 | 197 | 0 事件 |

## 配对（makespan, bootstrap 95% CI / Wilcoxon）

| 对比 | vs B1 | vs B0 |
|---|---|---|
| B0 | +0.44s [−8.6,+19.7] p=0.20 | — |
| REACH | +1.42s p=0.028* | +1.69s p=0.013* |
| SVR | +0.98s p=0.028* | +0.73s p=0.07 |
| STEER | **−0.30s [−10.7,−0.2]** p=0.96 | +0.04s p=0.20 |

*符号表示 REACH/SVR 在此格子略慢（不显著或边缘显著）。

## 核心结论（诚实表述）

1. **失败链真实存在但非本格子 makespan 瓶颈**：B0 中位数 74.7s 且 8/10 完成；失败链主要集中个别 run 的单机单目标（REACH r1 单目标 425 次）。makespan 由"最后一架完成时间"主导，失败链不一定拖慢它。
2. **SVR 稳定性异常**：10/10 完成、全批仅 9 次 A* 失败——但 reuse=0，机制未实际介入；低失败更像实例随机（失败链为随机事件，同 seed 不可复现）。需要 v2 中给 SVR 增加复用触发的验证（检查 digest 变化原因）——不，v2 保持 SVR 不变，R2 数据将验证其是否为巧合。
3. **REACH as-implemented 有害**：∞冷却 + fallback 回路 → 502 次失败（比 B0 还多），机制事件 2371 次但 0 边调整（证据不跨机 + 键不匹配）。v2 修复：跨机证据板 + 中心邻近风险 + 有限冷却 + all-cooled hold。
4. **STEER 未触发**：194 次失败全为"无替代候选"型（multi-frontier 死端），视图轮换路径（single-frontier）从未进入。v2 修复：all-cooled hold 打断回路。

## 对论文的含义（暂定）

- 主声称候选 1："执行失败证据闭环抑制规划浪费"（A* 失败次数：B0 285 → STEER 197 / SVR 9；机制可审计）——但必须在 open/2 格子上验证 makespan 收益。
- 主声称候选 2：若 v2 REACH 能通过分配层避免死端任务，makespan 才可能显著下降。
- 必须报告失败率/卡死（MEF-Explore 风格），不能只报 makespan。
