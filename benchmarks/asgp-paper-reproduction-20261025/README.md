# ASGP 論文實驗復現

本實驗用提供的 C++ ASGP solver 重跑完整搜尋，保留成功、過擬合、初代成功與負結果，供後續 GAGP 比較。這和固定 parents 的單代速度 benchmark 不同。研究討論來自 ASGP/GAGP benchmark thread `5c8c22b3-13b0-45be-8f40-9ef527aa67b5`。

**提供的solver＋本次資料／設定沒有復現短版paper的成功率。** 五題各100 seeds已完成，500次搜尋均正常結束。每次最多500,000 NFE，找到training解可提前停止。

**定位修正：使用者已確認ZIP由論文作者提供（[來源確認](provenance.json)），作者來源不再有疑問。目前仍需查清執行設定與搜尋差異，尚未建立與論文實驗對齊的ASGP baseline；在查清差異前，不應用本次成功率推導GAGP相對論文ASGP的優劣。**

使用者也確認目前只有這份ZIP，沒有額外的產表命令、設定或成功程式。已補讀ZIP內學位論文的100 train／1000 test設定，並依README的count-aligned補測四個finite tasks；結果另列於下方「作者文件設定補測」。下表保留最初legacy診斷，沒有覆寫或混合樣本。

[互動結果總覽](results/overview.html)可切換五題100-run、作者文件175次補測及30題10-run，並分別選擇短版paper或學位論文的正確分母。

| 題目 | 本次成功 /100 | 短版paper /100 | 學位論文 DP /25 | 初代即成功 |
| --- | ---: | ---: | ---: | ---: |
| Sum of Elements | 100 | 95 | — | 68 |
| Median | 0 | 99 | — | 0 |
| Fibonacci | 100 | 23 | 23 | 83 |
| House Robber | 40 | 22 | 22 | 0 |
| Smallest | 0 | 100 | — | 0 |

表中是C++簡化後成功率。House Robber簡化前為34/100，簡化後40/100，6次從test失敗變成功、沒有反向退化；簡化後另有2次training解未通過test。後處理確實影響House Robber。Median/Smallest的100次training都未解出，因此缺少MATLAB後處理無法解釋這兩題的training搜尋失敗。上述差異沒有用調參消除。

- [500次逐seed測量與SHA](results/focus-100/measurements.json)、[CSV](results/focus-100/summary.csv)、[互動報告](results/focus-100/index.html)
- [匯出圖表 PDF](results/focus-100/success-rates.pdf)、[PNG](results/focus-100/success-rates.png)、[SVG](results/focus-100/success-rates.svg)
- [thread討論及GAGP交接](discussion.json)
- [驗證記錄與artifact hashes](validation.json)

五題100-run確認是在單seed環境診斷後固定的探索性subset；沒有用其test結果挑選GAGP設定，也沒有將不同study中重複seeds計為額外獨立樣本。30題各10 seeds的探索另存 `logs/asgp-paper-20261025/native-10/`，不與這張表合併。

30題×10 seeds也已完成，所有300次搜尋正常結束。兩輪合計800次主要執行，包含50個重複(task,seed)；共有750組不同(task,seed)，成功率仍逐study、逐題計算。

- [30題探索的逐seed證據](results/native-10/measurements.json)、[完整CSV](results/native-10/summary.csv)、[互動報告](results/native-10/index.html)、[圖表PDF](results/native-10/success-rates.pdf)
- [60份原始資料及120份GAGP fixtures索引](inputs.json)

另外7次執行僅作單seed環境與1T/4T驗證；1T/4T只核對Sum、Median、Fibonacci的seed0。原始資料及fixtures通過五個獨立reference、共4452案例的輸入／標籤核對，這不是GAGP執行語義驗證。跨study可重疊的57組重跑終態品質欄位全部一致（不比較時間或完整逐代軌跡），沒有增加成功率樣本數。

目標是 `ref/ASGP.pdf` 的 Table III。論文名稱為 *Grammer-Guided Program Synthesis for Algorithm-Structured Programs*；本地 PDF 第5頁為設定、第7頁為成功數、第8頁為程式大小。

