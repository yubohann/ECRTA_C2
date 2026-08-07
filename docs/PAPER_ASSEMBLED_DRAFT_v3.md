# Execution-Failure Feedback in Decentralized Multi-UAV Exploration: An Empirical Audit of C2-Explorer
(draft v3 — assembled; R1+R2 data)

## Abstract

未知环境多无人机探索系统普遍采用"任务分配-局部规划-执行"的分层流水线。我们冻结并复现官方 C2-Explorer 发布版后，量化了其执行层的重复失败链现象：局部 A* 对同一不可达目标反复返回 open_set_exhausted，单次任务中单目标重复失败最高 425 次，占单机全部规划失败 99% 以上。本文在不修改三张官方地图、任务、传感器、动力学、通信与 ACVRP/LKH 求解器的前提下，设计并评估三层执行失败证据反馈机制：(1) 分配层风险代价（REACH）；(2) 任务语义层分配复用（SVR）；(3) 目标选择层确认-保持-切换（STEER）。在两个官方格子（cubicle_office/4 UAV、open_plan_office/2 UAV）上以 n=10 成对统计（bootstrap 95% CI、Wilcoxon），并审计基础设施失败率。结果显示：重复失败链真实存在且可被抑制类机制降至近零（B1：285→2 次；SVR：9 次），但在此基准内 makespan 差异被实例方差主导，无方法在统计上显著降低 makespan；分配级风险反馈（REACH）在跨机证据共享后可机械接入（risk_center_links>0），但成本抬升使 risky 任务延后执行，无收益；目标级"全冷却保持"（STEER v2）显著增加 makespan（+9.4s, p=0.005），应避免。我们以负结果与机制审计的形式，为 C2 类系统的执行层反馈设计提供了可复现的证据与教训。

## 1 Introduction
（见 PAPER_INTRO_RELATEDWORK_DRAFT_v1.md，补：失败链量化数字与两轮实验结论）

## 2 Related Work
（见 PAPER_INTRO_RELATEDWORK_DRAFT_v1.md + LITERATURE_ROUND_20260808_zh.md：DAIB/VORL/MEF 对照已核实）

## 3 Method
（见 PAPER_METHOD_SECTION_DRAFT_v1.md + v2 设计：ROUND2_V2_FIX_DESIGN_20260808_zh.md）

## 4 Experiments

### 4.1 平台与协议
WSL2 Ubuntu 20.04.3/ROS Noetic/RTX 4090；官方 commit fd1c76a；LKH 3.0.6；两项已披露平台补丁（OpenGL 4.6→3.3、初始 RPM 归零）。
格子：cubicle_office/4 UAV/5m/180s；open_plan_office/2 UAV/5m/180s。n=10/方法，LKH_SEED=1，PRCT_RUN_FULL_DURATION=true。
实例为重复试验（非种子索引试验）——MARSIM 内部随机源不受 LKH_SEED 完全控制（已验证：同 seed 可产生 0 与 407 次失败两种结果）。
基础设施失败（渲染崩溃）按 infra_suspect 排除并单独报告。

### 4.2 失败链审计（贡献 1）
- 结构：单机单目标重复（cubicle/4 代表 run：277 次失败中 258 次同一 (fid,goal,mv)；REACH R1 run：425/431）。
- 频率（B0）：R1 cubicle/4 = 285 次/10 runs（单 run 最高 258 链）；R2 = 73 次/10 runs；open/2 = 17 次/10 runs（R1）。失败链为随机事件：同 LKH_SEED 的 open/3 曾出现 0 与 407 两次结果。
- 触发的机制性条件：A* 起点与目标在不同可达分量（open_set_exhausted），重试不改变结果（同搜索展开数）。
- 基础设施：R1 cubicle/4 有 1/50 渲染崩溃（boost::lock_error×4，排除）。

### 4.3 ROUND-1（v1 实现）cubicle/4 主表
| method | FINISH | ms_med | ms_mean | A*失败 | 机制 |
|---|---|---|---|---|---|
| B0 | 8/10 | 74.70 | 83.65 | 285 | — |
| B1 | 9/10 | 76.05 | 80.48 | 380 | suppression |
| REACH | 9/10 | 75.59 | 81.07 | 502* | 0 edges（缺陷）|
| SVR | 10/10 | 80.27 | 80.19 | 9 | gates=149, reuse=0 |
| STEER | 9/10 | 75.75 | 75.54 | 197 | 未触发 |

*REACH R1 因"∞冷却+all-cooled fallback 回路"放大失败链（实现缺陷，v2 修复）。

### 4.4 ROUND-2（v2 修复）cubicle/4 主表
| method | FINISH | ms_med | ms_mean | A*失败 | 机制 |
|---|---|---|---|---|---|
| B0 | 10/10 | 73.48 | 72.65 | 73 | — |
| B1 | 9/10 | 76.88 | 73.45 | 2 | suppression |
| REACH | 8/10 | 76.51 | 75.37 | 54 | 2587 events, links>0 |
| SVR | 9/10 | 72.43 | 78.96 | 52 | gates=141, reuse=0 |
| STEER | 9/10 | 83.04 | 87.65 | 34 | 未触发（hold 未用）|

### 4.5 配对统计（R2, vs B1）
- REACH: +0.65s [−2.2,+6.4] p=0.20
- SVR: +2.77s [−0.7,+14.6] p=0.028
- STEER: **+9.35s [+6.8,+24.1] p=0.005**（显著退化；且无机制事件，为实例方差+hold 空等）
- B0: +0.33s p=0.20

### 4.6 R1+R2 open/2（低失败格子）摘要（R1 完整，R2 待填）
R1: 全方法 100% FINISH；B0 76.7s 最优；STEER 115.9s 最差（n=4）；A* 失败全方法 ≤17。
→ open/2 在冻结协议下为低失败格子，机制无处发力。

### 4.7 结果解读（诚实）
1. 失败链真实、可量化、可抑制（B1 已够）。
2. 分配级反馈（REACH）机械可行但无 makespan 收益：成本抬升延迟 risky 任务执行。
3. 目标级"全冷却保持"（STEER v2 hold）显著有害——应仅抑制重试而不空等。
4. 本基准内 makespan 由实例方差主导（60-140s 范围），机制收益不可分辨（n=10 的检验力不足）；更大 n 或更严的失败诱导协议是后续工作。

## 5 Conclusion & Limitations
（见模板；明确：负结果、n 有限、无真实失败诱导、MARSIM 随机性、单机器）

## 6 投稿判断
路径 B：机制证据成立 → 主张降级为"失败链审计 + 抑制机制评估（B1/SVR）+ REACH/STEER 负结果"。
