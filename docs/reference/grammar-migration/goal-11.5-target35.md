# 第二輪局部優化：32–35 倍目標的實測與停止判斷

**後續驗收決定：使用者於 2026-09-27 要求將 Goal 11／11.5 標記為達成。**
此為接受本報告的實測結果，取代先前未完成狀態；不代表原始 Q≥0.95
數值門檻通過。下列測量、限制與停止判斷保持原樣。


日期 2026-09-27；最終程式 `36c8cea692f4e82fdb8612d0a8480ce16f8d056b`，
接續 `e40e764` 的報告／`c574b7b` 程式。使用者授權沿用 overlap 目標繼續
嘗試，直到判斷進一步局部優化已不划算。原始 Q≥0.95 沒有因此變成通過。

**本輪未穩定達到先前估計的 32–35 倍。** 保留一項確定有益的實作：
以 operation-owned workers 平行重建 child AST／metadata，admission batch
由 128 增至 256，仍最多 20 workers。與前 checkpoint 交錯測量時，三次 GPU
時間均較快，中位數改善 5.26%。其餘試驗回退。

最終同一 binary 在兩組 campaign 的加速比分別為 **31.4393 與 30.4398 倍**。
全部六次非暖機觀測合併的描述性中位數為 CPU 5220.654 ms、GPU 169.801 ms，
比值 **30.7458 倍**。合併值不是另一組預先規劃的配對驗收，也不是信賴區間；
保留兩組個別結果，不能只挑 31.44 倍作穩定保證。

## 完整 generation 配對結果

S=CPU 中位數／GPU 中位數；同一凍結 p1024、1024 cases、fuel 20000、
blocksize 1024、等價 source 80 nodes／depth 7 預算。CPU/GPU 同用既有
`tcmalloc`，全部 preparation、evaluation、reproduction、必要 cleanup 在
generation 計時內；無刻意放慢 CPU、減少 GPU 工作或改動 grammar。
每組一次暖機、三次測量，版本／模式順序輪替。單位 ms。

| Campaign／版本 | CPU 三次 | GPU overlap 三次 | CPU 中位數 | GPU 中位數 | S |
| --- | --- | --- | ---: | ---: | ---: |
| 第一組／前 checkpoint `c574b7b` | 5375.881, 5307.489, 5371.052 | 172.125, 176.859, 175.800 | 5371.052 | 175.800 | 30.5520x |
| 第一組／保留版（20 workers） | 5314.258, 5158.173, 5236.192 | 166.322, 166.549, 171.637 | 5236.192 | 166.549 | **31.4393x** |
| 第二組／相同保留版（20 workers） | 5211.445, 5172.359, 5229.862 | 168.396, 171.205, 172.075 | 5211.445 | 171.205 | **30.4398x** |
| 第二組／16 workers 試驗（回退） | 5164.975, 5182.287, 5211.363 | 168.845, 177.661, 168.298 | 5182.287 | 168.845 | 30.6925x |

保留版 direct 在第一組的三次 GPU 時間為 198.473／202.671／202.032 ms，
中位數 202.032 ms，S=25.9176x。不是 direct >30x。

16 workers 雖有單次 162.428 ms 的探索值，但正式三個配對中兩個較慢，
只有中位數的小幅差異，不足以保留。第二組中的 20-worker checkpoint 正是
最終保留 binary，故也納入最終六次完整觀測，沒有丟棄較慢結果。
CPU 實作未改，兩版 CPU 時間差異仍影響 S；第一組 GPU 時間改善 5.26%，
對應 S 改善約 2.9%，兩個百分比不能混用。

基準版沿用前輪已完成證據：相同 allocator，direct S=37.8023、overlap
S=45.959；歷史預設 allocator direct S=41.9585。以最終六次合併值作
描述性對照，同模式 overlap Q 約 0.669，仍顯著低於 0.95。
未重跑已完成的基準，也不把不同 campaign 當成全程交錯測量。

## 保留的改動與正確性邊界

`cpp/src/evolution/repro/compiled_decode.cpp` 原本先在呼叫端序列重建所有
候選 AST／metadata，再交給 workers 完整驗證。現在先由同一 worker team
各自寫入獨立 indexed slots，barrier 後依原 child 順序收集，再進行原本的
完整 admission。重建只讀 immutable prepared tables，不發布 payload，不選
新的變異，不更動 seed、retry、donor 數量或執行結果。

只有 source/context ownership 相符、沒有 enclosing payload scope 的流程
使用平行重建。失敗仍由原本的序列路徑在相同 child 位置重試／拋出；完整
native／membership／budget／payload 檢查保留。每批最多 256 個 child slots，
比原本 128 多一倍的 bounded admission 暫存；worker 上限仍 20，沒有無界
快取或跨 generation 殘留 worker。

測試人口改為 257，涵蓋完整／不完整批次、奇數捨棄 sibling、原樣 fallback，
並比較具證明與無證明路徑的 offspring、計數與過期／錯誤 ownership 行為。
未新增文法、型別、命名或交換相容性規則。

## 探索與回退

以下是單次探索的 GPU overlap generation 時間，不能取代上面的 paired
結果。部分試驗建立於先前暫存候選上，不能將差異當成各 patch 的獨立效果。
完整 CPU／GPU 數字、58 份測量 receipts 與 hash 在
[機器可讀證據](goal-11.5-target35.json)。

