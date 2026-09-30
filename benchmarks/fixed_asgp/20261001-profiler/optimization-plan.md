# GAGP 優化提案：從保守到改寫執行架構

依據 [1024 × 1024 profiler baseline](README.md)。以下全部是提案，**尚未實作，也不是預測已能達成的加速比**。`medium` 按目前 benchmark 的 Median 解讀。

## 目標與判斷

40× 可以作為 Sum／House 的架構驗收目標，但需要同時處理 host 與 eval kernel；依目前量測，只調 blocksize、PCIe、同步或 reproduction kernel 不足以達成。100× 可列為後續 stretch goal，現在沒有量測依據可以保證。

| 完整 generation | Sum of Elements | House Robber |
| --- | ---: | ---: |
| 本輪 ASGP 1T，ms | 1841.160 | 1282.012 |
| GAGP 最佳 unprofiled，ms | 805.996 | 761.333 |
| 40× 必須低於，ms | 46.029 | 32.050 |
| 距目前 GAGP 還需加速 | 17.51× | 23.75× |
| 100× 必須低於，ms | 18.412 | 12.820 |
| 目前 eval kernel alone，trace ms | 566.394 | 480.240 |

即使其他工作全部免費，保留現在的 eval kernel 也只有約 3.25×／2.67× 對 ASGP 的加速。光 kernel 就得快約 12.3×／15.0× 才能碰到 40× 的整代預算，而且那還沒留任何 host/repro 成本。

Median 不應沿用 Sum 的優化順序。它的 kernel 約 10.8 ms，而目前整代 265.2 ms，優先消除 host 工作才有效。可先以 **整代 <60 ms** 為第一階段目標，再評估 **10–20 ms**；後者是需要更大幅改寫的目標，不能由現有 profile 保證。

下列預算是一個可檢驗的設計方向，並非將現有各項獨立收益相加的預測：

| 建議 40× 設計預算，ms | Sum | House |
| --- | ---: | ---: |
| Host compile／packing／verification／repro 合計 | 8 | 6 |
| Eval kernel | 30 | 18 |
| GPU selection／repro | 2 | 2 |
| Transfers／allocation／sync／其他 | 2 | 2 |
| **完整 generation** | **42** | **28** |

## 1. 保守：先消除重複工作與配置成本

### 重用已驗證的結構，避免反覆全量分析

目前 bytecode packing 的 bounded verification 約 **52／52／76 ms**；parent grammar analysis 約 **59／64／70 ms**；child admission batches 約 **34／36／41 ms**。後兩項包含建 metadata、cache 與等待 workers，不能把整段都稱作可直接刪除的 verifier。

建議把 immutable compiled program 的 stack/local/region bounds 與 type 摘要作為可重用的驗證憑證；GPU packing 僅補查 target 限制與 buffer bounds。Parent 的 grammar witness、typed occurrence、donor compatibility 可在個體生命週期重用；mutation 只更新受影響節點。完整 grammar 資料重建與完整 replay 驗證移至需要匯出、除錯或外部輸入的路徑。

注意 compiler 的一般 `verify_bytecode` 在 Release 下受 `NDEBUG` 影響，不能假設目前所有完整驗證已經做過。保守版必須明確讓 compiler 產生足夠憑證，或保留一次必要的 GPU bounded verification；只將同一檢查移到 compile 不會省時間。獨立程序的 target verification 可先平行化，但仍應量 full-generation time，避免增加另一批互相競爭的 workers。

涉及：[compiler.cpp](../../../cpp/src/evolution/compiler.cpp)、[host_pack_gpu.cu](../../../cpp/src/runtime/gpu/host_pack_gpu.cu)、[compiled_decode.cpp](../../../cpp/src/evolution/repro/compiled_decode.cpp)。

### 修正 repro arena／pinned staging 的容量重用

每代 allocation/setup 約 **13 ms**，其中 pinned alloc/free 約 10 ms。`ensure_gpu_repro_host_staging_capacity` 使用完整 repro config 比較容量，包括 staging 不使用的 donor/occurrence/domain 維度；失敗時全部釋放並以新需求重新配置。GPU arena 也有整組重建行為。

優先改成各 buffer 的容量上限重用，host staging 僅比較真正影響其尺寸的欄位；成長時保留其他 buffer 容量，避免 crossover／mutation 在不同需求之間反覆重建。保留明確記憶體總預算與 stream ownership，不能無限制把每個維度都取最大值。

