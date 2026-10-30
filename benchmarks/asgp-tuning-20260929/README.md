# 固定 population=1024、cases=1024：ASGP 1T 比較與 GPU 調校

最佳評分設定為 frames=50、blocksize=512。完整一代 overlap on 為 **797.88 ms**，相對先前 ASGP 單執行緒 1851.64 ms 為 **2.321×**；尚未達到 40×。

這是同一批初代程式的系統時間比較；ASGP/GAGP 數值與 reproduction 規則的差異沿用[原實驗說明](../asgp-quick-20260929/README.md)。不是相同演算法的純 GPU 加速比，也不是搜尋到解的速度。

## 完整一代：評分＋產生子代

ASGP 1T 沿用原實驗 3 次正式量測中位數；GAGP 原設定與調校設定都在本次重測。每種模式 1 次暖機、3 次正式量測，off/on 次序輪替。

| Frames | Block | Overlap | 整代 ms | 相對 ASGP 1T | Evaluation ms | Reproduction ms |
| --- | --- | --- | --- | --- | --- | --- |
| 128 | 256 | off | 929.88 | 1.991× | 738.24 | 188.86 |
| 128 | 256 | on | 900.27 | 2.057× | 755.05 | 142.82 |
| 50 | 512 | off | 826.16 | 2.241× | 638.54 | 187.38 |
| 50 | 512 | on | 797.88 | 2.321× | 656.98 | 137.59 |

Phase 計時可能重疊；不能把 evaluation 與 reproduction 相加當作整代時間。
調校後 overlap on 比本次原設定改善 1.128×。

## 14 組設定：prepared evaluation

同一批已編譯程式與已初始化 cases/session。包含 program packing、上傳、kernel、回傳及每次必要清理；不含 AST 編譯或 reproduction。
每個設定先與原設定核對全部 1024 個 GPU aggregate fitness 完全一致；正式評分每次再核對。每組 1 次暖機、3 次正式量測，表列中位數。

| Frames | Block | Eval ms | Kernel ms | Packing ms |
| --- | --- | --- | --- | --- |
| 128 | 256 | 711.61 | 664.69 | 46.00 |
| 128 | 128 | 720.94 | 674.98 | 45.41 |
| 128 | 64 | 784.08 | 737.69 | 45.92 |
| 64 | 256 | 639.15 | 593.32 | 45.05 |
| 64 | 128 | 685.33 | 638.66 | 45.59 |
| 64 | 64 | 767.01 | 720.84 | 45.19 |
| 50 | 128 | 662.16 | 614.48 | 47.10 |
| 128 | 512 | 703.51 | 657.27 | 45.36 |
| 128 | 1024 | 787.31 | 740.64 | 46.19 |
| 64 | 512 | 638.81 | 590.27 | 46.76 |
| 64 | 1024 | 647.66 | 601.01 | 45.77 |
| 50 | 256 | 640.97 | 593.72 | 46.86 |
| 50 | 512 | 616.59 | 568.46 | 47.51 |
| 50 | 1024 | 632.25 | 585.29 | 45.93 |

## 為何調整 frames

原設定 frames=128、block=256 的 nsys capture 顯示 fitness kernel 啟動 62 blocks、每 block 256 threads、每 thread 64 registers；4 次 kernel 平均 669.09 ms，佔 capture 中 GPU kernel 時間 99.9%。這個 capture 僅供診斷，不混入正式 timing。

Runtime 對 region workspace 設定 512 MiB 上限，DRegionFrame 為 264 bytes。工作區大小會限制同時執行的 program blocks。frames=50、block=512 時依同一公式容納 79 blocks；這是根據程式公式計算，並非新 profiler 量測。

本題輸入長度 1–50，DC skeleton 在長度 ≤1 時結束，每次 split 都限制在內部切點、子 list 嚴格縮短，phase grammar 不允許額外遞迴。因此本工作負載最多需要 50 個 live frames。修改初代及 mutation grammar 的 frame 上限，不截短資料、不更換程式，也不降低 fuel。這個界限僅適用本題與本份 grammar，不能直接套用到其他 DC/DP 工作負載。

