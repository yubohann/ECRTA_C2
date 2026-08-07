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

### 2. VORL-EXPLORE（arXiv 2603.07973，cs.RO，2026-03-09 提交，已取全文摘要）
主张（摘要核实）：层级式探索把 frontier 分配与局部导航解耦 → 分配器缺乏执行难度感知 → 机器人在瓶颈聚集、震荡重规划、冗余覆盖。提出 **execution fidelity**（局部可导航性的共享估计）耦合进 **fidelity-coupled Voronoi 目标**（含机间斥力）+ 全局 A* 与反应式 RL 策略之间的风险感知仲裁；fidelity 模型用近期进度与安全结果的伪标签在线自监督重校准。评测：随机栅格 + Gazebo 工厂场景；代码"录用后公开"。
与我们的差异（必须写清，避免撞车）：
- VORL 的 fidelity 是**预测/估计**（学习模型）；REACH 是**实测失败证据**（A* open_set_exhausted 事件，map_version+frontier+owner 登记）——不是预测，不学习。
- VORL 的分配器是 Voronoi 目标；我们是 C2 的 ACVRP/LKH 连续性分配，代价矩阵受实测证据修正。
- VORL 处理"执行难度未知"；我们处理"执行失败已发生且重复重试"——失败链抑制与目标保持不在 VORL 范围。
- 不能声称的表述："把局部可导航性/执行难度耦合进分配"（VORL 已做）。
- 可写的表述："用实测失败事件证书修正 ACVRP 代价 + 确认-保持-切换抑制重复失败链"，并引用 VORL 作为最接近的相关工作。

### 3. MEF-Explore（arXiv 2505.23376 / IEEE TASE 2025，DOI 10.1109/TASE.2025.3575237，已核实）
主张（摘要核实）：通信受限多机探索；双层通信感知信息共享（低速通信共享位置、高速通信合并地图）+ 熵场探索 + **duration-adaptive goal-assigning module** 管理目标分配；仿真全场景优于现有方法，实机快 21.32%、成功率 +16.67%。
与我们的关系：duration-adaptive goal assignment 与我们"目标保持时间"同主题，但其动机是通信/信息共享调度，不是 A* 失败链；需在相关工作引用并说明差异（我们针对局部规划失败重试，且不改变 C2 目标定义）。

## 三方法差距结论（本轮）
- REACH（分配代价层）：与 VORL execution fidelity 同层。差异点必须写清：我们用**实测失败事件**（map_version+frontier+owner）而非预测分数。
- SVR（任务语义层）：LKH 求解去重/复用，与 CBBA-ETC 的事件触发相关但作用对象是"候选集不变时跳过重复求解"，需确认不为"事件触发"表述撞车。
- STEER（目标选择层）：与 DAIB 同层。必须把实现改成与 DAIB 等价或更保守的语义：**连续失败确认（而非累计计数）+ 目标保持 + margin + 同目标去重**。若当前实现与 DAIB 语义不等价，优先修代码再跑正式对比。

## 本轮待办
1. 等 formal batch 结果，按 R3 逐层归因。
2. 若 STEER 触发率低：检查失败是否连续、冷却是否过宽（120s takeover cooldown 残留？）、是否被 `prct_backoff_enabled_` 等旧开关挡住。
3. 网络恢复后补 VORL-EXPLORE 全文与 MEF-Explore 全文，更新本记录。
