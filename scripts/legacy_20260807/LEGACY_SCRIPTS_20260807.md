# LEGACY_SCRIPTS_20260807

以下文件是历史实验脚本，只保留审计追溯，不得进入 B0/B1/REACH-C2/SVR-C2/STEER-C2 新协议。

- `aggregate_prct_formal.py`
- `audit_c3_offline.py`
- `audit_formal_reachability.py`
- `audit_peer_handoff_active.py`
- `measure_cert_latency.py`
- `print_paired_ci.py`
- `run_b1plus_batch.sh`
- `run_c3_formal_batch.sh`
- `run_prct_batch.sh`
- `summarize_b1plus_v2.py`
- `summarize_b1plus_v3.py`
- `summarize_b1plus_v4.py`
- `summarize_c3_run.py`
- `summarize_c3v81_batch.py`
- `summarize_gates.py`
- `summarize_prct_stats.py`

`scripts/verify_three_method_gate.sh` 会检查 `run_scene_pilot.sh`、`run_three_method_batch.sh`、`search_b0_fixed_seed.sh` 是否引用了这些旧脚本；引用即返回 `VERIFY_FAIL`。
