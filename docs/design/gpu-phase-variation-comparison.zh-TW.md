# GPU phase reproduction：RNG 與 variation 規則的新舊比較

整理日期：2026-10-13。程式碼核對版本：`e3ed4df`；已驗證的最佳 runtime
來源為 `d1712a5`，後續 checkpoint 保存測試、文件與診斷結果。

本文說明這個 checkpoint 中，原生 AST reproduction 與實驗性
`native-gpu-phase-v2-counted` 的差異、設計理由及例子。它是版本限定的中文
解說，不另行定義一份契約；實作契約以
[GPU reproduction 設計文件](gpu-reproduction.md#experimental-native-gpu-phase-population)
與相關程式碼為準。例子用來解釋抽樣機制，不是實際 seed 的輸出紀錄。

## 1. 比較範圍與重要結論

這次改動包含兩種不同性質：

1. **抽樣分布相同，但亂數消耗不同。** 例如將 reservoir site sampling
   改成計數後抽 rank；同一 seed 不再選出相同 site，但每個候選仍等機率。
2. **搜尋規則本身改變。** 例如 tournament 改為有放回抽樣、雙向交換改成
   單向移植，以及先選 destination 再選 donor。這些會改變 offspring 分布，
   不能只用「RNG 軌跡不同」概括。

本文的「原本」指 GAGP 原生 AST 路徑，**不是 ASGP 的 operators**。CPU 與舊 GPU
原本也不是逐 child 相同的實作；需要區分的地方會分別說明。

| 執行模式 | 這套新規則是否生效 |
| --- | --- |
| ASGP 1T | 否，仍是獨立比較控制組 |
| GAGP CPU | 否，仍使用原生 CPU reproduction |
| GPU-e | 否，仍使用原生 CPU reproduction |
| GPU-er | 設定 `GAGP_GPU_PHASE_POPULATION=1` 且通過適用性檢查時生效 |
| GPU-er-o | 同上；此 profile 沒有原本的 host donor preparation，兩模式使用相同依賴排程 |

`GAGP_COUNTED_SITE_DRAW=1` 進一步啟用 counted 版本。不符合 profile 的 grammar
或設定會回到原生 AST backend，並記錄原因。這裡沒有使用已撤除的 CPU fragment
路徑，也沒有將 reproduction 搬回 CPU 多執行緒。

| 項目 | 原本 | 新的 | 性質 |
| --- | --- | --- | --- |
| Tournament | 每輪排列 population，再分組競賽 | 每個 child 獨立、有放回抽候選 | 改變選擇分布與候選間相關性 |
| Crossover children | 一對 parents 雙向交換，產生兩個 children | 每個 child 選 parent、donor，單向移植 | 改變 offspring 分布與方向性條件 |
| Crossover 選點 | 在候選表的合法 site pairs 中選取 | 先選 destination，再選相容 donor site，最多 8 次 | 改變配對權重及失敗機率 |
| Variation 範圍 | 由完整 grammar admission 建立 logical sites | 固定 root 內各 phase 的 nonterminal occurrences | 改變 operator 可操作範圍 |
| Mutation donor | CPU 準備 contextual donor pool，GPU 抽取與拼接 | GPU 當場生成新 subtree | 改變生成排程、RNG 與候選集合 |
| Counted site sampling | 新 profile 前一版使用 reservoir | 計數後抽一個 rank | 同一候選集合的單次分布不變 |
| 同分處理 | CPU ranking 用 program key；舊 GPU tournament 保留先遇者 | 排名保留 population 順序；tournament 保留先抽到者 | 須分別比較，不能概括為全部改變 |

## 2. Tournament：由每輪不重複參賽改為有放回抽樣

### 原本的流程

CPU 將 population 索引 shuffle 後分組；舊 GPU 使用置換索引後分組。
CPU 的 shuffle 與 GPU 的置換不是同一種排列分布，但都讓同一輪內每個個體
只出現在一個組別。需要更多 winners 時再開始下一輪；不同輪仍可重複選中個體。

假設 pressure 為 2：

```text
個體：A=10、B=8、C=3、D=1（fitness 越大越好）
排列：C、A、D、B
分組：(C,A) → A 勝
      (D,B) → B 勝
```

### 新的流程

每個 child 獨立進行兩次 tournament，分別取得 parent 與 donor。
每次抽 `selection_pressure` 個索引，允許重複，保留其中 fitness 最大者。
目前 fixed benchmark 的 pressure 仍是 2。

```text
某次 tournament：(A,A) → A 勝
另一次 tournament：(D,D) → D 勝
```

最差的 D 也可能因抽到兩次自己而勝出。在 P=1024、pressure=2 時，一次 tournament
內重複索引的機率是 `1/1024`。更大的差異是整輪不再保證每個個體都有參賽，
因此 parents 的出現次數與整批候選間的相關性也變了。

這種獨立抽樣讓每個 child 可以自行執行 selection，不需要維持原本的分輪分組關係。
它保留 tournament 的概念，但不是原 selection law 的逐步等價實作。

## 3. Crossover：由雙向交換改為單向移植

### 原本：一對 parents 產生一對 children

在兩個 site 的 nonterminal、scope 與其他契約皆相容的前提下：

```text
A = add(x, 1)
B = mul(y, 2)

選 A 的 x 與 B 的 y：
child 0 = add(y, 1)
child 1 = mul(x, 2)
```

兩個 children 共用同一對 parents 與交換位置。候選配對必須通過雙向移植所需的
契約與資源條件；對需要額外完整 admission 的情況，仍保留後續檢查。

### 新的：每個 child 獨立選 parent 與 donor

```text
child 0：parent=A、donor=B，將 y 移入 A → add(y, 1)
child 1：重新選 parent、donor 與 sites，不保證產生 mul(x, 2)
```

新的 donor 是提供 subtree 的來源，原 donor 不會被修改，也不必同時產生反向 child。
每代 population 數量保持不變，但每個 child 都獨立選取兩個來源。

假設一個方向移入小 subtree 後合法，反向移入大 subtree 卻超過容量：

- 舊的雙向配對會排除這個組合。
- 新的單向操作可以接受合法的方向。

因此新規則可能產生舊「雙向同時合法」規則不會接受的單向 offspring，也改變了
兩個相鄰 children 之間的相關性。這不只是少複製一次資料。

## 4. Crossover 選點：配對權重與八次重試

### 原本：以合法 site pair 為抽樣單位

CPU crossover 枚舉合法 site pairs，再用 reservoir sampling 選一對。
舊 GPU 在已準備的候選 site 表中計數合法 pairs，再抽 rank。候選表可能經過
shuffle 與數量上限裁切，因此不能將舊 GPU 說成必定涵蓋全部 grammar sites。

舊 GPU 的 rank 使用 hash 後取模；以下機率例子忽略有限位元取模的微小偏差，
用來說明以 pair 為單位的權重。

### 新的：先選 destination，再選 donor site

```text
最多嘗試 8 次：
    在 parent 的適用 sites 中均勻選 destination
    在 donor 中均勻選相同 nonterminal、相同 scope 的 site
    檢查單向移植後的 node、depth 與儲存容量
    成功：發布這個 child
全部失敗：保留選中的 parent，再進入 mutation 階段
```

這八次都使用已選定的 parent 與 donor，不會每次重新做 tournament。
若失敗的 splice 曾部分寫入輸出，實作會先恢復 parent，才繼續嘗試或發布結果。

假設 parent 只有 a、b 兩個 destination，且所有配對都能通過容量檢查：

```text
a 有 9 個相容 donor sites
b 有 1 個相容 donor site
```

| Destination | 舊：在 10 個合法 pairs 中選一個 | 新：先在兩個 destinations 中選一個 |
| --- | ---: | ---: |
| a | 約 90% | 50% |
| b | 約 10% | 50% |

新的抽樣較不偏向「擁有很多相容 donor 的 destination」，但不是所有合法 pairs
均勻抽樣。加入容量失敗與重試後，最終接受分布還會受到各 destination 成功率影響。

舊流程在準備好的候選表中找到合法 pair，就能選出它。新流程即使存在合法 pair，
也可能八次都沒抽到。舉例說，若每次嘗試的成功機率為 10%，在獨立抽樣的簡化模型下，
八次皆失敗的機率為 `0.9^8 ≈ 43%`。這只是機制示例，**不是實測 rejection rate**。

這項設計省掉完整 pair 掃描，代價是新的 fallback／unchanged 分布。
Crossover fallback 的 child 仍可能被 mutation 改變；它下一代仍完整評估，沒有漏評。

## 5. Variation site：固定外層，保留 phase 內的 grammar 契約

新 profile 要求一個固定 root region template，內含獨立的 whole-phase holes。
概念上可以寫成：

```text
Region(
    predicate = P,
    base      = B,
    combine   = C
)
```

例如 `C = add(recursive_result, index(xs, 0))`，operators 可以替換其中有適用
nonterminal 契約的 subtree，也可以替換 phase 根部的適用 site。
**它不是只做 whole-phase crossover，也不是只從有限舊 bank 重組。**

固定外層 `Region(...)` 不列入 variation sites，不能一次替換整個外層來同時更新
多個 phases。舊路徑則可以操作完整 admission 提供的 root／logical sites，
前提是 grammar 自身允許。

必須區分表示能力與 operator 可達方式：若 grammar 原本只有同一固定外層，
新表示法可能仍可表達相同的 phase 組合；但失去整個 root 的一步替換，仍會改變
搜尋路徑。不能據此宣稱新舊一步轉移機率或完整 grammar language 必然相同。

適用 site 必須保存 nonterminal 與 scope 身分。兩個 subtree 都回傳 Int，
不代表它們能互換。例如「只能生成非負數的 N」與「可生成任意整數的 I」即使
結果型別相同，仍可能屬於不同 grammar language。新路徑不會只因同型別就放行。

本 checkpoint 的限制包括：

- 固定單一 root region template，獨立 phase holes；phase expressions 為支援的
  Int／Bool／IntList 組合，常數 payload 為 Int／Bool。
- Identity scope mapping；不接受 lexical capture、nested templates 或 repeated holes。
- 變動 phase 使用單位 node／depth charges；固定骨架只接受受限的 charges，且無 depth reset。
- 特殊 mutation entry、closed crossover、crossover group、受限 variation policy 等
  不直接套用簡化規則，而是拒絕此 profile。
- 不接受額外 projected offspring budget 或混合 root contract 等不支援設定。

以上條件是為了讓局部 splice 能保存 membership 所需的構造資訊。
不適用時有明確 fallback；不會把 repeated-hole 原子替換悄悄降成獨立節點替換。

## 6. Mutation：機率不變，donor 建立方式改變

Fixed benchmark 保持：

```text
mutation_rate         = 0.3
mutation_subtree_prob = 1.0
```

Crossover 之後，每個 child 仍有 30% 機率嘗試 subtree mutation。
這不是每代保證剛好 30% children 改變；抽樣會波動，生成出的 subtree 也可能與原本相同。
沒有改成每個 child 只做 crossover 或 mutation 其中一種，也沒有提高 mutation rate。

### 舊 GPU 路徑

```text
分析 crossover 後的 population
→ 依合法 sites 與既有排程，在 CPU 準備 contextual donor pools
→ GPU 判斷 mutation、選 site、選 donor、拼接
```

舊流程已能預判實際 mutation candidate，只建立可能被讀取的 donor pool。
不能把這個既有的按需 preparation 當成本輪新增成果。CPU reference mutation
則直接使用自己的 contextual donor generation；不要與 GPU donor-pool 流程混稱。

### 新 GPU 路徑

```text
GPU 判斷是否 mutation
→ GPU 選 site
→ 依該 site 的 grammar、scope、可用 node／depth 在 GPU 生成新 subtree
→ GPU 直接拼接
```

例如選到一個 expression site，可當場生成新的 `sub(y, 3)`，而不是先從 CPU
準備的 pool 中抽取。生成器仍使用 grammar production weights 與 constant domain，
不會無條件把 productions 改成等機率。它依剩餘容量與 minimum-size 資訊判斷
可行 productions，按可行選項的權重抽樣。

但不同 RNG、生成排程與候選集合不保證新舊 donor 的完整分布相同。
此 profile 只接受 subtree mutation policy；要求其他政策的設定會 fallback，
不會把 constant-only mutation 靜默換成 subtree mutation。

### Node／depth budget 沒有被偷偷放大

舊路徑本來就計算 replacement allowance；新路徑是用受控表示的實際大小與
深度直接維護，不是新發明「剩餘 budget」。

```text
Whole-program 上限：100 nodes
目前大小：80 nodes
移除的 subtree：10 nodes
單一 occurrence 可放入的最大 subtree：100 - (80 - 10) = 30 nodes
```

新實作先扣除固定骨架，對變動 phases 使用等價的剩餘物理容量計算，再施加
phase depth 與 device storage 限制。這沒有降低 evaluator 的 fuel、list 或 memo
執行上限。複雜加權、depth reset、repeated holes 的計價不能直接套用上述單一
occurrence 例子，因此不在此 fast profile 的適用範圍內。

新生成器若違反建構保證或遇到內部錯誤會回報錯誤；不是把這類失敗全部靜默當成
保留 parent。它與正常 crossover 八次嘗試失敗的處理不同。

## 7. RNG 排程與 counted-site sampling

### 每代、每個 child、每種 operator 的亂數狀態

新路徑以 `cfg.seed` 初始化 host 的 `std::mt19937_64`，每代取一個 generation seed。
GPU 再用不同 operator 標記與 child index 派生亂數狀態。實作形式為：

```text
crossover_state = generation_seed XOR (crossover_tag + child_index)
mutation_state  = generation_seed XOR (mutation_tag  + child_index)
```

兩種 operator 使用不同標記，分開消耗亂數。因此某個 child 的 crossover 多重試
幾次，不會推移它的 mutation 亂數狀態，也不會消耗下一個 child 的狀態。
這是確定性的 state 派生方式，不是對 streams 完全統計獨立的額外證明。

舊 GPU 原本也有按 pass／child 派生 seeds 的機制。新路徑改的是派生公式、
事件順序與抽樣次數，不是由不可重現變成可重現。
相同 seed 不承諾跨 backend、profile 或版本重現相同 offspring；重現時還需固定
grammar、輸入順序、設定與 binary。

### Reservoir 與 counted：相同單次分布，不同軌跡

這裡的「舊版」特指新 GPU phase profile 的 uncounted 版本，
不是第 4 節的原生 AST 合法-pair 選擇。

```text
候選 sites：a、b、c、d

Reservoir：
    依序訪問候選，第 k 個候選以 1/k 機率取代目前選擇

Counted：
    第一遍數出 4 個候選
    rank = bounded(4)
    第二遍取得 rank 對應的候選
```

兩者對固定候選集合中每個 site 的機率都是 1/4。
但 reservoir 每遇到候選就呼叫一次 `bounded()`，counted 只呼叫一次。
因此相同 seed 可能選到不同 site，後續在同一 stream 的抽樣也會改變。
`bounded()` 內部使用 rejection 以避免 modulo bias，所以「一次呼叫」不等於
保證只消耗一個亂數 word。

Counted 版本多一次掃描，減少的是抽亂數工作；不能僅從程式碼判定必然更快。
本輪以同 binary、相同 frozen population、交錯三個獨立 processes 驗證：

| 題目 | Uncounted generation 中位數 | Counted generation 中位數 |
| --- | ---: | ---: |
| Sum of Elements | 50.603 ms | 48.885 ms |
| House Robber | 42.306 ms | 40.384 ms |
| Median | 15.565 ms | 12.214 ms |

每個設定共九次正式量測；這是包含 reproduction 的 generation 比較，
不是只報 sampling kernel，也不是完整架構改善的單因素歸因。

## 8. 同分處理：CPU ranking 與 GPU tournament 必須分開看

假設 A、B 的 ranking fitness 相同，B 的 program key 排序較前：

| 位置 | 行為 |
| --- | --- |
| 舊 CPU ranking／CPU tournament | 用 program key 打破同分，可能選 B |
| 舊 GPU tournament | 只有 fitness 嚴格較大才換 winner，保留先遇到者 |
| 新 GPU tournament | 同樣只有嚴格較大才換 winner，保留先抽到者 |
| 新 profile 的 history-best ranking | Stable sort，fitness 相同時保留 population 順序 |

因此「舊 GPU tournament 原本序列化 program key，現在取消」是不正確的說法。
新的路徑省掉了原本完整 ranking 所依賴的 key 比較需求，但 GPU tournament
的同分比較條件本來就不是 program-key ordering。
Ranking 前既有的 fitness canonicalization 仍保留，沒有在這裡另改精度。

## 9. 哪些成本被消除，哪些仍然存在

最大結構性收益是跨 operators 保留構造資訊，讓下一代直接成為可執行狀態：

```text
Cold import：外部 AST → 獨立 grammar admission → GPU genotype owner

每代：完整 GPU evaluation
   → GPU selection
   → GPU subtree crossover
   → GPU fresh-subtree mutation
   → GPU phase lowering
   → 必要的 code copyback／整合
   → 下一代可評估狀態
```

新流程取消了 crossover 與 mutation 之間逐 child 的完整 AST rebuild、
反向 membership reconstruction 與 host donor preparation。
Owned executable／capability proof 重用另減少重複 verification 與分析。
Direct phase／root、window／coordinate executor、unboxed frames 則屬於
evaluator 改善；不能把它們的收益歸給 RNG 或 variation 改動。

以下工作並未消失：

- 初次 admission、GPU 配置與編譯；一次性成本另外列出並納入搜尋總時間。
- 每代 phase lowering、compiled code copyback、CPU integration，以及 evaluator
  所需的 packing／upload；不能宣稱整個搜尋都已完全留在 GPU。
- 每代 history-best AST export 與獨立 admission，以及要求的 final exports。
- 必要的 memory／index／capacity／fuel 檢查，以及完整 program/case 評估。

Private owner 的生命週期與受控建構是 fast path 的信任來源；
外部 AST／bytecode 不會只憑一個公開的 `verified=true` 跳過驗證。

## 10. 性能與品質證據應如何解讀

P=1024、完整 cases=1024、CPU 1T 加 GPU 的未開 profiler generation 結果：

| 題目 | 九次正式量測中位數 | 相對 frozen ASGP 1T |
| --- | ---: | ---: |
| Sum of Elements | 48.884881 ms | 37.66× |
| House Robber | 40.384029 ms | 31.75× |
| Median | 12.213825 ms | 5.06× |

這是 frozen parents 的完整 generation。新 owner 的 cold admission 約 333–370 ms
另列，並包含在真實搜尋總時間中。以上不是 cold-start 單次呼叫加速比，也不是相同
演化軌跡的整段搜尋加速比。Scaling matrix 使用同輪 controls，不能與 frozen
headline 分母混用。

驗證包含三個 seeds 的短程篩查，以及較長的 Sum 64、House 96、Median 512
generations 搜尋。比較相同五秒搜尋預算時納入 cold initialization；House／Median
未出現明顯品質退化，獨立 held-out cases 亦保留當次觀察到的優勢。
Seed 0 的三個 final populations 共 3072 個程式經獨立 admission，完整 cases
CPU／GPU 重評與保存的 final fitness 相符。

這些證據仍有以下限制：

- 三個 seeds 與有限 workloads 不能證明收斂品質等價，也不能保證所有 grammar 受益。
- 沒有逐一拆出有放回 selection、單向 crossover、選點分布與 root 限縮的獨立
  品質貢獻；主要架構收益不能全部歸因為純執行效率。
- 後期 populations 有程式膨脹；不能拿不同 populations 的時間宣稱 identical-workload
  speedup，也不能把 frozen 初期時間外推為長跑每代時間。
- Frozen House snapshot 保留四筆先前就存在的 CPU／GPU fitness 差異；本輪沒有
  改變這些結果，不能宣稱所有 frozen snapshots 都 CPU／GPU 完全相等。
- Owner 只保留 live genotype buffers，但要求保存的每代 history-best AST／proof
  仍會隨 generations 增加；沒有證明整個 process 的總記憶體固定不成長。
- Attempted changes、最終 changed children、unique genotypes 與行為多樣性是不同指標。
  新 profile 的 final changed 判定比較完整 child 與選中的 parent，不能拿 operator
  嘗試次數代替有效新 offspring 數量。

因此，最準確的描述是：**這是一組保留完整評估、明列支援範圍與搜尋規則差異的
GPU GP profile，已有性能與品質篩查證據；不是與原本逐 child 等價的加速版本。**

### 10.1 實驗識別與量測邊界

以下為 2026-10-02 已保存的實驗結果，本次整理文件沒有重新跑性能實驗。GPU 為 RTX 3090，應用程式使用 CPU 1T；完整 1024 cases，fuel=2,000,000，GPU block size=512。Median 題目是三個整數的中位數，不是任意長度 list median。

正式 headline 使用 GPU-er-o，三個獨立 processes，每個 process 暖機一次、正式量測三次；與 uncounted 控制組交錯執行。每次 generation 由 frozen parents 開始，包含評估、selection、variation 與下一代必要整合。Cold owner initialization 另列；不把其成本假裝已消失。

```text
runtime commit: d1712a5
binary SHA-256: 32bbf36036566c7d34022121efef7c55fd174cbcaa3a97e6187f320af1fd0b5b
```

Frozen inputs 的個別 SHA-256、完整 flags 與九次 repeats 保存在 [正式結果 JSON](../../benchmarks/fixed_asgp/optimization/results/native-counted-validation.json)。使用本機封存的 jemalloc，`background_thread:false,narenas:1`；不是透過 CPU workers 加速。

### 10.2 正式中位數、範圍、分母與目標時間

| 題目 | Frozen ASGP 1T（ms） | GPU-er-o 中位數（ms） | 九次範圍（ms） | 加速比 | 30× 對應時間（ms） |
| --- | --- | --- | --- | --- | --- |
| Sum of Elements | 1841.160175 | 48.884881 | 48.580490–50.016281 | 37.66× | 61.372006 |
| House Robber | 1282.011816 | 40.384029 | 39.790332–42.125251 | 31.75× | 42.733727 |
| Median | 61.777141 | 12.213825 | 11.871306–12.702482 | 5.06× | 2.059238 |

加速比 = 該題 frozen ASGP 1T 分母 ÷ GPU generation 中位數。Sum／House 九次正式結果皆小於各自的 30× 時間門檻；Median 未達 30×。

### 10.3 Counted／uncounted 的全部九次原始值

下表單位皆為 ms；每個 process 的三個數值按正式量測順序列出，不包含 warm-up。Uncounted 是新 GPU phase profile 的 reservoir 版本，不是原生 AST reproduction。

| 題目 | 版本 | Process | Repeat 1 | Repeat 2 | Repeat 3 |
| --- | --- | --- | --- | --- | --- |
| Sum of Elements | uncounted | 1 | 51.724978 | 51.929535 | 51.735914 |
| Sum of Elements | uncounted | 2 | 50.844989 | 50.047047 | 50.122219 |
| Sum of Elements | uncounted | 3 | 50.602761 | 50.056163 | 49.468111 |
| Sum of Elements | counted | 1 | 48.884881 | 48.580490 | 48.656480 |
| Sum of Elements | counted | 2 | 49.819419 | 49.049208 | 48.873870 |
| Sum of Elements | counted | 3 | 48.672535 | 50.016281 | 49.033313 |
| House Robber | uncounted | 1 | 42.306261 | 41.688189 | 42.206785 |
| House Robber | uncounted | 2 | 43.799639 | 41.473564 | 41.669246 |
| House Robber | uncounted | 3 | 43.352604 | 44.390421 | 44.199670 |
| House Robber | counted | 1 | 40.249418 | 39.790332 | 39.853903 |
| House Robber | counted | 2 | 40.384029 | 40.940097 | 40.036987 |
| House Robber | counted | 3 | 41.814322 | 42.125251 | 41.564300 |
| Median | uncounted | 1 | 15.916658 | 15.564675 | 15.564720 |
| Median | uncounted | 2 | 15.842006 | 15.546272 | 15.502672 |
| Median | uncounted | 3 | 16.053629 | 15.548994 | 15.627992 |
| Median | counted | 1 | 12.702482 | 12.213825 | 12.257721 |
| Median | counted | 2 | 12.214982 | 11.918315 | 11.871306 |
| Median | counted | 3 | 12.306725 | 11.986752 | 11.972905 |

| 題目 | Uncounted 中位數 | Counted 中位數 | 減少時間（ms） | 時間下降 |
| --- | --- | --- | --- | --- |
| Sum of Elements | 50.602761 | 48.884881 | 1.717880 | 3.39% |
| House Robber | 42.306261 | 40.384029 | 1.922232 | 4.54% |
| Median | 15.564720 | 12.213825 | 3.350895 | 21.53% |

這個 A/B 隔離 counted flag，但兩者仍產生不同 offspring；它能支持 frozen-parent generation 的收益，不能當成相同長程演化軌跡的加速證明。

### 10.4 正式量測內的階段計時與 cold cost

以下取自 counted 的九次未開外部 profiler 量測。每一欄各自取中位數，包含父子區間；不能逐欄加總，也不能與下一節的 nsys 數字相減。

| 計時項目（ms） | Sum of Elements | House Robber | Median |
| --- | --- | --- | --- |
| 整段 evaluation | 43.223898 | 33.201784 | 5.340333 |
| 整段 reproduction | 5.593624 | 7.050122 | 6.536859 |
| Eval kernel | 39.673692 | 29.388713 | 2.140713 |
| Evaluator packing | 2.313452 | 2.356941 | 2.241284 |
| Reproduction kernels 合計 | 4.662400 | 5.742336 | 5.455968 |
| Code integration／decode 計時欄位 | 0.589244 | 0.775320 | 0.686670 |
| Reproduction D2H | 0.253498 | 0.374297 | 0.293104 |
| Cold owner admission／初始化 | 363.623284 | 332.976877 | 369.811237 |
| 完整單次 evolve 呼叫 wall time | 438.078135 | 397.968927 | 408.742844 |

單次 evolve wall time 包含額外初始化等呼叫內成本，因此明顯大於 generation；例如 House 是約 398 ms，不能拿 40.384 ms 代表冷啟動單次呼叫。此處 `repro_decode_ms` 是沿用的計時欄位名，不表示新路徑又做了逐 child 完整 AST decode。沒有獨立列出的 sync／verification 成本不代表為零。

### 10.5 五種原生模式與 scaling

每格為一次 process 中 warm-up 後三次正式量測的中位數，單位 ms。全表是四個 population × 三題 × 五模式，共 60 格；全部使用 1024 cases。P1024 的 CPU 格明確重用同輪、同 binary／設定的第三個 CPU-view process，其餘 controls 重新量測。

| P | 題目 | ASGP 1T | GAGP CPU | GPU-e | GPU-er | GPU-er-o | 同輪最快 GPU 比值 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 1024 | Sum of Elements | 1846.611 | 9463.718 | 392.870 | 48.987 | 48.893 | 37.77× |
| 1024 | House Robber | 1337.347 | 7909.963 | 399.659 | 41.222 | 40.604 | 32.94× |
| 1024 | Median | 59.113 | 684.355 | 377.890 | 11.854 | 12.223 | 4.99× |
| 2048 | Sum of Elements | 3742.526 | 19300.005 | 791.595 | 94.026 | 94.565 | 39.80× |
| 2048 | House Robber | 2668.223 | 16010.940 | 750.242 | 72.548 | 72.361 | 36.87× |
| 2048 | Median | 116.100 | 1358.792 | 740.768 | 27.021 | 26.381 | 4.40× |
| 4096 | Sum of Elements | 8162.754 | 40361.433 | 1586.747 | 184.023 | 182.512 | 44.72× |
| 4096 | House Robber | 5249.227 | 32305.824 | 1541.255 | 130.105 | 132.621 | 40.35× |
| 4096 | Median | 230.307 | 2855.964 | 1509.054 | 40.722 | 40.195 | 5.73× |
| 8192 | Sum of Elements | 17135.017 | 82281.561 | 3305.761 | 356.281 | 351.066 | 48.81× |
| 8192 | House Robber | 10546.576 | 64500.284 | 3047.228 | 269.042 | 264.246 | 39.91× |
| 8192 | Median | 466.695 | 5590.327 | 3016.391 | 88.373 | 85.497 | 5.46× |

最後一欄使用該列 ASGP control 作分母，並取三種 GPU 模式中的最快中位數；與第 10.2 節的 frozen 分母、九次 headline 不同，不能混用。ER／ER-o 在此 profile 的依賴排程相同，兩欄的小幅差異不能直接歸因於 overlap。

完整 [scaling 摘要 CSV](../../benchmarks/fixed_asgp/optimization/results/native-counted-scaling.csv) 與 [240 筆原始列（含 warm-up）](../../benchmarks/fixed_asgp/optimization/results/native-counted-scaling-raw.csv) 皆已追蹤。GPU-e 仍約數百 ms，顯示 CPU reproduction 沒有自動獲得新 GPU profile 的收益。

CPU window-view 是獨立 evaluator 改動，其九次 A/B 結果如下：

| 題目 | Views off 中位數（ms） | Views on 中位數（ms） | Off／on |
| --- | --- | --- | --- |
| Sum of Elements | 16960.628423 | 9465.922993 | 1.792× |
| House Robber | 7952.051310 | 7957.459415 | 0.999× |
| Median | 687.083184 | 686.401513 | 1.001× |

Sum CPU 約改善 1.79×；House／Median 差異很小，不據此宣稱它們也有明顯收益。CPU 九次原始值保存在正式結果 JSON 的 `paired_cpu_views`。

### 10.6 相同五秒預算的搜尋品質與 held-out 結果

`base` 是保留原生 AST reproduction 的 R 設定，`fast` 是最佳新 profile；不是 ASGP 品質控制組。比較從 evolve 入口起算五秒內完成的 generations，包含新增 cold initialization。共同的 benchmark JSON 載入／初始輸入重建不在此 evolve 預算內；不是完整 process 啟動到結束的五秒。Fitness 越大越好，0 為三題此評分下的理想值。

| 題目 | Seed | Base 完成代數 | Fast 完成代數 | Base 最佳 training | Fast 最佳 training | Base held-out | Fast held-out |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Sum of Elements | 0 | 18 | 26 | 0 | 0 | 0 | 0 |
| Sum of Elements | 1 | 17 | 30 | 0 | 0 | 0 | 0 |
| Sum of Elements | 2 | 18 | 31 | 0 | 0 | 0 | 0 |
| House Robber | 0 | 19 | 68 | -7570 | -5492 | -8056 | -5812 |
| House Robber | 1 | 21 | 72 | -7465 | -4337 | -7925 | -5102 |
| House Robber | 2 | 21 | 75 | -7570 | -5843 | -8056 | -5933 |
| Median | 0 | 16 | 131 | -26620 | -26445 | -29242 | -26918 |
| Median | 1 | 16 | 145 | -28478 | -16159 | -31447 | -17000 |
| Median | 2 | 16 | 126 | -25038 | -18062 | -26286 | -19995 |

Held-out 每題完整 1024 cases，依 ASGP 原範圍獨立生成並排除與 training 完全相同的輸入；不是任意分布之外的泛化保證。評估的是五秒內選出的最佳程式，18 個程式均 CPU／GPU fitness 相符，且 error／timeout／fallback／unscored 皆為零。

| 題目 | 預先訂定 training 門檻 | Base 達標 seeds | Fast 達標 seeds |
| --- | --- | --- | --- |
| Sum of Elements | = 0 | 3/3 | 3/3 |
| House Robber | ≥ -8000 | 3/3 | 3/3 |
| Median | ≥ -25000 | 0/3 | 2/3 |

Sum 的最佳程式在 generation 0 就已出現，因此這組 Sum 品質結果主要確認保留正確解，不能證明新 operator 更容易從無解族群找到解。

搜尋 population 的 runtime error 比例（五秒內，依 seed 0／1／2 順序）：

| 題目 | Base error 比例 | Fast error 比例 |
| --- | --- | --- |
| Sum of Elements | 4.416% / 3.907% / 4.204% | 2.151% / 1.979% / 3.934% |
| House Robber | 0.000% / 0.000% / 0.000% | 0.000% / 0.000% / 0.000% |
| Median | 4.611% / 4.012% / 4.912% | 4.020% / 3.684% / 3.320% |

上述長跑所有題目／seeds 的 timeout、執行 fallback、unscored 計數皆為零。這裡的 execution fallback 不等於 crossover 重試耗盡；後者可能保留 parent，也可能再被 mutation 改變。Runtime errors 仍按規則計分，沒有跳過。

### 10.7 長程成本、程式大小與多樣性

以下是新 profile 各 seed 的完整 evolve wall time，包含 cold initialization 與 final materialization／evaluation。Base 跑 32 generations，新 profile 跑較多 generations，因此不能直接把兩者完整時間相除當加速比。

| 題目 | Seed | Generations | 完整 wall（s） | Cold（ms） | Final 平均 nodes | Final unique／1024 | 最後一代（ms） |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Sum of Elements | 0 | 64 | 14.761 | 368.159 | 332.40 | 1018 | 235.648 |
| Sum of Elements | 1 | 64 | 12.607 | 367.018 | 325.41 | 1022 | 214.798 |
| Sum of Elements | 2 | 64 | 11.158 | 366.299 | 307.97 | 1021 | 199.426 |
| House Robber | 0 | 96 | 9.318 | 330.554 | 554.12 | 1022 | 160.526 |
| House Robber | 1 | 96 | 7.891 | 344.527 | 595.61 | 1021 | 82.289 |
| House Robber | 2 | 96 | 7.609 | 333.609 | 581.47 | 1022 | 112.218 |
| Median | 0 | 512 | 31.620 | 368.678 | 794.88 | 1018 | 74.994 |
| Median | 1 | 512 | 29.926 | 377.613 | 823.81 | 1021 | 82.808 |
| Median | 2 | 512 | 31.689 | 382.908 | 825.93 | 1022 | 73.457 |

Final unique 是最終 population 的 genotype 去重數量，不是整段搜尋累積的新 offspring 數，也不是行為等價類。Seed 0 final population 的 32-case behavior probe 分別得到 Sum 27、House 205、Median 76 種 signatures，不能外推到完整輸入空間。

Fast 在五秒內的「每代 final child 相對選中 parent 有變更」中位數依 seeds 為：

| 題目 | Seed 0 | Seed 1 | Seed 2 |
| --- | --- | --- | --- |
| Sum of Elements | 930.0 | 936.0 | 950 |
| House Robber | 966.5 | 950.0 | 952 |
| Median | 950 | 954 | 969.5 |

這些數字仍不是 unique new offspring；不同 children 可得到相同 genotype。Base 對應歷史欄位是 operator outputs 的累計，可能大於 1024，因此不把 base／fast 的該欄直接比較成 mutation 成功率。

獨立 Median 512-generation 記憶體診斷：peak RSS = 1,344,524 KiB（約 1313.0 MiB），genotype owner device storage = 899,100,980 bytes（約 857.4 MiB）。RSS 的 5–15 秒區間約 1011 MiB，25–30 秒約 1075 MiB；final audit／report 期間達到 peak。Device 數值不含 evaluator scratch 或 driver context。History-best 的保留仍隨 generations 成長。

### 10.8 GPU nsys 診斷：House kernel 成本

這是開啟 nsys 的診斷結果，每個 kernel 各四次 launches 的平均，包含該實驗的冷／暖呼叫。不得用它計算正式加速比，也不能直接由正式 generation 中位數扣除。

| Kernel／工作 | Launches | 平均（ms） |
| --- | --- | --- |
| Evaluation | 4 | 29.826657 |
| Mutation | 4 | 2.014886 |
| Crossover（含 tournament selection） | 4 | 1.642783 |
| Phase lowering／compile | 4 | 1.366687 |
| Import annotation（初始化） | 4 | 0.913807 |
| Offset scan | 4 | 0.531048 |
| Final genotype compare | 4 | 0.323304 |
| Code compaction | 4 | 0.007216 |

Selection 融合在 crossover kernel，不能將未獨立列出的 selection 當成 0 ms。Import annotation 只屬於初始化，不是每代都額外加一次。

### 10.9 CPU perf 診斷

權限開放後，使用相同 archived binary 取樣；未更換 debug build、未增加 CPU workers。來源為 [perf 診斷 JSON](../../benchmarks/fixed_asgp/optimization/results/native-perf-20261002.json)。

| Sum CPU 函式 | Self user-cycle 樣本比例 |
| --- | --- |
| run_code_impl_true | 43.59% |
| execute_bounded_region | 14.26% |
| RegionAdapter_run | 11.18% |
| builtin_call | 8.94% |
| ValueStack_push_back | 7.43% |
| CpuListViews_call | 2.30% |

上表涵蓋整個 process，包括匯入與四次 generations；不是純 generation wall time 拆解。它支持優先減少 interpreter、phase binding 與 Value movement，不能把 43.59% 直接解讀成可省下 43.59% 端到端時間。

| Median GPU-e：CPU reproduction 呼叫鏈 | Inclusive user-cycle 樣本比例 |
| --- | --- |
| VariationAnalysisCache_analyze | 86.08% |
| certify | 72.40% |
| analyze_variation_impl | 69.89% |
| WitnessBuilder_run | 18.19% |
| runtime_cache_identity | 15.67% |
| canonical_cache_key_serialize | 13.05% |
| compile_for_eval | 11.45% |
| verify_ast | 7.55% |

Median 深層取樣為 999 Hz、32768-byte stack capture，共 2345 samples，其中 1296 能辨識出 CPU reproduction 呼叫鏈。上表只以這個子集為分母，包含子呼叫、不能相加；其中 14.76% leaf samples 無法解析。GPU 等待與 kernel wall time 不由這些 CPU cycles 比例涵蓋。這是優化方向的證據，不是新增的性能改善。

### 10.10 正確性檢查與可重現入口

既有 runtime 驗證紀錄：完整 native suite 127/127、fixed benchmark tools 5/5、repository checks 24/24；CUDA memcheck 在 P16、完整 1024 cases、32 generations 測例為 0 errors。專用測試顯式啟用新 reproduction profile，舊 reference RNG 測試仍保留其對應模式。這些是歷史 runtime 驗證，並非此次編輯 Markdown 又重跑了所有 GPU 測試。

三題 seed 0 的 final population 共 3072 個程式，獨立 grammar admission 與完整 cases CPU／GPU 重評無差異。Frozen House 的四筆既有差異另有保留，不與這批 evolved-program 驗證混稱。

在具備 archived dependency 與 frozen artifacts 的實驗 worktree 根目錄，可以使用新輸出目錄重跑五模式日常檢查：

```bash
python3 benchmarks/fixed_asgp/optimization/native_candidate.py daily \
  --out logs/optimization/native-daily-NEW
```

同一 runner 的 `paired` 階段可重跑 counted／uncounted 九次比較，`scaling` 可跑完整矩陣。執行前核對目前 build binary 與 manifest；`daily` 不等於三 processes 的正式九次 headline。不要對 frozen artifacts 重跑 prepare，也不要覆寫歷史輸出。

## 11. 程式碼與結果索引

以下相對路徑指向本 repository；函式名稱可用來定位，不依賴易變動的行號。

| 主題 | 實作／證據 |
| --- | --- |
| 新 selection、選點、crossover、mutation | [`phase_population.cu`](../../cpp/src/evolution/repro/gpu/phase_population.cu)：`tournament`、`choose_site`、`crossover_population`、`mutate_population` |
| 啟用條件、ranking、每代 seed 與完整流程 | [`evolve.cpp`](../../cpp/src/evolution/evolve.cpp)：`evolve_gpu_phase_population` |
| 舊 CPU selection 與 program-key tie-breaking | [`selection.cpp`](../../cpp/src/evolution/selection.cpp) |
| 舊 GPU tournament 與合法-pair 選擇 | [`selection_kernels.cuh`](../../cpp/src/evolution/repro/gpu/device/selection_kernels.cuh) |
| 舊 CPU crossover／mutation | [`crossover.cpp`](../../cpp/src/evolution/grammar/crossover.cpp)、[`mutation.cpp`](../../cpp/src/evolution/grammar/mutation.cpp) |
| 舊 GPU donor preparation／mutation | [`grammar_prep.cpp`](../../cpp/src/evolution/repro/grammar_prep.cpp)、[`compiled_mutation.cuh`](../../cpp/src/evolution/repro/gpu/device/compiled_mutation.cuh) |
| 舊 replacement allowance | [`variation_contract.cpp`](../../cpp/src/evolution/grammar/variation_contract.cpp) |
| 新 grammar profile 與 cold import 邊界 | [`phase_variation.hpp`](../../cpp/src/evolution/repro/gpu/phase_variation.hpp)、[`phase_layout.hpp`](../../cpp/src/evolution/repro/gpu/phase_layout.hpp) |
| 新 subtree generation／splice 契約 | [`phase_generate.cuh`](../../cpp/src/evolution/repro/gpu/device/phase_generate.cuh)、[`phase_variation.cuh`](../../cpp/src/evolution/repro/gpu/device/phase_variation.cuh) |
| 隨機數與 unbiased bounded 抽樣 | [`grammar_random.cuh`](../../cpp/src/evolution/repro/gpu/device/grammar_random.cuh) |
| 跨代、fallback、export 與 CPU reevaluation 測試 | [`test_gpu_phase_population.cu`](../../cpp/tests/gpu/test_gpu_phase_population.cu) |
| 計時契約與重現方式 | [Fixed benchmark guide](../guides/fixed-asgp-benchmark.md) |
| 正式 repeats、品質、scaling 與限制 | [`native-counted-validation.json`](../../benchmarks/fixed_asgp/optimization/results/native-counted-validation.json) |
| 目前最佳設定與原始 artifact 位置 | [`PROGRESS.md`](../../benchmarks/fixed_asgp/optimization/PROGRESS.md) |

原始 binary、manifest、逐次量測與搜尋 logs 保存於本機
`logs/optimization/native-counted-validation/`。這些本機 artifacts 不等同於遠端
repository 必定包含的檔案；分享文件時可使用已追蹤的結果 JSON 與上述索引。
