# 全題統一2048-node容量驗證（執行中）

這是一組新的benchmark設定：所有30題的GAGP physical whole-AST node budget由1024提高到2048，depth24不變；每phase的GPU容量仍1024，payload workspace仍16GiB。ASGP來源phase depth7、frozen seed0 parents、1024cases及演化規則不變。它解除額外實體容量限制，不等同兩邊搜尋空間完全一致。

舊1024-node完整150格矩陣保持原樣，見[原始報告](../scaling-all30-20261016/final-same16/REPORT.zh-TW.md)。本輪不把新House結果補進舊主表，也不把未重新量測的其他題目標成新budget的速度結果。

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/nodes2048-20261016/run.py \
  --out logs/nodes2048-20261016
```

只接受新目錄。Runner凍結binary，所有native模式使用同一套新grammar。先準備House的完整16384個初始個體，執行known witness與五種模式P8/C1024/G2，再測House P16384/C1024/G4+final eval並在計時外對全部最終個體做CPU fitness audit。GPU主測使用GPU繁殖；ASGP同P實測分母僅在source/cases hash相同且ASGP路徑未改時重用。

其餘29題也逐一重新準備完整16384個體。各題grammar除了node budget以外必須與原本完全相同；29題的完整初始AST與舊artifact比較，House則比較先前有效的8192個體prefix。沒有抽換或刪除個體。NVML每200ms記錄整張卡顯存，含桌面，可能漏掉短尖峰。

原始資料與逐步狀態在`logs/nodes2048-20261016/manifest.json`。失敗、尚未執行、admission與GPU繁殖結果分開記錄；只有實際完成與CPU audit零差異才算成功。最終結果尚待執行完成。

使用者後續要求全30題都能執行，因此新增完整最大P實跑：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/nodes2048-20261016/run_searches.py \
  --prepared logs/nodes2048-20261016 --out logs/nodes2048-searches-20261016
```

此階段等待全部離線準備結束，再對每題P16384/C1024/G4+final eval及全部最終個體做CPU fitness audit。每題採同一新binary、2048-node grammar、16GiB workspace與GPU繁殖；House另檢查五模式P8/G2。全30題正式速度與驗證在新的searches manifest，不能把只有prepare成功視為runtime成功。先期pilot曾被generic GPU request上限擋住，該失敗保留在原manifest；較大phase owner成功admission後才略過generic preflight，generic fallback仍保留1024-node硬限制。
