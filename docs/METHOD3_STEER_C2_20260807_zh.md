# METHOD3: STEER-C2

日期：2026-08-07
状态：三方法并行方案的 Method 3，先做机制审计和成对实验，不构成投稿结论。
Current formal runner method set: B0/B1/REACH/SVR/STEER; B2/B3 in this doc are internal ablation targets, not separate current runners.

复现协议更新：原始 C2 的 LKH SEED=0 会取系统时间，导致同一实例每次分配不稳定。当前 runner 已增加固定 LKH_SEED，所有 B0/B1/REACH/SVR/STEER 必须在同一 lkh_seed 下成对运行；未记录 lkh_seed 的旧日志不参与公平比较。

## 1. 一句话主张

在不修改 C2 三张地图、任务生成、传感器、动力学、通信、LKH/ACVRP、终止条件和评价定义的前提下，STEER-C2 把“局部 A* 失败”当作需要确认和治理的执行事件，通过目标最短保持时间、连续阻塞确认、切换 margin、同目标去重和负载感知的下一合法目标选择，在规划命令层直接降低重复失败链、等待开销和超时长尾，不依赖 peer takeover，也不重跑完整分配器。

## 2. 为什么是这条线

本地证据已经否定两条主线的端到端有效性：

- peer takeover 在端到端没有稳定收益，且引入证书过期、重复覆盖和等待；
- 时间残差校准没有稳定的任务身份配对，不能包装成时间上界。

但 B1 的重复失败抑制确实能降低 A* 失败与卡死。STEER-C2 把这一层做完整，并刻意与 REACH-C2、SVR-C2 分开：

- REACH-C2 把风险反馈进 ACVRP/LKH 分配代价；
- SVR-C2 在任务语义与回执层决定是否值得调用原始分配器；
- STEER-C2 不改分配代价，不改任务语义，只改“当前命令是否保留、何时切换、切换到哪个合法目标”。

## 3. 方法结构

### 3.1 确认阻塞与目标保持

- 当前目标一旦被设置，记录 goal_set_wall_s；
- 连续 blocked_confirm_updates 次 A* open_set_exhausted 或轨迹不可行才确认阻塞；
- 当前目标最短保持时间为 goal_min_hold_time_s，保持期未到不允许切换；
- 候选目标必须满足 goal_switch_margin 才有资格替换当前目标；
- 同一 (frontier_id, rounded_goal, map_version, owner_id) 按冷却键去重。

### 3.2 负载感知候选选择

当当前目标确认不可继续后，STEER-C2 从 C2 下一合法候选集中选择：

```text
score = nominal_cost * (1 + beta * load_bias) + gamma * repeat_penalty
```

其中：

- load_bias 由本机已分配任务数/路径负载估计，不假设全局信息；
- repeat_penalty 由失败链长度和证据新鲜度得到；
- 不改变候选合法性，不跳过不可达候选，不改变最终覆盖判定。

### 3.3 安全回退

- 无候选时回退原始 C2 的下一个合法目标选择；
- 冷却键无法构造、地图版本变化、目标消失或本地重试成功时解除冷却；
- 禁止固定等待作为切换完成条件；
- 禁止周期性无条件重规划。

### 3.4 离线候选策略档案（可选，不作为主贡献）

从 U 盘 QD 项目可以借鉴“多策略候选 + 行为档案”的评估思想，但只允许作为离线诊断：

- 为三种候选选择策略记录行为档案（失败率、重复链长、路径长度、覆盖率）；
- 在线仍使用确定性规则，不训练 RL，不做 QD 泛化主张；
- 该模块若有收益，也只写成“离线策略档案用于诊断”，不写成“QD 驱动的在线选择”。

## 4. 消融与对比

- B0：原始 C2；
- B1：重复失败冷却/去重；
- B2：只加目标保持与确认阻塞；
- B3：完整 STEER-C2（保持 + 确认 + margin + 负载感知 + 去重）。

## 5. 主实验矩阵

优先高失败率格子：

- open_plan_office / 2 UAV / 5 m；
- cubicle_office / 4 UAV / 5 m；
- octa_maze / 4 UAV / 5 m。

每格至少 10 个成对 repeated instance，目标 20。补充 10/15 m 与无限通信。

## 6. 核心指标

- A* 失败次数、同一目标重复失败链长、切换次数；
- FINISH 率、makespan、p90/RMST；
- 覆盖率、总路径长度；
- 碰撞、不可行轨迹、断连、LKH 失败；
- 在线规划时延 p50/p95；
- WAIT_HANDOFF/固定等待开销（应为 0）。

## 7. 预注册门槛

- B3 相对 B1 的成对 makespan 中位改善 >= 10%，或 FINISH 率改善 >= 20pp；
- 重复失败链长显著下降；
- p90/RMST、覆盖率、总路径、碰撞、LKH 失败、在线时延不得系统性恶化。

## 8. 失败判定

若 B3 不优于 B1，或收益来自更保守的“少跑 A*”而非实际完成任务，则 STEER-C2 不作为主方法，保留负结果。

## 9. 文献锚点

- DAIB-Explorer：blocked_streak、goal_min_hold_time、goal_switch_margin、same_goal_tolerance；
- MEF-Explore：卡死 120 s 定义失败、duration-adaptive goal assigning；
- 本地 B1+ v1-v5：第一失败即驱逐太激进，需要确认与保持；
- U 盘 QD 项目：行为档案与候选策略组合仅作离线诊断参考。
