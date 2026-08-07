# REACH-C2: Execution-Aware Reachability Allocation

日期：2026-08-07
状态：方法设计草案，不构成投稿结论

## 1. 为什么放弃前几条主线

### 1.1 ECRTA-C2 的执行时间校准

该主线假设 C2 的分配代价系统性低估真实完成时间，并希望通过在线执行残差构造时间上界。本地机制审计没有找到稳定、可预测、足以改变分配排序的执行时间错配。它被否证，不能重新包装成主贡献。

### 1.2 PRCT-C2 的 peer takeover

该主线假设 owner 的局部 A* `open_set_exhausted` 后，peer 用局部地图证明可达并接管目标。机制审计发现 peer 可达率很高，但端到端没有稳定收益，且引入地图一致性、证书过期、重复覆盖和等待开销。peer takeover 不再进入主方法。

### 1.3 B1+ v1-v5 的失败冷却与驱逐

B1 的重复失败抑制有价值，但 B1+ v5 在正式 batch 中只有 1/5 的 run 实际触发机制，其余 run 只是随机 makespan 波动。v5 使用 `prct_evict_on_first_failure=true`，一次失败就驱逐目标，缺少“确认阻塞”和“目标保持”机制，比 DAIB-Explorer 的做法更激进，不稳定是预期中的。

## 2. 从网上刊会和开源实现得到的关键证据

### 2.1 VORL-EXPLORE（arXiv 2603.07973）

VORL-EXPLORE 明确指出：层级式多机器人探索把 frontier 分配与局部导航解耦，会使系统在密集和动态环境中变脆；分配器不知道执行难度时，机器人会在瓶颈处聚集、产生震荡重规划和冗余覆盖。

它提出 `execution fidelity`，把局部可导航性耦合进分配目标，并在全局 A* 和 reactive policy 之间做风险感知仲裁。这直接支持“执行证据必须回到分配层”，而不是只做失败后的补丁。

### 2.2 MEF-Explore（arXiv 2505.23376 / TASE 2025）

MEF-Explore 把“持续给机器人分配新目标”识别为卡死来源。它的 duration-adaptive goal-assigning module 只在以下条件满足时才换目标：

- 机器人已到达当前目标；
- 当前目标耗时超过 `kref * distance / vmax`；
- 或存在明确更好的候选目标。

它还把“所有机器人卡死 120 秒”直接定义为失败，因此实验能测出成功率差异。这提示 C2 改进也应该把“卡死、重复失败、未完成”作为主指标，而不是只比较 makespan。

### 2.3 DAIB-Explorer（开源 ROS1）

DAIB-Explorer 的单机目标选择逻辑包含：

- `segmentBlocked(start, goal)` 判断当前目标是否被障碍挡住；
- `blocked_streak_` 和 `goal_blocked_confirm_updates`，默认连续确认 3 次才判定目标阻塞；
- `goal_min_hold_time_s`，避免过早换目标；
- `goal_switch_margin`，避免目标来回震荡；
- `goal_timeout_s` 和 `same_goal_tolerance`，避免重复发布同一目标；
- 无可行目标时进入 `WAIT_FOR_FRONTIER`。

这些机制不需要 peer 接管，直接在单机执行层解决重复 A* 失败。

### 2.4 动态重分配类工作

2026 年的 Energy-Balanced Task Allocation and Dynamic Rescheduling（Applied Sciences）在检测到机器人失败或新任务后，用事件触发局部贪心插入实现快速任务接管。nubot-nudt/dynamic_task_allocation 使用拍卖、vacancy chain 和 DQN 做动态任务分配。

这些工作的共同点是：任务语义是共享、可撤销、可重新分配的。C2 的原始 ACVRP/LKH 分配不是这种语义，因此直接照搬动态重分配会改变原论文任务定义，违反项目边界。

## 3. 新方法：REACH-C2

全称：Execution-Aware Reachability Allocation for Connectivity-aware Multi-UAV Exploration。

一句话主张：在不改变 C2 三图、LiDAR 探索任务、传感器、动力学、通信协议、ACVRP/LKH 后端、终止条件和评价定义的前提下，把本地 A* 与轨迹执行的失败证据反馈到任务分配代价，并用“确认阻塞 + 目标保持 + 事件触发本地再分配”降低重复失败链、卡死和超时长尾。

### 3.1 固定边界

- 不修改三张官方 PCD 地图；
- 不修改 LiDAR 探索任务、无人机动力学、通信协议；
- 不修改 ACVRP/ATSP 建模、LKH 调用和原始终止条件；
- 不修改覆盖率定义和评价指标；
- 不修改 upstream 快照；
- 不加入 RL、LLM、VLM、像素世界模型、QD、CARTA 作为主方法；
- OpenGL 4.6 到 3.3 的 WSLg 兼容补丁继续披露，不计为算法贡献。

### 3.2 模块 A：执行可达性证据

按 `map_version + region/frontier_id + owner_id` 维护轻量在线证据：

```text
astar_success_count
astar_fail_count
trajectory_fail_count
blocked_streak
last_failure_time
goal_set_time
evidence_hash
```

每次 A* 成功或失败、轨迹请求失败、目标被覆盖时更新。地图版本变化后，旧证据只能衰减，不能直接套用。

