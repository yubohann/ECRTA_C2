# ECRTA_C2

C2-Explorer 改进实验库。当前有效主方法是 B1+ v4：Local-Evidence-Gated Goal Quarantine。Peer takeover（B2/B3/C3）与执行时间残差校准（ECRTA）已被本地机制审计否定，不作为主贡献。

## 当前状态

- B1+ v4 已实现、编译通过，并完成 1 个 pilot。
- Pilot：open_plan_office / 3 UAV / 5 m / 120 s，FINISH 3/3，makespan 71.08 s，A* fail 12，quarantine release 3。
- 正式主矩阵第一轮已完成：open_plan_office / 3 UAV / 5 m / 180 s，B0/B1/B1+ v4 成对 5 组。
- 门槛判定：B1+ v4 相对 B1 的成对中位改善约 3.87%，低于 10% 预注册阈值；未通过，保留为负结果。
- 正在修复 v4 已知实现问题并设计失败链可复现的 v5；不得把无失败实例的随机 makespan 差异写成收益。
- 仓库不包含 C2 上游源码、地图、rosbag 和大日志，只保存方法、协议、脚本和聚合结果。

## 固定边界

- C2 三张官方地图：open_plan_office、cubicle_office、octa_maze。
- 不修改 LiDAR 探索任务、传感器、无人机动力学、通信协议、LKH/ACVRP、终止条件或评价指标。
- 不修改 upstream/c2_explorer_official。
- OpenGL 4.6 到 3.3 的 WSLg 兼容补丁必须披露，不是算法贡献。

## 方法

- B0：原始 C2，不做失败抑制。
- B1：固定 5 s 冷却。
- B1+ v4：同一目标在相同局部 occupancy/inflated occupancy evidence hash 下连续失败达到阈值后隔离；局部证据变化、目标消失或 A* 成功才释放；候选全部隔离时回退原始 C2。
- B2/B3/C3：peer 证书与 takeover 已实测无端到端收益，保留在历史文档中，不进入主消融。
- ECRTA：执行时间残差校准机制审计未通过，不进入主方法。

## 历史方法判定

- B1+ v2 指数退避：n=5 无收益，判废。
- B1+ v3 map-epoch quarantine：5 组正式成对中相对 B1 改善，但相对 B0 不稳定，且全局 epoch 释放过粗，升级为 v4。
- Peer takeover：5 组正式端到端收益为 0，不再进入主消融。
- ECRTA：机制审计未通过。

## v3 正式成对 batch

场景：open_plan_office / 3 UAV / 5 m / 180 s。

| instance | B0 makespan | B1 makespan | B1+ v3 makespan | B1+ v3 - B1 |
|---:|---:|---:|---:|---:|
| 1 | 61.50 | 98.28 | 81.57 | -16.71 |
| 2 | 66.82 | 104.27 | 73.66 | -30.61 |
| 3 | 84.03 | 84.85 | 70.17 | -14.68 |
| 4 | 76.06 | 77.33 | 76.13 | -1.20 |
| 5 | 80.30 | 60.08 | 94.63 | +34.55 |

- B1+ v3 vs B1：成对 mean=-5.73 s，median=-14.68 s。
- B1+ v3 vs B0：mean=+5.49 s，median=+6.83 s。
- 结论：v3 不能作为主方法，继续修成 v4。

## v4 Pilot

| 指标 | 值 |
|---|---:|
| FINISH | 3/3 |
| makespan | 71.08 s |
| A* fail | 12 |
| prct_retry_suppression_register | 12 |
| prct_retry_suppression_skip | 8 |
| prct_quarantine_release | 3（goal_removed） |
| prct_candidate_filter | 7 |

单次 pilot 不是论文结论，只作为进入正式 batch 的机制依据。

## 下一步

1. v4 正式结果已归档：results/B1_PLUS_V4_BATCH_20260807/README.md。
2. 修复 v4 默认 evidence radius 不一致和 cooldown key 与 map epoch 耦合。
3. 以失败链稳定可复现为前提设计 v5；同初始状态/种子控制不可省略。
4. 若 v5 仍无端到端收益，保留负结果，不放宽阈值。

## 参考文档

- docs/B1_PLUS_V4_LOCAL_EVIDENCE_QUARANTINE_20260807_zh.md
- docs/B1_PLUS_V3_QUARANTINE_METHOD_SPEC_20260807_zh.md
- docs/PRCT_C2_REPORT_zh.md
- docs/PRCT_C2_PAPER_DECISION_zh.md