這是小範圍且證據明確的起點，但 13 ms 只是該類成本總量，不代表可以全部消失。[arena.cu](../../../cpp/src/evolution/repro/gpu/arena.cu)；NVIDIA 也指出 pinned allocation 成本較高，適合重用與批次傳輸。[CUDA Best Practices](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device)

### 重新安排 overlap 與 worker 數量

目前 preparation 與 **實際 GPU kernel 重疊 0 ms**，主要重疊 CPU compile/pack；compile wall 在 overlap on 約多 15 ms。先試在 compile 結束後才啟動 preparation，讓它與 serial packing 及 kernel 重疊；或使用同一個有優先序、總 worker 數受限的 pool。

不要一律等 kernel launch 才開始：Median 的 kernel 只有約 11 ms，無法藏住約 60 ms preparation。應用真實 critical path 決定排程，並將各題的 overlap off/on 都保留為比較。

### 將小收益排在後面

- 僅上傳 live nodes/bytes、合併小 copies、重用 grammar-static tables；H2D+D2H 目前只有約 4 ms，所以不是 40× 的主力。
- GPU selection+variation 已約 1.5–1.9 ms，先不重寫 tournament kernel。
- Raw sync wait 幾乎都是 kernel 執行時間，exposed sync 只有約 0.1 ms；不能把刪掉同步當成數百 ms 收益。
- CUDA Graphs 可以降低重複提交的 host 開銷，但本輪直接 launch overhead 很小，無法修好 VM、payload 或 grammar analysis。將它排在架構穩定後。[CUDA Graphs](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/cuda-graphs.html)

這一層適合先驗證工程方向，特別有利 Median；即使非常成功，也不足以讓 Sum／House 達到 40×。

## 2. 中度：讓通用文法產生適合 GPU 的執行形式

**通用文法前端可以保留，後端不必所有問題都走同一個 Mixed VM。** 依 grammar/type/region 結構選擇執行形式，無法辨識的結構繼續走 generic backend。這與寫死某題答案不同；演化中的 solve/combine expression 仍然照常執行。

| 方向 | 量測／source 依據 | 建議設計與適用條件 |
| --- | --- | --- |
| Typed、compact VM/IR | 三題都走 Mixed payload、regions enabled、64 registers/thread | Int-only 與 IntList-only 路徑省略 String payload、動態 Value tag dispatch 與不需要的臨時 state；使用 grammar-level specialization、superinstructions 或 register IR。先維持 int64 語意，降精度另列有損選項。 |
| Sum：zero-copy sequence views | DC window transition 目前呼叫 Slice，複製 values、hash、登記 local payload | 以 `(base, offset, length)` 表示 slice；Len/Index 直接使用 view。內容 hash 僅在可觀察操作需要時產生，真正生成新 sequence 才配置。這也可能改善現有 payload pool 耗盡造成的誤差。 |
| House：dense memo／較小 region frame | Memo lookup 現在逐 cell 比較 keys；frame 固定容納 4 state、4 prepared、8 results | 對可證明有界整數座標、固定遞減依賴的 DP，改成 indexed memo；可證明無環時使用 bottom-up schedule，需要的 live range 足夠小才用 rolling storage。保留 evolved expression，不直接計算 House 正解。 |
| Median：移除無用的 region machinery | 固定 cases 都是長度 3、base predicate 保證成立，但仍配置 generic region workspace | 由 input shape 與 region base predicate 證明後，直接執行 base solve expression。這是結構特化；不能對未知輸入直接假設所有遞迴分支都不會發生。 |
| 更小的 region workspace | Sum/Median 受 512 MiB 預算限制為 79 blocks，GPU 有 82 SM；House 160 blocks | 按真實 state/結果型別與數量配置 frames，依控制流程算需要的 depth。再量 blocksize/case tiling。79 blocks 是 launch 限制的證據，不等於已量到 occupancy。 |
| 簡化 fitness accumulation | Generic kernel 反覆 canonicalize 浮點 reduction | 若輸出、誤差與 penalty 可證明為整數且累加不溢位，使用整數 sum，最後再轉結果型別；不能直接套用到任意浮點 grammar。 |

對 Sum，view 路徑可同時減少 payload lookup、copy/hash 與容器 state；對 House，dense memo 與 compact frames 可減少 generic state machine／workspace 操作。但 nsys 沒有同一 kernel 內各 instruction 的耗時比例，**現在無法替這些方向承諾 2×、10× 等數字**。後续仍以 nsys、同一批 programs/cases 做逐项 A/B。

