# C3 v8.1: B1 保底层 + Trust-Gated Marginal-Cost Takeover

状态：2026-08-07，正式交错 pilot 暴露 C3 v8 无端到端收益后提出的最小修正。本文只描述机制修正，不代表论文结论。

## 1. 为什么修

open_plan_office / 3 UAV / 5 m / 120 s 的 5 对交错 pilot 中：

- B1 makespan：69.81, 74.74, 65.96, 63.23, 107.23 s；
- C3 makespan：70.24, 89.34, 75.07, 90.73, 120.57 s；
- C3 相对 B1 的成对差值均为正，中位约 +13.34 s；
- 碰撞、lock 均为 0，C3 只有 2 次 takeover sent/executed，invalidate 2。

根因不是 takeover 本身被证明无价值，而是 C3 v8 的配置和状态机少了一个基础机制，并多了一段无收益等待：

1. C3 分支把 prct_enable_retry_suppression=false，所以 C3 没有使用 B1 的重复 A* 失败抑制；
2. marginal gate 要求 repeat_count >= 3 且 benefit 严格为正，绝大多数失败链不会进入 takeover；
3. 失败后仍进入 certificate 查询/等待，即使没有接管也会增加 owner 等待和状态机时间。

## 2. v8.1 方法定义

C3 v8.1 = B1 重复失败抑制（保底层） + peer reachability certificate（事件层） + trust-gated marginal-cost takeover（决策层） + COMPLETED invalidate（闭环层）。

固定不变：三张官方地图、LiDAR 探索、动力学、通信协议、LKH/ACVRP、终止条件和评价指标。

### 保底层

- 每次局部 A* open_set_exhausted 都记录失败；
- 同一 frontier_id + rounded_goal + map_version + owner_id 连续失败达到 prct_repeat_threshold 后进入冷却；
- 冷却期内 owner 不再重复攻击同一目标；
- 地图版本变化、目标消失或任务列表无候选时回退原始选择。

### 事件层

- 冷却不替代 takeover；只有 owner 仍尝试处理同一失败目标且满足 C3 marginal gate 时，才查询 peer 可达性证书；
- certificate 必须记录 peer id、位姿、地图版本、状态年龄、A* 结果、路径长度、规划耗时；
- 无有效证书不得发送 takeover。

### 决策层

- takeover 候选必须满足 peer_trust >= c3_trust_threshold；
- takeover 收益按 estimateOwnerStuckCostS() - peer_marginal_cost_s 计算；
- 收益不严格超过 c3_benefit_margin_s 时跳过接管；
- peer 选择依次比较收益、路径长度、状态年龄、负载。

### 闭环层

- owner 收到 peer COMPLETED 后对同一 frontier 设置 c3_takeover_completed_invalidate；
- 防止同一目标被反复发送；
- ACCEPTED / REJECTED / COMPLETED / ABORTED / STALE 全部进入回执统计；
- 无响应、证书过期、ABORT、REJECTED 或计算超预算时回退原 C2 决策。

## 3. 本次代码变更

1. isPrctTargetCooled()：C3 激活时先检查 B1 base cooldown，再检查 takeover cooldown，二者任一成立即抑制；
2. prctFilterCooledTargets()：C3 与 B1 统一走 isPrctTargetCooled()，不再只查 takeover cooldown；
3. registerPrctFailure()：C3 激活时除维护 c3_failure_repeat_counts_ 外，也更新 B1 的 prct_cooldowns_ 和 prct_frontier_cooldowns_，并保留 prct_retry_suppression_register 遥测；
4. run_scene_pilot.sh：删除 C3 模式必须 prct_enable_retry_suppression=false 的限制；
5. run_c3_formal_batch.sh：C3 分支改为 suppress=true，保证 B1 基础机制不缺失。

## 4. 下一步验证门槛

先跑 4 个 open_plan_office / 3 UAV / 5 m pilot，逐个检查：

- C3 的 A* 失败次数和 suppression skip 数量不再明显高于 B1；
- takeover 仍能触发，COMPLETED invalidate 仍出现；
- 不再出现 c3_takeover_exhausted；
- 无死锁、无空转、无覆盖旧日志。

pilot 通过后再开新 batch_id 跑正式成对交错实验。C3 相对 B1 的门槛仍保持：成对 makespan 中位改善 >=10%，或未完成率改善 >=20pp；覆盖、碰撞、LKH、在线时延不得退化。

若 pilot 或正式实验仍显示无收益，按负结果保留完整日志，不把 takeover 包装成主贡献。

## 5. 2026-08-07 C3 v8.1 pilot 结果

4 个 open_plan_office / 3 UAV / 5 m / 120 s C3 pilot 全部 FINISH，audit-complete：

| 实例 | makespan(s) | A*失败 | suppression_register | suppression_skip | takeover sent/executed | completed | fallback |
|---|---:|---:|---:|---:|---:|---:|---:|
| c3v81_open3_5m_001 | 75.70 | 11 | 11 | 23 | 2/2 | 2 | 3 |
| c3v81_open3_5m_002 | 79.14 | 3 | 3 | 0 | 0/0 | 0 | 1 |
| c3v81_open3_5m_003 | 80.84 | 3 | 3 | 1 | 0/0 | 0 | 1 |
| c3v81_open3_5m_004 | 74.36 | 2 | 2 | 0 | 0/0 | 0 | 0 |

说明：实例标签不是官方随机种子；这些 pilot 只验证机制闭环，不能与 B1 做论文级成对比较。001 证明重复失败抑制和 takeover COMPLETED invalidate 可以同时工作，002/003/004 证明失败链少时 takeover 不强行触发。

下一步：正式成对 batch 使用 instance-major 交错顺序 B0/B1/C3，再统计成对 makespan、FINISH 率、A* 失败、takeover、碰撞、LKH 和在线时延。

## 6. 第一轮正式交错 batch：open_plan_office / 3 UAV / 5 m / 120 s

5 对 repeated instance 全部 FINISH，无碰撞、无 LKH 失败、无 takeover 等待：

| 实例 | B0(s) | B1(s) | C3(s) | C3-B1(s) |
|---|---:|---:|---:|---:|
| 1 | 69.36 | 99.15 | 78.01 | -21.14 |
| 2 | 96.59 | 85.32 | 77.76 | -7.56 |
| 3 | 76.77 | 86.65 | 73.95 | -12.70 |
| 4 | 81.08 | 64.70 | 69.27 | +4.58 |
| 5 | 61.68 | 74.09 | 81.29 | +7.20 |

- C3 相对 B1 成对中位差为 -7.56 s，B1 中位 85.32 s，约 8.9% 改善，未达到预注册的 10% 门槛；
- 5 对 C3 的 takeover_sent/executed 均为 0，只有 retry suppression 生效，因此本轮收益不能归因于 peer takeover；
- C3 相对 B1 的均值差为 -5.92 s，相对 B0 的成对中位差为 -2.82 s；
- 无官方随机种子，repeated instance 标签不等价于论文 trial；本轮只是中间证据。

原始文件：results/C3_V8_1_FORMAL_OPEN3_20260807.csv 与 .json。

## 7. 下一步：2 UAV 失败链压力测试

2 UAV 配置历史上 A* 失败次数最高，正式交错 batch 已启动，用于检验 peer takeover 是否能在失败链存在时产生机制级收益。

若 2 UAV 仍无 takeover 或 B3 未超过 B1，保留负结果并停止把 peer takeover 作为主贡献。
