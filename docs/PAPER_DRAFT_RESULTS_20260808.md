# Paper: Execution-Failure-Aware Multi-UAV Exploration (working title)

## Title (draft)
Certified Execution-Failure Feedback for Robust Decentralized Multi-UAV Exploration

## Abstract (draft, will be finalized after results)

C2-Explorer 在未知室内环境多无人机探索中通过连通性感知任务表示与 ACVRP/LKH 分配实现连续覆盖；其执行层存在对不可达目标的重复 A* 失败链，造成等待、卡死与超时长尾。本文在不改变三张官方地图、任务、传感器、动力学、通信与求解器的前提下，提出执行失败证据的闭环反馈：(1) 分配代价层失败风险加权（REACH）；(2) 任务语义层分配复用（SVR）；(3) 目标选择层确认-保持-切换（STEER）。在 C2 官方基准上成对比较 B0/B1 与三个方法，报告 makespan、完成率、失败链抑制、覆盖率与求解开销。

## 1 Introduction (skeleton)
- 问题：多无人机未知环境探索；C2 的链路与贡献。
- 缺口：执行层不可达失败链未建模；失败重试拖尾。
- 我们的主张（一句话）与三方法分层。
- 贡献列表（待结果收敛后定稿）。

## 2 Related Work (skeleton)
- 多机探索/任务分配：RACER、FAME、C2-Explorer。
- 执行失败/恢复：DAIB-Explorer（保持+确认+margin）、MEF-Explore（卡死定义）、VORL-EXPLORE（execution fidelity）、Online Path Repair。
- 学习式方法（我们不采用及其原因）：RL/MARL 三图数据不足；QD 无机制证据。

## 3 Method
### 3.1 基线 C2 与失败链定义
- A* open_set_exhausted 重复失败；同 (frontier, goal, map_version) 重试。
### 3.2 REACH：执行可达性证据 → 分配代价
- 失败事件登记；风险因子折算进 ACVRP 代价矩阵。
### 3.3 SVR：分配摘要复用
- digest（drone/blocked/center/hull/position）匹配；exact_identity 或 stable_overlap 复用上次分配。
### 3.4 STEER：确认-保持-切换目标选择
- 目标保持时间、视图轮换、切换 margin、全冷却回退。

## 4 Experiments（结果表占位）
| config | B0 | B1 | REACH | SVR | STEER |
|---|---|---|---|---|---|
| cubicle/4/5m: makespan med (s) | TBD | TBD | TBD | TBD | TBD |
| cubicle/4/5m: finish rate | TBD | TBD | TBD | TBD | TBD |
| cubicle/4/5m: A* fail med | TBD | TBD | TBD | TBD | TBD |
| open/2/5m: ... | TBD | | | | |

## 5 Conclusion
（待结果）

## 6 投稿判断
- 路径 A：显著改善 → 主方法=第一名，其余消融
- 路径 B：机制证据 → "失败链分析与抑制机制"
- 路径 C：负结果报告
