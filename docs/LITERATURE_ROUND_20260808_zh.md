# 文献轮记录 ROUND-1（2026-08-08，协议 §3）

## 对照对象与关键机制

### 1. DAIB-Explorer（GitHub YYY0702/DAIB-Explorer，ROS1 单机探索）
源码 `src/explorer_core.cpp` 逐行核对，核心目标选择机制：

| 机制 | DAIB 实现 | 我们的对应实现 | 差距 |
|---|---|---|---|
| 目标保持 | `goal_min_hold_time_s`，`goal_set_time_` 在设置目标时记录；hold 期间除非到达/阻塞/超时/周期重规划，否则不重选 | STEER `steer_goal_min_hold_s`（默认 3.0s，goal_set_by_coord 记录） | 基本一致 |
| 阻塞确认 | `updateGoalStatus` 10Hz：`segmentBlocked` 连续命中才 `blocked_streak_+=1`，`goal_blocked_ = raw_blocked && streak >= goal_blocked_confirm_updates`（默认≥1，常用 3） | 我们的 A* 失败本身即阻塞信号；v5 曾"一次失败即驱逐"，v3 改为阈值+冷却 | **DAIB 是"连续 N 次确认才切换"，我们是"连续失败阈值+冷却"。需确认 STEER 是否等价地要求连续确认而非累计计数** |
| 切换 margin | `best_score > current_score + goal_switch_margin * max(1,|current_score|)` 才允许切换 | STEER `marginal_cost <= steer_switch_margin_`（代价侧 margin） | 方向一致但量纲不同（score vs cost），需标定 |
| 同目标去重 | timeout 后 best 落在 `same_goal_tolerance_m` 内 → 保持原目标并重置计时器，计数 `suppressed_goal_republishes` | `steer_all_views_cooled` 冷却视图 | DAIB 防抖更直接：同一目标不重发；我们冷却的是视图/目标 |
| 无替代目标 | `no_safe_frontier` / `WAIT_FOR_FRONTIER`，blocked_streak 清零 | `steer_all_views_cooled` 回退 | 一致 |

DAIB 是单机、无 ACVRP/LKH、无 peer；其"连续确认+保持+margin+去重"组合是审稿人可引用的最接近先例，我们不能声称这四个词中的任何一个为创新，只能把**"多机 C2 场景下的执行失败证据 → 目标选择与分配代价联合反馈"**作为我们的贡献面。

### 2. VORL-EXPLORE（arXiv 2603.07973，本轮网络不可达，依据先前记录）
主张：层级式探索把 frontier 分配与局部导航解耦后，分配器不知道执行难度 → 机器人在瓶颈聚集、震荡重规划；提出 `execution fidelity` 把局部可导航性耦合进分配目标。→ 与 REACH 同一层（分配代价层），但 VORL 是"预测可导航性"（前瞻），我们是"实测失败证据回填"（后验）。网络恢复后需补读全文确认其如何计算 execution fidelity，避免撞车表述。

### 3. MEF-Explore（arXiv 2505.23376 / TASE 2025，依据先前记录）
"持续给机器人分配新目标"是卡死来源；到达或超时才换目标；"所有机器人卡死 120s"定义为失败。→ 实验必须报告失败率/卡死，不能只比 makespan（我们已纳入：FINISH 率、A* 失败数、180s 截断）。

## 三方法差距结论（本轮）
- REACH（分配代价层）：与 VORL execution fidelity 同层。差异点必须写清：我们用**实测失败事件**（map_version+frontier+owner）而非预测分数。
- SVR（任务语义层）：LKH 求解去重/复用，与 CBBA-ETC 的事件触发相关但作用对象是"候选集不变时跳过重复求解"，需确认不为"事件触发"表述撞车。
- STEER（目标选择层）：与 DAIB 同层。必须把实现改成与 DAIB 等价或更保守的语义：**连续失败确认（而非累计计数）+ 目标保持 + margin + 同目标去重**。若当前实现与 DAIB 语义不等价，优先修代码再跑正式对比。

## 本轮待办
1. 等 formal batch 结果，按 R3 逐层归因。
2. 若 STEER 触发率低：检查失败是否连续、冷却是否过宽（120s takeover cooldown 残留？）、是否被 `prct_backoff_enabled_` 等旧开关挡住。
3. 网络恢复后补 VORL-EXPLORE 全文与 MEF-Explore 全文，更新本记录。
