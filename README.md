# ECRTA_C2

C2-Explorer 改进实验仓库。当前主方法是 PRCT-C2；ECRTA-C2 已作为失败分支归档，不进入主方法。

## 当前状态

- Octa Maze / 4 UAV / 5m 通信主配置已跑完，最终统计已更新。
- G0-G3 机制审计通过；G4/G5 尚未通过。
- 最终数据不支持“B3 peer takeover 稳定优于 B1”的主张。
- 仓库不包含 C2 上游源码、地图、rosbag 和大日志，只保存方法、协议、脚本和聚合结果。
- 最终实验记录见 docs/PRCT_C2_REPORT_zh.md。

## 固定边界

- C2 三张官方地图：open_plan_office、cubicle_office、octa_maze。
- 不修改 LiDAR 探索任务、传感器、无人机动力学、通信协议、LKH/ACVRP、终止条件或评价指标。
- 不修改 upstream/c2_explorer_official。
- OpenGL 4.6 到 3.3 的 WSLg 兼容补丁必须披露，不是算法贡献。

## 方法

- B0：原始 C2。
- B1：只增加重复 A* 失败抑制和冷却。
- B2：B1 + 只读 peer 可达性证书。
- B3：B1 + 证书 + 事件触发 peer takeover。

## 关键参数

- communication_threshold_m：5.0。
- duration_s：180。
- prct_repeat_threshold：3。
- prct_cooldown_s：5.0。
- reachability_shadow_max_candidates：B0/B1=0，B2/B3=3。
- reachability_peer_shadow_max_peers：B0/B1=0，B2/B3=3。
- prct_enable_peer_takeover：B3=true。
- prct_peer_cert_wait_s：0.25。
- prct_peer_handoff_timeout_s：2.0。
- prct_peer_state_max_age_s：2.0。

## 当前聚合结果

聚合文件见 results/PRCT_C2_STATS.csv 和 results/PRCT_C2_STATS.json。
RMST 使用修正口径：未全机 FINISH 的实例按 180s 截断。

| 场景 | UAV | 方法 | n | 全机FINISH | A*失败 | RMST(s) |
|---|---:|---:|---:|---:|---:|---:|
| open_plan_office | 3 | B0 | 13 | 7/13 | 4081 | 130.70 |
| open_plan_office | 3 | B1 | 13 | 13/13 | 61 | 57.57 |
| open_plan_office | 3 | B2 | 13 | 13/13 | 41 | 65.94 |
| open_plan_office | 3 | B3 | 13 | 13/13 | 49 | 58.17 |
| cubicle_office | 4 | B0 | 12 | 12/12 | 45 | 71.18 |
| cubicle_office | 4 | B1 | 13 | 12/13 | 37 | 83.73 |
| cubicle_office | 4 | B2 | 13 | 13/13 | 33 | 76.21 |
| cubicle_office | 4 | B3 | 12 | 12/12 | 21 | 81.28 |
| octa_maze | 4 | B0 | 13 | 13/13 | 46 | 85.13 |
| octa_maze | 4 | B1 | 13 | 13/13 | 3 | 77.98 |
| octa_maze | 4 | B2 | 13 | 13/13 | 13 | 83.19 |
| octa_maze | 4 | B3 | 13 | 13/13 | 10 | 75.38 |

## 门槛判断

- G0 可审计运行：通过。
- G1 重复不可达失败链：通过。
- G2 peer 可达样本：通过。
- G3 证书可靠性：机制层通过。
- G4 端到端收益：未通过。
- G5 无退化：未通过。

## 下一步

1. B3 相对 B1 在 Open、Cubicle、Octa 均无稳定收益；Octa 为 5胜8负、中位慢 2.58s。
2. 主方法不能继续声称 peer takeover 降低 makespan。
3. 若继续投稿，应改为“重复失败抑制 + 可达性证书”，或如实报告负结果。
4. 当前仓库结果不是论文结论，只是可审计实验记录。
