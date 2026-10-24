# GAGP G64：速度與品質

P=1024，G=64；30題，速度重複3次；GAGP獨立品質10個新seeds。

Training每題19–200 cases；test每題最多1000，有限輸入域較少。

- 相對原最快GAGP：**1.079×**。有效速度覆蓋30/30題；目標1.3×尚未證明達到。
- 相對本輪ASGP1T：**4.673×**。分母為本輪相同P/G/cases的未profile搜尋時間。
- 獨立品質完整判題300/300對；最終train+test全對：90→90。
- 初代已全對：21→21；後續新解：69→69。

ASGP品質只判速度cohort的seed35：完成30/30題；可確認最終全對10/30。這與GAGP的多seed品質表分開呈現。

P16384/G64：GAGP配對完整核對30/30題；ASGP三方法有效對照30/30題；每題一輪。相對舊GAGP幾何平均1.080×。ASGP1T／新GAGP幾何平均8.076×。

P16384整卡VRAM：有有效取樣60/60次GPU搜尋，觀測最大21.47GiB；缺漏保留未知。搜尋成功60/60次，通過指定CPU稽核60/60次。約200ms取樣，包含桌面與失敗呼叫，可能漏掉短暫峰值，並非精確最大用量或普遍容量保證。

倍率包含GPU評分與GPU繁殖。CPU16搜尋後抽查與離線原生CPU全族群／逐代冠軍判題的成本另列。

搜尋中CPU核對：有記錄840/840次GPU搜尋；已知fitness修正0次、wrong修正0次、best變更0次，記錄耗時0.000秒。此耗時已包含在搜尋wall time。

搜尋後CPU稽核區段：已記錄60.7秒（840/840次GPU搜尋）。包括cases準備、全族群identity及top16編譯／CPU評分，不是單純CPU評分時間。

離線判題process wall：已記錄5882.8秒，含失敗呼叫0.0秒；另有0筆已開始但成本未知。缺漏不當作零。這包含GPU／ASGP native及共同判題，不是純CPU時間。
其中通過證據核對的判題呼叫為5882.8秒；搜尋後與離線成本不加入headline搜尋倍率，不含本次報告讀取。

[互動結果](dashboard.html) · [逐題速度](speed.csv) · [P16384逐題對照](secondary_tasks.csv) · [品質明細](quality.csv) · [品質配對](quality_pairs.csv) · [速度cohort品質](speed_quality.csv) · [離線驗證成本](audit_costs.csv) · [原始彙總](results.json)

## 逐題結果

| Task | GAGP倍率 | ASGP／新GAGP | 品質全對：舊→新／預訂seeds |
|---|---:|---:|---:|
|filter_odd|1.012×|6.865×|1→1/10|
|sum_of_elements|1.004×|11.052×|10→10/10|
|is_sorted|1.358×|6.366×|1→1/10|
|index_of_element|1.035×|6.690×|3→3/10|
|all_equal|1.340×|4.164×|1→1/10|
|cumulative_sum|0.998×|8.527×|5→5/10|
|fuel_cost|0.934×|18.982×|1→1/10|
|luhn|0.977×|6.434×|0→0/10|
|min|0.962×|7.450×|10→10/10|
|mpi|0.948×|9.642×|0→0/10|
|count_odds|0.820×|9.538×|1→1/10|
|last_index_of_zero|1.108×|7.573×|3→3/10|
|small_or_large|0.988×|1.803×|0→0/10|
|for_loop_index|1.001×|4.289×|0→0/10|
|double_letters|0.999×|15.610×|1→1/10|
|replace_space_with_newline|0.999×|8.856×|2→2/10|
|even_squares|1.003×|4.136×|0→0/10|
|sum_of_squares|1.038×|1.296×|7→7/10|
|negative_to_zero|1.007×|7.955×|9→9/10|
|scrabble_score|0.883×|26.013×|0→0/10|
|grade|0.988×|0.927×|0→0/10|
|median|1.002×|0.903×|0→0/10|
|smallest|1.004×|1.071×|0→0/10|
|syllables|0.963×|18.808×|0→0/10|
|fibonacci|1.009×|0.778×|10→10/10|
|tribonacci|3.606×|1.432×|5→5/10|
|house_robber|0.951×|6.461×|0→0/10|
|subregion_sum|0.961×|1.483×|10→10/10|
|fibonacci_array|1.510×|2.261×|10→10/10|
|stock_with_cooldown|1.555×|3.489×|0→0/10|

