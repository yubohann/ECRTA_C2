# BASELINE_FAILURE_LOG_FIX_20260807

## 问题

统一 schema 要求 B0/B1/REACH/SVR/STEER 都输出 `failures.jsonl`。旧实现中，B0（`method_mode=baseline` 且 `prct_enable_retry_suppression=false`、`c3_enable_marginal_gate=false`）会在 `registerPrctFailure()` 提前返回，因此 A* `open_set_exhausted` 只进入遥测诊断，不进入 `failures.jsonl`。

## 修复

- 在 `registerPrctFailure()` 中增加纯基线分支：只维护 `baseline_failure_repeat_counts_` 并写 `failures.jsonl`，不改冷却、不改目标过滤、不改 FSM。
- 在 `c2_exploration_manager.h` 增加 `std::unordered_map<std::string, int> baseline_failure_repeat_counts_;`。
- 键仍使用 `prctCooldownKey(frontier_id, goal, map_version)`，地图版本变化会自然形成新的链。

## 验证

- `catkin_make -j2` 通过。
- `verify_three_method_gate.sh` 返回 `VERIFY_OK`。
- 旧 `b0_fixed_seed_2_5p0m` 目录没有 `failures.jsonl`，因此不作为新 schema 下的 pilot；新 pilot 使用 `candidate_seed2_pilot_v2`。

## 边界

该改动不改变原始 C2 决策，也不让 B0 参与重试抑制；仅用于让基线失败事件可审计。
