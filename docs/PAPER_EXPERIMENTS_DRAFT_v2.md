# Experiments (draft v2 — with R1 data tables)

## 4.1 ROUND-1 (as-implemented mechanisms, v1 binary)

设置：cubicle_office/4 UAV/5.0m/180s，LKH_SEED=1，n=10/方法，PRCT_RUN_FULL_DURATION=true。
1 run（B0 r1）为渲染崩溃基础设施失败（boost::lock_error ×4），按 infra_suspect 排除并单独报告。

### Table 1: cubicle/4 — 主指标

| method | n | FINISH率 | makespan中位数(s) | makespan均值 | A*失败总数 | 机制事件 |
|---|---|---|---|---|---|---|
| B0 | 10 | 8/10 | 74.70 | 83.65 | 285 | — |
| B1 | 10 | 9/10 | 76.05 | 80.48 | 380 | suppression active |
| REACH | 10 | 9/10 | 75.59 | 81.07 | 502 | 2371 adj events, 0 edges |
| SVR | 10 | **10/10** | 80.27 | 80.19 | **9** | 149 gates, 0 reuse |
| STEER | 10 | 9/10 | 75.75 | 75.54 | 197 | 0 (机制未触发) |

### Table 2: cubicle/4 — 配对比较（vs B1, makespan）

| method | wins/losses | median diff | bootstrap95 | Wilcoxon p |
|---|---|---|---|---|
| B0 | 5/5 | +0.44 | [−8.6,+19.7] | 0.20 |
| REACH | 3/7 | +1.42 | [−7.3,+8.1] | 0.028 |
| SVR | 3/7 | +0.98 | [−5.0,+3.8] | 0.028 |
| STEER | 7/3 | −0.30 | [−10.7,−0.2] | 0.96 |

### 失败链结构（R1 代表性 run）

- REACH r1：431 次失败 = 425 次同一目标 (fid=24, 4.86,−3.72, mv=60) + 2 目标零星；drone 2 占 427 次。
- STEER r1：182 次失败全部同一目标 (fid=10, 9.9,−4.3)；drone 1。
- B0（前一 pilot 批次）：277 次 = 单目标 258 次。

### R1 结论（驱动 v2）

1. 失败链为"单机单目标重复 A* 失败"，最多占单 run 全部失败 99%；
2. 机制缺陷：REACH 证据不跨机且键不匹配 → 0 边调整；REACH/SVR ∞冷却 + all-cooled fallback 回路 → REACH 502 次失败（比 B0 更多）；STEER 视图轮换路径从未进入（死端为 multi-frontier 无替代型）。
3. SVR 10/10 完成 + 9 次失败异常稳定，但 reuse=0（需 R2 验证是否实例巧合）。
4. makespan 无显著差异：本格子失败链非 makespan 瓶颈。

## 4.2 ROUND-2（v2 二进制，预注册）

修复：跨机证据板（/c2_reach_evidence）+ 中心邻近风险（risk_center_links>0 已验证：pilot 中 2 中心 44 边）；all-cooled hold 打断回路；reach/svr 有限冷却 30s。

机制验证 pilot（v2）：REACH risk_center_links=2, risk_adjusted_edges=44 ✓（机制接入决策）。

设置：与 R1 相同两格子，n=10/方法。
通过门槛：REACH risk_center_links>0；STEER steer_all_cooled_hold 出现；B1/REACH/SVR/STEER 的 A* 失败中位数 ≤ B0。

### Table 3/4（R2 结果待填）

| cubicle/4 (R2) | B0 | B1 | REACH | SVR | STEER |
|---|---|---|---|---|---|
| FINISH率 | | | | | |
| makespan med | | | | | |
| A*失败 med | | | | | |
| risk_center_links>0 的 run 数 | | | | | |
| steer_all_cooled_hold 次数 | | | | | |

| open/2 (R2) | B0 | B1 | REACH | SVR | STEER |
|---|---|---|---|---|---|
| FINISH率 | | | | | |
| makespan med | | | | | |
| A*失败 med | | | | | |

## 4.3 基础设施失败率（两轮合并，单独报告）

- R1 cubicle/4：1/50（2%）渲染崩溃（B0 r1）
- R1 open/2：0/50
- R2 待统计
- 处理：infra_suspect 排除于主统计，单独列示；论文中披露（WSLg/OpenGL 环境限制）。
