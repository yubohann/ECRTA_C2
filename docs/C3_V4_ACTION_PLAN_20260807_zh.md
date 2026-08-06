# C3 v4: 修复证书窗口与信任反馈，保留 Trust-Gated Marginal Reallocation

## 1. 为什么不是全盘否定

C3 不是 PRCT B3 的失败重演，而是把 PRCT 的 "peer 可达就接管" 改成 "peer 可达且
接管边际收益明显为正时才接管"。B3 无端到端收益不能推出 peer takeover 无价值；
它只证明 "可达性证书不是接管信号"。C3 保留证书、冷却、去重、回执和回退框架，
把信号改成边际收益，方向正确。

C3 v3 的三个 pilot 证实代码路径已经生效：

- 独立失败计数生效，低重复次数不再触发证书查询；
- 收到证书但没有收益时记录 no_benefit_winner 并回退原 C2；
- 没有证书时记录 no_peer_certificate，不静默跳过目标；
- Open/3 三个实例全部 FINISH，说明 C3 没有破坏基线。

但这些 pilot 仍没有产生真实 takeover，不能作为收益证据。必须先把两个机制缺陷
修掉，再用真实长失败链验证。

## 2. 已确认的机制缺陷

### 2.1 证书窗口按名义网络时延设置，没有覆盖回调处理时延

prct_peer_cert_wait_s=0.25 的计时从 owner 发出查询开始，到 owner 的
hasPendingPeerHandoff() 做决定时结束。实际日志显示：

- peer 的 peer_local_map_reachability_response 在窗口结束前已经发出；
- 但 owner 收到并记录 peer_local_map_reachability_probe 的时间晚了约
  0.1-0.2 s；
- 因此部分成功证书在 owner 做 no_peer_certificate 决定之后才进入回调，
  被错误丢弃；
- c3_open3_v3_003 中 plan_seq=6/7 就是这类案例：peer 响应先到，owner 仍记录
  no_peer_certificate。

修法：证书收集不再等于固定 0.25 s，而是 "名义证书窗口 + 可解释的本地回调宽限"，
默认值从正式 B2/B3 与 C3 pilot 的查询到回调处理时延 p95 标定。若宽限结束后仍
无证书，才回退 no_peer_certificate。

### 2.2 peer 边际成本里的负载项被审计脚本漏掉，且默认权重需要校准

运行时代码的 peer_marginal_cost 已包含
c3_load_weight * peer_load，但 scripts/audit_c3_offline.py 只按路径长度、
handoff overhead 和 trust penalty 计算，导致离线审计比在线决策更乐观。

另外，C3 v3 pilot 中 peer_load 约为 7-9，c3_load_weight=0.5 会给 peer 边际
成本增加 3.5-4.5 s，和 7-13 s 的飞行路径同量级，这是 takeover 几乎不可能触发的
主要原因之一。负载项必须保留，但不能先验地给每个剩余目标 0.5 s；后续先用真实
任务执行日志标定 "增加一个任务对 peer makespan 的边际影响"，再做敏感性实验。

### 2.3 REJECTED 回执没有降低 trust

当前 updatePeerTrust() 只惩罚 ABORTED 和 STALE，REJECTED 不进入
alpha/beta，peer 拒绝一次接管后 trust 不变。这不符合 "历史回执可靠度" 的定义，
也削弱了 C3 的 trust 门控。修法：REJECTED 也按负证据更新 beta。

## 3. C3 v4 修改清单

1. 在 C2ExplorationManager 中新增 c3_peer_cert_grace_s，证书收集截止时间改为
   query_time + prct_peer_cert_wait_s + c3_peer_cert_grace_s，并受
   prct_peer_handoff_timeout_s 上界约束；
2. 在结果回调中允许证书在收集截止前进入 pending_peer_certificates_，避免
   "响应已到但回调处理稍晚" 被误判；
3. 在 telemetry 中记录 c3_cert_collection_deadline_s 等审计字段；
4. updatePeerTrust() 对 REJECTED 更新 beta；
5. 更新 scripts/audit_c3_offline.py，把 load_weight * peer_load 加入离线
   peer 边际成本；
6. 新增 scripts/measure_cert_latency.py，统计正式 B2/B3 与 C3 pilot 的
   查询到 owner 回调处理时延，作为默认宽限的依据；
7. 重跑 C3 v3 的 001/002/003 同协议 pilot，至少验证：
   - no_peer_certificate 显著减少；
   - 成功证书能进入边际收益比较；
   - takeover 只有在 expected_benefit > margin 时发送；
   - 无收益时仍回退原 C2；
   - 不出现固定等待、重复发送或死锁。

## 4. 门槛不变

C3 v4 仍服从 PRCT 项目的预注册门槛：

- 成对 makespan 中位改善 >= 10%，或未完成率改善 >= 20 个百分点；
- 覆盖率、总路径、碰撞、LKH 失败和在线时延不系统性退化；
- 如果最终没有收益，保留负结果，不把证书窗口修复写成主贡献；
- 默认不引入 RL、LLM、VLM、QD、CARTA 或世界模型。

当前文件只描述机制修复与验证计划，不是论文结论。
