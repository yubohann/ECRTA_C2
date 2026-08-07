# B1+ v3 成对 batch

场景：open_plan_office / 3 UAV / 5 m / 180 s。

状态：已完成 5 组成对 batch；该版本被判定不足以作为主方法，当前主方法升级为 B1+ v4。

| instance | B0 makespan | B1 makespan | B1+ v3 makespan | B1+ v3 - B1 |
|---:|---:|---:|---:|---:|
| 1 | 61.50 | 98.28 | 81.57 | -16.71 |
| 2 | 66.82 | 104.27 | 73.66 | -30.61 |
| 3 | 84.03 | 84.85 | 70.17 | -14.68 |
| 4 | 76.06 | 77.33 | 76.13 | -1.20 |
| 5 | 80.30 | 60.08 | 94.63 | +34.55 |

## 统计

- B1+ v3 vs B1：成对 mean=-5.73 s，median=-14.68 s。
- B1+ v3 vs B0：mean=+5.49 s，median=+6.83 s。

## 判定

- 相对 B1 有改善，但相对 B0 不稳定。
- v3 使用全局 frontier viewpoint map epoch，任意无关 frontier 变化都会释放全部隔离，机制过粗。
- 结论：v3 不能作为主投稿方法；保留完整结果，当前主方法改为 B1+ v4 Local-Evidence-Gated Goal Quarantine。

## 边界

- 当前 batch 使用 repeated instance 标签，不是官方随机种子；同一标签下不同方法的 ROS 执行仍有随机性，不能把“成对”理解为严格同种子配对。
- 正式投稿前必须补充种子控制和更多配置，否则不能下统计结论。