**兩份文獻的分母不同**：短版paper Table III caption明寫每題100 runs；學位論文 `/home/hschi1106/r13921069/學位論文_陳繼威.pdf` §5.2.1/Table5.4則是DP每題25 runs。六個DP題的成功數字相同，但對應比例不同，例如Fibonacci是短版23/100、學位論文23/25。本報告主表按短版 /100，另列學位論文 /25，不能混用兩者。兩份PDF的SHA另存 `logs/asgp-paper-20261025/documents.json`。

## 最初legacy診斷的固定設定

[protocol.json](protocol.json) 記錄30題非 N/A 題目及逐題論文目標值。Population=1000、phase depth=7、crossover=0.7、mutation=0.3、tournament=2、最多500,000 NFE。沿用原碼 generational survival、elite=1、one-phase variation、fitness cache，以及找到 training 解即停止的規則。NFE包含初代1000個體，cache命中也計數；simplifier的額外測試不計入搜尋 NFE。

資料使用原碼 `legacy` 預設，data seed=20260626。每題共用固定 train/test，只改 GP seed。一般題目100/1000筆，四個 finite tasks 依原碼覆寫。所有實驗採4個 fitness workers；同時執行品質搜尋，所以時間僅作診斷，不是 ASGP-1T 效能基準。

原碼未提供 git history。64個 source/tests/scripts/README/CMake 檔案均與提供的 `ASGP.zip` 位元組相同；建立獨立快照後保留逐檔 SHA-256，solver source 沒有修改。Release 使用 GCC 11.4。

## 與論文的差異

- 短版paper未列train/test筆數；ZIP內學位論文§4明列100/1000，但沒有可對照產表的原始資料檔與seeds。最初legacy設定在四個finite tasks使用特殊筆數，這部分未充分對齊附帶文件，已依README另作count-aligned補測。
- 原碼 `--unified` 對部分題目加入 sliding-window，超出論文 DC&DP。這輪使用各題原生 DC 或 DP；未聲稱復現 unified 欄。
- 原碼 `--simplify` 使用 training-behavior-preserving C++ rewrites，缺少論文 MATLAB Symbolic Math Toolbox 階段。程式大小不能宣稱復現 Table IV。
- Median、Smallest、MIN、MPI 等整數題使用 exact loss；原碼另有 list edit distance 與 tuple summaries。論文文字的整數 MAE 描述不足以重建全部 task-specific fitness。
- 原碼依task/carrier裁剪list productions，並有逐題常數範圍；PDF的總文法表不足以逐題核對完整搜尋分布。本輪沿用zip中的設定，沒有為貼近成功數改文法。
- Fibonacci為21/31筆、Tribonacci為19/26筆、Fibonacci Array為81/169筆、Sum of Squares為30/100筆。這四題的 test 均包含全部 training 輸入。其餘題目也可能有抽樣重複；實際重疊與 unique counts 都保存。
- `count-aligned` 只是有限 support 的有放回重抽，沒有建立不重疊的新測試集；後續依作者文件另跑四題補測，未將兩個profile的結果混合或用來挑選GAGP設定。
- 整數為 wrapping int64；division/modulo by zero及 `INT64_MIN/-1` 回傳0。執行限制沿用原碼 max_steps=200000、max_recursion=4096、max_arena=65536，沒有直接與 GAGP fuel/frames 等同。

因此這是**提供程式的可重跑復現嘗試**。結果不同只能判定這組程式／資料／設定沒有復現論文表格，不能單憑這輪斷言是哪個差異造成。

## 落差診斷：exact 與 absolute loss

[有界對照結果](results/loss-contract-4seed/diagnostic.json)固定原始資料、演化設定和未修改的ASGP library，只切換task的metric；Median/Smallest各4個seeds、每次最多500k NFE，關閉兩組的simplification。

- 原始exact組與absolute組在兩題都是0/4 training solved、0/4 test success。這個小對照沒有證明loss差異是100-run落差的原因，不能聲稱換成MAE就能恢復paper結果。
- 切換原碼metric也會將invalid-case penalty從1改成1000；absolute路徑是每case截斷於1000的absolute error，再對固定100筆資料求和，尚非已確認與paper完全相同的fitness。
- 8個exact控制組的終態品質／NFE／節點數與原始100-run紀錄相符。原碼的手寫Median/Smallest正解測試均可在depth≤7通過全部預設train/test；包含它們的66個native tests全通過。因此已能排除「完全無法表示／執行這兩題正解」，但尚未找出搜尋失敗的確切原因。

