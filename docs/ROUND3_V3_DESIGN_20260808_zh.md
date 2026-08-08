# ROUND-3 (v3) 设计与预注册（2026-08-08）

## v2 结果的三个归因（本轮修改依据）

1. **STEER 系统性变慢的真因**：不是 hold，而是 `steer_load_bias_=0.1` 在**无失败时也改变 fallback 目标选择**（cost×(1+0.1·load)），违反"非活动时 ≡ C2"原则。→ v3：默认 0.0（保留风险项，证据门控）。
2. **REACH 中心匹配弱**：v2 仅按"失败点-中心"邻近匹配；cubicle 中心与执行目标解耦。→ v3：失败事件绑定**自己正在执行的任务中心**（drone 的 assigned center_positions 内最近中心，半径内），证据板携带 center；methodCenterRisk 优先按 center 归属计分，无归属时回退邻近。
3. **SVR 复用从未触发**：候选指纹/ID 每次变化。→ v3：改为**分配身份复用**——当前候选中心多重集与上次分配的中心并集（半径内）匹配即复用上次分配结果，跳过 LKH。

## v3 代码变更
- STEER：`steer_load_bias_` 默认 0.0；all-cooled → `steer_all_cooled_wait`（FAIL，不 hold 不重试冷却目标）。
- REACH：`ReachEvidenceEntry` 增加 center/has_center；registerPrctFailure 绑定 assigned center；msg 增加 center_x/y/z；methodCenterRisk 按 center 归属。
- SVR：reuse 判定改为 assignment_identity（位置多重集匹配）。

## pilot 验证结果
- SVR：**assignment_identity ×3 触发，decision=reuse_previous_allocation**——机制首次真正复用 ✓
- REACH/STEER：pilot 实例无失败（clean），机制无触发对象；由正式批次统计验证（cubicle 约 40% run 有失败链）。

## R3 批次（预注册）
- cubicle_office/4UAV/5m/180s × LKH_SEED=1 × n=10（正式，机制格子）
- open_plan_office/2UAV/5m/180s × LKH_SEED=1 × n=10（正式，低失败格子）
- octa_maze/4UAV/5m/180s × LKH_SEED=1 × n=5（泛化探针，第三官方地图）
- 通过门槛：REACH risk_center_links>0（含中心归属）；SVR reuse 次数>0；STEER 无失败时机制事件=0（即无行为改变）；steer_all_cooled_wait 出现。

## 论文预期（若 R3 成立）
- REACH：open/2 保持/扩大优势 + cubicle 由中性转正（中心归属修复）
- SVR：reuse 减少 LKH 调用（可报告 solver 调用次数下降）
- STEER：与 B1 无差异（非活动时≡C2），验证"证据门控"原则
