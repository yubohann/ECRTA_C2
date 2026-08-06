# ECRTA_C2

C2-Explorer 改进实验库。当前主方法是 C3: Trust-Gated Marginal-Cost Reallocation；ECRTA 与 PRCT B3 已作为负结果分支归档，不再进入主方法。

## 当前状态

- C3 v8 已实现并编译通过：证书、回执、trust、边际成本门控、takeover-completed invalidation。
- v8 修复 v7 根因：owner 收到 COMPLETED 后不再反复攻击同一 frontier；默认 `c3_takeover_completed_cooldown_s=120.0`。
- 已修复 `registerPrctFailure` 遥测 JSON 未闭合问题。
- C3 v8 机制 pilot：open_plan_office/3 UAV/5 m 实例 007/008/009 均已 audit-complete；007/008 出现 takeover COMPLETED 后 completed-invalidate 1 次且 exhausted 0。
- 正式端到端收益尚未验证，pilot 不能作为论文结论。
- 仓库不包含 C2 上游源码、地图、rosbag 和大日志，只保存方法、协议、脚本和聚合结果。

## 固定边界

- C2 三张官方地图：open_plan_office、cubicle_office、octa_maze。
- 不修改 LiDAR 探索任务、传感器、无人机动力学、通信协议、LKH/ACVRP、终止条件或评价指标。
- 不修改 upstream/c2_explorer_official。
- OpenGL 4.6 到 3.3 的 WSLg 兼容补丁必须披露，不是算法贡献。

## 方法

- B0：原始 C2。
- B1：只增加重复 A* 失败抑制和冷却。
- B2：只读 peer 可达性证书。
- B3：证书 + 事件触发 peer takeover。
- C3：B2 证书框架 + trust 门控 + 边际成本接管 + takeover-completed 失效 + 无收益回退。

## 关键参数（C3 pilot）

- communication_threshold_m：5.0
- duration_s：120
- prct_enable_peer_takeover：true
- c3_enable_marginal_gate：true
- c3_min_repeat_count：3
- c3_benefit_margin_s：1.0
- c3_takeover_cooldown_s：30.0
- c3_max_takeover_attempts：3
- c3_takeover_completed_cooldown_s：120.0

## v8 pilot

| 实例 | 场景 | UAV | FINISH | A*失败 | takeover | completed invalidate | exhausted | makespan proxy(s) | 审计 |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|
| c3_open3_v8_007 | open_plan_office | 3 | 3/3 | 11 | 1 sent/1 executed/1 completed | 1 | 0 | 64.75 | audit-complete |
| c3_open3_v8_008 | open_plan_office | 3 | 3/3 | 11 | 1 sent/1 executed/1 completed | 1 | 0 | 85.44 | audit-complete |
| c3_open3_v8_009 | open_plan_office | 3 | 3/3 | 4 | 0 | 0 | 0 | 77.44 | audit-complete |

## 门槛判断

- G0 可审计运行：通过。
- G1 重复不可达失败链：v7 存在，v8 需要多实例复核。
- G2 peer 可达样本：通过。
- G3 证书可靠性：机制层通过。
- G4 端到端收益：未通过正式统计。
- G5 无退化：未通过正式统计。

## 下一步

1. 机制 pilot 已通过，进入 B0/B1/C3 同一实例成对批量实验。
2. 主门槛：C3 相对 B1 成对 makespan 中位改善 >= 10%，或未完成率改善 >= 20pp。
3. 覆盖三图、2/3/4 UAV、5 m 通信，并补充 10/15 m 与无限通信。
4. 统计碰撞、不可行轨迹、LKH 失败、在线时延 p50/p95；失败样本全部进入分母。
5. 当前仓库结果只是可审计实验记录，不是论文结论。
