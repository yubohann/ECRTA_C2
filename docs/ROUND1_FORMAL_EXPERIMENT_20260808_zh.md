# ROUND-1 正式实验记录（2026-08-08，协议执行第 1 轮）

## 本轮实验设计（预注册）

- 批次：`formal_r1_20260808`
- 格子：`cubicle_office / 4 UAV / 5.0m / 180s`，`LKH_SEED=1`，`PRCT_RUN_FULL_DURATION=true`
- 方法：B0（原始 C2）/ B1（纯重复失败抑制）/ REACH / SVR / STEER，同一 runner、同一参数序列
- 每方法 10 个实例（run_001..run_010），总计 50 次运行
- 选择理由：cubicle/4 是协议指定的高失败格子；seed1 的 B0 已实测 274 次 `open_set_exhausted`（failures.jsonl 277 条），3/4 FINISH，机制触发概率高
- 通过门槛：每方法≥5 实例；JSONL 解析错误=0；B0 至少 1/10 实例 A* 失败≥100

## 启动前机制审计（R1，本轮前置证据）

| 方法 | 事件 | 数量 | 判定 |
|---|---|---|---|
| REACH | reach_cost_adjustment / reach_allocation_cost_adjustment | 189 / 9 | 触发 ✓ |
| SVR | svr_reallocation_gate | 17 | 触发 ✓ |
| STEER | goal_view_skip / goal_switch / steer_* | 0（该实例无 A* 失败） | 待本轮多实例验证 ⚠ |

STEER 零触发原因已定位：该实例 STEER 模式下 failures.jsonl 为空（B0 同实例 277 条），即失败链本身是运行间随机事件；相同 LKH_SEED 不能完全控制 MARSIM 内部随机源。因此正式对比必须以分布比较（n≥10）进行，不能要求"同实例严格成对"。此限制已写入 batch manifest（classification=repeated_instances_not_seed_indexed_trials）。

## 文献轮结论（详见 LITERATURE_ROUND_20260808_zh.md）

- DAIB-Explorer（单机）已逐行核对：目标保持 + 连续阻塞确认（blocked_streak≥confirm_updates）+ 分数 margin 切换 + 同目标去重；我们的 STEER 语义已对齐（保持+margin+视图轮换），但"连续确认"需在失败登记处确认等价。
- VORL-EXPLORE 的 execution fidelity 与 REACH 同层：我们的差异面是"实测失败证据回填分配代价"而非预测分数。arXiv 本轮不可达，待补。
- 不可声称创新：peer 可达、事件触发、ACK、目标保持、margin、去重。

## 结果（待批次完成后填写）

| method | n | finish | makespan_med | astar_fail_med | 机制事件 | vs B1 | vs B0 |
|---|---|---|---|---|---|---|---|
| b0 | | | | | | | |
| b1 | | | | | | | |
| reach | | | | | | | |
| svr | | | | | | | |
| steer | | | | | | | |

## 轮次判定（待填写）

- G4（相对 B1 显著改善）：？
- G5（无退化）：？
- 下一轮动作：？
