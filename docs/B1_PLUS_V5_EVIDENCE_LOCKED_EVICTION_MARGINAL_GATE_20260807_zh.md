# B1+ v5：Evidence-Locked Goal Eviction with Marginal Replacement Gate

## 1. 状态

状态：v5.1 已编译通过；已跑 3 个机制 pilot，其中实例 003 在失败链上验证首次失败锁定、无替代空候选回退与 3/3 FINISH。正式 v5 batch 已在 tmux 后台运行，尚不能作为论文结论。

前置结论：

- B1+ v4 正式 5 组成对 batch 已完成，相对 B1 的成对中位改善仅约 3.87%，低于 10% 预注册阈值，作为负结果保留。
- v4 只做“达到 3 次失败后隔离”，但正式 batch 中仅 2/5 实例达到阈值，机制覆盖率不足。
- v4 隔离后没有检查替代目标的边际成本，可能在失败目标被跳过时选择远距离替代目标，抵消收益。
- repeated instance 标签不是官方种子，B0/B1/B1+ 之间随机差异很大，后续必须以失败链可复现和同初始状态为前提。

## 2. 一句话主张

在 C2 固定三图、固定 LiDAR 探索任务、固定通信、固定局部规划器和固定 LKH/ACVRP 后端下，B1+ v5 将局部 A* 的 `open_set_exhausted` 视为目标局部证据锁定的可证伪执行事件；当首选目标被锁定时，只有当存在低边际成本的替代目标时才驱逐首选目标，否则回退到原始 C2 目标选择，从而既降低重复失败等待，又避免隔离导致的远距离绕行。

## 3. 为什么 v4 需要修改

1. 阈值 3 造成确认延迟：正式 batch 中 run 4 连续失败 2 次但未触发隔离，机制没有介入。
2. 隔离决策只问“是否跳过”，没有问“替代目标要付出多少额外代价”。若替代目标距离很远，跳过可能比重试更慢。
3. 释放条件与目标自身证据绑定是对的，但缺少“当前是否有可接受替代”的条件，导致隔离可能产生新的 makespan 瓶颈。

v5 保留 v4 的局部 evidence hash 与目标消失释放，新增边际替代成本门控和首次失败即可锁定的配置。

## 4. 算法定义

### 4.1 失败确认

默认首次 `open_set_exhausted` 即建立证据锁定，避免重复 A* 浪费；`prct_repeat_threshold` 保留为可配置确认阈值。

键：`frontier_id + 0.1 m 取整目标坐标 + owner_id + evidence_hash`。

### 4.2 目标驱逐

候选过滤时计算：

- 首选成本：原始候选集中的最低 C2 cost。
- 最佳替代成本：未锁定候选集中的最低 C2 cost。
- 边际替代成本：`best_alternative_cost - first_choice_cost`。

若首选目标被锁定，且存在替代目标，且边际替代成本不超过 `prct_eviction_max_extra_cost`，则跳过首选并选择替代目标。

### 4.3 边际门控回退

若首选目标被锁定但当前候选集中没有可接受替代，则返回空候选集，交由上层全局非冷却可达目标回退；若全局也没有，才回退原始 C2 逻辑。禁止因为隔离而选择超出边际门的远距离目标。

### 4.4 释放

以下任一条件满足时解除锁定：

1. 本机对该目标 A* 成功；
2. 目标从当前 frontier viewpoint 集合消失；
3. 目标周围小邻域的 occupancy / inflated occupancy evidence hash 变化。

### 4.5 安全回退

- 无法计算替代成本、候选为空、边际门控不可用时，回退原始 C2 逻辑；
- 不修改 LKH/ACVRP、通信、动力学、传感器、终止条件和评价指标。

### 4.6 无替代候选回退

当首选目标已锁定且当前任务候选集中没有满足 `prct_eviction_max_extra_cost` 的替代目标时，v5 不再返回已锁定目标让上层重复 A*；它返回空候选集，使现有 C2 no-frontier 回退路径先在全局非冷却可达目标中搜索。只有全局也没有非冷却可达目标时，才回退原始 C2 最近目标选择，并记录回退事件。该修正来自 pilot：首版 v5 在唯一候选被锁定时仍反复重试同一目标，30 次 A* 失败全部落在同一 goal；修正后失败目标只尝试 1 次。

## 5. 实现变更

- 新增参数 `prct_evict_on_first_failure`（默认 true）与 `prct_eviction_max_extra_cost`（默认 20.0）。
- `prctFilterCooledTargets()` 增加边际替代成本计算；当冷却目标为首选且替代成本超限时保留冷却目标并记录回退。
- 保留 `prct_local_evidence_radius_m=0.2` 作为 launch 默认，并同步头文件默认值。
- 修复 v4 中 `prct_cooldowns_` key 与 map epoch 耦合导致旧条目无法释放的问题。
- 增加遥测：`prct_eviction_marginal_gate_reuse`、`prct_eviction_replacement_cost`、`prct_eviction_triggered`。
- 三张 launch 已增加两个 v5 参数；run_scene_pilot.sh 与 run_b1plus_batch.sh 已支持传递和校验这些参数。
- 新增遥测 prct_eviction_marginal_gate_no_alternative，并在无替代时返回空候选集。
- pilot 证据：open_plan_office/3 UAV/5m 实例 003，1 次 A* 失败、1 次登记、1 次无替代回退、0 次重复失败、3/3 FINISH、makespan proxy 68.24s。

## 6. 实验设计

### 6.1 机制门槛

先复现失败链，再进入端到端：

- 至少两个场景、多个 UAV 规模下，同一 repeated instance 能稳定出现 3 次以上 `open_set_exhausted`。
- v5 触发隔离后，`prct_eviction_triggered` 与 `prct_eviction_marginal_gate_reuse` 均可审计。
- 隔离不应造成远距离替代：边际门控回退事件必须有记录。

### 6.2 正式主矩阵

- 三张 C2 官方地图；
- 2/3/4 UAV；
- 5 m 通信为主，补充 10/15 m 与无限通信；
- 每个 repeated instance 成对运行 B0/B1/B1+ v5；
- 至少 10 个成对实例，目标 20 个。

### 6.3 预注册阈值

- B1+ v5 相对 B1 的成对 makespan 中位数改善 >= 10%，或未完成率改善 >= 20 个百分点；
- p90/RMST 与覆盖率、总路径、碰撞、LKH 失败、在线时延不得系统性退化；
- 机制触发实例单独报告，不允许用无失败实例的随机 makespan 差异论证收益。

## 7. 审计边界

- 不修改 C2 三图、LiDAR 任务、传感器、动力学、通信协议、LKH/ACVRP、终止条件和指标；
- 不修改 upstream 快照；
- OpenGL 3.3 WSLg 兼容补丁必须披露；
- 若 v5 仍无稳定收益，保留负结果，不放宽阈值。
