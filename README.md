# ECRTA_C2

C2-Explorer 改进实验库。当前有效主方法是 B1+ v3：失败确认目标隔离；C3/peer takeover 已进入机制级否定，不再作为主贡献。

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
- B1+ v3：在 B1 固定冷却之上，达到阈值后当前 frontier map epoch 内隔离失败目标；v2 指数退避已因 n=5 无收益判废。
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

1. 已新增 B1+ v3 设计文档：docs/B1_PLUS_V3_QUARANTINE_METHOD_SPEC_20260807_zh.md。
2. 已新增 B0/B1/B1+ 成对 batch 脚本：scripts/run_b1plus_batch.sh。
3. v3 正式成对 batch：open_plan_office / 3 UAV / 5 m / 180 s，至少 5 对后扩展到其他两图和 2/4 UAV。
4. 主门槛：B1+ v3 相对 B1 的 A* 失败次数与失败链长度下降，makespan 不系统性恶化，FINISH 率不低于 B1；失败样本全部进入分母。

## 2026-08-07 v8.1 更新

- C3 v8.1 将 B1 重复 A* 失败抑制作为保底层，只有同时满足 trust gate 和 marginal-cost gate 时才进入 peer takeover。
- 已修复 C3 正式 batch 中 suppress=false 的配置错误；C3 现在与 B1 一样使用 prct_enable_retry_suppression=true。
- 4 个 C3 v8.1 pilot 全部 FINISH；有失败链的实例同时出现 retry suppression 与 takeover COMPLETED invalidate，无失败链实例不会强行 takeover。
- 5 对 B0/B1/C3 正式交错 batch 已完成；详细结果见 C3_V8_1_TRUST_GATED_WITH_B1_SUPPRESSION_20260807_zh.md。
- 5 对 B0/B1/C3 正式交错 batch 已完成：C3 相对 B1 成对中位差 -7.56 s（约 8.9%），低于 10% 预注册门槛；5 对中 takeover 触发次数为 0，因此本轮收益不能归因于 peer takeover。
- 2 UAV 正式交错 batch 已完成：C3 相对 B1 成对中位差 -14.31 s，约 14.3%，但 takeover 总数仍为 0，不能归因给 peer takeover。
- 180s 失败链压力 batch 已完成：B0 出现 1/5 实例卡死（343 次 A* 失败、2/3 完成），B1 与 C3 均为 3/3 完成；5 对实例中 takeover_sent/executed 均为 0，C3 相对 B1 成对中位差 +4.76 s，因此 peer takeover 无端到端收益。
- 下一阶段主方法切换为 B1+：在 B1 冷却之外增加 owner-level 失败目标降权/过期失效，takeover 只作为冷却后仍持续失败的实验性后备，不再进入正式主消融。
- B1+ v3 已完成 120 s 参数验证 pilot：open_plan_office/3 UAV 与 2 UAV 均正常运行，quarantine_enabled=1、backoff_s=-1；2 UAV 观测到 open_set_exhausted 与隔离注册。
