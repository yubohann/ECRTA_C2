# ETA-C2 开发记录（2026-08-08）：bug 挖掘、修复、测试与实证结论

## 已发现并修复的 bug

| # | Bug | 修复 |
|---|---|---|
| 1 | 节点端 METHOD_MODE 白名单缺 `eta`，eta 模式被静默降级为 baseline | 白名单加 eta（+current_method_protocol） |
| 2 | 审计脚本 grep 错误文件（method_events.jsonl 而非 telemetry_drone_*.jsonl），服务事件误报为 0 | 修正审计脚本 |
| 3 | 服务时间锚点不稳定：region_tour_[1] 每 ~3s 被 ATSP 刷新重排；state.center_positions_.front() 每 ~1s 翻转 | 改为**网格锚点**（etaMatchGrid：状态中心最近 1m → center_grid_ids_，回退 hgrid） |
| 4 | etaCenterKey/etaServiceTime 残留字符串键（与 int 键 map 冲突）→ 编译错误 | 清理废弃函数 |
| 5 | 均衡阈值单位混乱（米 vs 秒） | 统一为秒（eta_min_improvement_m_=3.0s，travel/speed+service） |
| 6 | 多前沿分支漏挂服务时间钩子 | 三个分支全部挂钩 |

## 测试（写代码验证）

- `scripts/test_eta_balance.py`（离线单元测试，镜像 C++ 均衡算法）：
  - makespan 单调不增 ✓（200 随机实例）
  - 任务不丢失/不重复 ✓（200 随机实例）
  - 边界：空分配/单机/单边负载 ✓
  - 服务时间主导场景：重任务被移走、makespan 下降 ✓
- 在线验证：eta 模式激活、eta_balance/eta_service_grid 事件产生 ✓

## 实证结论（重要，诚实）

用网格锚点实测 C2 任务粒度：
- **每任务服务时间中位数仅 0.7-0.9s，最大 ~9s**——C2 的任务单元（网格）极细；
- 分配层负载 = 行程（30-40s）+ 服务（~15s），**ACVRP 解已近似均衡**（max_load 38.9 vs 40.6）；
- 均衡 pass 在阈值 3s 下几乎无移动空间（moves≈0）；阈值放宽则移动反而增加跨机行程、使 makespan 变差；
- **makespan 的 2-33s 完成时间差来自执行层**（每跳启停/规划开销，~150-180 次轨迹跳 × 每跳开销），**不是分配不均衡**。

## 对论文的含义

1. "分配层 makespan 均衡"在本基准上**无收益空间**——这是有测量支撑的负结果（与 VORL/IMD-TAPP 等 makespan 优化工作的设定不同：它们针对已知任务/大任务单元，C2 的任务单元 ~1-2s）。
2. 数据指向的真正瓶颈是**执行层细粒度启停**：~150 跳 × 每跳固定开销 ≈ 占 makespan 的 30-40%。文献中 MCFS（费马螺旋连续覆盖）、"任务粒度"相关工作（Science Robotics 2023）正对这一层。
3. ETA 的负载估计机制（网格服务时间 EMA）可保留为**诊断工具**（eta_service_grid 遥测），用于论文的执行层开销分析章节。

## 下一步选项（待用户定夺）

- A：转向"执行层启停开销"修复（如视角合并/长程目标选择——减少跳数），证据最充分；
- B：把 ETA 定位为"诊断+负结果"并入论文执行层分析；
- C：继续调 ETA 参数（如按网格聚合服务、跨机共享）——预计收益有限。
