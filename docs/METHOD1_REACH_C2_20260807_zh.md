# METHOD1: REACH-C2

日期：2026-08-07
状态：三方法并行方案的 Method 1，先做机制审计和成对实验，不构成投稿结论。
Current formal runner method set: B0/B1/REACH/SVR/STEER; B2/B3 in this doc are internal ablation targets, not separate current runners.

复现协议更新：原始 C2 的 LKH SEED=0 会取系统时间，导致同一实例每次分配不稳定。当前 runner 已增加固定 LKH_SEED，所有 B0/B1/REACH/SVR/STEER 必须在同一 lkh_seed 下成对运行；未记录 lkh_seed 的旧日志不参与公平比较。

## 1. 一句话主张

在不修改 C2 三张 PCD 地图、LiDAR 探索任务、传感器、无人机动力学、通信协议、LKH/ACVRP、终止条件和评价定义的前提下，REACH-C2 把局部 A* 与轨迹规划产生的执行失败证据反馈到 ACVRP/LKH 分配代价，并用“确认阻塞 + 目标保持 + 事件触发局部再分配”抑制重复失败链和超时长尾。

## 2. 为什么是这条线

C2 的原分配代价主要由几何距离、图邻接惩罚和连通性约束构成，不消费“这个目标在本地地图上是否反复不可达、轨迹是否反复失败”的执行证据。本地复现已经确认：

- A* 失败均为 open_set_exhausted，不是节点池或时间限制；
- open_plan_office / 2 UAV 和 cubicle_office / 4 UAV 等格子存在大量重复失败；
- peer takeover 不带来端到端收益，因此不能把“让 peer 去证明可达”作为主机制；
- ECRTA 的时间残差校准没有稳定机制证据，因此不预测绝对完成时间上界。

REACH-C2 只做可解释、可回退的执行风险修正，不引入学习模型，不改变 LKH 求解结构。

## 3. 方法结构

### 3.1 执行证据表

按 (frontier_id, rounded_goal, map_version, owner_id) 维护：

```text
astar_fail_count
astar_success_count
trajectory_fail_count
blocked_streak
last_failure_wall_s
last_success_wall_s
goal_set_wall_s
local_evidence_hash
map_version
```

证据只来自本机真实规划结果；地图版本变化时旧证据只衰减，不直接继承。

### 3.2 风险感知分配代价

在 C2 原有候选分配代价基础上增加：

```text
C_alloc = C_nominal * (1 + lambda * rho) + kappa * stuck_penalty
```

其中：

- `C_nominal` 是 C2 原始 ViewNode/ACVRP 代价；
- `rho` 是有界执行风险，由失败率、blocked_streak、轨迹失败和证据新鲜度在线估计，初始为 0；
- `stuck_penalty` 只对已确认阻塞的目标进入；
- `lambda`、`kappa` 在批量实验前冻结；
- 风险项不改变连通性约束，不改变 LKH 文件格式，只改变进入分配器的候选代价。

实现位置：优先复用 prctFilterCooledTargets 之后的候选集合，并在 findTourOfFrontier / allocateTasks 的代价构造入口做 risk-adjusted cost。

### 3.3 确认阻塞与目标保持

- 当前目标在连续 blocked_confirm_updates 次检查中均失败，才进入阻塞确认；
- 当前目标被确认前不因候选列表变化立即切换；
- 只有替代目标在风险代价后仍优于当前目标超过 switch_margin 时才切换；
- 同一 (goal, map_version, owner) 不允许重复发布，除非局部证据变化或目标消失；
- 无替代目标时回退原始 C2 的下一个合法选择，不空转、不做 peer takeover。

### 3.4 事件触发局部再分配

只允许在以下事件触发：

- 当前目标被确认阻塞；
- 某个候选的执行风险超过阈值；
- 轨迹规划失败；
- 本机成为 makespan 瓶颈的日志证据出现。

禁止周期性无条件重规划；禁止把固定等待作为完成条件。

## 4. 消融与对比

- B0：原始 C2；
- B1：重复失败冷却/去重；
- B2：只加执行风险代价；
- B3：完整 REACH-C2。

## 5. 主实验矩阵

优先高失败率格子：

- open_plan_office / 2 UAV / 5 m；
- cubicle_office / 4 UAV / 5 m；
- octa_maze / 4 UAV / 5 m。

每格至少 10 个成对 repeated instance，目标 20。补充 10/15 m 与无限通信。

## 6. 核心指标

- A* 失败次数与同一目标重复失败链长；
- FINISH 率、makespan、p90/RMST；
- 覆盖率、总路径长度；
- 碰撞、不可行轨迹、断连、LKH 失败；
- 在线规划时延 p50/p95；
- 切换次数、重分配次数、每次重分配的真实收益。

## 7. 预注册门槛

批量前冻结，不得事后放宽：

- B3 相对 B1 的成对 makespan 中位改善 >= 10%，或 FINISH 率改善 >= 20pp；
- 重复失败链长显著下降；
- p90/RMST、覆盖率、总路径、碰撞、LKH 失败、在线时延不得系统性恶化。

## 8. 失败判定

若满足以下任一条件，REACH-C2 不作为主方法：

- 高失败率格子中失败链不稳定或可被简单目标保持消除；
- 风险代价在 held-out instance 上无稳定收益；
- B3 不优于 B1；
- 收益以覆盖率、路径长度或安全性退化换取。

## 9. 文献锚点

- VORL-EXPLORE：执行保真度耦合分配；
- MEF-Explore：目标保持与卡死失败定义；
- DAIB-Explorer：blocked_streak、goal_min_hold_time、switch margin；
- Energy-Balanced Task Allocation and Dynamic Rescheduling：事件触发局部重分配；
- 本地 B1+ v1-v5：失败冷却有价值，但第一失败即驱逐和 marginal gate 不充分。
