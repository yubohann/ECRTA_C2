# PRCT-C2 门槛审计快照 v2

更新日期：2026-08-06
状态：正式实验进行中；Octa/4 UAV 尚未补满；本文档是中间审计，不是最终论文结论。

## 当前正式样本

数据根：/home/c2dev/c2_explorer_reproduction/logs/reachability_retry/formal
聚合文件：PRCT_C2_STATS.csv / PRCT_C2_STATS.json

| 场景 | UAV | 方法 | 实例数 | 全机FINISH | A*失败 | RMST(s) | takeover sent | takeover executed | 等待总时长(s) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| open_plan_office | 3 | B0 | 13 | 7/13 | 4081 | 130.70 | 0 | 0 | 0.00 |
| open_plan_office | 3 | B1 | 13 | 13/13 | 61 | 57.57 | 0 | 0 | 0.00 |
| open_plan_office | 3 | B2 | 13 | 13/13 | 41 | 65.94 | 0 | 0 | 0.00 |
| open_plan_office | 3 | B3 | 13 | 13/13 | 49 | 58.17 | 18 | 18 | 5.76 |
| cubicle_office | 4 | B0 | 12 | 12/12 | 45 | 71.18 | 0 | 0 | 0.00 |
| cubicle_office | 4 | B1 | 13 | 12/13 | 37 | 83.73 | 0 | 0 | 0.00 |
| cubicle_office | 4 | B2 | 13 | 13/13 | 33 | 76.21 | 0 | 0 | 0.00 |
| cubicle_office | 4 | B3 | 12 | 12/12 | 21 | 81.28 | 11 | 11 | 3.30 |
| octa_maze | 4 | B0 | 13 | 13/13 | 46 | 85.13 | 0 | 0 | 0.00 |
| octa_maze | 4 | B1 | 5 | 5/5 | 0 | 84.00 | 0 | 0 | 0.00 |
| octa_maze | 4 | B2 | 3 | 3/3 | 3 | 91.82 | 0 | 0 | 0.00 |
| octa_maze | 4 | B3 | 3 | 3/3 | 0 | 76.57 | 0 | 0 | 0.00 |

RMST 使用修正口径：未全机 FINISH 的实例按 180s 截断；仍不是论文全局覆盖完成指标。

## 成对差异（当前快照）

| 场景 | 对比 | 样本 | 胜/负 | 成对中位差(s) | 说明 |
|---|---|---:|---:|---:|---|
| open_plan_office | B3 vs B0 | 13 | 10/3 | -16.44 | 约改善 24%，但主要是 B1 的作用 |
| open_plan_office | B3 vs B1 | 13 | 6/7 | +1.60 | B3 相对 B1 没有稳定收益 |
| open_plan_office | B1 vs B0 | 13 | 11/2 | -3.92 | 重复失败抑制收益明确 |
| cubicle_office | B3 vs B0 | 12 | 5/7 | +1.63 | B3 总体 RMST 比 B0 高约 10 s |
| cubicle_office | B3 vs B1 | 12 | 7/5 | -1.55 | 成对中位略好，但总体 RMST 更高 |
| octa_maze | B3 vs B1 | 3 | 2/1 | -12.60 | 初步约 15%，样本不足，不能当结论 |

## B3 回执审计合计

- takeover sent：29
- takeover received：29
- takeover executed：29
- ACCEPTED：29
- COMPLETED：29
- REJECTED：0
- ABORTED：0
- fallback：3
- 等待总时长：9.05 s

当前审计说明接管机制能触发并闭环，但不代表端到端收益。

## 门槛判定

| 门槛 | 当前判定 | 证据 |
|---|---|---|
| G0 可审计运行 | 通过 | 三图可启动、日志/配置/审计文件可生成 |
| G1 重复不可达失败链 | 通过 | open_plan B0 13 实例共 4081 次 A* 失败 |
| G2 peer 可达样本 | 通过 | B2 190/190 响应成功；B3 73/75 响应成功 |
| G3 证书可靠性 | 机制层通过 | 无 REJECTED/ABORTED；2 次 B3 查询失败均为 peer 侧 open_set_exhausted，即证书正确返回不可达，未见假可达 |
| G4 端到端收益 | 未通过 | B3 相对 B1 在 Open 无稳定收益，Cubicle 总体退化 |
| G5 无退化 | 未通过 | Cubicle B3 RMST 明显高于 B0；覆盖率、碰撞、通信断连仍需完整提取 |

## 当前可得出结论

1. 重复 A* 失败是真实、可复现的问题，尤其 Open-plan/3 UAV。
2. B1 重复失败抑制可显著减少 A* 失败并改善完成率；Open 场景中 B1 是主要收益来源。
3. B3 peer takeover 机制可触发、可执行、可闭环，但当前端到端数据不支持宣称它优于 B1。
4. 若 Octa 补满后 B3 仍不优于 B1，论文主方法应改写为“重复失败抑制 + 证书式 peer 可达性”，或如实报告负结果。

## 不可得出结论

- 不能说 PRCT-C2 已通过 G4/G5。
- 不能说“能跑起来”等于改进有效。
- 不能说 RMST proxy 等于论文指标。
- 不能说当前 12-13 个 repeated instance 已完成全部主矩阵。
- 不能把 Octa 当前 n=3 的初步差异写成正式收益。
