# ASGP vs GAGP GPU：快速實驗（2026-09-29）

這是一題、單一 population seed 的探索性系統時間比較。ASGP 與 GAGP 使用同一批起始程式及 cases，但保留各自 reproduction 規則及已知數值差異；不能視為相同演算法的純 GPU 加速比，也不是完整搜尋的 time-to-solution。

## 一輪時間：評分＋產生子代

每格為 3 次正式量測的中位數。GAGP off/on 的比值獨立表示 overlap 效果。

| Population | Cases | ASGP 1T ms | ASGP 10T ms | ASGP 20T ms | GPU off ms | GPU on ms | Overlap 倍率 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1024 | 1024 | 1851.64 | 198.86 | 181.51 | 926.66 | 899.36 | 1.030× |
| 2048 | 1024 | 3783.76 | 392.50 | 359.60 | 1777.96 | 1713.13 | 1.038× |
| 4096 | 1024 | 8080.97 | 816.96 | 780.01 | 3624.88 | 3513.45 | 1.032× |
| 8192 | 1024 | 16858.47 | 1904.61 | 1787.99 | 13578.84 | 11086.15 | 1.225× |

## 相對 ASGP 的時間比

倍率 = ASGP 時間 ÷ GAGP 時間；大於 1 才是 GAGP 較快，小於 1 表示 GAGP 較慢。下表使用「各版本中位數的比值」。

| Population | Cases | off / 1T | on / 1T | off / 10T | on / 10T | off / 20T | on / 20T |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1024 | 1024 | 1.998× | 2.059× | 0.215× | 0.221× | 0.196× | 0.202× |
| 2048 | 1024 | 2.128× | 2.209× | 0.221× | 0.229× | 0.202× | 0.210× |
| 4096 | 1024 | 2.229× | 2.300× | 0.225× | 0.233× | 0.215× | 0.222× |
| 8192 | 1024 | 1.242× | 1.521× | 0.140× | 0.172× | 0.132× | 0.161× |

## Evaluation 單獨計時

GPU 欄使用已編譯 program、已初始化 case/session；仍包含 program packing、上傳、kernel、回傳與每次必要的配置／清理。這是次要的 prepared-evaluation 指標，不可代替上方完整一輪倍率。

| Population | Cases | ASGP 1T ms | ASGP 10T ms | ASGP 20T ms | GPU prepared eval ms | GPU / ASGP 1T |
| --- | --- | --- | --- | --- | --- | --- |
| 1024 | 1024 | 1850.98 | 198.16 | 180.84 | 726.57 | 2.548× |
| 2048 | 1024 | 3782.22 | 391.21 | 358.26 | 1354.06 | 2.793× |
| 4096 | 1024 | 8078.05 | 814.01 | 777.49 | 2720.69 | 2.969× |
| 8192 | 1024 | 16851.85 | 1898.37 | 1781.27 | 5511.10 | 3.058× |

## 設定與計時邊界

- 同一台 Snoopy：Intel i9-10900K（10 cores / 20 threads）、RTX 3090、CUDA 12.6；Release / -O3，系統 allocator，GPU device 0。
- Sum of Elements，ASGP 原始資料生成器，count-aligned，data seed 20260626；輸入 list 長度 1–50，值域 0–100。Test set 不參與本效能測試。
- Population 1024 / 2048 / 4096 / 8192，全部使用 1024 cases。
- Population seed 0，ASGP 原生 ramped depth 2–7、full/grow 交錯。每個 cell 固定同一批 parent programs/cases，沒有依 GPU 表現篩選程式。
- 每個版本 1 次暖機＋3 次正式量測；版本順序輪替。只有一個 population seed，屬於快速探索，未宣稱跨 seed 顯著性。
- ASGP：1 / 10 / 20 worker threads、tournament 2、phase crossover 0.7、one-phase mutation 0.3、max expression depth 7、elite 1、產生 N−1 個新子代。呼叫原始 Score / CrossoverByPhase / MutateByPhase；關閉 fitness cache、early stop、simplification。
- GAGP：GPU evaluation＋GPU reproduction，overlap off/on；tournament 2、每對 parent 嘗試 crossover、mutation 0.3、subtree mutation 比例 1、產生 N 個子代。這些演化規則不等同 ASGP。
- GAGP grammar：7 nonterminals、61 productions、1 DC template；Int constants 0–100。terminal/constant 權重 4，其他 productions 權重 1，供 GAGP mutation 使用；不改變已匯入的 ASGP population。
- GAGP physical limits：1024 nodes、depth 48；DC frames 128；GPU blocksize 256。ASGP recursion limit 128。兩邊執行預算設為 2,000,000（計數單位不同），numeric error cap / penalty 1000。
- 轉接直接映射原生算子，短路布林映射為 if；同值常數去重後重建 grammar membership。保留負數 modulo、整數數值路徑及執行資源的差異，沒有修改任一專案的 runtime。

