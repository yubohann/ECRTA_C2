# B1+ v4：Local-Evidence-Gated Goal Quarantine

## 1. 状态

状态：实现已通过编译，pilot 与正式 5 组成对 batch 均完成；正式门槛未通过，作为负结果保留。

前置结论：

- B1（固定 5 s 冷却）在 open_plan_office / 3 UAV / 5 m 的首轮成对运行中明显劣于 B0，问题来源是失败后的定时等待。
- B1+ v3（map-epoch goal quarantine）去掉了定时等待，5 组正式成对运行中相对 B1 的成对中位数改善约 14.68 s，但相对 B0 的中位数仍慢约 6.83 s，机制不稳定；且“全局 frontier viewpoint 集合变化就释放全部隔离”过粗。
- PRCT peer takeover 已实测无端到端收益，不进入主方法。
- ECRTA 执行时间残差校准机制审计未通过，不进入主方法。

## 2. 一句话主张

在 C2 固定三图、固定 LiDAR 探索任务、固定通信、固定局部规划器和固定 LKH/ACVRP 后端下，B1+ v4 把局部 A* 的 `open_set_exhausted` 视为可证伪执行事件；对同一目标连续达到确认阈值后，在当前目标局部占用/膨胀证据未发生变化前不再重复选中该目标，只有 A* 成功、目标消失或目标局部证据变化才允许释放，从而降低重复失败、等待开销和超时长尾，同时不改变原始分配与评价定义。

## 3. 为什么 v3 需要修改

v3 用整个 `ed_->points_` 的粗略哈希作为 map epoch。只要任意 frontier 视点集合发生变化，所有旧 epoch 隔离都会被清除。这会造成两种问题：

1. 无关 frontier 被其他无人机探索后，本机已确认失败的目标被过早释放，重复进入 A* 失败链。
2. 隔离状态与目标自身证据脱钩，无法回答“这个目标为什么现在值得重试”。

v4 保留 v3 的确认式隔离，但把释放条件改成目标局部证据：

- 目标本身或目标周围小邻域的 occupancy / inflated occupancy 发生变化；
- 目标从当前 frontier viewpoint 集合中消失；
- 本机 A* 已成功到达该目标。

## 4. 算法定义

### 4.1 失败确认

键：

`frontier_id + 0.1 m 取整目标坐标 + owner_id`

每个 `open_set_exhausted` 事件写入 `prct_retry_suppression_register`。连续失败计数达到 `prct_repeat_threshold=3` 后，该目标进入隔离。

### 4.2 隔离

在候选 frontier 过滤和 assigned-center fallback 中，隔离目标不再被选中。若当前任务候选全部隔离，回退到原始 C2 最近合法目标，并记录 `prct_all_cooled_fallback`。

### 4.3 释放

以下任一条件满足时清除隔离：

1. 本机对该目标 A* 成功，`registerPrctSuccess()` 重置计数；
2. 目标不再出现在当前 frontier viewpoint 集合；
3. 目标周围小邻域的 occupancy / inflated occupancy 证据哈希变化，说明地图局部连通信息已经更新。

### 4.4 安全回退

- 无目标可去时，使用原始 C2 的最近合法目标选择；
- 预测器、LKH、分配、通信协议和评价指标全部保持原样；
- 任何一次隔离释放都必须写入 telemetry，便于审计。

## 5. 实现变更

- `PrctCooldownEntry` 增加 `goal` 和 `goal_evidence_hash`；
- 新增 `prctGoalEvidenceHash()`，在目标周围固定半径内采样 occupancy / inflated occupancy；
- `isPrctTargetCooled()` 在 quarantine 模式下检查当前局部证据，证据变化即视为可重试；
- `updateFrontierStruct()` 不再因任意 frontier 集合变化清空隔离；改为按目标存在性和局部证据逐项更新；
- 保留 B0/B1/B1+ v3 的配置开关，新增 v4 由 `prct_backoff_enabled=true` 进入；
- launch 增加局部证据半径参数，默认 0.2 m。

## 6. 实验设计

当前第一轮：open_plan_office / 3 UAV / 5 m / 180 s，B0/B1/B1+ v4 成对 5 组。Pilot 指标为 FINISH 3/3、makespan 71.08 s、A* fail 12、quarantine release 3，只作为进入正式 batch 的机制依据，不作为论文结论。正式结果见 results/B1_PLUS_V4_BATCH_20260807/README.md；相对 B1 的中位改善约 3.87%，低于 10% 预注册阈值。

正式主矩阵：

- 三张 C2 官方地图；
- 2/3/4 UAV；
- 5 m 通信为主，补充 10 m、15 m、无限通信；
- 每个实例同一 repeated label 下成对运行 B0/B1/B1+ v4；
- 至少 10 个成对 repeated instance，目标 20 个。

主指标：

- FINISH 率；
- makespan；
- A* 失败次数和失败链长度；
- 隔离注册、隔离跳过、证据释放、目标消失释放次数；
- 覆盖率、总路径、碰撞、不可行轨迹、LKH 失败、在线规划时延 p50/p95。

预注册阈值：

- B1+ v4 相对 B1 的成对 makespan 中位数改善 >= 10%；
- 或未完成率改善 >= 20 个百分点；
- 同时 p90/RMST 和安全指标不得系统性退化。

## 7. 审计边界

- 不修改 C2 三图、LiDAR 任务、传感器、动力学、通信协议、LKH/ACVRP、终止条件和指标；
- 不修改 upstream 快照；
- 不把 OpenGL 3.3 WSLg 兼容补丁作为贡献；
- 不以单次运行、pilot 或视频作为结论；
- 若 v4 仍无稳定收益，保留负结果，不放宽阈值。
