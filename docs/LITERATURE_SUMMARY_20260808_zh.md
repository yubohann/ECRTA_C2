# 文献学习总结（2026-08-08，含原文精读）

## 已精读原文（下载/全文）

### 1. VORL-EXPLORE（arXiv 2603.07973，2026，全文 HTML）
- execution fidelity：8 特征逻辑回归门（拥挤、stuck 标志、到前沿距离、可行动作比、未知区比、阻塞密度、**A* 可行性标志**），自监督在线校准；同时调制 Voronoi 分配（Φ=u−λ(p)d−ρ(p)r）与 A*/RL 仲裁（双阈值滞回+驻留 K）；恢复覆盖机制。
- 借鉴：其"stuck/可行性标志"正是我们失败链证据的学习版——我们是确定性真值版（论文差异点）。其"不可行→恢复机动不空等"支持 STEER 修复。

### 2. DAIB-Explorer（GitHub，源码精读）
- 目标保持（goal_min_hold_time）+ 连续阻塞确认（blocked_streak≥confirm_updates）+ 分数 margin 切换 + 同目标去重 + WAIT_FOR_FRONTIER。单机。
- 我们的 STEER 语义已对齐；不能声称这些词。

### 3. MCFS（ICAPS 2024，arXiv 2403.13311，全文）
- 已知地图上的连续覆盖：等距 isolines → isograph → MMRTC（Min-Max Rooted Tree Cover，MIP 求解）→ Fermat 螺旋连续路径。核心目标=**makespan**；强调**连续平滑路径降低非完整约束机器人的减速/急转开销**；图增广+解精炼（PIS 分裂、重复访问拆分）降低重复覆盖。
- 对我们的意义：**"连续路径省启停开销"有文献背书**——支持 HOP 方向；但其假设已知多边形地图+离线，我们是未知环境在线 LiDAR——HOP 是我们场景下的对应物（远目标合并跳数，利用 10m 感知覆盖中途）。

### 4. LS-MCPP（AAAI 2024，arXiv 2312.10797，摘要+方法）
- 图分解网格 MCPP：ESTC + 局部搜索邻域算子，makespan 降低最高 35.7%。证明"后处理局部搜索均衡 makespan"有效——ETA 均衡的文献基础；但已知地图离线。

### 5. PA-MCPP（RA-L 2026，arXiv 2601.00580，摘要）
- 优先级感知 MCPP：按区域权重最小化加权延迟+makespan；两阶段（贪心分配+局部搜索、Steiner 树残差覆盖）。

### 6. SC-RRT（arXiv 2503.17005v2，2025，摘要）
- 低成本 2D 探索：RRT 扩展中可通行性检查+全局 RRT 剪枝**消除不可达前沿**，降低死锁/碰撞；序列化目标选择避免振荡。→ 与失败链问题同题。

### 7. 其他核实的相关摘要
- IMD-TAPP（2026）：分配+排序+轨迹联合优化 makespan（IPSO）。
- TRAITS（AAMAS 2026）/ STEAM-E-ITAGS（ISRR 2024）：特质型分配，makespan 预算。
- SABA / HRRA（2025）：农业 MRTA，能量约束下 makespan 均衡（锚定+拆分再平衡）。
- 机器人再分配（2025）：Voronoi 路标分区 + 推拉再分配，makespan。
- GVP-MREP（IROS 2024）：动态拓扑图 Voronoi 分配，通信高效。
- MEF-Explore（TASE 2025）：熵场+时长自适应目标分配。
- RegionGraph（2025）：分层区域图，减少全局重规划频率。

## 对当前方法方向的影响
1. **ETA（分配均衡）**：文献中 makespan 均衡都作用于**已知任务/离线或半在线**；我们的实测（任务 1-2s、分配已均衡）证明该层在本基准无空间——定位为负结果+诊断。
2. **HOP（执行层合并跳数）**：MCFS 背书"连续路径省启停"；实测跳数下降 10-25%，makespan 效应噪声大（n=2-3 不确定）——需更大 n 判定。
3. **失败链**：SC-RRT"消除不可达前沿"支持我们的证据化处理；VORL 的可行性标志支持"证据反馈"方向。

## 下一步文献动作
- 下载 MCFS 代码（github reso1/MCFS）与 LS-MCPP 代码对照实现细节（如需要）。
- 检索 "online exploration viewpoint merging / long-range goal" 是否已有未知环境版本（避免撞车）。