一輪的起點是記憶體中的未評分 parent population，終點是子代已建立。GAGP 使用原生 generation_total_ms，包含每輪編譯、評分、reproduction 準備、GPU 傳輸、kernel、decode、驗證及生命週期處理；overlap 不將重疊的 phase 時間相加。ASGP 使用相同階段範圍的 wall clock。

兩邊都不把離線資料生成、AST 轉接、grammar 編譯、初始 population 複製、thread pool / CUDA session 建立放入主計時；沒有計入子代的下一次評分。GAGP 額外保留 run_ms，但未與 ASGP generation time 混用。ASGP 專用 interpreter 與 GAGP 通用 VM／grammar 驗證成本也是系統差異的一部分。

## 數值差異

依使用者指示，差異不阻擋計時。全部 initial programs 比較 ASGP/GPU aggregate fitness；GAGP CPU oracle 在子程序隔離，算術例外以診斷失敗記錄，不混入計時；前 16 個程式另做所有 cases 的 CPU raw output／error-status 核對，詳見 measurements.json 的 parity 區塊。GPU 沒有逐案例 raw output 檢查。

| Population | Cases | GAGP CPU vs ASGP：fitness 不同的程式 | GAGP GPU vs ASGP：fitness 不同的程式 |
| --- | --- | --- | --- |
| 1024 | 1024 | 40/1024 | 201/1024 |
| 2048 | 1024 | 81/2048 | 428/2048 |
| 4096 | 1024 | SIGFPE：診斷未完成 | 905/4096 |
| 8192 | 1024 | SIGFPE：診斷未完成 | 1858/8192 |

這些差異並非全部已證明只是浮點捨入。例如 ASGP 負數 remainder 採 C++ 語意，而 GAGP imod0 採另一種符號規則；資源限制及大整數路徑也未完成等價證明。數值一致性不作為本次結果的宣稱。

## 重跑與附件

從 repository root 執行：

```bash
# 先依專案指引建立 Release CUDA 的 GAGP native libraries（本機 sm_86）。
bash benchmarks/asgp-quick-20260929/build.sh
python3 benchmarks/asgp-quick-20260929/run.py
python3 benchmarks/asgp-quick-20260929/report.py
```

ASGP_SOURCE 預設 /home/hschi1106/r13921069，可透過環境變數指定。run.py 逐 cell 輸出摘要，拒絕覆寫未完成的既有量測；.done 標示已完成的 cells。本次四個 populations 都以隔離 CPU oracle 的同一版 adapter 量測。先前混合 cases 的紀錄保留於 previous-mixed-cases/。需要全新量測時應使用新的輸出目錄，不能混用不同 binary / grammar 的結果。

- [完整設定及命令](manifest.json)、[來源與硬體證據](environment.json)
- [數值表](summary.csv)、[逐次量測與差異統計](measurements.json)
- [實驗接合程式](quick.cpp)、[grammar](grammar.json)、[grammar 產生器](make_grammar.py)
- 原始 population、cases、完整 log：../../logs/asgp-quick-1024cases/ready-*.inputs.json、ready-*.log。
- 前置診斷失敗保留在 ../../logs/asgp-quick/；未混入 summary.csv。

ASGP 原始測試已通過。本實驗未改動兩套系統的 production source；未重現先前 scalar workload 的 30×，也未測 DP 或其他 ASGP 題目。
