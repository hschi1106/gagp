# 單機24小時：ASGP／GAGP多代速度與品質

速度矩陣G8：合格150/150格；品質P1024/G64：完成獨立CPU判題300/300 runs。

控制器總時間9.23小時。所有P使用同一binary與16GiB workspace；整卡NVML取樣最高21.13GiB，包含桌面、可能漏短尖峰。

## Population加速比

| P | 合格題數 | 快於ASGP | 倍率中位數 | 幾何平均 |
| --- | ---: | ---: | ---: | ---: |
| 1024 | 30/30 | 30/30 | 10.64× | 7.95× |
| 2048 | 30/30 | 30/30 | 13.95× | 10.22× |
| 4096 | 30/30 | 30/30 | 16.05× | 11.49× |
| 8192 | 30/30 | 28/30 | 14.94× | 10.30× |
| 16384 | 30/30 | 29/30 | 15.22× | 11.07× |

## 全題探索性品質

兩方法使用相同訓練／測試切分及配對初代／variation seeds0–4。成功需要final training-selected candidate在train及input-disjoint test均exact全對。沒有以test挑選candidate，沒有simplification。括號為gen0已成功的runs數，避免把initializer成功誤稱演化收益。

| 題目 | Train/Test unique | ASGP成功/5（初始） | GPU成功/5（初始） | ASGP test accuracy中位數 | GPU test accuracy中位數 |
| --- | ---: | ---: | ---: | ---: | ---: |
| filter_odd | 200/1000 | 0/5 (0) | 0/5 (0) | 2.1% | 1.8% |
| sum_of_elements | 200/1000 | 5/5 (3) | 5/5 (3) | 100.0% | 100.0% |
| is_sorted | 200/1000 | 0/5 (0) | 0/5 (0) | 84.2% | 81.9% |
| index_of_element | 200/1000 | 1/5 (0) | 0/5 (0) | 33.9% | 17.0% |
| all_equal | 200/1000 | 1/5 (0) | 0/5 (0) | 93.6% | 96.3% |
| cumulative_sum | 200/1000 | 5/5 (0) | 1/5 (0) | 100.0% | 1.9% |
| fuel_cost | 200/1000 | 2/5 (0) | 0/5 (0) | 1.9% | 0.1% |
| luhn | 200/1000 | 0/5 (0) | 0/5 (0) | 7.1% | 7.4% |
| min | 200/1000 | 5/5 (0) | 5/5 (0) | 100.0% | 100.0% |
| mpi | 200/1000 | 0/5 (0) | 0/5 (0) | 55.6% | 43.1% |
| count_odds | 200/1000 | 5/5 (0) | 0/5 (0) | 100.0% | 20.2% |
| last_index_of_zero | 200/1000 | 4/5 (0) | 0/5 (0) | 100.0% | 27.0% |
| small_or_large | 200/1000 | 0/5 (0) | 0/5 (0) | 51.6% | 51.6% |
| for_loop_index | 200/1000 | 0/5 (0) | 0/5 (0) | 4.1% | 4.3% |
| double_letters | 200/1000 | 1/5 (0) | 0/5 (0) | 66.8% | 18.3% |
| replace_space_with_newline | 200/1000 | 0/5 (0) | 0/5 (0) | 64.7% | 64.7% |
| even_squares | 79/20 | 0/5 (0) | 0/5 (0) | 10.0% | 10.0% |
| sum_of_squares | 30/70 | 5/5 (0) | 5/5 (0) | 100.0% | 100.0% |
| negative_to_zero | 200/1000 | 5/5 (0) | 4/5 (0) | 100.0% | 100.0% |
| scrabble_score | 200/1000 | 0/5 (0) | 0/5 (0) | 3.4% | 3.3% |
| grade | 200/1000 | 0/5 (0) | 0/5 (0) | 39.0% | 19.1% |
| median | 200/1000 | 0/5 (0) | 0/5 (0) | 41.7% | 50.3% |
| smallest | 200/1000 | 0/5 (0) | 0/5 (0) | 23.4% | 40.6% |
| syllables | 200/1000 | 0/5 (0) | 0/5 (0) | 21.8% | 21.2% |
| fibonacci | 21/10 | 5/5 (5) | 5/5 (5) | 100.0% | 100.0% |
| tribonacci | 19/7 | 3/5 (0) | 4/5 (0) | 100.0% | 100.0% |
| house_robber | 200/1000 | 1/5 (0) | 0/5 (0) | 35.4% | 23.8% |
| subregion_sum | 200/1000 | 5/5 (1) | 5/5 (1) | 100.0% | 100.0% |
| fibonacci_array | 81/88 | 5/5 (1) | 5/5 (1) | 100.0% | 100.0% |
| stock_with_cooldown | 200/1000 | 0/5 (0) | 0/5 (0) | 7.4% | 5.8% |

## 設定與解讀

- 全部GAGP採whole AST2048、physical depth24、每phase1024、GPU繁殖、CPU1T。ASGP logical phase depth7、operator/RNG、ranking及resource units等差異保留，fully_aligned=false。
- ASGP比較組是benchmark search_asgp_1t：原生Score／operators順序執行、elite1、每輪重評完整population；不是含cross-generation program-fitness cache的stock Run入口。倍率只相對這個公開1T控制組。
- 速度：完整1024cases、seed0、每格單次cold process；evolve_wall_ms包含呼叫內初始化／admission、演化、最後評分與清理。CLI解析／匯入準備、export與獨立CPU audit另列process wall；不能稱完整CLI加速或純硬體加速。
- 品質採本地ASGP BuildTask domain，data seed20261017；不是official PSB fixture，也不是原論文100run／500000NFE重現。每題unique train最多200、test最多1000，input overlap=0；有限域見dataset-splits.json。其heldout_outside_training_support欄位實際計算test輸入未出現在4096筆原始training pool的數量，不能僅依此判定分布外推。
- 品質只比較P1024，不能從此判斷不同population的收斂優劣；每題五個seeds只屬探索性，沒有品質等價／非劣性宣稱。Test曲線為事後判題的training-selected champion，不向搜尋回饋。
- 每次GPU搜尋先以CPU核對first16 final programs；品質的每代champion及final candidate另用各自原生CPU表示對指定train/test做exact判題，post-search去重只節省重複audit。未做新的全population CPU重算，不能宣稱全部個體parity。
- 每題native錯誤／timeout仍照原契約計入fitness；程序失敗、資源限制、未完成與未解題分開記錄。失敗不得提供合格倍率，成功率分母保留預訂五次。
- 舊G4與1024-node資料只用作時間預測／歷史背景；正式分子分母皆來自本輪選定設定。時間pilot只用耗時／資源選規模；匹配正式設定的pilot作該格唯一sample，較大pilot不混入縮小後主表。

[重現與範圍](../README.md) · [互動表](dashboard.html) · [速度CSV](speed.csv) · [品質CSV](quality.csv) · [速度PNG](speedup.png) · [品質PNG](quality.png) · [來源](provenance.json)
