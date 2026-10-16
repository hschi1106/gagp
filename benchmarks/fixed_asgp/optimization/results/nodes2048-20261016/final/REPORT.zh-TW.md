# 全30題 P16384：統一2048-node／16GiB workspace

完整執行與全量CPU fitness audit通過 **30/30**。NVML每200ms取樣整張24GiB RTX3090，最高 **21.30 GiB**（含桌面，可能漏掉短尖峰）。

所有題目採同一binary、CPU1T、seed0、完整16384初始個體與1024cases、四代加最後評分。
GAGP全AST上限統一2048、physical depth24；每phase GPU容量仍1024、genotype配置guard仍12GiB、workspace仍16GiB。ASGP來源phase depth7及其他演化規則維持原樣，搜尋空間仍未完全對齊。

跨題倍率中位數 **14.38×**，幾何平均 **10.12×**；28/30題快於ASGP。每題一次cold process，屬粗略速度與支援檢查，非精確重複量測或收斂等價證明。

| 題目 | ASGP 1T秒 | GPU-er-o秒 | 倍率 | CPU mismatch | 顯存取樣GiB |
| --- | ---: | ---: | ---: | ---: | ---: |
| filter_odd | 187.743 | 13.367 | 14.05× | 0 | 21.26 |
| sum_of_elements | 184.322 | 5.497 | 33.53× | 0 | 13.65 |
| is_sorted | 189.528 | 87.255 | 2.17× | 0 | 21.28 |
| index_of_element | 220.973 | 13.566 | 16.29× | 0 | 13.65 |
| all_equal | 187.227 | 86.009 | 2.18× | 0 | 21.28 |
| cumulative_sum | 241.670 | 13.931 | 17.35× | 0 | 21.26 |
| fuel_cost | 172.386 | 5.414 | 31.84× | 0 | 12.73 |
| luhn | 69.243 | 4.306 | 16.08× | 0 | 13.67 |
| min | 219.285 | 5.625 | 38.98× | 0 | 13.68 |
| mpi | 152.112 | 5.366 | 28.35× | 0 | 13.65 |
| count_odds | 160.478 | 5.469 | 29.34× | 0 | 5.28 |
| last_index_of_zero | 159.579 | 12.282 | 12.99× | 0 | 13.67 |
| small_or_large | 5.927 | 6.118 | 0.97× | 0 | 21.25 |
| for_loop_index | 146.856 | 12.663 | 11.60× | 0 | 21.26 |
| double_letters | 656.365 | 36.415 | 18.02× | 0 | 21.26 |
| replace_space_with_newline | 470.584 | 34.245 | 13.74× | 0 | 21.30 |
| even_squares | 277.016 | 18.819 | 14.72× | 0 | 21.22 |
| sum_of_squares | 102.552 | 4.314 | 23.77× | 0 | 13.62 |
| negative_to_zero | 275.537 | 15.609 | 17.65× | 0 | 21.22 |
| scrabble_score | 269.994 | 5.890 | 45.84× | 0 | 13.63 |
| grade | 5.491 | 6.366 | 0.86× | 0 | 21.21 |
| median | 4.279 | 3.008 | 1.42× | 0 | 13.64 |
| smallest | 4.270 | 3.027 | 1.41× | 0 | 13.62 |
| syllables | 285.917 | 5.965 | 47.94× | 0 | 13.63 |
| fibonacci | 66.656 | 7.709 | 8.65× | 0 | 10.66 |
| tribonacci | 67.010 | 12.811 | 5.23× | 0 | 10.66 |
| house_robber | 71.596 | 4.146 | 17.27× | 0 | 10.56 |
| subregion_sum | 20.450 | 4.435 | 4.61× | 0 | 10.67 |
| fibonacci_array | 131.078 | 20.143 | 6.51× | 0 | 10.68 |
| stock_with_cooldown | 103.804 | 15.013 | 6.91× | 0 | 10.66 |

## 驗證與限制

- 全30題先逐一重新準備16384個初始個體；grammar僅改node budget，不抽換／刪除個體。29題完整初始AST與舊artifact相同，House前8192個體相同。House僅2個初始AST超過1024，最大1086，2048設定可接受這批來源。
- 每個成功格對全部16384個最終個體、全部1024cases做CPU fitness audit，零mismatch、四代零unscored。這是fitness核對，不宣稱每個隨機程式都正確或都能解題；errors/timeouts照原契約計入fitness。House另有原生五模式P8/C1024/G2檢查。
- 搜尋時間採evolve_wall_ms，包含evolve呼叫內的GPU初始化、admission/materialization、四代、最後評分與清理；CLI輸入解析／匯入準備、離線準備、輸出及獨立CPU audit在此計時外。ASGP同P控制組以相同source/cases hash驗證後重用，ASGP路徑未改且不讀native grammar budget。
- 只提高整體node budget，沒有把每phase緩衝區加倍。Mutation donor同時符合whole budget與剩餘phase容量；phase owner先通過獨立admission才略過generic GPU preflight。若phase profile不支援，generic kernel仍保留1024-node硬限制，不以CPU繁殖替代。
- 完整Release build及131/131 native tests通過；tools116項（4 skips）、repository24項通過。大AST公共evolve入口、CPU/GPU fitness、generic fallback拒絕與既有32代staged/resident回歸均覆蓋。
- 這份表只包含新budget的P16384。舊1024-node五種population主表保持原樣，沒有把新House單格補進舊表。先期2048-node pilot的generic preflight失敗也保留；本表使用後續修正的同一binary重新實測全部30題。

[設定與重現](../README.md) · [原始秒數與counters](cells.csv) · [互動表](dashboard.html) · [PNG](speedup.png) · [PDF](speedup.pdf) · [來源雜湊](provenance.json)

Runtime checkpoint：`12c6849`。完整raw資料：`/home/hschi1106/.t3/worktrees/gagp/gagp-support-20261015/logs/nodes2048-searches-20261016`。
