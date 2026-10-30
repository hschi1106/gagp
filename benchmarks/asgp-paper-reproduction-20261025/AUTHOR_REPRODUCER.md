# 作者提供版本的最小重現問題

使用者確認目前只有作者提供的 `ASGP.zip`，沒有額外的產表命令、設定或成功程式。以下整理可直接核對的觀察，不據此判定論文結果錯誤，也不以修改後較高的成功率代替復現。

原ZIP SHA-256：`99c5d41809d6fbe25f724213267d673cad6726e23e76644069cb2f36258812ed`。本機Linux/GCC11.4、Release、C++20；64個source/tests/scripts/README/CMake檔案與ZIP相同。原始碼沒有修改。

## 直接重跑

在全新空目錄解開本重現包後執行：

```bash
unzip ASGP.zip -d source
cmake -S source -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
build/src/asgp run median --runs 4 --seed 0 --threads 4 --data-profile count-aligned --simplify
build/src/asgp run smallest --runs 4 --seed 0 --threads 4 --data-profile count-aligned --simplify
c++ -std=c++20 -O2 -I source/src diagnose_cumsum_grammar.cpp build/src/libasgp_core.a -pthread -o cumsum_probe
./cumsum_probe
```

本機觀察到Median/Smallest皆0/4成功，與逐seed執行的紀錄一致。原生66個tests通過，包含深度≤7的手寫Median/Smallest正解在全部預設train/test通過，因此未發現完全無法表示／執行這兩題正解的問題。兩題在legacy/count-aligned的輸入及標籤陣列相同。

## CumulativeSum的結構反例

[Probe](diagnose_cumsum_grammar.cpp)檢查原生factory建立的實際pruned grammar；[反例與hash](results/cumsum-grammar-witness/witness.json)指向第一筆heldout：

```text
input    = [1,16,17,7,12,-9,-11,14,1]
expected = [1,17,34,41,53,44,33,47,48]
```

在預設DC/base1、無scalar／位置讀取的條件下，每葉Solve最多產生`[]`或`[f(x)]`。Combine的實際productions僅concat、r1、r2。因此輸出值集合是各葉值集合的子集，且相同x的可見context相同，必有：

```text
distinct(output) ≤ distinct(input) = 8 < 9 = distinct(expected)
```

固定資料下，這筆case對任何合法深度、任何GP搜尋seed及NFE都無法零錯誤。原生C++ simplifier不會引入新的位置讀取、Solve多元素建構或Combine數值轉換，也保留此上界。故這組資料／文法／C++簡化條件無法產生學位論文Table5.3及短paper所列的CumulativeSum 3/100。此結論不涵蓋未取得的MATLAB實作，也不解釋Median/Smallest的搜尋失敗。

## 已檢查的差異

- `--every-phase`讓兩題4seeds各有1次簡化後成功，raw test仍全失敗；不是已重現99/100或100/100。論文描述one phase at a time，不能直接認定此旗標才是產表設定。
- exact切換至原碼的capped absolute-loss路徑未恢復成功率；該切換也會改變invalid-case penalty，所以不是純粹MAE因果證明。
- 兩題seed0在`--no-cache --threads 1`下的品質欄位與原始結果一致。
- 作者README建議count-aligned；有限域題目已另以100 train/1000 test補測，沒有與最初legacy數據混合。

## 需與產表條件核對的項目

1. Median/Smallest的完整執行命令、grammar/constant設定、資料生成與seed，以及搜尋過程中的同分排序／更新策略。
2. CumulativeSum成功程式的raw AST、使用的grammar、資料及後處理流程；是否還有本ZIP未包含的list操作或跨子問題資訊。
3. 若有實際產表腳本或原始結果，請提供其對應程式版本，以便逐項對照。

