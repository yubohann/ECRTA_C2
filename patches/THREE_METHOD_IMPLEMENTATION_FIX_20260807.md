# THREE_METHOD_IMPLEMENTATION_FIX_20260807

日期：2026-08-07
状态：已构建，v3 pilot 运行中

## 修改内容

### STEER-C2
- `prctFilterCooledTargets()` 的判断从 `prct_backoff_enabled_ && first_cooled` 改为 `(prct_backoff_enabled_ || methodSteerActive()) && first_cooled`。
- STEER 模式下 margin 拒绝切换时不再直接返回原候选集，而是落入冷却目标过滤逻辑，避免继续选已被确认阻塞的目标。
- 新增 `steer_switch_margin_rejected` 遥测事件。

### SVR-C2
- 新增 `SvrCandidateSnapshot`，记录候选中心数量、blocked 数量、grid/center/type/hull 尺寸和位置。
- reuse gate 支持两类复用：
  - exact identity：稳定候选摘要一致；
  - stable overlap：候选数相同，grid/center/type/hull 一致且位置距离 <= `svr_reuse_match_radius_m`。
- 原 digest 中导致每次地图微变都失效的 `prctGoalEvidenceHash` 不再参与复用判定。
- `svr_reallocation_gate` 增加 `overlap_matched`、`reuse_reason` 字段。

### REACH-C2
- allocation matrix 不再使用固定半径内扫描多个中心；改为把每个带风险的 frontier viewpoint 映射到最近任务中心。
- 匹配半径默认提升到 5.0 m，同时保持 `reach_center_match_radius_m` 可配置。
- 对 allocation matrix 与 local frontier tour matrix 同时施加 `factor = 1 + reach_risk_weight * risk` 和加性惩罚 `reach_risk_penalty * risk`。
- 新增 `risk_center_links`、`unmatched_risk_frontiers` 遥测。

### Runner 可追溯性
- `run_scene_pilot.sh` 新增 `REACH_CENTER_MATCH_RADIUS_M` 与 `SVR_REUSE_MATCH_RADIUS_M` 环境变量。
- 新参数写入 `run_manifest.txt`，并加入 `method_check.tsv` 校验。

## 验证
- `catkin_make -j2` 构建成功。
- `bash scripts/verify_three_method_gate.sh` 输出 `VERIFY_OK`。
- 旧 PRCT/C3 开关保持默认关闭，三方法协议不引用旧 runner。

## 待验证
- v3 必须出现：STEER 的 `goal_switch` 或明确的 `steer_switch_margin_rejected`；SVR 至少一次 `svr_reuse`；REACH 至少一次 `risk_center_links > 0` 且 `risk_adjusted_edges > 0`。
- 若仍未触发，先审查实现或遥测，不进入正式批量。

-## 补充：launch 参数接入
-
- `candidate_seed2_pilot_v3` 首次运行时 method_check 失败，原因是新参数只写进 `launch_args`，但 launch XML 未声明对应 `<arg>` 和 `<param>`，ROS 参数服务器上不存在 `reach_center_match_radius_m` 与 `svr_reuse_match_radius_m`。
- 已在 `open_plan_office.launch`、`cubicle_office.launch`、`octa_maze.launch` 中同步新增两个 arg/param，默认 5.0。
- 失败批次 `candidate_seed2_pilot_v3` 完整保留；修正后以 `candidate_seed2_pilot_v3b` 重跑同一实例。
