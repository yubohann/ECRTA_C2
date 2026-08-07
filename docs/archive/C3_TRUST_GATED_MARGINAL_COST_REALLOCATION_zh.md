# C3 v3: Trust-Gated Marginal-Cost Reallocation

## 1. 问题定位

PRCT B3 的失败不是 peer 可达性证书没有价值，而是把 peer 可达误当成应该接管。正式数据中 B3 只发送 36 次 takeover、执行 33 次，却在 Open/3、Cubicle/4、Octa/4 三组对比中都没有稳定优于 B1。与此同时，B1 的重复失败抑制在 Open/3 有效，但 Cubicle/4 可能因盲目冷却仍可达目标而退化。

C3 保留 PRCT 的可达性证书、冷却、去重和回执框架，把决策规则改为：

1. 先估计 owner 继续等待或重试的保守成本；
2. 再计算 peer 接管的边际执行成本；
3. 只有边际收益超过冻结阈值，才允许接管；
4. 所有 peer 均不可达时才冷却目标；
5. 无收益 winner、低 trust、证书过期或无响应时，一律回退到原始 C2。

## 1.1 C3 v2 的修正

首个 C3 pilot 显示，`owner_stuck_cost = elapsed + fallback_penalty` 会在
同一目标只失败 1-3 次时给出 3-4 s 的成本，而 peer 边际成本通常为 8-15 s，
因此 C3 永远不会接管。这个结果不是“peer takeover 无价值”的证据，而是成本模型
没有区分“短暂失败”和“重复失败链”。

C3 v2 增加两个证据约束：

1. `c3_min_repeat_count=3`：同一 `frontier_id + goal + map_version + owner`
   失败次数低于阈值时，不启动 peer 查询，也不产生等待。
2. `c3_owner_repeat_cost_s=0.3`：每次额外重复失败按真实 B0 重试间隔 p50
   （0.273 s）计入 owner 卡住成本；该值来自正式 B0 日志，不是为产生接管而调参。

C3 v2 还修正了两个审计问题：

- 若证书窗口结束但 `pending_peer_certificates_` 为空，记录
  `no_peer_certificate` 并允许下一次失败重新收证，不冷却目标。
- 只有确实收到至少一个 peer 证书且全部无收益时，才把该目标冷却，避免
  响应延迟被误判为“无收益”后静默跳过 5 s。

## 1.2 C3 v3 的关键修复

离线回放正式日志后发现，C3 v2 的失败计数与 B1 重试抑制耦合，导致 C3 永远不会
遇到真正的长失败链：

- B0 存在大量长失败链：Open/3 单实例最多 704 次、704 次重复；
  Cubicle/4 单实例最多 22 次；Octa/4 单实例最多 10 次。
- 但正式 B2/B3 都开启 retry suppression，目标在第 3 次失败后即被冷却，
  因此 B2/B3 中最大重复次数只有 3，C3 没有机会看到长链。
- 原 registerPrctFailure() 只在 B1 抑制开启时计数；即使 C3 门控开启，
  关闭 B1 抑制也会导致 C3 的 repeat_count 恒为 0，C3 仍然不会触发。

C3 v3 的修复原则：

1. C3 使用独立失败计数 c3_failure_repeat_counts_，不再依赖 B1 抑制开关。
2. C3 模式关闭 B1 的“重复失败就冷却”，只保留证书门控冷却：
   - 所有 peer 不可达时冷却；
   - 收到可达证书但无边际收益时冷却；
   - 未收到证书、低重复次数或 takeover 成功前，不提前抑制目标。
3. C3 模式的目标过滤只跳过 takeover 冷却目标，避免冷却后继续空转重试。
4. pilot 脚本强制 C3 实验使用 retry_suppression=false，防止再次把 B1 的
   收益或限制混入 C3。

离线审计结果（results/C3_OFFLINE_AUDIT.json）：

- 156 个正式实例；
- C3 v2 在当前 B2/B3 日志上触发 0 次，其中 B3 实际发送 36 次 takeover
  被 C3 全部拒绝；
