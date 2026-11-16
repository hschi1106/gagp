# GAGP

**在 GPU 上執行的文法引導遺傳程式設計（grammar-guided genetic programming）。**
[English](README.md)

GAGP 從輸入／輸出範例中搜尋能解決問題的程式。你用一份文法描述候選程式的空間，
GAGP 依照這份文法產生程式族群，再以錦標賽選擇與型別化子樹交配、突變演化它們。
整個搜尋迴圈都在 GPU 上執行。另有一個純 C++ 的參考引擎在 CPU 上執行完全相同的運算子，
並產生相同的族群、fitness 值與冠軍程式，因此每一項加速都是在同一個搜尋上量測的。

## 快速開始

需求：Linux、CMake 3.16 以上、C++17 編譯器；GPU 引擎另需 NVIDIA GPU 與 CUDA toolkit
（已驗證：RTX 3090、CUDA 12.6、GCC 11.4）。

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j --target gagp
cpp/build/gagp run examples/polynomial/task.json --out runs/polynomial
```

```text
gagp run polynomial: engine=gpu population=1024 generations=64 seed=0 cases=21
best fitness 0, wrong 0 of 21 train cases, 13 nodes; 0.4 s
program: (add 1 (mul x (sub (mul x x) 2)))
test: 40 of 40 correct (solved)
```

GAGP 從 21 個範例找到計算 `x³ - 2x + 1` 的程式，並在 40 個保留測資上驗證通過。
`runs/polynomial/result.json` 記錄了程式、分數、每代歷程與完整的搜尋設定。

若 CMake 找不到 `nvcc`，設定 `CUDACXX=/usr/local/cuda/bin/nvcc`，並加上
`-DCMAKE_CUDA_ARCHITECTURES=86`（你的 GPU 的 compute capability）只為該 GPU 建置。
沒有 GPU 時，加上 `-DGAGP_ENABLE_CUDA=OFF`，並在執行時加上 `--engine cpu`。

一個任務是一份小 JSON 檔，指定文法、訓練測資、可選的測試測資與誤差度量：

```json
{
  "format": "gagp-task-v1",
  "grammar": "grammar.json",
  "train": "train.cases.json",
  "test": "test.cases.json",
  "fitness": {"metric": "absolute"}
}
```

撰寫自己的任務與文法，請看 [getting started](docs/guides/getting-started.md)。

## GAGP 證明了什麼

以下每一項主張都列出它的意思、本儲存庫中現有的證據，以及重現它的指令。
量測皆在一張 RTX 3090 上、使用預設搜尋設定，族群大小、代數與 seed 見所連結的紀錄。
GAGP 不主張它的搜尋優於 ASGP、不主張在 PSB1 上達到最先進水準，也不主張重現了 ASGP 論文的數字。
基準測試報告中列出的已發表成功次數只是脈絡，不是條件相同的比較基準。

### 1. 整個迴圈都在 GPU 上執行

選擇、交配、突變、把新程式編譯成 bytecode，以及在每個測資上評估，全部在裝置端完成，
包括處理字串與列表的程式。主機只負責排序 fitness 向量並記錄歷程。
設計見 [gpu-reproduction.md](docs/design/gpu-reproduction.md)，字串與列表的儲存方式見
[payload.md](docs/design/payload.md)。

證據：常駐引擎與它的測試（`ctest -L gpu`），以及 `result.json` 中每一代的 `eval_ms` 與 `repro_ms`。
`asgp30` 任務處理列表，`psb1_g3p` 任務處理字串與列表。字串任務需要主張 2 的測資檔：

```bash
cpp/build/gagp run benchmarks/asgp30/filter_odd/task.json --generations 16
cpp/build/gagp run benchmarks/psb1_g3p/small_or_large/task.json --search benchmarks/psb1_g3p/small_or_large/search.json --generations 16
```

### 2. 使用者自訂的文法可以執行

搜尋空間是一份文法檔，不是固定的指令集。兩種差異很大的文法家族在同一個引擎上執行：

- **演算法結構樣板。** ASGP 的 30 個任務（[`benchmarks/asgp30`](benchmarks/README.md)）：
  分治與動態規劃架構，搭配有界遞迴，由運算子集合填入。
- **已發表 G3P 研究風格的敘述式文法。** 六個 PSB1 問題
  （[`benchmarks/psb1_g3p`](benchmarks/psb1_g3p/README.md)）：賦值、`if`、`for` 與 `while` 迴圈，
  以及列表與字串內建函式，移植自 Forstenlechner 的 G3P 文法。

語言具備這些文法所需的功能：

- **有界的 `while`。** `while_stmt(Int, Bool, Block)` 執行主體直到條件為假或達到上限，然後正常離開
  （[程式文法](spec/grammar.md#program-grammar)）。
- **列表與字串內建函式。** `insert`、`remove_at`、`set_at`、`index_mod`、列表的 `contains`、`range`、
  `strip`、`lstrip`、`rstrip`、`startswith`、`endswith` 與 `capitalize` 在兩個引擎上都能執行
  （[列表內建函式](spec/builtins_runtime.md)、[字串內建函式](spec/builtins_base.md)）。
- **配置規則。** 任務設定 `fitness.list_allocation_limit` 時，程式建立的每個列表與字串以及每個輸入都計入上限，
  超出上限是計分的測資錯誤。實體記憶體耗盡絕不是分數，而是讓執行停止
  （[任務檔](spec/task.md#task-file)、
  [配置邊界](spec/fitness.md#cpu-payload-allocation-boundary)）。

兩個測試集都由 `benchmarks/run.py` 執行。PSB1 的測資檔來自 PonyGE2，第二個指令負責轉換：

```bash
python3 benchmarks/run.py run --quick
git clone https://github.com/PonyGE/PonyGE2 /path/to/PonyGE2
python3 benchmarks/psb1_g3p/build.py cases --data /path/to/PonyGE2
python3 benchmarks/run.py run --suite psb1_g3p --quick
```

文法格式見 [grammar authoring 指南](docs/guides/grammar-authoring.md)。

### 3. 速度快

- **對 GAGP 自己的 CPU 參考引擎。** 在 30 個 ASGP 任務、族群 1024 上，同樣的搜尋
  GPU 比單執行緒 CPU 快 14 到 298 倍（幾何平均 108 倍）。在六個 `psb1_g3p` 題目上
  快 2.1 到 8.7 倍。結果完全相同。
- **對單執行緒的 ASGP。** 在 30 個任務、64 代、凍結的 ASGP 初始族群上，
  單執行緒 ASGP 花的時間在族群 1024 時是 GAGP 的 46 倍、4096 時是 59 倍
  （對任務取幾何平均，再對 5 個 seed 取平均），16384 時是 73 倍（1 個 seed）。
  ASGP 的時間已修正機器競爭。
  ASGP 與這些族群不在本儲存庫中，所以這項比較有記錄但無法在此重現
  （[細節](docs/design/research-mainline.md#results)）。

在相同任務上執行兩個引擎並比較。報告會列出每個任務的加速比，並檢查兩個引擎是否找到相同的最佳程式與相同的 fitness 歷程。
CPU 引擎很慢，所以只選幾個任務：

```bash
python3 benchmarks/run.py run --quick
python3 benchmarks/run.py run --quick --engine cpu --tasks median,smallest
python3 benchmarks/run.py report
```

在完整族群上比較兩個引擎的原生測試：

```bash
ctest --test-dir cpp/build -R gagp_test_phase_population_cpu_reference --output-on-failure
```

加速比請用 Release 建置、且不掛 profiler 的結果。

### 4. 品質不會變差

速度不是用搜尋品質換來的。決定要探索哪些程式的是搜尋設定，引擎從不影響這件事，
所以一項最佳化只有在匯出的冠軍與歷程和前一版完全相同時才會被接受。
在 30 個 ASGP 任務上（族群 1024、64 代），GPU 引擎在 150 次執行（每任務 5 個 seed，以保留測資判定）中解出 56 次，
`report.md` 會把已發表的 ASGP 成功次數列在 GAGP 的結果旁邊
（[已記錄的結果](benchmarks/README.md#recorded-results)）。

```bash
python3 benchmarks/run.py run
python3 benchmarks/run.py report
```

完整執行在 GPU 上約 9 分鐘。`--engine cpu` 執行相同的搜尋並得到相同的次數，只是很慢。

在六個 `psb1_g3p` 題目上，用已發表的 G3P 設定各跑 100 次，GAGP 有三題和已發表的次數持平，
三題較低。各題次數與 `median` 落差的原因見
[`psb1_g3p` 的結果](benchmarks/psb1_g3p/README.md#recorded-results)。

## 限制

- **沒有無界迴圈。** 語言有 `for_range`、有界的 `while_stmt`、對列表或字串的走訪，以及有 frame、memo 與 fuel 上限的有界遞迴區域。
  fuel 限制每個程式，用完 fuel 的程式在該測資上失敗。
- **接納容量。** 常駐引擎把族群存成固定大小的基因，所以文法必須符合它的上限：
  每個程式最多 2048 個節點、深度 64，每個變動階段最多 1024 個節點，族群最多 16384，還有其他上限
  （[常駐容量](spec/search_config.md#resident-capacities)）。
- **語言限制。** 常駐引擎會拒絕文法語言的兩種構件：`sequence` 常數域，以及在兩種不同詞法綁定環境下被到達的非終結符。
  它們只在[語言限制](spec/search_config.md#language-restrictions)中列出一次。
- **拒絕就是錯誤。** 無法被接納的文法會讓 `gagp run` 在第一代之前以結束碼 2 停止，
  訊息指出構件或容量與超出的值。沒有後備引擎。
- **語言本身的其他限制。** 選擇只有錦標賽，列表不能巢狀，值型別共八種
  （[型別](docs/guides/grammar-authoring.md#anatomy)）。

## 指令

| 指令 | 用途 |
| --- | --- |
| `gagp run TASK.json` | 搜尋程式（`--population`、`--generations`、`--seed`、`--engine gpu\|cpu`、`--search`、`--init`、`--out`、`--save-all`） |
| `gagp eval TASK.json PROGRAM.json` | 在測試或訓練測資上為程式評分 |
| `gagp grammar validate\|inspect\|resolve` | 檢查與檢視文法 |
| `gagp grammar generate` | 產生初始族群 |
| `python3 benchmarks/run.py run [--suite asgp30\|psb1_g3p]` | 執行基準測試集 |

搜尋參數集中在 [`configs/search/default.json`](configs/search/default.json)，
`--search` 接受只列出要修改欄位的檔案。所有選項見 [指令參考](docs/reference/cli.md)。

## 文件

文件以英文為主：

| 文件 | 內容 |
| --- | --- |
| [Getting started](docs/guides/getting-started.md) | 建置、範例、自訂任務、疑難排解 |
| [Grammar authoring](docs/guides/grammar-authoring.md) | 撰寫文法：常數、作用域、樣板、遞迴 |
| [Command reference](docs/reference/cli.md) | 所有指令、選項與設定 |
| [Benchmarks](benchmarks/README.md) | `asgp30` 與 `psb1_g3p` 測試集與已記錄的結果 |
| [Research mainline](docs/design/research-mainline.md) | 主張、「完全相同」的意義、量測結果、已淘汰的設計 |
| [Architecture](docs/design/architecture.md) | 元件，以及一次執行如何流經它們 |
| [Specifications](spec/README.md) | 規範性的格式與語意 |
| [Documentation index](docs/README.md) | 全部文件 |

## 程式庫

`cpp/` 是 C++/CUDA 實作與測試，`spec/` 是規格，`docs/` 是指南與設計說明，
`configs/` 放文法與預設搜尋設定，`examples/` 是可直接執行的任務，`benchmarks/` 是基準測試集
（[目錄結構](docs/reference/repository-layout.md)）。

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug && cmake --build cpp/build -j
ctest --test-dir cpp/build --output-on-failure
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
```

測試、preset 與修改流程見 [development](docs/guides/development.md)；版本資訊見
[VERSION.md](VERSION.md)（3.0.1）。