## P16384逐題對照

G=64，每題1輪；倍數只顯示通過既有原始證據核對的對照。未完整核對的列仍保留，完整失敗狀態見CSV。

| Task | 舊GAGP／新GAGP | ASGP1T／新GAGP | 三方法對照 |
|---|---:|---:|---|
|filter_odd|1.003×|11.310×|完整|
|sum_of_elements|0.922×|18.703×|完整|
|is_sorted|1.421×|6.889×|完整|
|index_of_element|1.132×|13.131×|完整|
|all_equal|1.397×|6.896×|完整|
|cumulative_sum|0.993×|13.093×|完整|
|fuel_cost|0.865×|23.609×|完整|
|luhn|0.900×|8.356×|完整|
|min|0.906×|16.169×|完整|
|mpi|0.863×|17.881×|完整|
|count_odds|0.846×|36.547×|完整|
|last_index_of_zero|1.114×|11.331×|完整|
|small_or_large|0.981×|3.008×|完整|
|for_loop_index|1.004×|14.732×|完整|
|double_letters|0.998×|24.200×|完整|
|replace_space_with_newline|0.995×|16.231×|完整|
|even_squares|0.994×|8.081×|完整|
|sum_of_squares|1.233×|3.956×|完整|
|negative_to_zero|0.996×|15.393×|完整|
|scrabble_score|0.825×|28.093×|完整|
|grade|1.013×|1.338×|完整|
|median|0.977×|1.545×|完整|
|smallest|0.981×|1.484×|完整|
|syllables|0.780×|26.621×|完整|
|fibonacci|1.155×|1.823×|完整|
|tribonacci|2.904×|1.820×|完整|
|house_robber|1.042×|8.080×|完整|
|subregion_sum|1.111×|3.178×|完整|
|fibonacci_array|1.719×|4.777×|完整|
|stock_with_cooldown|1.715×|5.034×|完整|

## 採用的GPU執行方式

- TupleSummary 的私有 list 路徑把有案例的 lanes 集中成 tiles；200 cases／512 logical lanes 時由16個 tiles 改為7個，分數仍寫回原位置並按原順序加總。
- View kernel 依案例數及已編譯 phase 的加法特徵選 physical block；使用相同的全域規則，logical lanes 與評分案例保持不變。

## 限制與驗證範圍

- 固定30題、P1024/G64；速度3輪共用同一新初代seed35。時間區間只描述這3輪，不代表新任務或新初代的性能分布。
- 倍率採已固定的engine搜尋wall。兩個GAGP版本的main.cpp計時邊界相同；ASGP從parents複製／grammar建立前開始，含每代history編碼，GAGP使用已編譯grammar、JSON匯出與CPU16稽核在搜尋後。兩者涵蓋的準備／記錄工作不同；完整CLI process時間另列。
- 品質資料seed20261024，演化seeds35起；每題train/test輸入互斥、跨seed相同。有限輸入域與歷史資料重疊在split metadata揭露。
- 品質未知或失敗保留在預訂分母；非劣性界以seed為cluster，未知對視為退步。目前不足以支持5pp非劣性。
- 核對範圍為每代冠軍、最終族群分數描述與指定完整族群稽核；不代表每一代所有個體都已CPU重算，也不宣稱普遍等價。fitness採原40bit ranking canonicalization。
- ASGP1T為本輪新分母，使用此repo的ASGP整合及list-combine extension；外部來源資料夾缺Git metadata，精確檔案hash保留，無論文重現宣稱。
- ASGP與GAGP的原生算子及grammar sites未全面對齊；ASGP速度倍率不代表兩者演化能力相等。
- P16384獨立列示；跨P初代前綴不保證相同。每個P16384方法共用同一保留draw，未重抽失敗或不准入個體。
- NVML每約200ms取樣整張卡，包含桌面占用，可能漏掉短暫峰值。搜尋後／離線CPU判題成本與cold process時間另列，不併入headline搜尋倍率。
- 驗證成本只加總實際記錄：保留失敗呼叫耗時，未知／未執行不作零成本。cpu_audit_ms涵蓋cases準備、identity與top16編譯／CPU評分；離線process另含GPU工作，兩者都不代表純CPU評分時間。