- 这不能证明“边际接管无价值”，因为 B2/B3 日志没有给 C3 提供长链样本；
- C3 v3 是否有效，必须在新协议 retry_suppression=false + peer_takeover=true
  的长链实例上重新验证。

## 2. 方法定义

### 2.1 Owner 卡住成本

当 owner 对目标 T_j 重复失败后，定义保守卡住成本：

    owner_stuck_cost = fallback_penalty
                     + alpha * elapsed_since_failure
                     + c3_owner_repeat_cost_s * max(0, repeat_count - 1)

其中：

- elapsed_since_failure 是当前失败链已经消耗的时间；
- fallback_penalty 是回退到下一个合法候选目标的保守惩罚，默认 3.0 s；
- alpha 默认 1.0，只做可解释标定，不引入学习模型。

### 2.2 Peer 边际成本

peer 返回 fresh 可达性证书时，owner 估计：

    peer_marginal_cost = path_length / nominal_speed
                       + load_weight * peer_load
                       + handoff_overhead
                       + (1 - trust) * trust_penalty

其中：

- peer_load 使用 peer 当前剩余目标数量估计；
- trust 是 owner 对 peer 历史证书和回执的贝叶斯估计；
- handoff_overhead 覆盖查询、状态交接和潜在重复探索开销。

### 2.3 接管条件

只有满足以下条件才发送 takeover：

    repeat_count >= c3_min_repeat_count
    trust >= trust_threshold
    owner_stuck_cost - peer_marginal_cost > benefit_margin

候选选择顺序：

1. expected_benefit 最大；
2. benefit 相近时路径长度更短；
3. 仍相近时 peer 状态年龄更小；
4. 仍相近时 peer_load 更轻。

### 2.4 证书门控抑制

当证书查询显示所有 fresh peer 均不可达时，才冷却 T_j。若存在可达证书但没有 benefit winner，不冷却 T_j，而是回退到原始 C2 的下一个合法目标选择。

## 3. 与已有方法的关系

| 方法 | 决策依据 | 保留价值 |
|---|---|---|
| B0 | 原始 C2 | baseline，用于量化 A* 失败链 |
| B1 | 重复失败就冷却 | 保留，但冷却必须被证书门控约束 |
| B2 | 只读证书 | 保留 shadow 审计 |
| B3 | peer 可达就接管 | 丢弃该决策规则，保留消息与回执框架 |
| C3 | trust + 边际收益 + 负载 + 共识式选择 | 新主方法 |

## 4. 代码修改点

- C2ExplorationManager.h：增加 C3 参数、PeerTrustEntry、证书成本字段和辅助方法。
- C2ExplorationManager.cpp：
  - 读取 C3 参数；
  - 在证书响应中计算 peer_marginal_cost 和 expected_benefit；
  - 修改 selectBestPeerCertificate 为 benefit 优先；
  - 在 hasPendingPeerHandoff 中执行 trust 和 benefit 门槛；
  - 在回执回调中更新 peer trust；
  - 增加 C3 遥测字段。
- launch 文件：增加 c3_enable_marginal_gate、c3_benefit_margin_s、c3_trust_threshold 等参数。

## 5. 实验门槛

1. 先回放 Open/3 和 Cubicle/4 日志，确认 B1 的退化是否来自冷却仍可达目标。
2. 再统计 owner_stuck_cost 与 peer_marginal_cost 的分布。
3. C3 相对 B1 必须达到预注册效应阈值：
   - 成对 makespan 中位数改善不小于 10%；
   - 或未完成率改善不小于 20 个百分点；
   - 同时不恶化覆盖率、碰撞、LKH 失败和在线时延。
4. 若 C3 仍无收益，保留负结果，不重新引入 RL、LLM、QD 或世界模型。

## 6. 当前 pilot 证据（不是论文结论）

- Open/3 C3 v2 pilot 004：3/3 FINISH；前两次失败被低重复门控跳过；
  第三次失败收到 peer 证书，但 owner_stuck 4.12 s < peer_marginal 12.64 s，
  无收益回退原始 C2。
- Cubicle/4 C3 v2 pilot 001：4/4 FINISH；本次实例无 A* 失败，未触发 C3，
  因此不构成对 B1 退化问题的判断。
