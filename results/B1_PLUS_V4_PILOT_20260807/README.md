# B1+ v4 pilot: open_plan_office / 3 UAV / 5 m / 120 s

状态：pilot 通过，机制审计通过，正式 batch 运行中。

## 结果

- FINISH：3/3
- local_finish_makespan_wall_s：71.08
- A* fail：12
- prct_retry_suppression_register：12
- prct_retry_suppression_skip：8
- prct_quarantine_release：3（goal_removed）
- prct_candidate_filter：7

## 机制证据

1. 同一目标在相同局部 evidence hash 下连续 3 次失败后进入隔离，后续候选过滤直接跳过。
2. 局部 evidence hash 变化后，失败计数从 3 归零重新累计，避免跨证据上下文误隔离。
3. 目标从 frontier viewpoint 集合消失后，隔离释放。

## 边界

- 这是单次 pilot，不能作为论文结论。
- 正式 batch 使用 B0/B1/B1+ v4 成对 repeated instance，阈值在统计完成后判定。