診斷driver為 [diagnose_loss.cpp](diagnose_loss.cpp)，原始JSONL、逐代進度和程式存於 `logs/asgp-paper-20261025/diagnostics/loss-contract-20261026/`。這是另列的診斷variant，不覆寫原始800次主要執行，也不加入原始成功率分母。作者來源已確認，產生論文表格的完整執行設定仍待核對。

## 作者命令與 phase 策略核對

已直接執行作者CLI，不使用本repo新增的runner：`asgp run median/smallest --runs 4 --seed 0 --threads 4 --data-profile count-aligned --simplify`。兩題各0/4，逐run品質欄位與原始紀錄相符。這兩題及CumulativeSum的legacy/count-aligned輸入與標籤陣列完全相同；兩題seed0在`--no-cache --threads 1`下也與原始品質欄位一致。

[完整2×2對照](results/search-contract-4seed/diagnostic.json)及[driver](diagnose_search_contract.cpp)固定兩題、各4seeds，切換原碼exact/absolute loss與OnePhase/EveryPhase，共32次執行。開啟相同C++ simplifier；34組終態控制比較通過。

- OnePhase的exact和absolute，以及EveryPhase的absolute，兩題每組皆0/4成功。
- EveryPhase的exact：Median 2/4解出training、raw test 0/4、簡化後1/4；Smallest 1/4解出training、raw test 0/4、簡化後1/4。
- 預設OnePhase下，DC的Solve crossover/mutation嘗試率分別為0.7/3與0.3/3；69%的子代相對所選parent不會嘗試改動Solve。EveryPhase才對每phase各抽0.7/0.3，與CLI的per-phase字樣一致。但paper也描述一次更新一個phase，因此沒有證據可把EveryPhase直接認定為應補開的paper設定。

這些有界對照沒有恢復paper成功率，也尚未唯一定位Median/Smallest失敗原因；它們是另列診斷，不替換原始結果或冒充作者實驗設定。

## CumulativeSum：預設文法的具體反例

[可重跑probe](diagnose_cumsum_grammar.cpp)連結未修改的作者library，核對實際pruned grammar。[結構反例與SHA](results/cumsum-grammar-witness/witness.json)使用自附heldout的第一筆：

```text
input    = [1,16,17,7,12,-9,-11,14,1]     # 8種不同數值
expected = [1,17,34,41,53,44,33,47,48]    # 9種不同數值
```

預設DC/base1下，Solve的IntList productions僅有Input、Empty、If、Singleton，每個singleton leaf最多輸出`[]`或`[f(x)]`。本題沒有scalar/位置/其他遞迴結果讀取，InputSize固定為1，Index只能讀當前singleton窗口，因此相同x的可見context相同。Combine實際只剩concat、r1、r2，不能創造新的scalar值。

故所有合法程式必有`distinct(output) ≤ distinct(input) = 8`，無法產生這筆目標的9種值；在固定資料下增加depth、GP搜尋seeds或NFE都不改變這個上界。原生C++ simplifier只取既有子樹、消包裝或折疊scalar常數，也保留此限制。指定thread已只讀核對此證明。

因此**在本次固定資料、預設DC/base1、作者文法和原生C++簡化流程下，CumulativeSum的test success必為0**；這組條件無法產生paper的3/100。此證據不涵蓋未取得的MATLAB流程或其他實驗文法，不能唯一定位表格與程式之間的差異，也不能外推為Median/Smallest的原因。

需要對照的作者資料已具體化為：CumulativeSum當時的文法／資料／成功程式，以及Median/Smallest產生表格時的完整命令與設定。在取得對照前，保留作者原碼及全部既有結果，不以調參後數字替代paper復現。

## 作者文件設定補測

