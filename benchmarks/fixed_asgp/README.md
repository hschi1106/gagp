# Fixed ASGP/GAGP benchmark results

Contract 與重跑方式見 [fixed-asgp-v1](../../docs/guides/fixed-asgp-benchmark.md)。
本目錄保存可供比較的量測證據；大型固定父代、cases 與 process logs 保留在
`logs/fixed-asgp/`。

## 2026-10-01 daily baseline

RTX 3090、Intel Core i9-10900K、Release build；固定 GPU 0。
三題皆為 popsize=1024、cases=1024，五種模式各暖機一次、量測三次。
15/15 cells 全部成功，總 wall time **180.36 秒**，在 300 秒日常預算內。
180 秒目標僅超出 0.36 秒；建置與一次性輸入準備不在此時間內。

以下為完整一代的中位數，單位 ms。`GPU eval` 使用 CPU reproduction；
後兩欄同時使用 GPU evaluation 與 GPU reproduction。

| 題目 | ASGP 1T | GAGP CPU | GPU eval | + repro，overlap off | + repro，overlap on | 最佳 GPU 相對 ASGP 加速比 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Sum of Elements | 1882.095 | 18462.708 | 1382.352 | 837.195 | 807.276 | 2.331× |
| Median | 64.535 | 1358.477 | 936.988 | 287.858 | 262.598 | 0.246× |
| House Robber | 1308.357 | 8601.055 | 1480.927 | 788.157 | 763.605 | 1.713× |

Sum 尚未達到 40× 目標；Median 的最佳 GPU 模式仍比 ASGP 慢約 4.07 倍。
Overlap 相對 GPU reproduction 同步模式的加速比分別為 1.037×、1.096×、1.032×。
原始 phase timings 保留在 summary；有重疊的 phase 不應直接相加。

- [完整 15-cell 報告，含 min/max 與兩種 baseline 比例](20261001-daily/report.md)
- [所有 warmup／measured samples、命令與 frozen hashes](20261001-daily/manifest.json)
- [硬體、build flags、binary/source hashes](20261001-daily/environment.json)
- [Phase timings 與 variation counters](20261001-daily/summary.json)
- [固定輸入與已知正解檢查](20261001-daily/input_audit.json)

三題各 8192 個原始 ASGP 父代已全數轉換，未篩選或替換個體。
已確認日常 1024 父代與 8192 版本的前綴完全相同；本次正式量測為 daily，
**未執行完整的 60-cell scaling 矩陣**。

已知正解在 ASGP 與 GAGP CPU 的全部 1024 cases 上均正確。
GPU audit 的 Median、House Robber fitness 為 0；Sum 為 −690000，保留現有
有損 payload 行為的誤差，不把非零 fitness 解讀為逐 case 錯誤數。
這份報告衡量固定工作負載的時間，不代表搜尋成功率或 time-to-solution。

此變更另修復 benchmark 揭露的 CPU `INT64_MIN / -1` SIGFPE：
`idiv0` 在 CPU／GPU 一律回繞至 `INT64_MIN`，並更新規格與邊界測試。
House Robber 的 adapter 移除可由固定 DP domain 保證的不必要索引上界檢查，
讓全部 8192 父代皆能在既有 1024-node 限制內轉換。

驗證結果：工具測試 89 項（85 passed、4 skipped）、repository checks 24 passed；
native builtin、GPU smoke、fitness parity、evolution parity、fixed benchmark
integration 共 5 項通過。Benchmark integration 另涵蓋大型 House Robber
父代的轉換回歸案例。

在已完成建置與 prepare 的本機 workspace 重跑：

```bash
GAGP_CUDA_DEVICE=0 PYTHONPATH=tools python3 -m gagp_tools benchmark fixed-asgp run \
  --out logs/fixed-asgp/daily-next
```

每次使用新的 `--out` 路徑。輸入已準備至 8192，後續完整 scaling 可加
`--suite scaling`；其時間不受 daily 的五分鐘預算約束。

## 2026-10-01 profiler baseline

[Nsight Systems／NVTX 完整報告](20261001-profiler/README.md) 將三題、三種
GPU 模式拆成 compile、packing、H2D、eval、selection、repro、D2H、
verification 與 sync。另附未開 profiler 的重新量測控制組；上方 daily
數字仍保留原紀錄。NVTX 只加入獨立 source 副本，未實作 production 優化。

[從保守到激進的優化提案](20261001-profiler/optimization-plan.md) 依據這份
baseline，列出 Sum／House 達成 40× 所需預算與 Median 的優先改善方向。