Case length/program length 分組也可降低不同執行長度的拖尾，但要保留每個 program 的完整 1024 cases；排序、packing 成本須算入整代。不要先堆一層昂貴 scheduler。

涉及：[fitness_gpu.cu](../../../cpp/src/runtime/gpu/fitness_gpu.cu)、[kernels.cuh](../../../cpp/src/runtime/gpu/device/kernels.cuh)、[region_execution_device.cuh](../../../cpp/src/runtime/gpu/device/region_execution_device.cuh)、[builtins_device.cuh](../../../cpp/src/runtime/gpu/device/builtins_device.cuh)、[region_types_gpu.hpp](../../../cpp/include/gagp/runtime/gpu/region_types_gpu.hpp)。

## 3. 大幅改寫：讓 evolution 的主要資料留在 GPU

這是我認為衝 40× 最值得投資的長期方向。現有 GPU repro kernel 本身已快，昂貴的是周邊 host 流程：

```text
GPU crossover → D2H → CPU decode/admit
              → CPU mutation prepare → H2D
              → GPU mutation → D2H → CPU decode/admit
```

建議演化狀態直接採用 **可在 GPU 原位執行與修改的 typed genome／IR**：

1. Grammar 一次編譯為 production/type/slot tables，個體維持緊湊的 GPU layout。Node 的型別、subtree span、局部 scope 摘要可隨 mutation 增量更新。
2. GPU 保留 fitness、selection、parent/child buffers；crossover 與 mutation 合併為產生下一代的裝置端流程，避免兩次完整 decode/admission 往返。
3. Variation 依 grammar production/type 直接產生有效子代；能維持 scope、同名 hole coupling 等約束的部分，依 construction 維持。Compile 使用 GPU lowering、局部 bytecode patching，或讓 genotype 本身就是可執行 IR。
4. CPU 只收 top-K、摘要與 checkpoint；需要顯示／匯出時才重建完整 AST、推導歷史與 provenance。**D2H 本身只占不到 0.3 ms；重點是消除 D2H 前後的 host 分析與重建。**

