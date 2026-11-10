# GAGP

**在 GPU 上執行的文法引導遺傳程式設計（grammar-guided genetic programming）。**
[English](README.md)

GAGP 從輸入／輸出範例中搜尋能解決問題的程式。你用一份文法描述候選程式的空間，
包括輸入、常數、運算、敘述、迴圈與有界遞迴樣板。GAGP 依照這份文法產生程式族群，
再以遺傳程式設計（錦標賽選擇、型別化子樹交配與突變）演化它們。

整個搜尋迴圈都在 GPU 上執行：選擇、交配、突變、把新程式編譯成 bytecode，以及在每個測資上評估，
全部在裝置端完成，主機只負責排序 fitness 向量。另有一個純 C++ 的參考引擎在 CPU 上執行完全相同的運算子。
相同輸入與 seed 下，兩個引擎的結果逐位元相同，因此加速不會改變搜尋本身。

## 成果

在一張 RTX 3090 上、使用預設搜尋設定（族群 1024、64 代）：

- **速度**：同樣的搜尋，GPU 引擎比單執行緒 CPU 參考引擎快 5 到 185 倍（幾何平均 30 倍），結果完全相同。
- **基準測試**：在 ASGP 論文的 30 個程式合成任務上
  （[`benchmarks/asgp30`](benchmarks/README.md)），以保留測資判定，GAGP 在 150 次執行
  （每任務 5 個 seed）中解出 56 次。

細節與研究脈絡見 [research-mainline.md](docs/design/research-mainline.md)（英文）。

## 建置

需求：Linux、CMake 3.16 以上、C++17 編譯器；GPU 引擎另需 NVIDIA GPU 與 CUDA toolkit
（已驗證：RTX 3090、CUDA 12.6、GCC 11.4）。

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j --target gagp
```

沒有 GPU 時，在 cmake 加上 `-DGAGP_ENABLE_CUDA=OFF`，並在執行時加上 `--engine cpu`。

## 快速開始

```bash
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

## 指令

| 指令 | 用途 |
| --- | --- |
| `gagp run TASK.json` | 搜尋程式（`--population`、`--generations`、`--seed`、`--engine gpu\|cpu`、`--search`、`--init`、`--out`、`--save-all`） |
| `gagp eval TASK.json PROGRAM.json` | 在測試或訓練測資上為程式評分 |
| `gagp grammar validate\|inspect\|resolve` | 檢查與檢視文法 |
| `gagp grammar generate` | 產生初始族群 |
| `python3 benchmarks/run.py run --quick` | 執行基準測試（約 10 秒的快速檢查） |

搜尋參數集中在 [`configs/search/default.json`](configs/search/default.json)；
`--search` 接受只列出要修改欄位的檔案。所有選項見 [指令參考](docs/reference/cli.md)。

## 文件

文件以英文為主：

| 文件 | 內容 |
| --- | --- |
| [Getting started](docs/guides/getting-started.md) | 建置、範例、自訂任務、疑難排解 |
| [Grammar authoring](docs/guides/grammar-authoring.md) | 撰寫文法：常數、作用域、樣板、遞迴 |
| [Command reference](docs/reference/cli.md) | 所有指令、選項與設定 |
| [Benchmarks](benchmarks/README.md) | `asgp30` 基準測試與已記錄的結果 |
| [Architecture](docs/design/architecture.md) | 元件，以及一次執行如何流經它們 |
| [Specifications](spec/README.md) | 規範性的格式與語意 |
| [Documentation index](docs/README.md) | 全部文件 |

## 程式庫

`cpp/` 是 C++/CUDA 實作與測試，`spec/` 是規格，`docs/` 是指南與設計說明，
`configs/` 放文法與預設搜尋設定，`examples/` 是可直接執行的任務，`benchmarks/` 是基準測試
（[目錄結構](docs/reference/repository-layout.md)）。

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug && cmake --build cpp/build -j
ctest --test-dir cpp/build --output-on-failure
```

測試、preset 與修改流程見 [development](docs/guides/development.md)；版本資訊見
[VERSION.md](VERSION.md)（3.0.1）。
