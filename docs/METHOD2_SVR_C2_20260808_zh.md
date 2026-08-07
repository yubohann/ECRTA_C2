# METHOD2: SVR-C2

日期：2026-08-08
状态：三方法并行方案的 Method 2，先做机制审计和成对实验，不构成投稿结论。

## 1. 一句话主张

在不修改 C2 任务生成规则、ACVRP/LKH、局部规划、三张 PCD 地图和评价定义的前提下，SVR-C2 为 C2 的连通性任务增加“语义身份、执行回执、租约边界”三层记录，并在任务有效性和净收益判定通过后才调用原始 C2 分配器，减少断连、split/merge、旧消息乱序和重复覆盖造成的无效重分配。

## 2. 为什么是这条线

C2 官方源码已经有 Proposal -> Commit -> Finalize、ACK、Cancel、超时、回滚和 host failover，不能把这些工程机制当作新贡献。真正可验证的缺口是：

- MeetingOpt 传输 stamp、grid ID、中心和凸包，但没有每任务的 connectivity-graph epoch、task revision、支持区域摘要、owner lease epoch 和 execution receipt；
- DroneState 周期广播只能靠“中心距离约 1 m”的启发式恢复 split-hull 任务语义；
- C2 没有对“重分配值不值得”做量化，LKH 调用和协议开销也未纳入论文指标；
- 本地 peer takeover 证明“peer 可能可达”不够，必须在任务状态层先证明“同一任务、仍有效、切换有净收益”。

## 3. 方法结构

### 3.1 任务语义记录

```text
task_uid = (creator_uav, birth_epoch, local_component_id)
task_revision
support_digest
task_state = proposed | active | completed | invalid | unreachable
owner_id
lease_epoch, lease_expiry
receipt = {started, progress, completed, invalid, unreachable}
```

support_digest 是任务凸包、粗 grid、未知体素量和连通分量标签的轻量摘要，不是完整地图，也不是神经网络特征。task_revision 只在未知量、凸包、连通分量或可达性实质变化时递增。receipt 来自真实执行。

### 3.2 三个判定

1. 同一性：新旧 descriptor 是否指向同一任务；不能用中心距离启发式继承所有权。
2. 有效性：task_revision/support_digest 是否一致、lease 是否有效、receipt 是否表明任务仍有信息增益。
3. 净收益：

```text
Delta J = C_continue - (C_reallocate + C_switch + C_comm + C_solver + C_stale_risk)
```

只有 Delta J 的下置信界大于 0 且任务语义有效时才调用原始 C2 ACVRP/LKH。C_solver 从真实日志估计任务数、UAV 数、矩阵构建时间和 LKH wall time，不拍脑袋。

### 3.3 安全边界

- 模型证据不足时保守继续原任务或等待下一次可通信接触；
- lease 只是断连时避免双 owner 的安全边界，不是主贡献；
- 不引入 RL、VLM、世界模型；
- 不把 event-triggered reallocation 单独作为创新主张。

## 4. 与 REACH-C2、STEER-C2 的分工

- REACH-C2 改分配代价，SVR-C2 改任务状态和“是否值得重分配”；
- STEER-C2 改规划命令层的目标保持与替换，SVR-C2 不改局部命令，只改任务语义和重分配触发；
- 三方法共享统一 failure/task event schema，但在代码中使用独立 method_mode 和独立 telemetry 文件，不互相依赖。

## 5. 实验设计

### 5.1 机制审计

先只读记录：

- task_uid 匹配错误数；
- split/merge 后 ownership 继承错误数；
- 旧消息导致的重复分配数；
- 断连重连后的双 owner 或空 owner 时长；
- 每次重分配的 LKH 和协议开销；
- 无效飞行距离与重复覆盖率。

### 5.2 扰动实验

- 丢包 0/10/30/50%；
- 有界随机时延、乱序、短暂网络分区后重连；
- C2 自然 split/merge、frontier 消失、其他 UAV 覆盖；
- host 切换；
- 执行变慢或局部不可达，但必须是通信/执行扰动，不是新地图。

### 5.3 消融

- 原始 C2；
- C2 + 固定 TTL；
- 仅 version/digest；
- 仅 lease；
- 朴素阈值重分配；
- always-reallocate；
- 完整 SVR-C2。

## 6. 核心指标

- 完成/覆盖率、探索时间、总路径；
- 失效任务执行距离和时间、重复覆盖率；
- 任务所有权冲突数、切换次数、重分配次数及真实收益；
- LKH 与完整分配 P50/P95/最大 wall time；
- 消息数、字节数、端到端提交时延；
- 重连恢复时间、version mismatch 取消数、lease 安全释放数。

## 7. 预注册门槛

- 机制审计中必须出现可量化的任务身份漂移或无效重分配，且 SVR-C2 能降低其数量；
- SVR-C2 相对最优简单消融在 makespan 或重复覆盖率上有统计可靠收益；
- LKH、协议和在线时延不得系统性恶化。

## 8. 失败判定

若机制审计显示 C2 在三图自然运行中几乎没有任务身份漂移，或简单 version 字段已足够，则 SVR-C2 不作为主方法，保留为负结果。

## 9. 文献锚点

- C2 官方源码：已有事务协议，SVR 只补语义版本与回执；
- Event-Triggered Adaptive Consensus for Multi-Robot Task Allocation（arXiv:2604.06813）：事件触发不能单独作为新点；
- 本地 C2 citation audit：没有可核验的 C2 后续引用，不能声称解决遗留问题；
- Energy-Balanced Task Allocation and Dynamic Rescheduling：事件触发重分配可借鉴，但需要净收益量化。
