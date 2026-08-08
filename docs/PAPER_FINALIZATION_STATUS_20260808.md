# 论文定稿工作区（2026-08-08 状态）

## 论文结构（最终版目标）

1. **Introduction**：分层流水线断层；C2 简介；双类执行层失败预告；三方法分层；贡献列表。
2. **Related Work**：VORL（全文已读）、DAIB（源码已读）、MCFS/LS-MCPP/PA-MCPP（makespan 覆盖，已知地图）、SC-RRT（消除不可达前沿）、MEF（卡死定义）、PC-Explorer、GVP-MREP、CBBA-ETC。
3. **Method**：B0 基线 + 三方法（REACH v2 跨机证据板、SVR v3 分配身份复用、STEER v3 确认-保持-切换）+ HOP（可选补充）。
4. **Experiments**：见 PAPER_EXPERIMENTS_FINAL_v5.md（全部审计后数字）。
5. **Discussion**：
   - 双类失败耦合（B1 抑制 A* 失败放大 kinodynamic 失败 381x）——核心新发现；
   - 任务粒度 1-2s → 分配层均衡无空间（ETA 负结果）；
   - makespan 方差主导、n=10 检验力限制；
   - 基础设施崩溃 1/200 与排除口径。
6. **Conclusion**：失败链审计 + REACH open/2 趋势 + 双类失败分析 + 负结果。

## 投稿判断（最终，待 HOP 定论后确认）
- **路径 B+**：以"双类执行层失败的量化审计与机制评估"投稿 SCI 2/3 区
  （候选：Drones / IEEE Access / Sensors / Applied Sciences）。
- 主贡献降级为：① 双类失败量化（可达性+动力学）与耦合发现；② 三机制系统评估
  （REACH 趋势、SVR 复用真实生效、STEER 门控）；③ 完整可复现统计协议。
- 不声明任何显著改善；REACH open/2 以"方向一致趋势"表述。

## 待办（补齐/判定后）
- [ ] R3 完整（50/50）聚合复核
- [ ] HOP n=8 判定（进入论文补充 or 记录为机制验证）
- [ ] 更新 PAPER_FINAL 为 v5（引用新实验章节与结论）
- [ ] 全部提交推送
