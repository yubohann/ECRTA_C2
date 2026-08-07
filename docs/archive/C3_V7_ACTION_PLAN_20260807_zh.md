# C3 v7: 回执闭环、trust 语义与接管预算

## 1. 结论先写清楚

当前主方法仍是 C3: Trust-Gated Marginal-Cost Reallocation，不是 PRCT B3，也
不是 ECRTA。PRCT 的 peer takeover 已经实测没有端到端收益，这个结论保留；但
我们不再因此否定 "证书 + 边际收益 + 回执 + 接管" 的机制，而是修掉 C3 自身
暴露出的状态机问题，再用相同数据集做更严格的成对实验。

v5/v6 的结果只用于机制审计，不写进论文结论。

## 2. v5 暴露的根因

c3_open3_v5_003 是第一个真正触发 takeover 的 pilot，但也暴露了两个错误：

1. owner 收到 ACCEPTED 后约 5 ms 就 clear peer handoff，立刻回到 PLAN_TRAJ，
   然后马上再次 A* 失败，再次发送 takeover；
2. 同一 frontier 在 120 s 内被发送 20 次 takeover，prct_takeover_suppressed
   达到 184 次，最终 2/3 FINISH。

事件时间线证据：

- 1786038838.1734: owner 发送 takeover，target=2；
- 1786038838.1736: peer 返回 ACCEPTED；
- 1786038838.1781: owner 记录 receipt_complete=ACCEPTED，错误清空等待；
- 1786038838.1782: owner 从 WAIT_HANDOFF 回到 PLAN_TRAJ；
- 1786038838.2411: peer 实际完成接管并返回 COMPLETED；
- 1786038838.4338: owner 已经再次 A* 失败，开始新一轮重复。

这说明问题不是 "peer 不能接管"，而是 "ACCEPTED 被当成终态回执，导致 owner
不等 COMPLETED/ABORTED 就重复抢占同一个目标"。

## 3. v6 的说明

c3_open3_v6_003 使用 30 s takeover 冷却后 3/3 FINISH，makespan 代理约 65.4 s。
但这轮没有出现 A* open_set_exhausted，也没有触发任何 C3 takeover，所以它只能
证明 "该实例没有失败链"，不能证明 "v7 修好了重复接管"。

该轮日志还记录了一次 Drone 2/Drone 3 碰撞和收尾阶段的 boost::lock_error。碰撞
属于运行期安全事件，必须保留在失败审计中；boost::lock_error 出现在 FINISH 后
收尾阶段，仍需确认是否与进程退出时序有关，不能直接忽略。

## 4. C3 v7 修改内容

### 4.1 ACCEPTED 不再是终态回执

peerTakeoverReceiptCallback 现在只有 COMPLETED、ABORTED、STALE、REJECTED
会设置 peer_handoff_observed_。ACCEPTED 只记录状态，owner 继续等待
COMPLETED/ABORTED，或等到 prct_peer_handoff_timeout_s 后回退原 C2。

### 4.2 trust 只统计完成或失败

updatePeerTrust 不再把 ACCEPTED 记为 success。只有 COMPLETED 增加 alpha；
REJECTED、ABORTED、STALE 增加 beta。这样 trust 描述的是 "接管后是否能闭环完成",
而不是 "peer 是否愿意先收下任务"。

### 4.3 新增 takeover 尝试上限

新增参数 c3_max_takeover_attempts，默认 3。同一 frontier+goal+map_version+
owner 的接管尝试达到上限后，不再进入 PRCT 查询/接管，记录
c3_takeover_exhausted 并回退原 C2 目标选择，避免对同一不可解目标反复轰炸。

### 4.4 参数与脚本同步

- C2ExplorationManager.h/.cpp 增加 c3_max_takeover_attempts；
- open_plan_office、cubicle_office、octa_maze 三个 launch 增加
  c3_max_takeover_attempts arg/param；
- run_scene_pilot.sh 支持第 28 个参数，并在 run_manifest 和 c3_check 中记录。

## 5. 当前验证状态

已完成：

- v7 修改后 catkin_make -j2 成功，构建输出只有既有 warning；
- 代码中已确认 c3_max_takeover_attempts 进入 header、cpp、launch；
- 尚未完成 v7 端到端 pilot。

接下来至少完成：

1. 在 open_plan_office/3 UAV/5 m 通信下，用同一协议重跑 v7 pilot，目标复现
   有长失败链的实例；
2. 统计 takeover sent、ACCEPTED、COMPLETED、ABORTED、exhausted、
   WAIT_HANDOFF 等待时长，确认没有重复发送同一 frontier；
3. 检查是否仍有碰撞、boost::lock_error、超时或未 FINISH；
4. 再跑 v6 同实例或新实例，确认无失败链时行为不退化；
5. 只有多实例成对统计通过后，才进入正式 B0/B1/B2/C3 对比。

## 6. 预注册门槛不变

- C3 相对 B1 的成对 makespan 中位改善 >= 10%，或未完成率改善 >= 20 个百分点；
- 覆盖率、总路径、碰撞、LKH 失败、在线时延不得系统性退化；
- 失败、超时、碰撞、ABORT、exhausted 全部保留在分母；
- 默认不引入 RL、LLM、VLM、QD、CARTA 或世界模型；
- 本文档只是机制修复与验证计划，不是论文结论。
