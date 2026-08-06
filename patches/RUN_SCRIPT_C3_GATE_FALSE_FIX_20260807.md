# run_scene_pilot.sh C3 参数审计修复

## 问题

run_scene_pilot.sh 在参数审计中对 c3_enable_marginal_gate 做硬编码：

- 只接受 true/1；
- 实际传入 false 时 B0/B1 仍被判定参数不匹配；
- run_exit=80，导致正式 batch 中 B0/B1 无法产生审计结果。

## 修复

按 c3_expected 分别校验：

- expected=true：接受 true/1；
- expected=false：接受 false/0。

修复后 B0/B1 的 c3_check 通过，peer_takeover_audit.json 为 audit-complete。

## 影响

只影响运行参数自检，不改变仿真、方法逻辑或评价指标。