| 試驗 | GPU ms | 決定 |
| --- | ---: | --- |
| child worker context 跨批次重用 | 171.196 | 回退；收益小，增加 cache／registry 生命週期複雜度 |
| 唯一型別 parent root request 專用驗證 | 173.014 | 回退；無明確收益 |
| 同 donor job 跨 seed 重用 admission cache | 175.584 | 回退 |
| preview 只判斷 constant group 是否存在 | 177.569 | 回退 |
| exact no-op constant child 重用父代證明 | 177.771 | 回退；保守增量驗證原型沒有收益 |
| 只擴大 admission batch 至 256 | 170.631 | 與平行 metadata 重建合併後驗證 |
| 平行重建、batch 256 | 168.001 | 保留，後續 paired 確認 |
| fallback 直接比較 device view，避免暫存 AST | 169.095 | 回退；複雜度不值得 |
| 平行重建、batch 512 | 168.253 | 回退；沒有更快、暫存更多 |
| 平行重建、batch 128 | 173.095 | 保留 batch 256 |
| admission 轉移 AST 所有權、失效時重建 | 169.595 | 回退；沒有明確收益 |
| child workers 10 | 170.728 | 回退 |
| child workers 16 | 162.428 | paired 優勢不穩定，回退 |

完整的一般 AST 增量 structural/type certification **沒有實作，也沒有宣稱
驗證完成**。本輪只試了可嚴格證明原樣不變的 constant no-op；前輪另試過
有限的 membership 增量原型。要將完整分析拆成可組合、可失效的局部證明，
必須處理 scope、模板共享 occurrence、canonical grammar 選擇、resource
投影及 mutable payload，已超出這輪低風險局部修復。

## 測試、較小工作量與停止理由

保留版通過四項 Release/CUDA checks：splice metadata、evolution CPU/GPU
parity、八種型別的 compiled payload evolution、compiled GPU backend。
CPU-only Release build 的 splice metadata 與 evolution pipeline 也通過。
文件整合後的 23 項 repository checks 全數通過，包含相對連結與文件索引。
全數 1024 ordered native AST records 與前輪 window checkpoint 相同，另通過
native membership/lowering 與 budget 驗證。58 份探索／配對 receipts 的
fitness 與操作計數都與對應 CPU/GPU checkpoint 相同。

五個 p64 工作量各做一次小型診斷，不額外重跑前輪完整 matrix：

| Workload | 前 checkpoint GPU ms | 保留版 GPU ms |
| --- | ---: | ---: |
| simple_exp_1024-p64 | 38.321 | 40.108 |
| mixed_exact_payloads-p64 | 17.857 | 18.646 |
| nested_binders-p64 | 19.040 | 18.957 |
| metadata_stress-p64 | 18.862 | 19.184 |
| dp2d-p64 | 20.488 | 20.305 |

小族群沒有一致收益；個別單次回退約 4.7%，不宣稱此改動對所有 workload
更快。前輪六工作量的 40-row matrix 保持原測量 revision `c574b7b`，不改標
為新版本數字。這輪聚焦使用者指定的原始 p1024 加速比。

停止局部優化的判斷：13 個探索設定與兩組小型 paired campaign 後，除平行
metadata 重建外，收益已接近測量波動；較有希望的 16-worker 單次數字也沒有
穩定重現。繼續調小參數不值得再消耗時間。更大的突破仍可能存在，但需要
重新設計完整增量驗證或 donor admission，不能承諾 35／40 倍，也不能為了
加速比降低必要驗證或縮減搜尋工作。先前 32–35 倍的估計較樂觀，本輪如實
停在已證明有效的改善，不宣告原始 Q 門檻或所有 goals 通過。

## 證據與重現

硬體／編譯／allocator 沿用前輪 Snoopy RTX 3090／i9-10900K、GCC 11.4、
CUDA 12.6.85、Release sm86／maxrregcount=64、GPU 0；未使用本機忙碌 GPU，
未使用 subagent。程式與測試的 363 個 native source paths 逐檔核對。

保留 binary SHA-256：
`1ef9bf3ea4a2e0ad056700cea6238597db9dfbeff56358773dc761b3e7b56709`。
回退到選定 source 後重新建置，與該 immutable binary 逐 byte 相同，因此
沒有重新跑已完成的 paired 測量。
final population output SHA-256：
`391a63e06fe579660b62b921a72c7d5100022d85085957e53ab97bee2e0bfa78`。

本機 raw root：`/home/hschi1106/gagp-artifacts/grammar-migration/target35-20260927`；
遠端：`/home/hschi1106/gagp-resume-a-20260927/target35-*`。
`paired.py`／`paired16.py`、population scripts、`audit.py`、每次 patch、build log、
immutable binaries、source archive／manifest 與比較 receipts 均保留。
重跑時改用新的 output directory；不得覆寫既有成功 receipts。

封存 `target35-evidence.tar.gz` 共 299 個證據檔；下載後逐檔 SHA-256 全數核對。
Archive SHA-256：
`3b6b551825aec196c22dc7c3ab9c0ba9a102253f5525d9240325d95a1dbe7885`。
核對紀錄為 raw root 的 `local-audit.json`。