這符合 NVIDIA 對中間資料常駐 device、避免 host 往返的建議；本案最主要收益是否成立仍要由整代量測確認。[CUDA Best Practices](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#data-transfer-between-host-and-device)

可以積極簡化的設計：

- 每代完整 AST/derivation 重建、全量 parent reanalysis、每個中間 offspring 再做一次完整 admission。
- Crossover、mutation 各自 prepare／pack／copyback／decode 的 host orchestration。
- 每個候選與 donor 的全面 compatibility 列舉，改為 typed production 分桶與有限次抽樣；抽样分布改變屬明確的搜尋策略變更。
- Hot path 的詳細 provenance、replay snapshots、payload transaction bookkeeping：改成 versioned immutable references、按需重建與抽樣稽核；仍需維持執行資料的生命週期。
- GPU 上持續維護 CPU 完整物件模型的需求：改成獨立 execution format，CPU representation 成為匯出格式。

這不是一個小 patch。先以支援的 grammar capability 分級，保留 generic reference path 與外部輸入 admission。對不支援快速路徑的文法回退，而不是讓整個快速 backend 被最複雜的情況拖住。

**文法生成有效性與有損數值執行是不同契約。** 若把「符合作者文法」再放寬成「型別正確、GPU 可執行」以換取更簡單 reproduction，就需要明確的新 fast-mode contract；不能默默破壞 scope/hole 關係後仍宣稱是原文法的相同搜尋空間。無論是否有損，buffer bounds 與有效記憶體存取仍須保證。

涉及：[repro/gpu.cpp](../../../cpp/src/evolution/repro/gpu.cpp)、[compiled_decode.cpp](../../../cpp/src/evolution/repro/compiled_decode.cpp)、[compiled_variation.cuh](../../../cpp/src/evolution/repro/gpu/device/compiled_variation.cuh)、[compiled_mutation.cuh](../../../cpp/src/evolution/repro/gpu/device/compiled_mutation.cuh)。

## 4. 更激進的有損 profile 與搜尋空間取捨

這些可以進一步探索，但必須各自標記改變的語意與品質成本。

| 激進程度／方向 | 可省的工作 | 代價與比較方式 |
| --- | --- | --- |
| Int32／FP32、較粗 fitness ranking | Value/寄存器/workspace 大小、64-bit 算術與 canonicalization | 正解數值小不代表隨機 program 不會溢位；ranking 會改變。使用 CPU/reference 重評 top-K，觀察排名與解品質。 |
| 較小 depth、frame、memo、list cap；fuel 每一段而非每個 VM op 計數 | 大 workspace、長尾執行、頻繁 counter | 更早 timeout／截斷會改 fitness。完整 1024 cases 仍執行；要另記 approximate/error/timeout 比例，避免以全部提早失敗換取漂亮速度。 |
| 固定 grammar skeleton + evolvable expressions、linear/register/DAG genotype | Tree traversal、昂貴 subtree matching 與 lowering | 搜尋空間改變；保留多種 grammar templates，但不強求所有原始推導路徑。需獨立品質評估。 |
| 每次固定少量 mutation/crossover 嘗試，失敗保留 parent | Donor/candidate 全面列舉與 admission 重試 | 子代多樣性、有效變異率降低。需報 changed offspring、fallback，不能只看 generation ms。 |
| Grammar/template JIT，或固定常用 typed interpreter families | 大型 VM dispatch 與多餘 generic 分支 | 模板編譯一次可攤提；若每代每個個體都 JIT，1024 次編譯可能成為新瓶頸。任何個體相關 codegen/compile 要計時。 |
| 128/256 cases 初篩、cascade evaluation、近似 fitness | 直接减少 evaluated program-case pairs | **另立 approximate-evaluation benchmark**，不能宣稱達到目前「1024 個實際 cases 全評」fixed BM 的 40×。完整品質由獨立全量 evaluation 確認。 |

NVRTC 可在 runtime 編譯 CUDA；這裡較合理的是 grammar/template 級特化與 cache，而非預設每個個體每代重新編譯。[NVRTC 12.6 文件](https://docs.nvidia.com/cuda/archive/12.6.1/nvrtc/index.html)

我會先做「仍跑完整 cases、但執行表示更簡單」的方向，再考慮 sample cases。這樣測到的改善可直接延續本輪 baseline。

## 5. 實施順序與驗收

1. **先做 host 成本減法**：arena/staging reuse、驗證憑證與 metadata 重用、overlap 排程。Median 完整一代是否接近／低於 60 ms，是比只報某段快幾倍更有用的驗收。僅 arena 與排程不足以達到這個目標，需要解決分析／重建。
2. **並行規劃、逐项實驗 typed eval**：Sum views、House indexed memo／compact frames、Median base-only lowering；再試 case tiling/blocksize。保留未特化 fallback，先量真正 kernel 與 full-generation 兩者。
3. **以完整 generation 的預算決定 GPU resident 改寫範圍**：目標 Sum <46 ms、House <32 ms；host 若仍要 100+ ms，就不應繼續只優化 kernel 的幾 ms。
4. **40× 達成後再衡量 100×**：18.4／12.8 ms 的整代預算對任何每代完整 CPU reconstruction 都很苛刻；更可能需要精簡 IR、裝置端 generation 與受限制的快速 grammar profiles 一起成立。

每次局部更動先跑受影響題目的 fixed cells，再跑完整 daily；保持相同 frozen hashes、1024 population、1024 actual cases、完整 variation、1 warmup + 3 measured。接近目標或差異小於噪音時才增加 repetitions／交替執行 old/new 控制組；不必讓日常 BM 變慢。

品質檢查與速度測量分開：已知正解、獨立 top-K CPU reevaluation、fitness ranking/top-K overlap、有效變異與 fallback 比率，再以少量多 seed 搜尋比較 held-out fitness 與 time-to-solution。逐 case error/timeout/approximation 計數是**建議新增**的診斷；目前 aggregate fitness 不能反推這些數量。現有 Sum GPU 正解 audit 為 −690000、CPU/ASGP 為 0，說明已有 payload 損失，優化時應持續追蹤，不能用更多錯誤冒充執行效率。

GPU resident 與跨代增量資訊特別適合連續 evolution。現行 fixed BM 每個 repetition 是 frozen-parent 的一次完整 evolution call；**不能利用重複父代預存 fitness，或把每代 compile／packing 偷移到計時外**。既有 session/grammar-static setup 的邊界照原 contract。未來若要量多代攤提收益，可額外加連續例如 32 代的測試，同時報 startup 與 steady generation；它是補充指標，不取代這份 fixed baseline。
