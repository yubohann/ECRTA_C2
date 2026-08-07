# THREE_METHODS_PARALLEL_OVERVIEW

日期：2026-08-08
状态：并行实现与对比的总入口，不是投稿结论。

## 1. 三方法一句话

- REACH-C2：把执行失败证据反馈到 ACVRP/LKH 分配代价，做风险感知分配；
- SVR-C2：为任务加语义身份、执行回执和租约边界，用同一性/有效性/净收益判定决定何时调用原始分配器；
- STEER-C2：在规划命令层做目标保持、连续阻塞确认、切换 margin 和负载感知下一目标选择，不重跑分配器。

## 2. 共同边界

- 不修改 C2 三张 PCD 地图、LiDAR 探索任务、传感器、动力学、通信协议、LKH/ACVRP、终止条件和评价定义；
- 不修改 upstream/c2_explorer_official；
- OpenGL 4.6 到 3.3 补丁继续披露，不计为算法贡献；
- peer takeover 与 ECRTA 时间上界不进入主方法；
- RL、QD、CARTA、LLM、VLM、像素世界模型默认不进入主方法。

## 3. 统一配置

新增顶层 ROS 参数 `exploration/method_mode`，取值：

- `baseline`：原始 C2，等价 B0；
- `suppress`：失败冷却/去重，等价 B1；
- `reach`：REACH-C2；
- `svr`：SVR-C2；
- `steer`：STEER-C2。

每个方法保留独立开关，但 batch runner 只接受 method_mode，不散落拼装 PRCT/C3 参数。

## 4. 统一事件 schema

所有方法必须输出以下 jsonl：

- failures.jsonl：A* 失败、frontier_id、goal、map_version、重复链长、失败原因；
- task_events.jsonl：task_uid、task_revision、support_digest、owner、lease、receipt；
- command_events.jsonl：goal set、goal keep、goal switch、cooldown、fallback；
- method_events.jsonl：risk cost、reallocation decision、net benefit、solver time。

现有 telemetry_drone_*.jsonl 保留，不删除。

## 5. 成对实验顺序

1. B0/B1 pilot 于高失败格子，确认事件可捕获；
2. REACH/SVR/STEER 各自单实例 pilot，确认不破坏启动和 FINISH；
3. 正式主矩阵：三图 x 2/3/4 UAV x 5 m，每格至少 10 个成对实例；
4. 补充矩阵：10/15 m/无限通信，每格至少 5 个成对实例；
5. 每轮统计后同时优化所有方法，不只优化当前第一。

## 6. 首轮主实验格子

- open_plan_office / 2 UAV / 5 m；
- cubicle_office / 4 UAV / 5 m；
- octa_maze / 4 UAV / 5 m。

这些格子已知失败事件多，避免 open_plan_office / 3 UAV 的低失败率随机波动。

## 7. 正式门槛

默认冻结：

- 相对 B1 的成对 makespan 中位改善 >= 10%，或 FINISH 率改善 >= 20pp；
- 重复失败链长显著下降；
- p90/RMST、覆盖率、总路径、碰撞、LKH 失败、在线时延不得系统性恶化。

不得在结果出来后放宽；若门槛不通过，保留负结果并继续修实现，不换故事。

## 8. 迭代协议

- 每轮结束后输出 ALL_METHODS_RANKING.md，列 B0/B1/REACH/SVR/STEER 的指标；
- 即使第一不变，也要审阅非第一方法是否因实现缺陷低估；
- 修复后重跑同 instance 标签，新增 instance ID，不覆盖旧日志；
- 若第一变化，则对所有方法继续优化再跑；
- 当连续两轮第一不变且非第一方法无法通过机制审计时，才冻结当前结论。

## 9. 交付物

- 三份方法 MD；
- 本总览；
- 统一 runner；
- failures/task/command/method 四类 jsonl；
- 成对实例原始日志与 rosbag；
- ALL_METHODS_RANKING.md；
- LIMITATIONS_AND_THREATS_TO_VALIDITY.md。
