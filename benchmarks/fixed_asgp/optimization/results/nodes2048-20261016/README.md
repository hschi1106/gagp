# 全題統一2048-node容量驗證（完成）

全30題在P16384／完整1024cases／G4＋final evaluation完成，全部最終16384個體的CPU fitness audit均零差異、零unscored。所有GPU主測使用同一runtime `12c6849`、GPU繁殖與16GiB workspace；24GiB RTX3090整卡NVML取樣峰值21.30GiB。House五模式P8/G2也全部通過。通過指完整執行與fitness核對，不代表所有題目已被解出。

[完整中文報告](final/REPORT.zh-TW.md) · [互動表](final/dashboard.html) · [原始秒數與counters](final/cells.csv) · [PNG](final/speedup.png) · [PDF](final/speedup.pdf) · [來源雜湊](final/provenance.json) · [測試證據](final/validation.json)

跨題倍率中位數14.38×、幾何平均10.12×，28/30題快於ASGP；每題單次cold process，屬粗略速度與支援檢查。House為17.27×、取樣峰值10.56GiB，最終最大AST1115節點，確實走過大於舊1024上限的GPU演化。

這是一組新的benchmark設定：所有30題的GAGP physical whole-AST node budget由1024提高到2048，depth24不變；每phase的GPU容量仍1024，payload workspace仍16GiB。ASGP來源phase depth7、frozen seed0 parents、1024cases及演化規則不變。它解除額外實體容量限制，不等同兩邊搜尋空間完全一致。

舊1024-node完整150格矩陣保持原樣，見[原始報告](../scaling-all30-20261016/final-same16/REPORT.zh-TW.md)。本輪不把新House結果補進舊主表，也不把未重新量測的其他題目標成新budget的速度結果。

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/nodes2048-20261016/run.py \
  --out logs/nodes2048-20261016
```

只接受新目錄。Runner凍結binary，所有native模式使用同一套新grammar。先準備House的完整16384個初始個體，執行known witness與五種模式P8/C1024/G2，再測House P16384/C1024/G4+final eval並在計時外對全部最終個體做CPU fitness audit。GPU主測使用GPU繁殖；ASGP同P實測分母僅在source/cases hash相同且ASGP路徑未改時重用。

其餘29題也逐一重新準備完整16384個體。各題grammar除了node budget以外必須與原本完全相同；29題的完整初始AST與舊artifact比較，House則比較先前有效的8192個體prefix。沒有抽換或刪除個體。NVML每200ms記錄整張卡顯存，含桌面，可能漏掉短尖峰。

準備資料與先期pilot在`logs/nodes2048-20261016/manifest.json`。先期pilot的generic preflight失敗保留為歷史，不能當作最終runtime結果；全30題初始admission均成功。只有實際完成與CPU audit零差異才算正式成功。

使用者後續要求全30題都能執行，因此新增完整最大P實跑：

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/nodes2048-20261016/run_searches.py \
  --prepared logs/nodes2048-20261016 --out logs/nodes2048-searches-20261016
```

此階段等待全部離線準備結束，再對每題P16384/C1024/G4+final eval及全部最終個體做CPU fitness audit。每題採同一新binary、2048-node grammar、16GiB workspace與GPU繁殖；House另檢查五模式P8/G2。全30題正式速度與驗證在新的searches manifest，不能把只有prepare成功視為runtime成功。先期pilot曾被generic GPU request上限擋住，該失敗保留在原manifest；較大phase owner成功admission後才略過generic preflight，generic fallback仍保留1024-node硬限制。

正式raw資料、不可變binary、硬體與VRAM取樣位於`logs/nodes2048-searches-20261016/`，controller已退出。每題1800秒process timeout包含計時外的全量CPU核對，所有題目均在限制內完成；CPU核對不混入`evolve_wall_ms`加速比。

```bash
PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/nodes2048-20261016/report.py \
  --matrix logs/nodes2048-searches-20261016 \
  --out <NEW_REPORT_DIRECTORY>
```

Report只接受完整矩陣與新輸出目錄，重新驗證每格完整搜尋／CPU audit、GPU phase profile及控制組雜湊後才產生結果。`final/publication-validation.json`另記錄來源、輸入、連結與互動表檢查。這份新設定只重測P16384的30格；舊1024-node五種population的150格結果保持獨立。