依ZIP內README推薦的count-aligned及學位論文§4的100 training／1000 test，僅補測受profile影響的四個finite tasks。DP使用學位論文§5.2.1的25 runs，SumOfSquares使用DC的100 runs；原碼、source library、seed起點0、threads4、500k NFE、預設variation及C++ simplifier均保持相同。此為175次另列的補測，不加入原legacy分母。

- Fibonacci：25/25（學位論文23/25）。
- Tribonacci：22/25（學位論文19/25）。
- Fibonacci Array：25/25（學位論文17/25）。
- Sum of Squares：96/100（學位論文23/100；raw 95/100，C++簡化後96/100）。

[三個DP題目的逐seed證據](results/author-readme-finite-dp-25/measurements.json)、[CSV](results/author-readme-finite-dp-25/summary.csv)；[SumOfSquares證據](results/author-readme-sum-squares-100/measurements.json)、[CSV](results/author-readme-sum-squares-100/summary.csv)。這兩輪皆逐筆核對count-aligned資料SHA及100/1000實際筆數，175次正常結束；報告明確選用附帶學位論文的DP /25作對照。

補測修正了最初四題的資料筆數選擇，仍不能證明完整paper復現成立。有限support重抽仍有train/test輸入重疊；CumulativeSum、Median、Smallest的資料在兩profile下相同，其問題不因本次補測消失。

## 可交作者核對的最小重現包

[問題說明及直接執行命令](AUTHOR_REPRODUCER.md)包含CumulativeSum的結構反例、原生CLI失敗紀錄和需要核對的具體條件；[重現包索引](author-reproducer.json)記錄本機tar.gz位置、內容與SHA。原始ZIP與程式未改；解包後已獨立重新建置、通過作者測試，並重現反例及兩題各4次原生CLI結果。這是本機證據包，尚未聯絡作者。

## 重跑

從 repository root 執行。全新實驗使用新 `--root` 與 `--out`；已完成結果可用相同命令續跑，source/binary/settings 不同時拒絕混合。既有失敗紀錄不自動覆寫。

```bash
python3 benchmarks/asgp-paper-reproduction-20261025/prepare.py
python3 benchmarks/asgp-paper-reproduction-20261025/convert_cases.py

# 30題各10 seeds 的完整budget探索。
python3 benchmarks/asgp-paper-reproduction-20261025/run.py \
  --out logs/asgp-paper-20261025/native-10 --runs 10

# 固定五題各100 seeds；不要合併上方重複seed為額外樣本。
python3 benchmarks/asgp-paper-reproduction-20261025/run.py \
  --out logs/asgp-paper-20261025/focus-100 --runs 100 \
  --tasks sum_of_elements median fibonacci house_robber smallest

python3 benchmarks/asgp-paper-reproduction-20261025/report.py \
  --study logs/asgp-paper-20261025/focus-100 \
  --out benchmarks/asgp-paper-reproduction-20261025/results/focus-100 \
  --require-complete
python3 benchmarks/asgp-paper-reproduction-20261025/audit_inputs.py
python3 benchmarks/asgp-paper-reproduction-20261025/plot.py \
  benchmarks/asgp-paper-reproduction-20261025/results/focus-100/measurements.json
python3 -m unittest discover \
  -s benchmarks/asgp-paper-reproduction-20261025 -p 'test_*.py' -v

# 依作者README與附帶學位論文補測四個finite tasks，使用新的輸出目錄。
python3 logs/asgp-paper-20261025/source/scripts/benchmark.py \
  --exe logs/asgp-paper-20261025/build/src/asgp \
  --out logs/asgp-paper-20261025/author-readme-finite-dp-25 \
  --tasks fibonacci tribonacci fibonacci_array --runs 25 \
  --threads 4 --data-profile count-aligned --simplify --timeout 1800
python3 logs/asgp-paper-20261025/source/scripts/benchmark.py \
  --exe logs/asgp-paper-20261025/build/src/asgp \
  --out logs/asgp-paper-20261025/author-readme-sum-squares-100 \
  --tasks sum_of_squares --runs 100 --threads 4 \
  --data-profile count-aligned --simplify --timeout 1800
python3 benchmarks/asgp-paper-reproduction-20261025/report.py \
  --study logs/asgp-paper-20261025/author-readme-finite-dp-25 \
  --data-profile count-aligned --reference bundled-thesis \
  --out benchmarks/asgp-paper-reproduction-20261025/results/author-readme-finite-dp-25 \
  --require-complete
python3 benchmarks/asgp-paper-reproduction-20261025/report.py \
  --study logs/asgp-paper-20261025/author-readme-sum-squares-100 \
  --data-profile count-aligned --reference bundled-thesis \
  --out benchmarks/asgp-paper-reproduction-20261025/results/author-readme-sum-squares-100 \
  --require-complete
```

