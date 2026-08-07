# 历史归档

这些文档保留历史负结果、旧方法迭代和失败教训，不是当前方案。当前方案只以 METHOD1_REACH_C2_20260807_zh.md、METHOD2_SVR_C2_20260807_zh.md、METHOD3_STEER_C2_20260807_zh.md 和 THREE_METHODS_PARALLEL_OVERVIEW_20260807_zh.md 为准。

历史结论：

- PRCT peer takeover 没有端到端稳定收益，不作为主方法；
- ECRTA 执行时间残差校准机制审计未通过，不包装成时间上界；
- B1+ / C3 系列曾出现触发不足、重复覆盖、固定等待和收益归因错误，不作为当前主方法。
- 当前统一使用固定 LKH_SEED 进行成对运行；任何未记录 lkh_seed 的旧日志都不能与当前方法做公平对比。
