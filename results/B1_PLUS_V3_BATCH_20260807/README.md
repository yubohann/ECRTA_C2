# B1+ v3 成对 batch（open_plan_office / 3 UAV / 5 m / 180 s）

状态：运行中，先记录已完成实例，全部完成后再做统计判定。

## 已完成

| 实例 | B0 makespan | B1 makespan | B1+ makespan | B1+-B1 |
|---:|---:|---:|---:|---:|
| 1 | 61.50 | 98.28 | 81.57 | -16.71 |
| 2 | 66.82 | 104.27 | 73.66 | -30.61 |
| 3 | 84.03 | 运行中 | 待运行 | - |

## 机制证据

实例 1 与 2 的 B1+ 运行均出现 3 次 open_set_exhausted、3 次
prct_retry_suppression_register、1 次 prct_retry_suppression_skip，
说明默认阈值 3 下“失败确认后隔离”确实生效。

## 边界

- 当前 batch 使用 repeated instance 标签，不是官方随机种子；
  同一标签下不同方法的 ROS 执行仍有随机性，不能把“成对”理解为严格同种子配对。
- 正式投稿前必须补充种子控制和更多配置，否则不能下统计结论。
