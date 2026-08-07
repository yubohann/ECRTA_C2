# ECRTA_C2

C2-Explorer 改进实验库。当前不在主方法中使用 peer takeover、执行时间残差校准或旧的 B1+ v4；它们已经被本地机制审计否定，只作为历史负结果保留。

## 当前状态

- 三方法并行方案已定稿并实现：REACH-C2、SVR-C2、STEER-C2。
- 方法说明见 `docs/THREE_METHODS_PARALLEL_OVERVIEW_20260807_zh.md`。
- 方法工作副本：`/home/c2dev/c2_explorer_reproduction/workspace/reachability_retry_c2_method`。
- 统一 `method_mode=baseline|suppress|reach|svr|steer`，launch 参数和遥测文件已接上。
- 已完成第一轮机制 pilot，确认代码能启动、能输出 `failures.jsonl / method_events.jsonl / command_events.jsonl / task_events.jsonl`。
- 当前协议已固定 `LKH_SEED`；下一步先用固定 seed 搜索可复现高失败 B0 实例，再成对运行 B0/B1/REACH/SVR/STEER。

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

- `scripts/run_scene_pilot.sh`：单实例运行器，支持 `METHOD_MODE`、`LKH_SEED`、方法参数和 `PRCT_RUN_FULL_DURATION`。
- `scripts/run_three_method_batch.sh`：B0/B1/REACH/SVR/STEER 成对 batch 运行器，支持 `LKH_SEED`。
- 当前协议固定 `LKH_SEED`；所有成对运行必须使用同一 `lkh_seed`，未记录 seed 的旧日志不参与公平比较。
- `scripts/search_b0_fixed_seed.sh`：逐固定 seed 搜索高失败 B0 实例，不产生方法收益结论。
 - `scripts/analyze_telemetry.py`：正式遥测聚合；`scripts/audit_peer_handoff_active.py` 仅保留给历史 peer 日志审计，不参与新协议。
 - `scripts/verify_three_method_gate.sh`：新协议硬门禁检查，确保旧 peer/C3 开关不进入 B0/B1/REACH/SVR/STEER 启动链。

## 下一步
1. 使用固定 `LKH_SEED` 搜索可复现高失败 B0 实例。
2. 在同一固定 seed 上完成 B0/B1/REACH/SVR/STEER 机制 pilot。
3. 若 REACH/SVR 没有触发，先找失败率更高的实例或扰动条件，不能把无失败实例写成收益。
4. 统计成对 batch，所有失败/超时样本保留，不删除、不挑种子、不放宽阈值。
 5. 每次批量前运行 `scripts/verify_three_method_gate.sh`，并保留 `method_check.tsv`。
