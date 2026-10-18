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