`prepare.py` 在沒有搜尋 manifest 時才建置；已測量的 binary 不重建。先凍結來源，再執行原作者 CTest/Python tests，連結唯讀 exporter，匯出30題×2 profiles的原始資料。`run.py` 呼叫未修改的原作者 `scripts/benchmark.py`，每個 task/seed 保留stdout、stderr、command、NFE、success、training status、simplification與節點數。

`audit_inputs.py` 對五題以獨立Python reference核對原始資料及GAGP fixtures，並比較可用的重複seeds；原始單seed診斷及1T/4T核對是驗證用途，不加入成功率分母。`plot.py` 為選用的匯出圖表步驟，需要matplotlib/numpy；其他Python流程僅用標準函式庫。

`report.py` 逐筆重讀CLI輸出，核對 source/binary/data SHA、實際筆數、預算及seed歸屬；未完成、基礎設施失敗與搜尋失敗分開呈現。Wilson95% CI依已完成樣本計算，缺失樣本不當成失敗；它描述固定資料下GP seed的抽樣變異，不涵蓋資料生成或兩版本差異。個別搜尋逾時不算正常500,000 NFE搜尋失敗，也不自動重試。

## 後續與 GAGP 比較

原始證據位於 `logs/asgp-paper-20261025/`：`source-identity.json`、`environment.json`、`datasets.json`、`datasets/`、兩個 study 目錄與controller logs。原始碼和大資料留在忽略追蹤的 `logs/`，compact evidence保存在本目錄。

`convert_cases.py` 另產生120份 `fitness-cases` 及 `gagp-fixtures.json`，依 [fixture contract](../../spec/fitness_cases.md) 保留實際資料與標籤：

- 原始 sequence input 映為 `xs`；IntList明確使用 `int_list`，CharList映為 `string`，空list不推測型別。
- 原始 ordinary scalar names 原樣保留。DP另輸入 `state_a/state_b/extent_a/extent_b`；無sequence的題目不杜撰list。
- expected使用原始 projected output。Boolean predicate的輔助tuple summaries仍保留在原始export，尚未移植成GAGP訓練fitness。

這些fixtures提供共同資料入口，**不代表搜尋空間、fitness、執行預算或演算法已對齊**。先查清ASGP實驗版本與搜尋設定，再為各題固定GAGP grammar/設定、核對reference outputs及搜尋限制，完成後才在相同train/test、相同NFE及獨立GP seeds比較成功率。對泛化能力的正式比較另預先凍結不重疊test，兩套solver都重跑；不得將新資料結果冒充論文復現。

ASGP的depth=7是每個phase expression的深度，不是含固定骨架的GAGP physical AST深度。NFE也要計入初代及實際評分次數，不能直接以相同generations數代替。Median/Smallest的來源base-size為3/4，本批所有輸入都只執行Solve；另外兩個DC phases的變異可能不影響輸出，這仍按原碼保留。

速度比較需另外量測 ASGP-1T 和指定 GAGP CPU/GPU 模式，記錄相同的端到端時間邊界、early stop/cache policy、simplification及失敗處理。品質比較應同時報raw與postprocessed成功率，另列simplifier額外評分與時間；原碼500k搜尋NFE不包含這些額外評分。這輪沒有產生GAGP speedup結論，也沒有使用本次test結果挑選GAGP設定。

指定thread交接的GAGP入口為 `/home/hschi1106/.t3/worktrees/gagp/gagp-support-20261015`、branch `opt/list-output-5x-20261015`，正式CPP commit `81b8a9c`。其P1024/G64/`list-combine-v1`實驗與ASGP控制組不等於本次原版solver protocol；可沿用入口與比較經驗，不能搬用數字當作本次baseline。
