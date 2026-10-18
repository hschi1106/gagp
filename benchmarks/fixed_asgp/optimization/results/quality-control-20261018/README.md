# 品質控制與長程GPU改善（2026-10-18，執行中）

使用者授權24小時完成同候選品質診斷、可控精確重評，以及長程GPU效能改善。預算自2026-10-18 01:17:45 UTC起算，硬截止2026-10-19 01:17:45 UTC（台灣10/19 09:17:45），不因重啟延長。

[凍結範圍與約束](protocol.json)。起點a948e41；舊binary及全部結果保留在上一輪目錄，本輪原始紀錄使用`logs/quality-control-20261018/`。主線CPU1T、GPU評分與繁殖、16GiB workspace、AST2048/depth24/phase1024固定。

先用舊training fixtures診斷，舊test不參與新設定選擇。最終候選及規模依training／parity／runtime證據凍結後，才使用新data seed及新初代做獨立驗證。`nsys`只作瓶頸診斷；正式速度以未profile程序量測。CPU精確重評的成本及修正數必須明列，不將其當GPU繁殖替代品，也不將零抽樣mismatch當作全族群語意證明。

初代稽核已完成：30題×1024候選，共30,720個；ASGP CPU／native CPU／resident GPU的fitness與wrong皆無差異。每題16384個配對偏好抽樣合計491,520對；source/native因節點成本破同分而有1011對不同（0.206%），native CPU/GPU偏好差異0。此結果只涵蓋指定候選及案例。

新增CPU複核與中後代checkpoint已通過Release的evolution_pipeline及fixed_asgp_bench整合2/2（49.06s）。診斷pipeline PID及分階段manifest在`logs/quality-control-20261018/diagnose-controller.json`；串行執行nsys兩題G64、9題×5seed×4個算子組合、seed0 ASGP控制及中後代同候選稽核。Development含checkpoint的基準時間僅作診斷，不納入正式速度倍率。

第一個initial controller在任何樣本開始前因Python3.10不支援hashlib.file_digest退出；已改為分塊SHA256，原錯誤log保留為initial-controller.attempt0.log。第二次完成60/60稽核程序，沒有替換樣本。

目前候選程式增加兩個預設關閉的控制：grammar-invocation phase depth（保留physical2048/24/phase1024），以及以grammar invocation count破同分。多代GPU owner測試對獨立canonical derivation的計數／深度、grammar membership與完整CPU fitness均通過；相關四個CTest通過189.40s。移除不必要的全域payload清理，沿用CPU評分既有逐案例8MiB discard overlay後，evolution_pipeline／adapter及新增list行為probe整合2/2通過49.69s。尚待完整回歸及品質採用判定。

TupleSummary私有內容評分及zero-cap保守配置界限分析已通過重新連結的CPU/GPU parity與extended payload 2/2（26.04s）。原型固定為tuple-bounded-bench進行全30題同候選及兩題G64 A/B；site-final-bench執行全30題初代logical count/depth／G1 admission及9題×5seed×4新增控制。所有控制程序共用同一flock，等待中的程序不與樣本或build並行。各binary與source hashes分別保存，尚未凍結正式版本。

nsys基準Is Sorted G64搜尋88.856s，評估GPU kernels占99.3% GPU kernel時間；cudaMalloc2376次、API區間累計46.193s（含等待，不能與kernel區間直接相加）。TupleSummary在原設定decline私有offset／typed/window／workspace reuse，故本輪先補該路徑；是否採用以相同候選及未profile長程搜尋決定。自動tiling與forced1在現有資料均同路徑，30個screen程序保留作重複量測，不算新的改善。

完整Release重建後130/131 native通過（251.68s），唯一失敗為將實驗說明寫入frozen spec造成hash gate失敗；已只移除本輪附加的spec文字，設計文件保留契約，凍結雜湊不改。Repository gate及adapter重跑2/2（49.56s），tools122（4skip）及repository24通過。CPU複核的raw GPU/CPU差異與40-bit ranking corrections進一步分離後，pipeline＋adapter2/2亦通過49.32s；既有failure logs保留，不描述成單次fresh131全通過。

初步未profile的Is Sorted/G64同初代A/B為88.716→9.128s（9.72×）；最終1024 AST、全代champion fitness及1024個最終個體CPU audit皆一致。這是單一seed開發結果，尚非新heldout正式結論。全30初代site metrics發現HeadIndex的native lowering會省略固定InputList子節點：Index of Element／Fuel Cost／Luhn有計數／深度差異；不將grammar invocation count宣稱為普遍等同ASGP source nodes。

