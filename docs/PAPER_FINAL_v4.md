# Execution-Failure Feedback in Decentralized Multi-UAV Exploration:
# An Empirical Study on C2-Explorer
(final draft v4, 2026-08-08 — all data complete)

## Abstract
（中文版最终稿）
多无人机未知环境探索的分层流水线（任务分配→局部规划→执行）存在执行可行性断层。本文冻结并复现官方 C2-Explorer 发布版（commit fd1c76a），量化其执行层重复失败链：局部 A* 对同一不可达目标反复 open_set_exhausted，单次任务单目标重复最高 425 次，占该机失败 99% 以上。在不改地图/任务/传感器/动力学/通信/ACVRP-LKH 的前提下，设计并评估三层执行失败反馈：(1) REACH——跨机失败证据板 + 分配中心风险代价；(2) SVR——分配摘要复用；(3) STEER——确认-保持-切换目标纪律。在官方两格子（cubicle_office/4 UAV、open_plan_office/2 UAV）× 两轮（实现修复前后）× n=10，共 200 次正式运行。结果：失败链可被抑制类机制降至近零（B0 73→B1 2 次）；**REACH 在 open_plan_office 显著降低 makespan（−8.7s vs B1，p=0.005，两轮一致）**且在 cubicle 中性；SVR 四批次 40 runs 39 完成、失败总和 61，最稳定；STEER 无收益，"全冷却保持"变体显著退化（+9.4s，p=0.005），给出设计教训；基础设施渲染崩溃率 1/200。全文以负结果与机制审计的诚实形式交付，附完整可复现协议。

## 1 Introduction
- 分层流水线断层（VORL-EXPLORE 同题：allocator 不知执行难度）。
- C2-Explorer 简介与冻结复现（commit、补丁披露、功能级复现边界）。
- 失败链量化预告：258-425 次同目标重复。
- 三方法分层与贡献列表（见 v3 + 最终数据）。

## 2 Related Work
（v1 + 已核实：DAIB-Explorer 源码对照表、VORL-EXPLORE 摘要、MEF-Explore 摘要、Online Path Repair、PC-Explorer、GVP-MREP、CBBA-ETC；明确不声称：peer 可达/事件触发/ACK/目标保持/margin/去重/execution fidelity 耦合。）

## 3 Method
### 3.1 基线 C2 与失败链形式化
e = (t, drone, frontier, goal, map_version, reason=open_set_exhausted)；同 key 重复计数。
### 3.2 REACH（v2）：跨机证据板 + 中心风险
- ReachEvidence.msg：drone_id, wall_s, 目标数组, 重复数；1Hz 广播；30s 证据窗口。
- methodCenterRisk(center)=min(1, Σcount(goal∈半径5m)/6)；ACVRP 边 cost×(1+λ·risk)+γ·risk。
- 有限冷却 30s；all-cooled 不空等。
### 3.3 SVR：分配摘要复用（exact_identity / stable_overlap）。
### 3.4 STEER：目标保持(3s)+连续确认(3次)+视点轮换+切换 margin(0.2)+证据门控释放；v2 增加 all-cooled hold（本实验证明有害）。
### 3.5 分层关系与消融结构（B0/B1/REACH/SVR/STEER 五模式）。

## 4 Experiments
### 4.1 平台与协议（冻结事实、两格子、n=10、重复实例非种子索引、infra 处理）
### 4.2 失败链审计（Table C）
### 4.3 cubicle/4 结果（Table A + 配对统计）
### 4.4 open/2 结果（Table B + 配对统计）
### 4.5 解读（6 条，见 FINAL_RESULTS）

## 5 讨论与设计教训
- REACH 为何在 open/2 有效而在 cubicle 中性：低失败格子的任务中心与执行目标匹配更稳定；cubicle 中心解耦（unmatched_frontier 现象）稀释风险信号。
- B1 抑制开销：低失败格子中抑制瞬态失败引入等待。
- STEER hold 有害：确认阻塞后空等 > 轻量重试/换候选。
- 实例方差与检验力；MARSIM 随机源不可控。

## 6 结论
负结果与正结果的诚实合并：机制可接入、可审计；REACH 带来一个格子的显著收益；其余为中性或负；失败链审计与复现包为贡献。

## 7 投稿判断（最终）
**路径 B+**：以"实证审计 + REACH 正结果 + 负结果设计教训"投稿 SCI 2/3 区（候选：Drones/IEEE Access/Sensors/Applied Sciences）。标题建议：
"Execution-Failure Feedback for Decentralized Multi-UAV Exploration: An Empirical Study on C2-Explorer"
主贡献：①失败链量化审计；②跨机证据 REACH（open/2 显著收益）；③三机制系统评估与负结果；④可复现实验包。
