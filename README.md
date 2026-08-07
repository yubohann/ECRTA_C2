# ECRTA_C2

C2-Explorer 改进实验库。当前不在主方法中使用 peer takeover、执行时间残差校准或旧的 B1+ v4；它们已经被本地机制审计否定，只作为历史负结果保留。

## 当前状态

- 三方法并行方案已定稿并实现：REACH-C2、SVR-C2、STEER-C2。
- 方法说明见 `docs/THREE_METHODS_PARALLEL_OVERVIEW_20260808_zh.md`。
- 方法工作副本：`/home/c2dev/c2_explorer_reproduction/workspace/reachability_retry_c2_method`。
- 统一 `method_mode=baseline|suppress|reach|svr|steer`，launch 参数和遥测文件已接上。
- 已完成第一轮机制 pilot，确认代码能启动、能输出 `failures.jsonl / method_events.jsonl / command_events.jsonl / task_events.jsonl`。
- 当前正在跑高失败格子：`open_plan_office / 3 UAV / 5 m / 180 s` 全时长成对 batch。

## 固定边界

- C2 三张官方地图：`open_plan_office`、`cubicle_office`、`octa_maze`。
- 不修改 LiDAR 探索任务、传感器、无人机动力学、通信协议、LKH/ACVRP、终止条件或评价指标。
- 不修改 `upstream/c2_explorer_official`。
- OpenGL 4.6 到 3.3 的 WSLg 兼容补丁必须披露，不是算法贡献。

## 三方法

- REACH-C2：把局部 A* 与轨迹规划执行失败证据反馈到 ACVRP/LKH 分配代价，做可回退的风险感知分配。
- SVR-C2：给任务加语义身份、执行回执、租约边界，在任务有效性/净收益判定通过后才调用原始分配器。
- STEER-C2：在规划命令层做目标最短保持、连续阻塞确认、切换 margin、同目标去重、负载感知候选选择和单 frontier 内视角轮换。

三方法共享同一套 B0/B1 对照和遥测 schema，但机制独立，后续必须同时启用、成对比较，并继续优化所有方法。

## 历史负结果

- Peer takeover：端到端无稳定收益，不作为主方法。
- ECRTA 执行时间残差校准：机制审计未通过，不包装成时间上界或鲁棒分配。
- B1+ v4 local-evidence quarantine：成对收益低于预注册阈值，保留为历史实验。

## 当前脚本

- `scripts/run_scene_pilot.sh`：单实例运行器，支持 `METHOD_MODE`、方法参数和 `PRCT_RUN_FULL_DURATION`。
- `scripts/run_three_method_batch.sh`：B0/B1/REACH/SVR/STEER 成对 batch 运行器。
- `scripts/analyze_telemetry.py`、`scripts/audit_peer_handoff_active.py`：遥测聚合与审计。

## 下一步

1. 完成高失败格子的三方法机制 pilot。
2. 若 STEER 能打断同一目标重复失败链，检查是否来自真实视角轮换而非“少跑 A*”。
3. 若 REACH/SVR 没有触发，先找失败率更高的实例或扰动条件，不能把无失败实例写成收益。
4. 统计成对 batch，所有失败/超时样本保留，不删除、不挑种子、不放宽阈值。