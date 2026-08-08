# 论文终稿（v5，2026-08-08）——所有数字经三轮统计审计

## 标题（建议）
Dual-Class Execution Failures in Decentralized Multi-UAV Exploration:
An Audited Empirical Study of C2-Explorer and Its Feedback Mechanisms

## Abstract（最终）
多无人机未知环境探索的分层流水线存在执行可行性断层。我们冻结并复现官方 C2-Explorer（commit fd1c76a），量化其执行层的**双类失败**：(1) 可达性失败——局部 A* 对同一不可达目标反复 open_set_exhausted（单目标重复最高 425 次）；(2) 动力学失败——kinodynamic 轨迹搜索无可行解。在不改地图/任务/传感器/动力学/通信/ACVRP-LKH 的前提下，评估三个执行反馈机制：分配层风险代价（REACH，含跨机证据共享）、分配复用（SVR）、目标保持-确认-切换（STEER），加纯抑制基线 B1。200 次正式运行（两格子 × 两轮 × n=10）+ 完整 R3 批次，经三轮统计审计（infra 排除、部分完成截断、按 index 配对、符号置换/MWU 置换检验）。结果：**无机制取得统计显著的 makespan 改善**；REACH 在 open/2 方向一致占优（池化 14/20 胜、中位 -6.5s，非显著）；**B1 将可达性失败从 73 抑制到 2，却将动力学失败从 1 放大到 381**——两类失败耦合，抑制须尊重动力学状态；SVR 复用真实生效（80 次）且失败总数最低；STEER v3 无证据时≡C2。论文以量化审计、机制评估与负结果交付，附完整可复现协议。

## 1 Introduction
（见 PAPER_INTRO_RELATEDWORK_DRAFT_v1 + 双类失败预告 + 贡献列表更新）

## 2 Related Work
（PAPER_INTRO_RELATEDWORK_DRAFT_v1 + LITERATURE_SUMMARY：VORL 全文、DAIB 源码、MCFS/LS-MCPP/PA-MCPP、SC-RRT、MEF 对照）

## 3 Method
（PAPER_METHOD_SECTION_DRAFT_v1 + v2/v3 修复说明：REACH 跨机证据板、SVR 分配身份复用、STEER 证据门控）

## 4 Experiments（全部审计后，详见 PAPER_EXPERIMENTS_FINAL_v5.md）
- 平台与协议（含统计口径清单）
- 双类失败量化（Table 1：B1 动力学失败 381x）
- 主结果（Tables A/B：R1/R2 n=10 + R3 n=10）
- 显著性（全部 p>0.08；R3 cubicle B0 最优、B1 显著变差 p=0.019）
- 机制事件（REACH links、SVR 80 次复用、STEER 0）
- 覆盖率代理（frontier_covered 2391-2633，无牺牲）与 LKH 耗时（p95<0.054s）

## 5 Discussion
- 双类失败耦合：抑制须尊重动力学状态（切换 margin 含速度项）；
- 任务粒度 1-2s → 分配层均衡无空间（ETA/HOP 负结果：分配已均衡、跳数 -4% 无收益）；
- makespan 方差主导（60-90s）与 n=10 检验力限制；
- 基础设施崩溃 1/200 与排除口径；
- 实例非真配对（同 seed 0 vs 407 失败证据）与双检验。

## 6 Conclusion
贡献：① 双类执行层失败量化与耦合发现；② 三机制系统评估（REACH 趋势、SVR 复用、STEER 门控、B1 代价）；③ 完整可复现统计协议与负结果。

## 投稿判断（最终）
**路径 B+**：投稿 SCI 2/3 区（Drones / IEEE Access / Sensors / Applied Sciences）。
主张范围：量化审计 + 机制评估 + 负结果教训；不声明显著改善。
