# Introduction + Related Work (draft v1)

## 1. Introduction

多无人机（multi-UAV）自主探索未知室内环境是搜索救援、设施巡检与建图的核心任务。现有系统普遍采用"任务表示 → 任务分配 → 局部规划 → 执行"的分层流水线：分配器在通信分量内把未知空间拆分为连通任务单元并求解 ACVRP（通常由 LKH 求解），每架无人机随后对分配到的前沿目标进行局部路径规划并执行。C2-Explorer [arXiv:2603.07699] 以连通性感知的任务表示与连续性惩罚在此类系统中取得了显著收益（较 RACER、FAME 平均探索时间降低 43.1%、路径长度降低 33.3%）。

然而，分层流水线存在一个执行层面的断层：**分配器与目标选择器不感知局部规划的可行性**。我们冻结并复现 C2 官方发布版（commit fd1c76a）后，在官方三张地图的高失败格子中观测到一种稳定的执行失败链：局部 A* 对同一（前沿、视点）目标反复返回 `open_set_exhausted`，单次任务中同一目标重复失败最多可达 258 次（cubicle_office / 4 UAV）乃至 463 次（open_plan_office / 2 UAV），占满 180 秒窗口内单机超过 70% 的规划调用，导致等待、卡死与团队 makespan 的严重长尾。该失败链不是求解器问题（ACVRP p95 < 40 ms），也不是算力问题（A* 搜索图不可达而非超时）。

本文不改变 C2 的三张官方地图、探索任务、传感器模型、无人机动力学、通信协议、ACVRP/LKH 后端、终止条件与评价指标，在三个分层上引入**执行失败证据的闭环反馈**：

- **REACH（分配层）**：将可验证的 A* 失败证据折算为 ACVRP 代价的风险因子，使分配器避免向实测不可执行的区域持续分配任务；
- **SVR（任务语义层）**：对候选任务语义摘要（digest）匹配的分配请求复用上次分配，消除候选集未变时的冗余 LKH 求解；
- **STEER（目标选择层）**：采用"目标保持-连续确认-视点轮换-切换 margin"的目标选择纪律，打断单目标重复失败链。

三者均可独立开关（method_mode），从而构成 B0（原始 C2）/ B1（纯重复失败抑制）/ REACH / SVR / STEER 的可审计消融。在官方固定格子上，我们报告 makespan、完成率、失败链抑制、覆盖率代理、碰撞与求解开销的成对统计（bootstrap 95% CI 与 Wilcoxon 检验）。

**贡献**：
1. 首个对 C2-Explorer 执行层失败链的量化审计（公开基线：失败类型、重复链结构、按机分布）；
2. 三方法分层反馈机制，全部只消费本地可观测执行证据，不引入学习、不改任务定义；
3. 官方固定格子上的成对统计对比与负结果保留（可复现实验协议、日志与脚本全公开）。

## 2. Related Work

### 2.1 多机器人探索与任务分配
- RACER [T-RO 2023]：通信约束下多机探索的 VRP 分配；
- FAME：快速自主探索分配基线（C2 的对比对象）；
- C2-Explorer [arXiv:2603.07699]：连通性感知任务表示 + 连续性惩罚 ACVRP/LKH，本文的冻结基线；
- Science Robotics 2023：任务表示粒度影响探索效率（固定双分辨率）；
- GVP-MREP [IROS 2024]：动态拓扑图 + Voronoi 所有权分区；
- PC-Explorer [RA-L 2025]：低带宽受限去中心化探索；
- LECES [RA-L 2024]：低带宽协同探索；
- CBBA-ETC：事件触发一致性束算法（"事件触发"表述已有先例，不作贡献）。

### 2.2 执行可行性感知
- VORL-EXPLORE [arXiv:2603.07973, 2026]：提出 execution fidelity（局部可导航性的共享估计），耦合进 Voronoi 目标并驱动 A*/RL 仲裁。fidelity 为**预测式**学习模型；本文 REACH 为**实测失败证据**（不学习、可审计），且分配器为 ACVRP/LKH 而非 Voronoi。VORL 处理"执行难度未知"，本文处理"执行失败已发生且重复重试"。
- Online Path Repair [RA-L 2024]：UAV 故障后其余机接管覆盖路线（任务级接管，非目标级重试抑制）。
- DFGP [JCDE 2026]：障碍密集环境的 makespan-aware MRTA 与 dead-end recovery。
- Right Place, Right Time [JAIR 2024]：任务到达时间/位置不确定下的 MRTA。

### 2.3 目标选择与失败恢复
- DAIB-Explorer [GitHub YYY0702/DAIB-Explorer]：单机探索的目标选择包含 goal_min_hold_time、blocked_streak 连续确认（goal_blocked_confirm_updates）、goal_switch_margin 与同目标去重。本文 STEER 与之一致地采用"保持-确认-切换"纪律，并扩展为：(i) 单前沿内多视点轮换；(ii) 证据门控冷却释放（map_version / 证据哈希变化即解除）；(iii) 多机 C2 场景下与分配层/任务语义层联动。
- MEF-Explore [arXiv:2505.23376, TASE 2025]：时长自适应目标分配（duration-adaptive goal assigning），动机为通信受限信息共享；本文的保持时间针对局部规划失败链。
- Applied Sciences 2026：事件触发局部重分配（失败/新任务才重调度）。

### 2.4 学习式方法（不采用及原因）
- MARL/MAPPO 探索 [RA-L 2022 MADE-Net, ICRA 2022 CapAM, Sensors 2024 TBDE-Net]：训练环境通常远多于三张；C2 仅三张官方地图，地图级训练-测试泄漏风险高，且本方法以可审计确定性机制为目标，不引入训练随机性。

## 3. Method
见 PAPER_METHOD_SECTION_DRAFT_v1.md（REACH/SVR/STEER 详述与实验协议）。

## 4. Experiments
见 PAPER_DRAFT_RESULTS_20260808.md（结果表待填充）。