## 距離 40×

ASGP 1851.64 ms ÷ 40 = **46.29 ms/代**。目前最佳 overlap on 仍需再快 **17.24×**。現有結果不支持單靠 blocksize、frame 上限或 overlap 達成目標。

下一階段應檢查 DC interpreter 的 phase 執行與 region memory 存取，以及 host 端的 program packing、reproduction 準備與子代 decode。這些是後續優化方向，尚未實作或證明能達到 40×；不能把不同工作負載或 kernel-only 倍率拿來填補整代差距。

## 重跑與證據

本次僅新增 benchmark harness，沒有修改 production runtime。硬體與 Release libraries 同原實驗：i9-10900K、RTX 3090、CUDA 12.6、sm_86。其餘設定沿用原實驗：seed=0、fuel=2,000,000、penalty=1000、tournament=2、mutation=0.3、subtree mutation=1、單代且不評分子代。

```bash
bash benchmarks/asgp-tuning-20260929/build.sh
python3 benchmarks/asgp-tuning-20260929/run.py
python3 benchmarks/asgp-tuning-20260929/report.py
```

run.py 保留既有完整結果、拒絕未完成或 identity 不符的結果。全新量測需使用新的輸出目錄；同時需要原實驗產生的 frozen inputs。選出 14 組中評分中位數最快者後，再與原設定比較整代時間；這是單一 seed 的小型探索，未估計跨 seed 變異。

- [所有設定、命令與 artifact hashes](manifest.json)
- [逐次量測](measurements.json)、[中位數 CSV](summary.csv)
- [獨立 C++ replay harness](replay.cpp)、[重跑腳本](run.py)
- [Profiler 命令與摘要](profile.json)、[驗證記錄](validation.txt)
- 原始 CSV、parity log 與 nsys capture：../../logs/asgp-tuning/（本機保留）
- [HTML 報告](index.html)

## 程式大小與執行限制核對

原本的 physical depth=48 是容許上限，不是初代實際深度。初代全部由 ASGP 同一批 expression trees 轉接；未另外生成深度 48 的 parent。

| 初代實際大小 | ASGP | GAGP physical AST |
| --- | --- | --- |
| Nodes 中位數 | 20 | 32 |
| Nodes 最大值 | 664 | 707 |
| Depth 最大值 | 7（每 phase） | 11（含實體骨架） |

GAGP 多出的節點含 DC skeleton、Program 包裝與算子轉譯輔助節點。子代的容許搜尋空間仍未對齊：GAGP physical 1024/48 比目前 parents 所需更寬，會影響 mutation donor 與 reproduction。上述初代核對不代表子代已公平。

以本份 DC skeleton 與長度 ≤50 的 cases，可保守界定最多 99 個 visited frames、50 個 live frames。初代每 case 的預算保守上界為 ASGP 65,736 steps、GAGP 75,540 fuel，皆小於 2,000,000。這是靜態上界，不是實測消耗，也不代表兩種計數單位等價；它排除了本批初代因 step/fuel 預算耗盡而提前終止的情況。

ASGP recursion 從根 depth=0 起算；max_recursion=128 允許最多 129 個 live frames，GAGP frames=128 允許 128 個。要對齊 128 個 live frames 應使用 ASGP max_recursion=127、GAGP frames=128；若採本題所需的 50 frames，則為 ASGP 49、GAGP 50。本次歷史量測仍保留原設定，未冒充已重跑的共同限制基準。

後續共同限制基準須在 crossover/mutation 後約束每 phase 的來源語言深度 ≤7，另列 physical AST 統計；不能直接把 GAGP physical depth 改成 7。若要比較 timeout 行為，還須對齊語意事件計數，不能只把 step/fuel 設成相同數字。

重現核對：`bash benchmarks/asgp-tuning-20260929/audit_limits.sh`。
[逐項統計與假設](limits-audit.json)、[C++ 靜態核對](audit_limits.cpp)、[統計腳本](audit_limits.py)。