核心實作已本地提交`4ca0909`。全30題新site政策初代與G1+final共90/90程序完成；CPU fitness/wrong稽核均通過。HeadIndex省略固定InputList子節點的差異另包含Last Index of Zero；合計4題1274個node-count、61個depth差異，不以native grammar-site政策宣稱ASGP搜尋成本完全一致。All Equal同初代G64 A/B亦完成：89.122→9.006892s（9.90×），完整最終AST與fitness/wrong相同。

正式驗證harness已用舊training完成7/7程序smoke，包含ASGP／GPU／CPUguard搜尋、原生CPU判題、無初代的共享test fixture形狀及resume不重跑。切分／完整工作與雙fitness+wrong稽核guard的8項Python測試通過。另預先固定CPU校驗劑量比較，詳見[guard-policy-plan](guard-policy-plan.json)；所有CPU校驗均使用當前training且計時。

[恢復入口](RECOVERY.md)列出所有控制程序。`validation_pipeline.py`只在診斷、training判題及checkpoint metrics完成後，依[分析計畫](analysis-plan.json)自動凍結候選／規模，再開始全新heldout。時間規則、命令、binary／來源／fixture雜湊與失敗均留存；不依新test調參。暫時30分鐘monitor在最終交付時停用。新seed／資料的正式搜尋已開始；結論尚待全部原生CPU判題。

正式統計與互動report程式已在新heldout之前撰寫，推論規則見[report-analysis-plan](report-analysis-plan.json)。報告將分開execution-only與operator變更，核對兩者的所有已匯出champion AST，且不把相同score/size descriptor當作全population AST相同。新增分析單元測試包含缺漏paired audit與相同descriptor但不同AST的反例；目前未產生任何新heldout報告。

CPU校驗舊training seed0/G64的9題cost screen已全部72/72搜尋／判題程序完成。相對execution-only的逐題wall ratio中位數：top1/i8為1.0108×、top8/i8為1.0819×、top32/i1為2.8081×；受檢program觀測分別81／648／18720，fitness及wrong修正皆0、raw最大差0。這是單一seed的成本資料與受檢範圍觀察，不能當作所有population parity或品質提升證明。完整計數在`logs/quality-control-20261018/guard-development-summary.json`；正式guard仍依預先固定top8/i8，沒有按本輪test選劑量。

正式驗證已於2026-10-18 04:22:53 UTC自動凍結，見[candidate-selection](candidate-selection.json)及[validation-freeze](validation-freeze.json)。僅依舊training選出`site7`：tuple執行路徑、uniform phase sites、source crossover gate與grammar-site depth7，未採site parsimony；physical AST2048/depth24/phase1024不變。舊training全對18/45、共同loss配對rank2.1111，逐題6改善／3變差，只作選擇依據，不能作heldout結論。

正式規模固定9題×新seeds5–24共20組×5方法（ASGP、原GPU、execution-only、operator、operator+top8/i8 CPUguard），P1024／G64，共900次搜尋及1800個原生CPU train/test history+final判題工作。資料seed20261018；凍結後才準備新heldout。全9題資料與180組初代已準備，程序尚在搜尋階段，品質結論待完整判題。所有方法同一pinned binary SHA256 `d347d95617aba1676af11b05d786bf06db882d9eddff6b97756207b3d8d9a48a`；不再按新test或單題結果選設定。

固定時間規則拒絕20×G128（保守總估24.685h），選20×G64：預估search5.523h、judge1.205h；搜尋／判題另計程序額外成本30分鐘，再乘1.5倍緩衝、加準備45分鐘與報告3小時，共14.592h，從凍結時預測至2026-10-18 18:58:26 UTC（台灣10/19 02:58:26）。這是舊training外推，非保證；硬截止仍2026-10-19 01:17:45 UTC。若新軌跡變慢，保留失敗／budget-not-run，不替換seeds或變更代數。

2026-10-18 08:47 UTC階段核對：正式900/900搜尋均完成並通過執行檢查，最後搜尋程序結束於2026-10-18 08:35:23 UTC；369個資料／初代準備工作成功。原生CPU train/test判題已開始，目前834/1800完成、1執行中、0程序失敗，涵蓋final及全代champions。這是工作進度，不代表解題成功或評分完全一致；須待全部雙判題及report integrity核對。Controller3166668及child正常，正式設定／binary／harness保持凍結。
