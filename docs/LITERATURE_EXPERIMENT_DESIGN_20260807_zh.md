# 网上刊会实验设计参考（2026-08-07）

状态：仅用于校准三方法对比的实验设计；不把摘要或第三方工作当作已证实结论。
2026-08-07 已通过 arXiv API 核对下列 ID，标题与摘要一致。

## 1. 检索到的主要工作

- C²-Explorer: Contiguity-Driven Task Allocation with Connectivity-Aware Task Representation for Decentralized Multi-UAV Exploration。arXiv:2603.07699v1。
- VORL-EXPLORE（arXiv:2603.07973）：层级多机器人探索中，分配器缺少执行保真度会导致瓶颈聚集、振荡重规划和重复覆盖；提出执行保真度与共享可导航性估计。对应 REACH-C2 的风险感知分配。
- Multi-CAP（arXiv:2509.14941）：连通感知的多机器人分层覆盖路径规划，维护邻接图并做动态更新。对应 SVR-C2 的连通任务语义与重分配触发。
- Probabilistic Frontier Prioritization with Dirichlet Process Gaussian Mixtures（arXiv:2604.03042）：在通信受限下增强 frontier 优先级，说明候选选择本身是重要实验维度。对应 STEER-C2 的候选选择与目标保持。
- MEF-Explore（arXiv:2505.23376）：通信受限多机器人熵场探索，强调信息共享与探索策略耦合。对应 SVR-C2 的任务有效性/净收益判定。
- Multi-Robot System for Cooperative Exploration in Unknown Environments: A Survey（arXiv:2503.07278）：用于说明多图、多规模、通信受限、失败统计仍是该方向主流实验要求。

## 2. 对当前实验设计的启示

1. 固定 LKH_SEED：原 C2 的 LKH SEED=0 会取系统时间，同一初始实例每次分配不同。正式实验必须固定 `lkh_seed`，所有方法在同一 seed 标签下成对运行，避免随机分配差被误判为方法收益。
2. 主矩阵：三张官方地图 x 2/3/4 UAV x 5 m 通信，至少 10 个成对实例；补充 10/15 m 和无限通信至少 5 个成对实例。
3. 机制审计先行：先确认 A* 失败链、任务身份漂移或执行风险可复现，再进入端到端收益统计。不把“能启动”或“单次 pilot”写成论文结论。
4. 消融必须隔离收益来源：B0 原始 C2、B1 重复失败抑制、REACH/SVR/STEER 各自完整方法。如果完整方法不优于 B1，不能把收益归因给额外机制。
5. 失败样本保留：未 FINISH、超时、A* 失败、轨迹不可行、LKH 失败必须进入分母和统计，不能只报告成功运行。

## 3. 三方法的位置

- REACH-C2：借鉴 VORL-EXPLORE 的“分配代价应感知执行困难”，但保持可解释、无学习模型、可回退。
- SVR-C2：借鉴 Multi-CAP/MEF-Explore 的“连通性和任务有效性耦合”，为 C2 增加语义身份、执行回执、租约边界和净收益判定。
- STEER-C2：借鉴 frontier priority/DAIB 类工作的“目标保持、确认阻塞、切换 margin”，不改分配器，只改规划命令层。

## 4. 明确边界

- 不引入 RL、QD、LLM、VLM 或像素世界模型作为主方法；除非有独立机制证据和跨场景隔离训练，否则只做诊断。
- 不把 peer takeover、ECRTA 时间上界、B1+ v4 重新包装为主贡献。
- 在线检索用于设计校准，不替代本地日志、机制审计和成对统计。