### 3.3 模块 B：风险感知分配代价

在 C2 原分配代价基础上增加执行可达性项：

```text
C_alloc = C_nominal * (1 + lambda * rho(frontier, robot, map_version))
        + kappa * stuck_penalty
```

其中：

- `C_nominal` 是 C2 原有 `ViewNode::computeCost` 或 ACVRP 代价；
- `rho` 是有界执行风险，初始为 0，由失败率、阻塞确认次数、轨迹失败和证据新鲜度在线校准；
- `stuck_penalty` 只在当前目标已确认阻塞时进入；
- `lambda`、`kappa` 在批量实验前冻结；
- 风险项不改变连通性约束、不改变 LKH 求解结构，只改变候选分配代价。

实现位置建议复用现有 `prctFilterCooledTargets()` 和分配前的 candidate cost 计算，不对 C2 原始 LKH 文件格式做侵入式修改。

### 3.4 模块 C：目标保持与确认阻塞

参考 DAIB/MEF，增加以下可解释规则：

1. 当前目标有效时，不因候选列表变化而立即切换；
2. 当前目标必须在连续 `blocked_confirm_updates` 次检查中都被判定阻塞，才进入冷却；
3. 只有替代目标在考虑风险代价后仍优于当前目标超过 `switch_margin` 时才切换；
4. 对相同 `goal + map_version + owner` 禁止重复发布，除非地图证据变化；
5. 无替代目标时回退到原始 C2 的下一合法目标选择，不空转、不发送 peer takeover。

### 3.5 模块 D：事件触发本地再分配

仅在以下事件触发时进行局部再分配：

- 当前目标被确认阻塞；
- 某候选的执行风险超过阈值；
- 轨迹规划失败；
- 当前无人机成为 makespan 瓶颈的证据出现。

禁止周期性无条件重规划。再分配只改变受影响无人机的候选集或分配代价，不做跨机任务交接。

## 4. 与既有工作的边界

| 工作 | 相同点 | REACH-C2 的不同点 |
|---|---|---|
| VORL-EXPLORE | 执行难度反馈到分配 | 不改 Voronoi 分配，在 C2 固定 ACVRP/LKH 代价内做风险修正 |
| MEF-Explore | 减少频繁换目标 | 保留 C2 的 contiguity 与 LKH，不做 entropy-field 重写 |
| DAIB-Explorer | 确认阻塞、目标保持、margin | 增加多机分配层证据回传与事件触发再分配 |
| ECRTA-C2 | 使用执行反馈 | 不预测完成时间上界，只预测并惩罚执行失败风险 |
| PRCT-C2 | 使用 A* 失败事件 | 不再依赖 peer 局部地图证书和 takeover |

## 5. 实验设计

### 5.1 主矩阵

优先选择已知高失败率格：

- `open_plan_office / 2 UAV / 5m`
- `cubicle_office / 4 UAV / 5m`
- `octa_maze / 4 UAV / 5m`

每个配置至少 10 个成对 repeated instance，目标 20。必须记录未完成、超时、A* 失败、重复失败链、轨迹失败和 LKH 失败，不允许从分母删除。

### 5.2 消融

- B0：原始 C2；
- B1：已有重复失败抑制/冷却；
- B2：只增加执行可达性风险代价；
- B3：完整 REACH-C2（风险代价 + 目标保持/确认阻塞 + 事件触发本地再分配）。

### 5.3 核心指标

- A* 失败次数；
- 同一目标重复失败链长度；
- FINISH 率；
- makespan 中位数与均值；
- p90/RMST；
- 覆盖率；
- 总路径长度；
- 碰撞、不可行轨迹、断连和 LKH 失败；
- 在线规划时延 p50/p95。

### 5.4 预注册门槛

在批量实验前冻结，不事后放宽：

- B3 相对 B1 的 makespan 成对中位改善 >= 10%，或 FINISH 率改善 >= 20 个百分点；
- 重复失败链长度显著下降；
- p90/RMST、覆盖率、总路径、碰撞、LKH 失败、在线时延不得系统性恶化。

## 6. 可证伪条件

以下任一情况成立时，REACH-C2 不能作为投稿主方法：

- 在高失败率格中，A* 失败链仍稀少或可被简单目标保持消除；
- 执行风险项无法在 held-out instance 上稳定改善分配；
- B3 不能超过 B1，说明冷却/抑制已经足够；
- 风险项改善 makespan 但以覆盖率、路径长度或安全性退化为代价。

## 7. 参考文献与来源

- C2-Explorer：https://arxiv.org/abs/2603.07699 ，OpenAlex W7134813111
- VORL-EXPLORE：https://arxiv.org/abs/2603.07973
- MEF-Explore：https://arxiv.org/abs/2505.23376 ，IEEE TASE DOI 10.1109/TASE.2025.3575237
- DAIB-Explorer：https://github.com/YYY0702/DAIB-Explorer
- Energy-Balanced Task Allocation and Dynamic Rescheduling：https://doi.org/10.3390/app16094311
- Dynamic Task Allocation for Exploration and Destruction：https://github.com/nubot-nudt/dynamic_task_allocation
