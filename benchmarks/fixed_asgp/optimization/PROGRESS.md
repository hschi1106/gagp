# 2026-10-23：G64加速24小時實驗

- 2026-10-24 06:25:52 UTC：正式全表及完整raw／source／binary／input核對結束，960搜尋與3240離線判題全完成。P1024/G64/R3全30幾何平均**1.079257×**（配對三輪95%CI1.077322–1.081183）、本輪ASGP1T／新GAGP**4.673147×**；P16384/G64/R1另列**1.080212×／8.075849×**。**1.3目標未達**。品質300/300配對冠軍AST／fitness／final描述一致，各90/300次執行最終全對，其中21初代已解、69後續新解；N10不證明5pp非劣性。完整結果見[報告](results/g64-optimization-20261023/final/report.md)。
- 收尾實際證據：fresh Release buildPASS、native136/136PASS（441.02s）、研究129PASS、tools122（skip4）、repo24PASS；原108及21未跑紀錄已被本次實際全回歸取代。PNG/PDF已看；T3原生及headless728/360篩選／選題／train-test65點／無水平溢位通過。Preset已匯出、兩個P的dry-run通過且無新搜尋；451CPP及7analysis／18輸出SHA重核不變。隔離frame的storage/access及12入口編譯檢查通過，但未整合、未VM執行、無加速結果。既有控制器全部退出；最後僅本機提交／原頁render／停用monitor，實際交付receipt置ignored RAW，數值目標不可標完成。

- 2026-10-24 05:21:19 UTC：P16384於05:13:51正式退出0，90搜尋／420判題全OK、240native controller-pass；30組舊新冠軍軌跡／final描述／full-final AST匯出均同。metadata閉合紀錄secondary-complete-observation0（994740邏輯觀測／886522每次呼叫內去重執行），仍待raw／source／mapping獨立重核，未算solved/gen0或宣稱1.3。原formal五階段皆exit0，所有計時已結束；既有closeout0自動進入full_release_build，當前child3632442，451CPP身份／136study*.py SHA已捕捉，期間勿改Python／另啟build/tests/report。Source7定點6/6及study108為目前實際既有PASS，新全回歸／21guards／report未完成；原截止不變。
- 2026-10-24 04:20:34 UTC：P16384已69/90搜尋OK、322/420判題OK、0終止失敗；Smallest指定判題完成，Syllables ASGP活躍（live log另存）。hour observer7374正常完成3600s觀測；58/266→60/279→69/312→69/322及真實PIDs／manifestSHA／closure receipt均保留，不是benchmark timeout。補CPU硬體context與既有GPU觀測連結；無新建置／測試／raw重分析。實際108pass、新guards21未跑、analysis snapshot4及1.3未達不變。
- 2026-10-24 03:17:08 UTC：同一P16控制器已56/90搜尋OK、252/420判題OK、0終止失敗；NegativeToZero ASGP子程序健康。DoubleLetters ASGP已正常退出0／2011.608s，隨後兩GPU及指定判題完成；未重跑。被動hour observer24492正常完成3600s觀測，15分鐘快照／真實PID／manifestSHA與收尾receipt保留，不是benchmark timeout；無新建置／測試／重分析。實際108pass、新guards21未跑／analysis snapshot4、1.3未達（暫定1.079257×）不變。
- 2026-10-24 01:27:00 UTC：P16384已40/90搜尋OK、182/420判題OK、0終止失敗；ForLoopIndex ASGP child3621703健康，原formal／secondary／closeout正常。observer58567正常900s觀測逾時後核對實際PIDs，未重跑。新增gpu-device-observation0.json保留唯一RTX3090／24576MiB／實際樣本GPU0且無CUDA mask、frozen serial sampler SHA，確認目前NVML取樣對應；非精確peak或全30容量證明。live MCP監控仍2h／enabled，下次03:20UTC；未建置／測試／重分析，實際108pass／新guards21未跑、analysis-source-freeze4與1.3未達不變。
- 2026-10-24 00:37:57 UTC：補P16384 VRAM可讀取樣覆蓋與CSV的次數／錯誤／scope；原serial在零有效取樣時也初始化peak=0，彙整現在將該情況保留未知，失敗呼叫peak不刪除，搜尋成功與CPU資格分開。3新增guards未跑，新待跑共21（report15＋preset6），實際仍108pass。修改前五檔及新7檔快照analysis-vram-before0／analysis-source-freeze4均留存，凍結矩陣與study_statistics SHA未變；未改採樣／倍率／資格／選樣，也未彙整正式VRAM或品質總數。既有計時流程繼續、無競爭測試。
- 2026-10-24 00:32:32 UTC：縮短本輪README為37行，保留暫定1.079×／4.673×與未達1.3、P/G/cases、品質重核範圍及ASGP計時／算子限制；操作細節連至既有RECOVERY。修改前完整README與SHA留存readme-before-readability0，未改數據／統計／資格／frozen scripts。逐一確認8個相對連結存在（非回歸套件）；既有P16程序／closeout仍健康，新增guards未執行。
- 2026-10-24 00:17:13 UTC：原formal／secondary／closeout父子程序健康；P16384已22/90搜尋OK、98/420判題OK、0終止失敗，當前luhn。輕量metadata及實際PID快照留存light-status-20261024-001713.json；未重啟／重跑或執行競爭建置、測試、重分析。P1024暫定1.079257×／ASGP4.673147×未變，1.3目標未達；完整raw重核與18個新guards仍待原矩陣結束。原截止2026-10-24 13:51:28UTC不變。
- 2026-10-23 23:54:02 UTC：首次輕量封存主表計時彙整（0.1s、未讀大型raw／未開GPU）：P1024/G64全30×3，原／新GAGP暫定1.0792574×，3輪配對時間區間1.0773225–1.0811828×；本輪ASGP1T／新GAGP4.6731470×。90配對與270搜尋／420判題／240native metadata閉合核對，來源SHA與producer保留primary-timing-interim0；1.3時間目標未達，完整raw／source／binary重核仍待final，不宣稱goal完成，未計solved/gen0總數或改設定。P16已18搜尋／84判題OK、0失敗，FuelCost prepare child3617251健康；observer28442後來正常逾時，非重啟理由。原截止／新tests18未跑不變。

- 2026-10-23 23:38:08 UTC：同一P16控制器已推進CumulativeSum，15搜尋OK＋1running／90、0失敗，前5題70判題OK；實際ASGP child3616431在100% CPU執行。被動observer32764正常600s逾時後再核對實際父／子PID，沒有重啟或新樣本。此輪為verified wait；未建置／測試／改凍結設定或計正式倍率，18新增guards仍未跑。原截止及1.3目標不變，非blocked。

- 2026-10-23 23:20:56 UTC：P16已14搜尋OK＋1running／90、0失敗，前4題56判題OK；AllEqual baseline child3615879健康，formal／secondary／closeout仍活躍。被動observer15064正常逾時，非樣本停止。新增browser_probe.js（尚未執行）及圖表cases／seeds／既有CI顯示，analysis-source-freeze3保留7檔，沒有改統計或選樣；新增tests仍18未跑、實際108pass。靜態核對30份grammar與選定CPP SHA，明示ASGP來源phase7與GAGP native2048/depth24／backend1024為不同表示法；主1.3只比原／新GAGP同一native規則。原截止不變、未證明達標，下一步仍等待原矩陣與既有closeout。

- 2026-10-23 22:28:54 UTC：P16已7搜尋OK＋1running／90、0失敗，前2題28判題OK；IsSorted原baseline child3613845健康，formal／secondary／closeout仍活躍。被動observer87207正常600s觀測逾時，主流程未停止。新增candidate_preset工具／使用範例／6個未跑guards，保留完整凍結設定、要求明確cases、同鎖hash核對及失敗receipt；尚無preset匯出／dry-run／新增搜尋，不是新倍率。新增待跑tests共18（report12＋preset6），實際仍108pass；等現有正式工作及closeout之後再匯出／dry-run，原截止不變。

- 2026-10-23 22:17:51 UTC：靜態審查發現full_experiment_coverage原漏查P16384 ASGP，可能在ASGP budget_not_run時誤標整輪完整。已加上同一凍結分母的ASGP有效數檢查與1個未跑回歸案例；修正前來源／當前analysis-source-freeze2皆留存。只改整輪覆蓋標記，不改逐樣本資格、倍率、選樣／程式或實驗；新報告tests共12未跑，最新實際仍108pass。現有formal／secondary／closeout皆活躍，P16已推進IsSorted；被動observer87207等待同一secondary，不重複樣本、不把timeout當失敗。1.3未證明、原截止不變。

- 2026-10-23 22:12:24 UTC：P16384前2題6次搜尋皆OK，第一題FilterOdd的14判題／8native均pass、完整final16384兩GPU核對及配對旗標通過（僅controller metadata；33158邏輯觀測／32652去重CPU執行）。保留原manifest快照於secondary-first-task-observation0，最終raw核對仍待報告。現有secondary3611715／SumOfElements native child3613060健康。修正報告漏列P16 ASGP可讀倍率與逐題表，新增secondary_tasks.csv、共用filter及4個尚未執行guards；先前來源／新7檔快照皆保留，未改統計資格／選樣／正式核心，未讀正式倍率。待計時結束共11個新report tests仍未跑，最新實際study仍108pass；不是1.3達標。

- 2026-10-23 22:04:09 UTC：新增[開發嘗試表](results/g64-optimization-20261023/development_attempts.md)及CSV／來源SHA，核對15個舊training設定與90組確認；明列完整30題1.062079×與8題篩選，未讀正式新資料倍率。初稿ordinary控制組名稱誤標已修正，舊source／輸出保留，全部數值／狀態／scope相同。不是新增回歸通過紀錄。P16384第一題3個搜尋皆OK，baseline判題5OK、candidate完整final native child3612536活躍，正式CPP／設定不變，1.3×尚未達；全回歸與正式報告仍等既有計時流程結束。

- 2026-10-23 21:47:25 UTC：N10獨立品質於21:46:08退出0；600搜尋／2400判題全OK、1200native核對全pass，300配對的冠軍AST／fitness、final descriptors及best export皆一致。40份manifest及完整lattice紀錄保留於quality-complete-observation0.json（78000邏輯觀測／20332去重CPU執行，仍待最終raw/hash重核，不宣稱非劣性或普遍等價）。同一formal0已自動進入P16384／G64，全30題child3611715活躍。被動observer30052收到實際退出事件後完成，無重跑。恢復紀錄已保留完整歷史並整理成單一目前狀態；7個報告新測試仍待計時結束後跑。

- 2026-10-23 21:22:53 UTC：獨立品質395次搜尋OK／600，s35–40完成、s41進行，formal0／child3594394健康。隔離frame新增真實VM的12入口CUDA compile-only probe，packed-executor-compile0 PID3605107目前只等待CPU access oracle；不launch kernel、尚未編譯，不列為VM/parity/速度結果。將在正式closeout與原CPU/CUDA及access檢查後共用鎖執行，含451來源快照／實際CUDA工具鏈／symbol與object SHA。正式核心與設定不變，7個報告新測試仍待計時結束後跑。

- 2026-10-23 21:10:32 UTC：formal0／品質child3594394健康，s35–38完成、s39進行，已282次品質搜尋OK＋1running。隔離packed frame補上state0初始化view、不可複製的local leaf及CUDA存取特化草稿，沒有改正式CPP或frozen runtime。CPU access oracle另排在原CUDA oracle之後，PID3602361目前僅等待；未建置／未測／未整合，沒有新倍率。原oracle輸入SHA未變。主目標1.3仍未證明，原截止不變；詳RECOVERY與prototype README。

- 2026-10-23 20:45:28 UTC：報告成本修正另加入實際搜尋中CPU核對耗時及fitness／wrong／best修正數，移除寫死0；缺漏保留未知，搜尋中耗時仍含於搜尋wall，搜尋後／離線另列。新回歸案例共7個，尚未執行；原108通過紀錄不變。正式核心與frozen runtime未改。

- 2026-10-23 20:39:28 UTC：主速度270／270後，首輪判題420／420也全部OK；240次native核對控制器均pass、mismatch0（73140邏輯觀測／57828去重CPU執行，最終報告仍須重讀原始證據）。同一formal0已自動轉N10獨立品質，child3594394健康、seed35進行；P16384待後續。修正報告未單列CPU稽核區段及漏掉失敗呼叫成本：新增report_costs、audit_costs CSV/JSON及6個回歸案例，原closeout會在計時結束後跑；目前未測，不能增加已通過測試數。正式核心／frozen runtime不變、倍率未重新計算、1.3仍未證明。詳RECOVERY。

- 2026-10-23 20:29:15 UTC：正式主速度270／270搜尋全部OK，原primary_speed於20:29:02 exited0；同一formal0已轉入primary_audits，child3592396及實際native樣本3592431健康。先做完整最終族群（去重後映回1024邏輯個體）與65個冠軍train/test核對，再做N10獨立品質、P16384；尚未整理正式倍率或宣布完整成功。被動pidfd observer15543收到退出事件後完成。新增4題舊training nsys瓶頸表及8份CSV hash核對，僅解釋GPU kernel成本、非正式分母。原1.3目標／固定截止不變；詳RECOVERY頂端。

- 2026-10-23 20:17:41 UTC：formal0／速度child健康，主速度205OK＋1running／270、0執行失敗；未計正式倍率或品質結論。凍結候選及原截止不變，開發1.062079×尚未達1.3×。隔離frame原型另完成CUDA儲存／位址／word ownership檢查原碼；packed-cuda-oracle1 PID3591456僅等待正式closeout及CPU oracle後才編譯執行，現在未建置／未整合。輔助啟動0因缺PYTHONPATH於import前失敗、未開測試，log及SHA保留；確認PID退出後以修正環境的獨立attempt1排程。正式樣本無重跑。詳RECOVERY頂端。

- 2026-10-23 19:58:18 UTC：正式新資料30題×10seeds準備完成，主速度103OK＋1running／270、0失敗；formal0 PID3587036／child3588621健康，無正式倍率／品質結論。原1.3目標仍未達，設定凍結不變。隔離frame壓縮草稿及CPU oracle原碼已寫，未整合／建置／無速度證據；CPU檢查PID3590044排在正式closeout結束後，避免競爭。詳RECOVERY最上LIVE SNAPSHOT。


- 2026-10-23 19:20:03 UTC：formal0已於2026-10-23 19:15:16 UTC在新資料前凍結：全30/P1024/G64/R3新ASGP，品質10新seeds35–44，P16384/G64/R1另列；binary81b26bde…／global tuple+hybrid。舊3seeds確認1.06208×（1.3仍未達），SoA/short與register-bank後續均無額外2%收益而拒絕。formal0 PID3587036/prepare child3587038活躍；closeout0 PID3587029已排程正式結束後全Release/native/tools/repo/report，勿重複。容量pilot因原成本規則budget_not_run，P16正式全表仍計畫。108guards與451CPP/source proof通過；schema verifier失敗0／更正版1均留存，沒有樣本重跑。原截止不變；詳RECOVERY。


- 2026-10-23 18:48:32 UTC：private7完整六臂180/180與fresh選定60/60完成，global hybrid全30舊TRAINING約1.05879×，未達1.3；編譯特化無額外收益而未選。3舊seed/native確認活躍3583458；新layout8（3583806）測SoA/精確短串列評分與dense交互作用，先8題再完整30控制，正式資料仍未開。取消並保存未啟動樣本的capacity7，容量pilot待最後全域候選；93guards與451CPP/source proof通過，checks14排鎖。詳RECOVERY最新FIRSTNEXT。


- 2026-10-23 18:28:48 UTC：native7實際6/6 PASS361.04s；private7全30六臂對照活躍PID3579655，尚無全30新收益／formal freeze。checks11實際85PASS；capacity7條件式pilot與formal-after-target1已排程，後者只在3舊seed確認>=1.3及實際容量配對完整時自動凍結；新容量guard checks12排同鎖。編譯失敗6與舊pre-dispatch gate完整保存，沒有搜尋重跑。詳RECOVERY最新FIRSTNEXT。

- 2026-10-23 17:58:10 UTC：native4實際6/6 PASS；但tile/view方案全30僅1.02567×、geometry17arms無額外2%收益，均拒絕。保留負結果；新全域tuple/hybrid＋private編譯特化build5曾因欄位引用錯誤編譯失敗（0搜尋），已保留並2行修正至新build6活躍PID3578246，private6 PID3578247等待全30六臂→新選定對照→3舊seeds。checks9實際81PASS；舊TRAINING reader/ASGP回讀90/60nativecalls全30通過。未新heldout／freeze／1.3達標。N10最低完整矩陣舊成本latestfreeze19:28UTC，須保留正式驗證時間；RECOVERY頂部有最新FIRSTNEXT。

- 2026-10-23 17:05:56 UTC：build4仍實際ptxas99.8%CPU活躍，native4未PASS；tiles4等待。新增report原始證據／閉合集驗證器、簡潔dashboard模板，checks6／成本preflight2／舊TRAINING reader-readback已排同鎖，尚未PASS／未freeze。最新FIRST NEXT已置於RECOVERY頂部，避免誤用較舊狀態。

- 2026-10-23 17:0xUTC：native3實際6/6 PASS；但dense3只Grade啟用、顯式untiled0全30GM.9890而拒絕。已保留負結果，真實initial/replay dense-tile＋generic sparse-view新實作套用，build4/native活躍PID3571321；tiles4 PID3571322等待六臂對照→全30→3舊seed確認。工具checks5實際64PASS。large舊TRAINING judge成本全30/120成功，未開新heldout／freeze／1.3達標。詳RECOVERY。

- 2026-10-23 16:32:49 UTC：3舊seed設定確認GM1.0291×，P16384/G64三題六樣本全OK、完整AST配對／CPU16抽查通過、sampled whole-card peak21.39GiB；非全30/P16全CPU證明。真正dense native3已套用／建置活躍（CPP6檔，禁止改）；尚無native3 PASS。新正式全範圍runner及來源／budget／統計guards已寫好，來源preflight1成功、ASGP資料夾無Git如實記錄；N20只診斷forecast，未freeze／新heldout。詳RECOVERY最新節。

- 2026-10-23 15:25:07 UTC：build2實際targeted6/6 PASS348.47s、工具38PASS；view128新native只比既有128約1.0123×，未達2%額外門檻而拒絕。nsys四題完成，私有arena／view／mutation瓶頸已定位。新dense-case scheduling原碼patch已完成，apply-dense3→build3→dense3三臂G64驗證排程等待舊來源使用者退出；目前CPP未競爭修改，尚無新native3 PASS／正式freeze／獨立品質或1.3達標。

- 2026-10-23 15:14:47 UTC：全30舊TRAINING設定新對照GM僅1.03547×（子集1.0884×不能外推）；新kernel128已編譯、native前三項PASS，其餘仍在跑。ASGP成本全30＋原生冠軍全30完成；尚無正式新分母。新增nsys G64四題診斷／全族群native成本校準與工具guards排同鎖。原確認／P16384鏈保留，無新heldout／freeze／1.3達標。詳RECOVERY最新節。

- 14:3xUTC：第一版trim native實際4/6，兩項測試抓到漏case。根因Arena採連續分段而非prefix，已拒絕並完整保存failed binaries/source，確認451CPP恢復baseline後改做kernel128編譯規格（目前3CPP變更，未PASS）。Python guard實際26PASS。settings2舊binary測試與ASGP成本／native稽核繼續；build2 PID3555545等待settings2，view3 PID3555590等待native，combined4 PID3555591將跑全30新組合。兩個無樣本queued控制器為修正activation guard而保留取消。沒有新heldout或正式1.3×宣稱，詳RECOVERY最新節。

- 14:1xUTC更正：screen0已完成48/48，view128子集GM1.0895，但BEST_ONLY=0因C++presence契約實際只匯出冠軍；原始記錄保留，不能聲稱全1024AST已驗證（全fitness描述／冠軍軌跡一致仍有效）。取消三個未啟動樣本的舊排隊控制器，保留證據；改development_v2要求真實1024exports，新trim2/settings2/combined2 PIDs3553665/66/67已排程。build1／CPP不改繼續建置；harness-checks0 PID3553682排同鎖，尚未PASS。新quality scale可在開新heldout前依成本擴80seeds，20全相同不足5pp推論。詳細見RECOVERY／RAW/export-scope-correction0.json。

- 使用者新授權：全30題P1024/G64相對目前最快GAGP>=1.3×，1.5／2×延伸；優先保持fitness/wrong及逐代冠軍軌跡；ASGP1T新對照、P16384另列。START2026-10-23 13:51:28UTC／DEADLINE2026-10-24 13:51:28UTC，不重設。新研究[README](results/g64-optimization-20261023/README.md)、[RECOVERY](results/g64-optimization-20261023/RECOVERY.md)。
- Baseline=a8460b6／pinned beb5585d…；451CPP檔完整對應、30題舊TRAININGhash核對。calibration0完成30/30，0失敗；G64評分／搜尋逐題占比平均66.1%、繁殖21.4%，只是開發成本診斷。
- 第一版實作已寫入：opt-in省略空initial tile，保持logical lanes／score slots／canonical reduction；新增CPUfractional/fuel/error/replay及fresh-generation/dirty-workspace對照。screen0 PID3552159活躍；build-attempt1 PID3552403等待其退出；trim1 PID3552530等待build/native通過後新三臂G64pilot。尚無新增native PASS或加速結論。
- CPU1T/GPU評分+繁殖/16GiB/AST2048/depth24/phase1024；所有重工作共用lock；無subagent/push，presentations及舊研究保持不動。新heldout未開啟，最終品質與全30驗證尚未開始。每30分鐘monitor記錄於logs/g64-optimization-20261023-monitor.json，最終停用。

# Latest state — 2026-10-23 13:18:15 UTC — REPORT / UI / EVIDENCE COMPLETE; LOCAL COMMIT + ORIGINAL RENDER CLOSING

- ACTUALoriginalreportfinal/completion.jsonOK/finished1792759853.2407768 (12:50:53UTC), validation_statusPARTIAL/target_reachedFalse/within_original_deadlineTrue. closeout1PID3544397finishedartifacts_ready_manual_review_pending/presetexportPASS. No livebenchmark/source/reportcontrollerremains. One passiveobserver failedProcessLookupError becauseactualreporthadEXITED; immediatelyinspectedterminalOK, no restart.
- CHECKEDRESULTS: all240speedsearchesOK; exactly600qualitysearchesOK,300/300fullyauditedidenticalpairedchampionAST/fitness/finalAST+score-descriptortraces, native1200qualitytrain/testjudgescomplete. Success BOTHmethodsGen0=23/300,finaljoint90/300,new67/300,lostinitial0. MainC1024/G8/R2all30timingmeansP1024=1.2174143104110058,P16384=1.7926839066808524,pooled=1.4773080389806241. Native119/120pass+1LuhnP16384baselineinterruptedUNKNOWN =>qualified59/60subsetmean1.4744465581958683, NEVERall60fullyaudited. NewqualityP1024/G64/C19–200mean1.000403624887436 (NOusefulspeedup). Seedcluster10/any-taskloss0,one-sided97.5%lowerbound−30.8497pp,5ppnoninferiorityFalse. Train/test30splitsdisjoint/10identicalseedsverifiedeach. SampledwholecardP16candidatepeak21.3698GiB, NOTuniversalpeakbound. CPUcorrections0; GPUeval+repro/1T/16GiB/AST2048/depth24/phase1024 unchanged. 2xUNREACHED, goalNOTcomplete.
- Source/regressionproof: cleanReleaseBUILD PASS, fullnativeFIRST135/136(runtime/GPUALLpass), onlyisolatedmissingignoredhistoricaldocfixture; selected-source-validation1copiedactualsha-a080eb...historicalmanifest tomirror, unchangedrepositorycontracttarget1/1PASS. Originalfailed0logs preserved; no singlefresh136/131claim. CurrentCPP451filesSHAexact26, sourcealignmentcopy[]remove[], new194studyPASS/tools122(4skip)PASS/repo24PASS. Recheckedfinaldocsrepo24PASS+52ownreportlinks. Onlyaffectedmaintaineddocs/navigationchanged, unrelatedpresentations132filesUNTOUCHED/excluded.
- ACTUALplotsPNG+PDFinspected; bothPDF1page, axes/labels/unknownrowreadable, qualitywidebounds/point0honest. Originalreportfinal preserved. NativeT3desktop728snapshotshowed47pxfirstcol/358pxstatus/7012pxtable; NEWpresentation-onlyfinal1thenfinal2fixcolumns+Pnowrap/shortstatuslabels+collapsedtimingdetails+P/G/C/globqualitytotals. CSV/statistics/PNG/PDFand#data payloadBITEXACTparentfinal. Newrender_readable_report.py/2 sources andparentcompletion/representationprovenancessealed. AuthoritativedeliveryFINAL2; no statslogicchanged/datafiltered/reselection. ActualT3html_preview728+360both11checksPASS(all60rows/filterhouse2rows/no-match0/taskselect/unknownretained/train-test-jointagainstpayload/actualdifferenttrain-testcounts/nohorizontaloverflow/PoneLine). NativeT3browsertoolwidgetsreplicated728/360PASS; servedHTMLSHA99476ef87b98f973670deb394945a846b32a20b5eff7c973a423a4c7ac776f43. UI_VALIDATION.json/PLOT_REVIEW.json/browser_probe.jsrecordproof. Nativepreviewtabtab_1_47e2e164-9f41-4a55-93d4-ff333dc9b821agentowned; localHTTPpreviewserversports35639(session37077)and44103(session34938)mustcleanupafteroriginalrender. Originalfinal2HTML storedfunctionskeyfinal_original_html; rereadfileiflost. DoNOTrenderinjectedprobe. All27parentsealedartifactsandallfinal1/final2artifacthashescheckedagain. DELIVERY_AUDIT.jsonrecordsobjectiveNOTachievedandprecommit/renderpending snapshot.
- CURRENT/READMErewrittenplainChinesefinalnumbers+knownnegativeG64+limitations+final2links. ActualwinnerpresetdryrunPASS/RUNNER_CHECK.json, no newsearch/newoutput. Originalfixedwindowstart1792682607/deadline1792769007 (15:23:27UTC) unchanged. Finalneeded: review/stageONLYownedCPP/study+affected3docs/PROGRESS, diffcheck, localcommitNO push; thenhtml_renderEXACTfinal2dashboardbeforefinalreply andcleanupownHTTPservers (useactualsocketPID/cwdverification). Recordpostcommit/renderreceiptunderRAW/DELIVERY_RECEIPT.json (ignored, avoidscommitselfhash); ifreceiptEXISTS deliveryalreadydone, doNOTrepeatcommit/render or sample. Commitmetadata+receiptauthoritativelater; thissnapshotdoesNOTclaimcommityet. No appmonitor/subagent/newgoal/oldstudyresume.
- IMPORTANTgoalremainsUNACHIEVED; doNOTmarkcompletefromartifactdelivery. Anotherfullcandidate/freshcontrols/quality/native/sourcevalidation round doesnotfitremaininginheritedwindow (originalactualofflineauditcalls2.32h alone plussearch/quality/build); originaldeadlineMUSTNOTresetwithoutuser. Do not invent2x, universalqualityequivalence, fullP16long-runquality or ASGPpaperreplication. Finalreplyconciselinks+materiallimits+localcommit/no push. Nativegoalstatusauditappliesifnoauthorizedfull-scopeworkremainafterdelivery; neverpausewithoutuser.

# Latest state — 2026-10-23 12:47 UTC — SOURCE VALIDATED WITH EXPLICIT FIXTURE RECOVERY / REPORT ACTIVE

- Actualsource0fullReleaseBUILD PASS. FullnativeFIRSTACTUAL135/136 PASS426.52s, onlygagp_test_repository_contract failed: isolatedgit-archive mirror omitted ignored historicalmanifest linked fromdocs/guides/fixed-asgp-benchmark.md (logs/all-task-repair-20261015/final-all30-g4/manifest.json). Specfreeze andALLruntime/GPU testsPASS. source0/queue0FAILED;closeout0STOPPEDbeforeanyalignment/tests/report, allrecordsunchanged. NOsinglefresh136or131claim.
- NEWrecover_source_contract.py/selected-source-validation1 executedundercommonlock afterallpriorPIDs/childrenexited. VerifiedsamecompleteCPP26map, copiedACTUALoriginalhistoricalmanifestbyteexactSHAa080eb3be358a7eef7633a428b98a08d8730c3b6cf528e6cc2487e7711388235 intoisolatedmirror0; noCPP/test/spec/binary/oldstudyfilechanged. TargetedUNCHANGEDrepositorycontract1/1 PASS. New1statusok recordsoldfailedmanifest/logSHA, original135/136+new1/1 +scope, samepinnedcandidatebeb5585; originalfailureretained. Addingmissingfixture tomirrorrecorded, not editingtest/historicalfile. source1 manifest inherits olderrorhistory, explicitrecovery/native_summarydistinguishescurrentPASS. Fulltestnotrerun.
- NEWcloseout_after_source1.py PID3544397LIVE, prior0preserved. Waitscompletedsourcevalidation1 thenfirstactualselected-source-alignment0 PASS (copy[]/remove[]/currentCPPexact26), selected-docs-alignment0PASS changedONLY.agents/skills/gagp-navigation/SKILL.md +docs/design/payload.md addingadjacentcontract. ACTUALstudy194PASS0.125s,tools122PASS4skip4.549s,repo24PASS0.568s; actualreport_compact--helpPASS. Allnewrecovery/qualitytiming/reportguards nowACTUALPASS, supersedingH35=185. NoCPP/source/freeze/settingschanges.
- ACTUAL12:47:20 closeout1phaseREPORT child3544543LIVE/Rs/CPU13:40/RSS~667MiB. NEWBASE/final/completion.jsonRUNNING; report.logempty/noerror. firstnative_recovery_evidence.json +interrupted_native_audits.json emitted (readerrawspeed+qualityphasepassed thusfar, finalsummary/statisticsnotyetdone). Reportusesactualbest1 freezes andregressionmanifestselected-source-validation1, samecapturedreportcode including135/136+target1/1limitation. DO NOTeditreportcodewhilelive/hashsealed; DO NOTduplicateNEWfinal/report/alignment/teststeps. Ifreportfails preservefailedfinal, fixactualanalysisbug/newfinal1 transparently. Existingcloseout1exportswinner_preset2ONLYafterreportPASS; finalplots/browser728+360/interactions/renderORIGINAL/diff/localcommitremainMANUALpending. Source1firstrecoveryguardfailurespreserved, no samplesrerun.
- Sourcephasecomplete(native119/120pass+1LuhnP16384baselineUNKNOWN), search240speed/600quality+1200nativejudgesunchanged. CurrentC7R2timing-onlypooled1.477308 target2xUNREACHED. No appmonitor/subagent/push/oldstudyresume; unrelatedpresentationsuntouched. Originaldeadline15:23:27UTC, originalstart unchanged. FIRSTNEXTcloseout1actualPID/child+finalcompletion/report.log; waithealthyactualreport orinspectfailure. GoalACTIVE.

# Latest state — 2026-10-23 12:21 UTC — SAME CLEAN BUILD STILL ACTIVE

- Actual12:21:02 validator3537589LIVE/full_release_build childcmake3537745LIVE2142s, tailstill76%GPUresultkernels; noerror/terminalrecord. Closeout3533439LIVEwaitingsource. Passive600spidfdwait61684exit0 at12:20:54 timeoutNOTterminal. No competing commands or retry. Buildlocal3600scap/final2hreserve13:23:27UTC/deadline15:23:27UTC unchanged; inspectactualterminalresult, neverrestartfromtimeoutobservation alone.
- Nativefinal119/120pass+1LuhnP16384baselineinterruptedUNKNOWN retained; originaltiming240quality600unchanged. NoCPP/freeze/settings/reportcodechanges. NewfullRelease/nativeAllNOTyetPASS; latestactualH35=185PASS/newreportguardsUNTESTED. FIRSTNEXTvalidator/closeoutactualPIDs+children/currentlogs. IfactualbuildFAIL/cap, preserveattemptandinspectbeforeanyexplicitrecovery; queuedcloseoutstopsratherthanhidingfailure. Actualreport/source/statistics/UI/localcommitpending, 2xUNREACHED1.477308timing-only. GoalACTIVE.

# Latest state — 2026-10-23 12:08 UTC — CLEAN BUILD 76%, GPU TEST KERNELS COMPILING

- Actual12:08:28 validator3537589LIVE/full_release_build childcmake3537745LIVE1388s; buildtail76%, extendedpayloadGPUpassedBUILDonly (NOTtestexecution). Remainingtest_bounded_region_gpu_results ptxas3541299R/154sCPU2:34 andgrammar_package_gpu_resultsptxas3541301R/149sCPU2:29. EarlierfitnessGPUcompilefinished; nonfataldevice-defaulted-copyannotationwarnings retained inlog, nofailedstatus. Closeout3533439LIVEwaitingsource. Prior600spidfdwait30045exit0 at12:08:20 timeoutNOTterminal. No duplicate/competingcommands.
- Nativefinal119/120pass+1interruptedUNKNOWN, freshsearches240speed/600qualityunchanged. NoCPP/freeze/settings/reportsourcechanges thisturn. NewfullRelease/nativeAllstillPENDING; latestactualH35=185PASS/newreportguardsUNTESTED. NEXTactualvalidator+closeoutPIDs/children/manifest+logs; neverduplicatequeuedbuild/tests/alignment/report. Build3600/native1800/final2hreserve13:23:27UTC/deadline15:23:27UTCfixed. Source/statistics/report/plots/UI/localcommitpending; 2xUNREACHED1.477308timing-only. GoalACTIVE.

# Latest state — 2026-10-23 11:57 UTC — CLEAN FULL RELEASE BUILD ACTIVE

- Actual11:57:03 sourcevalidator3537589LIVE/full_release_build childcmake3537745LIVE703s. Descendantfitness_gpu.cu compiler3538006 +ptxas3538609LIVE/R/586sCPU9:46; unchanged23%tailreflectslargeCUDAcompilation, NOTterminal/hang. Noerrorrecord. After-native-source3533083waitsvalidator;closeout3533439LIVEwaitingsource. Sourcequeue/readbuildlogsONLY; no competingtests/build/reportanalysis. Priorverified600spidfdwait22065exit0 at11:56:53 timeoutNOTterminal onACTUALLYLIVE3537589.
- Nativephasefinal119/120pass+1interruptedUNKNOWN (LuhnP16384baseline), all120statusesretained; no retry. CPP26/source/freezes/measuredbinaries/settingsunchanged. NewfullRelease/nativeAll stillPENDING; latestactualH35=185PASS, newreportguardsUNTESTED. FIRSTNEXTactualsourcevalidator/closeoutPIDs+children+manifest/currentlogs; DO NOTduplicateexistingbuild/tests/alignment/report. Buildcap3600s(originalstart11:45), nativecap1800s/final2hreserve13:23:27UTC/deadline15:23:27UTC fixed. Target2xUNREACHED1.477308timing-only; actualsource/statistics/report/UI/localcommitpending. GoalACTIVE.

# Latest state — 2026-10-23 11:45 UTC — NATIVE EXITED / EXACT SOURCE FULL BUILD ACTIVE

- Actualnative3532718EXITED11:45:15, recordfinished1792755914.975299. All120expectedauditstatusesnowpresent: baseline59OK+1interrupted,candidate60OK;119native_fidelity_passTrue, full_native_audits_pass=False. No missing/budgetrows; LuhnP16384baselineinterruptedUNKNOWNretained/no rerun. speedcontrollerrequested_phase_complete/119of120/passFalse; its error field retains earlier recovery1startup assertion ashistory (notcleared/hidden). Mainbest-effort1stoppedrecordpreserved; qualitycompleteunchanged. Searchtimings240/qualitysearches600 unchanged. No all-success/2xclaim.
- Actual11:45:25 after-native-source0PID3533083LIVE/source_validation_running child3537589LIVE; selected-source-validation0manifest full_release_build/currentcmake--build-j2 child3537745LIVE. Usestraining17selection +controllersbest-effort1/quality/speed/native-resume2; EXACTselected26source reconstructed/sourcemapchecked, pinnedmeasuredbinaryunchanged. CURRENT/README nowlabelnative119/120andSOURCEPENDING. No competingtests/build/analysis. Closeout3533439LIVEwaitingsource andwillrununchangedqueuedalignment/studytoolsrepo/report/preset onlyafterPASS. FIRSTNEXTinspectsource+closeoutactualPIDs/manifests/currentlogs; donotduplicatesourcebuild/test/report.
- Planupdatednativeexecutionstepcomplete (coverage119pass+1unknown, NOTfullsuccess), source/regressions+statisticstepINPROGRESS. LatestactualH35=185PASS/newreportguardsstillUNTESTED; newfullRelease/nativeAll NOTyetPASS. Source26/freeze/settings/numericalmathunchanged. Actualreport_compact/NEWfinal/CSVs/plots/UI728+360/interactions/renderoriginal/diff/localcommitstillpending. Originaldeadline15:23:27UTC, sourcecommandfinal2hreserve13:23:27UTC; timedcutoff12:23:27 irrelevantnativefinished early. Target2xUNREACHED1.477308timing-only. GoalACTIVE.

# Latest state — 2026-10-23 11:22 UTC — 77/120 NATIVE PASS

- Actual11:22:22 recovery3532718LIVE/child3536130LIVE baselineScrabbleScore/P16384/37s; baseline38OK+1interrupted+1running,candidate39OK; all77completednative_fidelity_passTrue. NegativeToZeroP16384 baseline615.39s/candidate10.26sOK. LuhnP16384baselineinterruptedUNKNOWN/no rerun unchanged. Passive600spidfdwait56330exit0 at11:22:15 timeoutNOTterminal.
- Sourcequeue3533083LIVEwaitingnative,closeout3533439LIVEwaitingsource; no sourcebuild/tests/report yet; donotduplicatequeuedsteps. Readonlyreport-data memory review: documents map retains score-descriptor outputs, NOTall exportedASTs; fullASTexports heldperpair andqualitycase-results discardedbycompact_judge. No actualmemorybug demonstrated/no readerchanges/newtestclaim. CURRENT/READMEtimestampedcoverageupdatedonly. LatestactualH35=185PASS/newguardsstillpending. Fixedcutoff12:23:27UTC/deadline15:23:27UTC; target2xUNREACHED1.477308timing-only. NEXTallthreeactualPIDs/children, lethealthyjobscontinue; originalregressions/report/UI/localcommitpending. GoalACTIVE.

# Latest state — 2026-10-23 11:11 UTC — 73/120 NATIVE PASS

- Actual11:11:35 recovery3532718LIVE/child3535675LIVE baselineNegativeToZero/P16384/90s; baseline36OK+1interrupted+1running,candidate37OK; all73completednative_fidelity_passTrue. EvenSquaresP16384 baseline429.72s/candidate5.96sOK; SumOfSquaresP16384 baseline90.12s/candidate7.45sOK. LuhnP16384baselineUNKNOWNinterrupted/no rerun remains. Verified600spidfdwait81307exit0 at11:11:28 timeoutNOTterminal.
- Sourcequeue3533083LIVEwaitingnative;closeout3533439LIVEwaitingsource. Noactualsourcebuild/tests/report started, noCPP/freeze/settings/reportcodechange. FIRSTNEXTactualallthreePIDs/children; neverduplicatequeuedsteps. LatestactualH35=185PASS/newguardsstillpending. Originalcutoff12:23:27UTC/deadline15:23:27UTC unchanged. Source/regressions/statistics/finalreport/plots/UI/localcommitpending; target2xUNREACHED1.477308timing-only. GoalACTIVE.

# Latest state — 2026-10-23 11:00 UTC — 65/120 NATIVE PASS

- Actual11:00:51 recovery3532718LIVE/nativechild3535236LIVE baselineEvenSquares/P16384/128s; baseline32OK+1interrupted+1running,candidate33OK; all65completednative_fidelity_passTrue. ReplaceSpaceWithNewlineP16384 baseline572.64s+candidate8.74sOK. LuhnP16384baselineinterruptedUNKNOWN retained/no rerun. Prior600spidfdwait observer99760exit0 at11:00:44/timeoutNOTterminal.
- Sourcequeue3533083LIVEwaitingnative andcloseout3533439LIVEwaitingsource; no actualsourcebuild/tests/report yet. FIRSTNEXTallthreeactualPIDs/children; donotduplicatequeuedsteps. NoCPP/freeze/settings/reportcodechanges thisturn, onlytimestampedCURRENT/READMEcoverage. LatestactualH35=185PASS, newguardsstillpending. Cutoff12:23:27UTC/deadline15:23:27UTC/finalreportreservesfixed; leavebudget_not_run/unknownifneeded. Target2xUNREACHED1.477308timing-only, actualsource/regression/statistics/plots/UI/localcommitstillpending. GoalACTIVE.

# Latest state — 2026-10-23 10:50 UTC — 61/120 NATIVE PASS

- Verifiedpassive600spidfdwait on3532718, observer56886exit0/timeout-not-terminal10:49:55. Actual10:50:03 parent3532718LIVE andchild3534776LIVE baselineReplaceSpaceWithNewline/P16384/143s. Baseline30OK+1interrupted+1running,candidate31OK; all61completednative_fidelity_passTrue. DoubleLettersP16384 baselineCPU671.17s+candidate9.56scompletedOK. LuhnP16384baselineinterruptedunknownstillpreserved/no retry.
- Existingsourcequeue3533083LIVEwaitingnative;closeout3533439LIVEwaitingsource. No sourcevalidation/build/tests/report started yet. DoNOTduplicatequeuedstages. NoCPP/freeze/settings/selection changes. CURRENT/READMEupdatedtimestampedcoverage; targetstillUNREACHED1.477308timing-only. LatestactualH35=185PASS; newreportguardsUNTESTEDuntiltimedends. Originalcutoff12:23:27UTC/deadline15:23:27UTC unchanged. FIRSTNEXTactualallthreePIDs/children, continuehealthywait; actualregressions/report/UI/localcommitstillpending. GoalACTIVE.

# Latest state — 2026-10-23 10:39 UTC — 57 NATIVE OK / SAME LIVE RECOVERY

- ACTUAL10:38:53: recovery3532718LIVE, nativechild3533979 baselineDoubleLetters/P16384LIVE242s, lastrecordprocess233.80s. Baseline28OK+1interrupted+1running;candidate29OK =>57/120completed ALLnative_fidelity_passTrue. LuhnP16384baseline remainsinterrupted/unknown/no rerun; candidateindependentCPU60.45sOK retained. No new failures/source/settings/freeze changes.
- Thisgoalturn completed aPASSIVE600s pidfdobservation onACTUALLYLIVE3532718 (observer42583EXIT0 at10:38:47 withOBSERVATION_TIMEOUT_NOT_TERMINAL), not minute-resampling or task restart. Actualsourcepipeline3533083LIVEwaitingnative, closeout3533439LIVEwaitingsource, as10:26note. NEXT inspectallthreeactualPIDs/children first; DO NOTduplicatequeuedsourcevalidation/tests/report/alignment. Detailedone-shotcommands areinpriornote andscripts.
- report_compact ONLYreportprose additions: read-table glossary(Pindividuals/Ggenerations/Ccases), baselineexplicitlypreviousfastestGAGP notASGP; ratioformula/direction. Forfull120pairtimingcoverage only, compute gapto2x fromcheckedpooledtimingmean, currentlyabout26.1% furthercandidategeometric-time reductionrequired; explicitly arithmetic, not claimedfuturegain or qualification. Mainstatistical/data/qualification/plotmath unchanged; no freshlabelsusedfortuning. Reportentry/newguards stillUNTESTEDuntiltimedexit/queuedcloseout. ACTUALlatestH35=185PASS remains.
- Allstudychecks/actualsourcefullRelease/nativeAll/finalreport/CSV/PNG+PDF/dashboard/plots/browser728+360/interactions/renderoriginal/localcommitpending. Originalfixedwindow/cutoff12:23:27UTC andfinalreportdeadline15:23:27UTC unchanged. No newgoal/appmonitor/subagents/push/oldstudy edits; unrelatedpresentationsuntouched. Target2xUNREACHED freshR2timing-onlypooled1.477308. GoalACTIVE.

# Latest state — 2026-10-23 10:26:58 UTC — ONE-SHOT CLOSEOUT QUEUED

- NEWcloseout_after_source.py PID3533439 waitsactualafter-native-source0PID3533083; noGPUlock/resourceswhilewaiting. AFTERsourcefullregressionactualPASS only: guarded26sourcealignment+capturedaffected-docsalignment, compileALLstudyPython then alltest_*.py undercommonlock, tools/repoactualchecks, actualreport_compact--help, NEWfinalreportactualbest1 viareport_compact withselected-source-validation0, winner_preset2. Eachstep recordscommand/log/hash/exit underRAW/closeout0, stops/preservesfirstfailure, no hiddenrerun. Reportdir notprecreated. Actualplots/browser728+360/interactions/renderoriginal/diff/localcommitremainMANUALPENDING; noautocommit/push/newsearch.
- FIRSTNEXTinspectnative-resume2 +after-native-source0 +closeout0 actualPIDs/children BEFORE anymanualsourcebuild/tests/report. DO NOTduplicatequeuedsteps or editnew closeoutscript whilelive. Ifanypipelinefailsinspectretainedcause, correctactualbugtransparently/newattemptwithinoriginaldeadline. Neverrestartsearch/nativeinterrupted/failedtag. Currentoldpipeline1startupfailurealreadypreserved. Source26/freezes/binaries/settingsallimmutable.
- No newactualtests yet; reporthelpers/newguards deferreduntiltimedexit. LatestactualH35=185PASS remains; donotclaimnew count. Twofoldtargetstillunreached; currentcompletefreshR2timing-onlypooled1.477308, fullnativecoveragepending. Budget/start/cutoff/reportreserveunchanged; finalpartialifneeded. Unrelatedpresentationsuntouched. GoalACTIVE.

# Latest state — 2026-10-23 10:22 UTC — NATIVE RECOVERY LIVE / SOURCE VALIDATION QUEUED

- ACTUAL10:21:55 native recovery3532718LIVE; baseline18OK+1interrupted/candidate19OK=37/120nativeOK, all completedflagsTrue. LastcandidateLuhnP16384 independentlyexecutedCPU60.45sOK, interruptedbaseline remainsUNKNOWN/no rerun. MaincurrentlyPythonpostprocessing, nochildatthatinstant; healthylivehandle, not stopped.
- NEW after_native_source_validation.py one-shot PID3533083LIVE/waiting_for_existing_native_recovery(waitpid3532718). Usespidfd wait withnoGPUlock/analysis; ONLYafterrecoveryterminal ANDallactualnativechildrenexited/no runningorwaitingjobs, launchselected-source-validation0 usingUNCHANGEDvalidate_selected_source.py +training17selection +controllersbest-effort1/quality/speed/native-resume2. ActualcleanfullRelease/nativeAll undercommonlock, originaldeadline/final2hreserve. No competingbuild/testsnow/no duplicatevalidation directory. FIRSTNEXTinspectbothcontrollersactualPIDs beforemanualsourcecommands; doNOTduplicatequeuedsourcevalidator.
- Newnative_recovery_evidence.py +2guardtests authoredUNTESTED: finalreportchecksoriginalarchived30successfuljobs byteequal, interruptedrecordretainedwithoutPID/command/cost replacement, newjobstartsafterrecovery, archive1==archive2manifestsprovesfailedrecovery1ran noaudits; rehashbothpreservedrecoveryharnesses/freezes andoriginaltimingmanifests. report_compact recordsnative_recovery_evidence.json. Pythonharnesshashonly (notJSONdecode); existingEvidence.checked onlyforJSON. report_validation4+2guards andpriorreportattempt2/qualitytiming3guards STILLUNTESTEDuntiltimedends; latestACTUALH35=185PASS remains. NoCPP/source/freeze/settingschanges.
- Originalcloseoutstillpending: awaitqueuedactualsourcefullregressions, guarded26source/doc alignment, allnewstudyguards +tools/repo underlock aftertimed, actualreport_compact--help +NEWfinalactualbest1 +winner_preset2, actualplots +HTML728/360/taskfilter/select/train-test/nooverflow/renderoriginal, diff/untracked/localcommitNO push. Prior scripts1failedstartup preserved. No new appmonitor/subagent/deadline/oldstudy work; unrelatedpresentationsuntouched. Target2xUNREACHED actualnewR2timing-onlypooled1.477308. GoalACTIVE.

# Latest state — 2026-10-23 10:19 UTC — CONTROLLER LOSS / EXPLICIT NATIVE-ONLY RECOVERY

- At10:13:59 main3508585 was first missing;10:16:25 /proc3508585 AND nativechild3531614 absent, no matching search/audit processes and lock unheld. Original launch log ended after LuhnP1024; cause of disappearance unproven. All240speed/600qualitysearches and their existing audits/comparisons preserved. Native30OK, LuhnP16384baseline was running with last-observed20.43s/no rawoutput; kept INTERRUPTED/coverageunknown, NOT rerun/replaced. Original controllers/manifests archived byte-exact under native-resume1-preserved before any update.
- Explicit resume_native1.py startup3532705 failed BEFORE opening any stage/native job: mutated dictionary aliased old process state to own newPID, so own-live assertion. Preserved script/controller/log/failure. NEW resume_native2.py uses copy.deepcopy for state only, verifies recovery1 and original processes absent, same freeze/harness/binaries/settings/inputs; archives current state under native-resume2-preserved; opens Stage(resume=True), retains all existing success/failure/interrupted rows and invokes unchanged admitted audit ONLY for missing tags. ACTUAL3532718LIVE; candidateLuhnP16384 running atlatestread, baseline15OK+1interrupted/candidate15OK+1running. FreshcandidateCPU executes independently when interrupted baseline cannot supply exact CPUreference. No search/freeze/tuning edits; cutoff12:23:27UTC unchanged.
- New report_validation4.py preserves version3, additionally verifies interrupted snapshot command/options/binary but DOES NOT invent executed-program counts or CPU/fidelity success. Forces validation issue for unknowncoverage, labels process_s only lowerbound. Newtest_interrupted_audit.py2 guards authored UNTESTEDuntilalltimedexit. report_compact adds explicit interrupted list/limitation and preserved startup-recovery failure. All other reportmath unchanged. Currentactualfresh timing-only1.217414/P1024,1.792684/P16384,pooled1.477308; target2xUNREACHED.
- NEXT verify actual3532718/nativechildren/stages first. Do not duplicate/overwrite any existing row. Originalmain nowstopped withunknown-lossnote; speedcontroller recordsactualrecoveryPID+link, qualitycomplete untouched. AFTERALLtimedexit proceed sourcecleanfullRelease/nativeAll, guardedsource/doc align26, studyhelper/tools/repo actualchecks, NEWfinalreport/plots/UI728+360/interactions/renderoriginal, localcommitNO push. No new deadline/appmonitor/subagents. Source26/beb5585 immutable. GoalACTIVE.

# Latest state — 2026-10-23 09:28 UTC — TIMINGS COMPLETE / FULL NATIVE ACTIVE

- ACTUALspeedtimingsfinished1792747265.8160057 =09:21:05UTC. At09:25:37read: main3508585LIVE/fresh_speed, speedcontroller timings_complete_native_audits_pending, nativechild3529558 baselineSumOfElements/P16384snapshotLIVE. Bothtimingstages120OK=240/240;120comparisonpairs allfinal_descriptors_equal/champion_ast_fitness_equal/final_export_equalTrue (60R0fullAST comparisons,60R1best-only). Fullnative baseline3OK+1running/candidate3OK=6/120done, all6native_fidelity_passTrue. QualityEXECUTIONstill600searches/2400judges/1200nativeALLok,300controllercomparisonrecordsequal. No failure/new source/candidate/setting/freeze edits.
- NEW ACTUAL LIGHT record RAW/timing-complete-observation0.json (immutable): checked entire120-cell task/P/repetition lattice/no duplicate,allpairedstatuses+fidelity andpositiveratios; recordscomparisons/freezes/completedtiming-stageSHA. Recomputedmetadata-only equal-weighttimingmeans P1024=1.2174143104110058, P16384=1.7926839066808524, pooled=1.4773080389806241. NOTnative/source/reportqualified, primary_target_proven=False; full native +allartifact/source recheckstillpending. No historical denominator reuse. This is freshR2 timingobservation, distinctfromC7DEV/R1=1.4711393831195734. 2xunreached. Nativeaudit/sourcebranchfixedunchanged; doNOTreselectfromnewnumbers.
- Goalturn included three verified600s pidfd waits onactual3508585 +readonlycode review. Suspected shape-cache content/invalid-payload hazards ruledout inCODE: fitness_gpu.cu requires resolved complete IntList/Int payload closure before view admission; shape check additionally rejects saturated/inconsistent backing lengths; runtime/view_profile.hpp:74..78 Eq/Ne admitsONLYInt/Bool, not list identity. Additional shape builtin dependency proof excludes element reads/listmemo; directroot caller captures proof retained. Existingnative shape/saturated/capturefixtures were partofprior actualtargetedpass. This ismanualreview, NOTnewtests/universalproof. NoCPPchanges. LastACTUALH35=185PASS0.126s/native26target15PASS353.17s; newreport_attempt_notes2+quality_timing3guards/entry wiring STILLUNTESTEDuntiltimedworkexits.
- CURRENT/README now display ACTUALnewR2timingobservations clearlypendingfullnative/sourcequalification; olderdev1.471 andnegativeoldG64C2.9807/C7.98785 retainedseparately. Actualnew-dataG64timingmean/gen0/finalsuccess statistics stillNOTcomputed (quality_timing/report deferredforisolation). Planseparates completedtimings fromactivefullnative; no fullquality/noninferiority/2x/131claim.
- NEXTFIRSTinspectexistingmain/quality/speedPIDs+realchildren andnative stages; let120groups finish orbudgetstopunderoriginal12:23:27UTCcutoff. DoNOTstartsourcebuild/tests/heavyreportanalysis whileparentorrealchildrenactive (allthreecontrollerssharePID3508585, qualitycontrollercompletealoneisNOTexit). Nearcutoffcapwaittoexactcutoff. AfteractualALLtimedexit: sourcecleanfullRelease/nativeAll(training17selection, allthreeexistingcontrollers), guardedsourcealign priorADMITTED26 +docs; studyallnewguards/smalltools/repo/actualreport_compact--help; NEWfinalreportactualbest1+winner_preset2; actualplots+HTML728/360filter/task/train-test/nooverflow/html_renderORIGINAL; diff/untracked/localcommitNO push. Unrelatedpresentationsuntouched. No subagents/newappmonitor/oldstudyresume. START1792682607/DEADLINE1792769007/3hreserve fixed. GoalACTIVE2xUNREACHED.

# Latest state — 2026-10-23 08:39 UTC — SPEED ACTIVE / REPORT LONG-RUN TIMING AUTHORED

- Actual08:38:06UTC: PID3508585LIVE/fresh_speed; speedcontrollerrunning; nativechild3526372candidateAllEqual/P16384/G8/r0LIVE44sCPU43s. Baseline19OK/candidate18OK+1running=37/240 completed speedsearches, nofailedrecords. QualityEXECUTION remainsCOMPLETE600searches/2400judges/1200nativejobsALLpass;300controllercomparisonrecordsallchampion/finalbest/score-descriptorequal. DoNOTrun sourcebuild/tests/heavyreportanalysis while speed/nativechainactive. No duplicates/candidate/settings/frozenharness/CPP changes.
- Useful report-only implementation: NEWquality_timing.py consumes ONLY report_data's already hash-checked search/native champion pairs AFTERtimedwork. Equal frozen tasks +equal paired seeds, keeps every expected pair, requires complete pair/native audit/trajectory evidence; ANYunknown prevents a full-task mean. Independent per-task observed seed-ratio range is descriptive only. DoesNOTreplace primaryC1024speedgrid or prove2x; full final-population CPU/AST coverage NOTclaimed. NEWtest_quality_timing.py3meaningfulguards authored: hand-derived geometricweighting; missing search/native/trajectory unknown cannotyieldfullmean +noinputmutation; invalidtimes/workload/duplicatekeys reject. NOtestsrunyet (deferredfortimingisolation), noactualfreshG64meancomputedyet.
- report_compact.py (NEVER RUN) wires this helper into quality_timing.json/quality_timing_pairs.csv/quality_timing_tasks.csv and mainMD/HTML. Uses checkedvalidation.input_splits training counts tolabelactual C-range, and actualfrozenP/G; sourceSHArecorded. Displays new-dataG64timingseparatelyfromC1024/G8primary andoldnegativeG64observations16/17; no selection/tuning fromfreshlabels. Originalreport_data/statistics/qualification/plots unchanged. Allnewreport_attempt_notes2guards +quality_timing3guards/newentry mustrun AFTERtimedjobs; latestACTUALH35=185PASS0.126s, CPP26targetnative15/15PASS353.17s ONLY, nofreshfullsuiteclaim. Alsoactualentry--help/importcheck beforefirstNEWfinalreport.
- Originalcloseoutpriority unchanged: AFTERALLtimedmain+childrenexit sourcecleanfullRelease/nativeAll usingtraining17selection/controllersbest-effort1+quality+speed, guardedsourcealign priorADMITTED26/docsalign, study/tools/repo checks, reportactualbest1NEWfinal +winner_preset2, actualplots/HTML728/360 interactions/nooverflow/html_renderORIGINAL, diff/untracked/localcommitNO push. No subagents/newappmonitor/oldstudyresume. Unrelatedpresentationsuntouched. Source26/beb5585.../sameglobalrecipe retained. START1792682607/DEADLINE1792769007/3hreserve fixed, capobserverat12:23:27UTCcutoffnearreserve. GoalACTIVE2xUNREACHED; nofinalspeed/qualityclaimfrompartialstatus.

# Latest state — 2026-10-23 08:30 UTC — QUALITY EXECUTION COMPLETE, SPEED ACTIVE

- ACTUALqualityfinished_epoch1792744045.5221362 =08:27:25UTC. At08:28:44read: best-effort1mainPID3508585LIVE/fresh_speed; qualitycontrollerrequested_phase_complete; NEWspeedcontroller running since1792744045.7549794. Child3525894 LIVE candidateFilterOdd/P16384/G8/r1. Allthree controllers now EXIST and sharePID3508585: sourcevalidation must wait entiremain/childrenEXIT, then passbest-effort1 +best-effort1-quality +best-effort1-speed (no phantomformal1).
- QUALITYACTUAL: baseline300OK+candidate300OK=600/600 searches; eachjudge stage1200OK =2400/2400common+nativejobs; native600OK pervariant=1200/1200native train/test jobs, ALLnative_fidelity_pass=True; no failed records. All630preparationsOK. Small113303-bytebest-effort1-quality-comparisons.json readlightly:300rows; everyfinal_descriptors_equal/champion_ast_fitness_equal/best_export_equal=True. These are CONTROLLERvalidated records, not yet the final report's full artifact/hash/split/native expansion recheck, gen0/final quality totals or statisticalnoninferiority claim. Do NOT call arbitrary program equivalence/all-intermediate-population AST proof. ActualG64performance still needs aggregation fromcheckedruns, separatefromC1024/G8primary.
- SPEEDACTUALfirstread: baseline3OK/candidate3OK+1running, nofailures. Frozen same731baseline/beb5585candidate26 SHAinbothstages; globalG8/R2/bothP/all30/C1024; original1T/GPUrepro/16GiB/AST2048/depth24/phase1024 unchanged. No build/test/heavyanalysis inparallel. This goalturn included two verified600spidfd waits onlive3508585 plusreport-only refinement, not a duplicate sample/controller.
- report_compact.py NEVER RUN, ownsourcehashwillrecordamendments. Newconditional timing-only paragraph: ifnativecoverage<timingcoverage, expose ORIGINALsummarytiming_observed_geometric_mean+coverage separately soan auditedsubset meanisnotmisread asall30. This does NOT substitute timing-only forqualification, alter statistical math/masks/target, or hidefailed/unknownrows. RawCSValreadykeepsall. Alsochangedgenericadmissioncaption tocoverage+failures (canbe0), withoutinventingcurrentfailedadmissions. Existingnewselected-development/negative16+17/processtime/cost/recovery/qualitytrace/R0native-scope appendices retained. Allreport additions andreport_attempt_notes2tests remain UNTESTED; actualH35=185PASS0.126s/native26targeted15/15PASS353.17s ONLY.
- Next let speed240searches then120R0full-native groups run normally underlock. Aftertimedmain+ALLactualchildrenexit: source cleanfullRelease/nativeAll (training17selection, prioradmitted26align), docs alignment, allstudy+smalltools/repo+actualreport_compact --help, NEWfinalreport/CSV/plotsactualdata, winner_preset2, actualplotsinspect+HTML728/360taskfilter/select/train-test/nooverflow+html_renderORIGINAL, diff/untracked/localcommit NO push. No subagents/newappmonitor/oldstudyresume. Unrelatedpresentationsuntouched. Near12:23:27UTC cutoff capobserverwaittothecutoff; do notlose10minofthe3hreserve. START1792682607/DEADLINE1792769007fixed. GoalACTIVE2xUNREACHED (latestcomplete C7DEV1.471; negativeoldG64C7.98785 retained, no finalspeed/qualityconclusionyet).

# Latest state — 2026-10-23 08:06 UTC — ACTIVE best-effort1

- Actual08:05:42UTC: controller3508585LIVE/fresh_quality, quality/search_and_judge; native child3521361 candidateall_equal/s32LIVE. Baseline214OK/candidate214OK+1running =428/600 completed qualitysearches, nofailedrecords. Judge stages856OK each; native428OK each =856 completed native train/test jobs, ALLnative_fidelity_pass=True. All630preparationsOK. No speedstageyet. Goalturn made report-only scope clarification and a verified600s pidfdwait; no competing build/test/heavyanalysis/native work. No duplicates/resumes/freeze/candidate/source changes.
- Inspected ORIGINAL FROZEN formal_shared_policy.py and UNCHANGED report_data/qualification: speed full AST export/full-native audits are deliberately REPETITION0 ONLY (120 task/P/method groups); later repetitions export BEST ONLY, CPU16 inline, paired genchampions/finalchampion+final population SCORE DESCRIPTORS. Do NOT claim every speed repeat has full-population AST/native coverage. report_compact.py (NEVER RUN) now states this explicitly in main MD/HTML, without changing any frozen script/numerical/statistical/qualification rules. Earlier new quality_trace_summary likewise distinguishes score descriptors from full-population AST export. This is a reporting scope clarification, NOT a relaxed gate/new experiment/repetition audit. Every quality search still requires all genchampion+final native train/test audits.
- Current report-only additions (selected-development appendix, report_attempt_notes2guards, paired trace counts, repetition scope) remain UNTESTED. Actual latest H35=185PASS0.126s and CPP26 targetednative15/15PASS353.17s; no fresh full suite claim. AFTER timed controllers AND real children exit, run all study helpers under commonlock and actual report_compact.py --help entry/import check before first real report (NEW final directory). Do NOT rerun source/native/timing now. Original source26/binarybeb5585.../fixed flags/resources allunchanged.
- Closeout still pending as07:52/07:25 notes: selected-source cleanfullRelease/nativeAll on training17 selection (only existingbest-effort1/quality/speed controllers), priorADMITTED26 alignment +affected docs, actual smalltool/repo checks, reportactualbest1/actualplots/HTML728+360filter/task/train-test/nooverflow/html_render ORIGINAL, winner_preset2 afterreportcreatesfinaldir, diff/untracked/localcommit NO push. Keepunrelatedpresentations. Near original12:23:27UTC timing cutoff, cap any observer wait to cutoff rather than losing10min of reporting reserve; verify actual terminal/main+children before sourcebuild. No appmonitor/subagents/oldstudy resume. START1792682607/DEADLINE1792769007/3hreserve unchanged. GoalACTIVE2xUNREACHED.

# Latest state — 2026-10-23 07:52 UTC — ACTIVE best-effort1

- This goal turn made report-only progress plus a verified10min pidfd wait on ACTUALLY LIVE controller3508585. At07:51:42UTC: best-effort1/fresh_quality and quality/search_and_judge; native child3518731 candidate double_letters/s30 LIVE. Baseline164OK/candidate164OK+1running =328/600 completed searches, no failed records. Judge stages656OK each; native328OK each =656 completed native train/test jobs, all native_fidelity_pass=True. All630 preparations remainOK. These are partial execution/audit counts, not solved-task/noninferiority/full-grid success. Speed stage not started yet. No competing native/test/build/heavy analysis, no duplicate work.
- report_compact.py (NEVER RUN/not frozen runtime harness) now additionally records quality_trace_summary from UNCHANGED checked quality reader pairs: expected count, exact trajectory pairs, fully-audited matching pairs. MD/HTML directly label identical per-generation champion AST/fitness +final champion AST+full final-population SCORE DESCRIPTORS; explicitly distinguish those descriptors from full final-population AST export. This is not a substitute for success/noninferiority and does NOT promote unknown/failed rows. No statistical/qualification/plot logic changed. New wiring and earlier report_attempt_notes2guards/selected-development appendix still need AFTER-TIMED checks; latest ACTUAL H35=185PASS0.126s. CPP26/source/binary/quality/freezes/settings unchanged.
- Previous source/report details and full closeout order in07:25/06:59 entries remain authoritative. Next FIRST actualbest-effort1/quality/speed PIDs/children+manifests. Wait healthy same frozen quality->fresh speed/native chain. AFTER all timed controllers/children really exit: selected-source fullRelease/nativeAll (training17 selection, only existing controllers), guarded sourcealignment priorADMITTED26, affected docs alignment, actual study/tool/repo checks, report_compact NEW final usingbest-effort1, winner_preset2, actual plots+HTML728/360 interactions/nooverflow and html_render ORIGINAL, diff/untracked/localcommit NO push. Unrelatedpresentations untouched. No subagents/appmonitor/oldstudy resume. START1792682607/DEADLINE1792769007/3hreserve fixed. GoalACTIVE2xUNREACHED.

# Latest state — 2026-10-23 07:25 UTC — ACTIVE best-effort1

- Actual 07:24:50 read: PID3508585 LIVE/fresh_quality, native child3512670 doing candidate syllables/s26 test judge. All630 dataset/initial-population/prepare jobs OK. Quality searches baseline54OK +candidate54OK =108/600, no failed search records; common/native judge stages baseline216OK, candidate214OK+1running. Native judges baseline108OK+candidate107OK, all215 completed native_fidelity_pass=True. These are observed process/audit counts, NOT completed full-grid quality/noninferiority or solved-task claims. Fixed source26/binarybeb5585.../C7recipe/new dataseed20261022/seeds25..34 unchanged. No speed stage yet; do NOT duplicate/resume any job.
- Useful report work during healthy timed jobs: report_compact.py (NEVER RUN, not frozen runtime harness) now reuses UNCHANGED development_evidence.read_families on ONLY the already-frozen selected development family. Recomputes and verifies selected binary and all selected per-P/pooled means; saves separate development_speed.json/CSV. Explicitly labels G8/R1 old development/selection bias/full-native audit incomplete; NEVER substitutes these cells/denominators for fresh speed or target qualification. Original statistical/numerical/plot readers unchanged. Additional source hashes recorded. Long-run negative16/17 observations stay visible.
- Dashboard cost/negative/startup notes consolidated under a plain details disclosure after quality; removed duplicate prose before quality. Development result separately disclosed, primary speed/quality remain first. Actual final728/360 previews/interactions/plots still PENDING. No synthetic UI/source-check claim.
- New report_attempt_notes.py +2 guards and the above report-only wiring still UNTESTED, intentionally deferred until timed controllers/children EXIT. Latest ACTUAL study helper check remains H35=185PASS0.126s; current CPP26 latest native15/15PASS353.17s, NOT a new full native131. Do not claim future checks. report_attempt_notes recovery verifies preserved empty0 failure/new1 scope and cost coverage separate from audit success. No C++/frozen runtime script/freezes/settings modified this turn.
- Next first inspect ACTUAL best-effort1/quality/speed controllers+children. Let quality then speed/native fresh-reference chain proceed under same exclusion lock; no competing builds/tests/heavy analyses. After actual timed exit: exact selected-source full Release/nativeAll using training17 selection and ONLY existing controllers; prior admitted26 for guarded source alignment; affected docs alignment; all study+small tools/repository checks including report additions; NEW final report_compact on best-effort1; export winner_preset2 after final dir exists; actual plots/HTML728+360/filter/task/train-test/nooverflow, html_render ORIGINAL; diff/untracked and localcommit, no push. Unrelated presentations/ untouched. START1792682607/DEADLINE1792769007/3hreserve unchanged. No subagents/new app monitor. Goal ACTIVE; 2x UNREACHED.


# Latest state — 2026-10-23 06:59 UTC — ACTIVE best-effort1

- IMPORTANTactualbranchchanged: development-native-cost0 STOPPEDatdeclared50min/06:46budget (26nativejobsOK+FuelCostP16384baselineTIMEOUT, total27/120attempted; remaining93notrun). No confirmedmismatch incompletedjobs. Allsources/logs/stagefailuresretained. final-pipeline6STOPPEDasrequired; NOformal1freeze/search/newdata. Allcost/final6PIDsexited. DoNOTresumeoldcost0/final6/formal0/final5.
- best-effort0 ACTUALfrozeG8/R2 andqualityG64/P1024/seeds25..34 at06:46, then FAILEDstartupassert BEFOREANYfreshdata/population/search/judge. RootcauseMYbuilder first_heldout_use='after immutable best-effort candidate/full-grid freeze' didnotmatchUNCHANGEDquality_data.prepareexactguard 'after immutable candidate/scale freeze'. Sourceold0/freeze0hashes/failedcontrollers/emptyinputStageallPRESERVED, NOTpatchedorre-attested. Confirmedinput0.jobs={}/noinputs/no pool/no sourcefiles, PID3506736andchildrenEXITED. Userexplicitlyinformedofmistake/correction.
- NEW best_effort_validation1.py / ACTIVEbest-effort1 PID3508585 correctedONLYmarkercontract inNEWattempt. Requiresold0actualexit+emptycohort; checksnew/old0 speedandqualitysettings/inputs/tasks/variants/resources/source/Candidate/P/G/R/seeds/data/limitsEXACTLYsame. Newfreeze includesempty_parent_recoverywithold0freezeSHA0d3cfb535db1c45e51a0f30aa5c9ebcd75e83952f73c3b07d319b6c830ab3de5/c8bdd8e39f86668ffd4faea7ae6d7136dbc88826c0c9c81a3870fd8e09175587. NOresampling/rerun/existingoutputoverwrite; old0had0samples. Nativequality_data/quality_dedup guardNOTweakenedorchanged.
- ACTUALbest-effort1freeze_epoch1792738182.596968 (2026-10-23 06:49:42UTC), beforefirstnewdatasetjob. SAME26binarybeb5585d6cd478be304ff37795dd6bf303cdf5460f2221a95f1aaf07708d2c57/globalC7recipe. Speedall30/P1024+16384/G8/R2/C1024, qualityall30/P1024/G64/data20261022/seeds25..34/train200/test1000. Planning_modebest_effort_after_budget_refusal/forecastpassedFalse; originalestimated7.88990h >available5.56236h,1.35safety/fixed1800reportedhonestly. Primaryqualificationpartialevenifalljobsfinish. OriginalSTART1792682607/DEADLINE1792769007/reportreserve10800UNCHANGED. NewqualityFIRST, thenfreshspeed/fullfreshCPUrefs (NEVERreusecalibrationrefs).
- ACTUALPID3508585LIVE/fresh_quality; best-effort1-quality-controllerpreparing, 287preparationjobsatlastread (dataset/freeze/prepare, NOTsearchsuccesses), observedjobshealthy. NewdataNOWOPENEDONLYAFTERfreeze; DONOTtune/reselectbinary/settingsfromfreshlabels. BothfreezesEXIST underRAW/best-effort1-*-freeze.json, qualitycanonicalfirstheldoutmarker. OnfuturecheckFIRSTinspectthisPID+children+input/search/judgestages. No duplicate/resumeold0. Planstep3completedfixedglobalgrid(withdeclareddeviation), step4validation/source/report/commitINPROGRESS.
- H34ACTUAL183PASS0.124s aftertimingscopeamendment. Newprocess_timing.py supplementsUNCHANGEDprimaryevolve_wall_ms with SAMEqualifiedmask/subprocessprocess_s ratios; no mutationofprimarydata, missing/nonqualifiedcellsnotpromoted, actualread-onlyprocess-timing-reader0 OKonC7G8/R1 withONLY11/60currentnativequalifiedcells (explicitpartialdevelopmentproof, NOTfullgridprocessratio). Copiesofchangingnativemanifestssavedinreaderout, primarysamples/cohortmembership/freezehashesNEVERmodified. RAW/timing-source-scope0.json validatescurrentCPPmainSHA==admitted26, timerblockBYTEEXACTreferencecommit538370e8... sliceSHA8b0ff8a50daa741cf79db1515221f205fb82b12bb854f49abf96b9cbf6b6ce7e.
- Correctedreporttext: evolve_wall_ms configure_source_depth+evolve_population includesINSIDEinit/nativeimport/GPUeval+repro/finaleval+populationrestore; EXCLUDESprecallrestore_cases/sourceparentsdecode/grammarcompile/preparedASTmeta+derivation andpostcallCPU16/JSON-ASTexport. Newprocess_timing.json/speed_process_cells.csv showseparateprocessmetric, never2xtargetsubstitution. report_views_compactoriginalnon-dashboardfunctionsUNCHANGED; report_compact.py NEVERrun/notruntimefreezeharness, ownsourceSHAwillrecordamendment. H35ACTUAL185PASS0.126s (adds2realqualitymarkercontractguards inclactualprepare(emptytasks) andbadmarkerrejectedbeforestageaccess); currentreportentry/allsourceschecked. NotnativeAll/131/tools122claims. CPPSTILL26/noedits.
- PREFERREDfinalreportreport_compact.py(importsreport_validation3) MUSTNOWusebest-effort1-speed/quality andREALbest-effort1freezes, NOTfailedemptybest-effort0/formal0/nonexistentformal1. Includeempty0startupfailure/correctiontransparentlyinthefinalMD/HTMLlimitations (freezehasproof; entrycurrentlyplanningdeviation+negative16/17+timingscope, addconciseemptyattemptnoteifneeded BEFOREfirstreport; sourceSHAseparate). Keepallactualfailed/budget/notrunrows, qualitygen0/new/lost, CPUcost/corrections. LatestfullC7dev1.471/P1k1.2127/P16k1.7846, lowcaseG64negative16=.9807/17=.98785 NOTfreshquality. No2xtarget/freshqualityconclusionyet.
- CURRENT/READMEtopupdated185/source26/actualqualityprep+timingscope+timeout+emptystartupfailure/new1; olderREADMEhistoricalparagraphsmentioncostrunning/old0waitingandshouldbereconciledforfinalreadability. DoNOTeditoldstudies/rawfreezes/failedattempts. FinalpendingAFTERtimedjobs: exactselected-sourcefullRelease/nativeAll (selectionRAW/training17-development-selection.json; passONLYexistingactualtimedcontrollersbest-effort1+quality+speedifexists); actualPIDs/childrenmustdead. alignsourceusingpriorADMITTED26, aligndocs, actualsmallrepo/tools/studychecks, report/CSVs/PNG+PDF/dashboard, inspectrealplots+HTML728/360/filter/task/train-test/nooverflow/html_renderORIGINAL, winner_preset2export(defaultfinaldirMUSTstayNEWuntilreport), inspectdiff/untracked(includingunrelatedpresentationsDONOTstage), localcommitNOPUSH. No appmonitor/subagents/push. GoalACTIVE2xUNREACHED.


- 2026-10-23 06:49:42 UTC：保留原預估拒絕後，明確凍結 best-effort1：同一CPP26全域候選；全30題、兩種P、G8/R2；品質全30題P1024/G64、seeds25–34，新資料種子20261022。先品質後速度。原安全係數1.35與完整核對不變，但預估未符合可排入規則，因此不保證跑完、不列為原規格完整達標。所有未完成格保留；原截止與3h報告預留不變；新資料尚未建立。


- 2026-10-23 06:46:19 UTC：保留原預估拒絕後，明確凍結 best-effort0：同一CPP26全域候選；全30題、兩種P、G8/R2；品質全30題P1024/G64、seeds25–34，新資料種子20261022。先品質後速度。原安全係數1.35與完整核對不變，但預估未符合可排入規則，因此不保證跑完、不列為原規格完整達標。所有未完成格保留；原截止與3h報告預留不變；新資料尚未建立。


# Latest state — 2026-10-23 06:26 UTC

- Goalturn PROGRESS: conditional best-effort FULLminimum-grid validation nowAUTHORED/LAUNCHED, replacing priorNOTEthatitwasnotauthored. Activebest-effort0 PID3506736, statuswaiting_for_standard_budget_decision, NOnewdata/freeze/searchyet. H33ACTUAL180PASS0.122s/PID3506735exited; new3budget-plan guards+planning-mode guardpass. Alloriginalnumeric/statistical/plots functionsunchanged, report_validation3snapshotclassifierBYTEEXACT2 verifiedactualASTsourcetext. CPPstilladmitted26/noedits/builds.
- development-native-cost0 PID3505616ACTUALLIVE, 18/120nativejobsrecordedlastread, allcompletedOK sofar; IsSortedP16384fullbaselineCPU/GPUpassed afterlongCPU1Tcall, currentIndexOfElement/nexttask nativechildhealthy. Original3000scalibrationcapends2026-10-23 06:45:50.141140UTC, notnewoverallclock. final-pipeline6 PID3506007stillwaitingfullcost, NOformal1freezeatlastread. DoNOTduplicate/resumeoldformal0/final5 oranyactivecostjob. H32176PASSandactualsnapshot-command-reader0 sevenrealcompletednativecommandsremainvalid.
- best_effort_plan.py usesONLYpriorbudgetrefusal (choiceMUSTNone), REALnow>=refusalepoch, originaldeadline/reserve, original1.35forecast/fixedfloor; minimumallowedfullgridG8/R2,bothP/all30,qualityG64/P1024/seeds25..34/all30, newdataseed20261022. ReturnsORIGINALFORECASTPASSED=False andPLANNEDFULLGRID=True, notfakefit. Permitsonlyterminalcompletecalibration ORdeclared3000sbudgetcutoff; anyconfirmednative mismatch/invalid/nonbudgetfailureblocks. Failed/unknownjobsremainfailures. Testscheckfalsequalification, fullscope,seeds,backwardsclock,nonbudgetstop/nativefailures.
- best_effort_validation0.py MUTUALLYEXCLUSIVE: waitcost0+final6actualEXITED. IfANYformal1freezeexists,exitnot_neededwithoutreadingitsfreshdata/replacingit. OtherwiseONLYafterbudget_insufficient (orcost0declaredtimeoutandfinal6stopped) plusH33actualpass. Checksallcompletednativejobsfidelity beforeanythingfresh. Selectsame26/globalrecipefromTRAINING17 andpriorrefusal, NOnewtuning. CreatesREALNEWbest-effort0speed+qualityfreezes once BEFOREfreshpoolcreation; explicitplanning_mode=best_effort_after_budget_refusal/original_forecast_passedFalse. FirstQUALITYall30/P1024/G64/10seeds, THENNEWspeedall30/bothP/G8/R2+freshALLnativeCPUpairedreferences/freshGPU, original16GiB/CPU1T/GPUrepro. NohistoricalorcalibrationCPUreference/denominatorreuse. original3hreportcutoffviaStageguards; noresample/overwrite/hiddenreruns. Everymissing/budget-not-runstaysunknown. Thisisexplicitplannedpartialcoverage, NOTshrunkprimarygoal/qualification.
- PREFERREDactualfinalreportentry STILLreport_compact.py, NOWimportsNEWreport_validation3.py. Originalreport_validation/2 unchanged. New3 preservesallsnapshot/binary/input/CPU/sourcechecks andaddsvisibleplanningissueforbest_effortplanning EVENifallcellsfinish, forcesvalidationpartial/primarytargetFalse. Standardformal1modeNone remainseligibleunderoriginalrule. ReportMD+HTML conditionallyshowplanningdeviation; numerical/statistical/plotsunchanged; allsourceSHArecorded. ThisreportentryhasNEVERrun; H33checksactualnewsource. CLIactualnameschooseformal1-speed/quality IFstandardfreeze, otherwisebest-effort0-speed/quality IFfallbackfreeze. NEVERinventformal0freeze.
- CURRENT/README nowactual1.471speeddev, lowcaseG64.981/.988negative,180helperchecks, originalbudgetrefusal andconditionalfallbackdisclosure. Usercommentaryexplicit: prior1.35forecastdoesnotfit, nofullcompletionpromise/primaryqualification; stillplanfull30/minimumallowedgrid, qualityfirst, allfailuresretained. Source/CPUnativeoracleNOTchanged/optimizedcases; noCPUreproductionreplacement.
- NextFIRSTinspectactualcost0/final6/best-effort0PIDs/children+firstformal1orbest-effort0freeze. WhenACTUALfreezefirstappears, recordG/seeds/forecast/planningmodeinPROGRESS+updateplan. Noappmonitor/subagents/push. Fullselected-sourceRelease/nativeAll, source/docsalignment, realreport+actualplots/HTML728+360filter/task/train-test/nooverflow/renderORIGINAL/localcommitstillpendingAFTERtimedjobs. Primary2x+independentqualityUNPROVEN, goalACTIVE. START1792682607/DEADLINE1792769007unchanged.


# Latest state — 2026-10-23 06:11 UTC

- Goalturn PROGRESS/actualbudgetrefusal+newfullnativecostmeasurement+harnessfixes. training17/timing12/prep10 ACTUAL COMPLETE (60G64searches/60nativeTRAININGaudits,60runtimejudge rows,30prep rows); all exited. NewG64C7/source26 oldTRAININGmean0.9878514144230027 retainedseparatelyfromC2=.9807017266880058 and C1024/twoP/G8R1speed1.47113938312. NOformal0freeze/search/freshheldout.
- formal0+final5 ACTUALbudget_insufficient at05:50UTC, PID3496708EXITED. Originalforecast RAW/formal0-runtime-forecast.json unchanged: available23604.3758769s (6.5568h), qualityseed734.7927614s, speedG8cost12358.4800s (search3434.9743+singleCPUreference7604.0968+GPU/construction1319.4089), min10/G8/R2 plan1.35safety+1800floor=7.89h >available. Rule correctlyrefused. Preserveoriginal START1792682607/DEADLINE1792769007/3hreserve/fixed1800/safety1.35/allowedG32/16/8/R2/seeds80/40/20/10, doNOTclaimcostsfit orcreatefakeformal0freeze.
- NEW actualdevelopment-native-cost0 PID3505616LIVE, all30/bothP/oldC7/G8 nativeFULLFINALaudits ONLY, NOsearchrerun/newseed/heldout; originalprotocolsettings/baseline731/candidate26unchanged. Stagepairs baselinefreshCPU+GPU, candidatefreshGPU+EXACTpairedCPUreference; allAST/context/cases/grammar/CPUsettings proofsretained. Nativealloriginalpopulationmembers/cases, exactASTdedup physicalcounts separately. Current10/120auditjobscompleted; laststagehealthy. Allattemptspreserved. LOCALcalibrationcap3000s (notnewoverallclock), cutoff 2026-10-23T06:45:50.141140+00:00; iftimedout/stopped keepfailed/absent, noresume/overwrite. Nativechild mayrunlongCPU1T(e.gIsSorted), verifyactualPID; no competingbuild/tests/heavyanalysis outsidecommonlock.
- Found real auxiliarypolicy mismatch: candidate26 explicitlysets GAGP_FUSED_COLD_EXECUTABLE=0 whilebaselineomitsit; originalstrictCPUsettingsidentitywoulddecline despite bothdisabled. Newfull_population_audit_shared_policy.py pinsliteral0 on BOTH SNAPSHOTS ONLY via explicitnative_audit_neutral_extra. DoesNOTaltersearchcommands, discardCPUsettingsfields orignoreAST/contextproof. cold_admission.hpp onlyenablesliteral1; absent/0disabled. Newtests rejectunknownflags/enabled-overrides, originalhelpersunchanged. ACTUALfirstP1024andP16384 FilterOddnativepairs passedCPUfitness/wrong/errors/timeouts; candidateCPUexecutions0/proofverified. NoCPP/runtime/binarychanged. NoCPUcasecache/oraclereplacementimplemented.
- NEW forecast_measured_native.py usesONLY COMPLETE120currentnativecalls(all30/twoP/bothmethods), includesactualchild+Python/JSONelapsed; rejectsmissing/failed/duplicate/unprovedrefs; retainsconservativeG/8scalingandoriginalsafety/scale/reserves. NEVERreusecalibrationCPUreference/resultsasformaldenominator/audit. Newformal_shared_policy.py body ASTEXACToriginalformal_shared exceptimportnewauxpolicy; freeze_and_run_measured_native.py newv3speedfieldneutral0, originalsremainunchanged.
- NEW final-pipeline6 PID3506007LIVEwaitingcompletecost0+actualH32. Oldfinal5mustNOTrestart. Ifcompletecostgridfitsoriginalrule, createsNEWformal1 once, globalG/R/seeds frozenBEFOREfreshdata, freshallCPU/GPUrefs afterfreeze; ifnofit preservesformal1budget_insufficient/nofreeze. Ifcost0stops, final6stopsandawaitinspection. Noformal1freezeyet. H32ACTUAL176PASS0.120s/PID3506006exited; allnewcost/policy/commandguards passed.
- Fixed actualreportanalysisbug TRANSPARENTLYinNEWreport_validation2.py: originalreport_validation assumed EVERYbenchmeasurewassearch_gpu_overlap and population==fullP, incorrectlyrejecting native snapshot's uniqueP/mode (qualitysnapshotlacksfullP). Newmodeclassifierpreservessearchchecks; snapshotchecksuniqueactualCPU/referencecount, neutral0policy, CPU1/GPU-onlyproof, nativeowner, casecount, binarySHA andonlydeclaredextraoptions. Originalreport_validation unchanged; report_compact.py NEVERrun, nowimports2andrecordsallsourcehashes. Existingnumerical/statistical/plot functions unchanged. ACTUALsnapshot-command-reader0 passed7completedrealC7nativejobcommands/currentpinnedbinarySHA underGPUlock; thisispartialcommandreaderproof, NOTfullreport/131/nativeAll.
- CURRENT/README actual1.471+negativeG64 andbudgetrefusal/nextcoststep updated; actual176helpertests notfullnativeAll. Nextinspectcost0/actualchildren/final6/firstformal1freeze. Onfreeze recordG/seeds/forecastandupdateplan. Ifcostcannotfit, doNOTsilentlylowerverification/seedfloor orclaimcompleteness; meaningfulremainingworkcouldexplicitlyplanBEST-EFFORT full30 minimumG8/R2/10seedgrid BEFOREheldout withallbudget-not-runfailures and disclosedbudget-ruledeviation, prioritizingfreshquality, neverfakeformalqualification. SuchfallbackisNOTauthored/launchedyet, doNOTinferitexists. Primary2xstillUNREACHED. Exactselected-sourcefullRelease/nativeAll +source/docsalign +actualreport/plots/HTML728/360/interactions/renderORIGINAL +localcommitnopushstillpendingAFTERtimedjobs. No subagents/appmonitor/push.


# Latest state — 2026-10-23 05:48 UTC

- Goalturn PROGRESS: TRAINING17 ACTUAL COMPLETE30tasks x2methods G64/P1024/seed0/oldTRAINING19..200cases. All60searches+60nativeTRAININGallchampion/final auditsOK; every30fullfinalAST/descriptors/allgenerationchampion pair exact. NEW selectedC7/source26 lowcase G64 ratio0.9878514144230027 (NOT1.471C1024speed/NOTfreshquality). Pinnedcompleteobservation RAW/long-run-training-observation17-0.json withcomp/selection/all4stageSHA, candidateSHA and60nativeaudits. Earlier16/source21=0.9807017266880058 remainsseparate. DoNOThide/replaceeitherwithfasterC1024matrix. Singledevelopmentseed/run, notsignificantregressionclaim.
- C17controller3502852 terminalrequested_phase_complete/Z (normalcompletedchildawaitingparentreap), NOTactive/restart. A lightpre-noteassertionexpectedstilllive butraceditsnormalcompletion; nofilemutation/nativejobstop occurred, actualterminalstateinspected. timing12PID3502853 ACTUALauditing_old_training, prep10PID3502854 ACTUALold_seed0_preparation/nativechild3505069last_indexofzeroLIVE; final5PID3496708livewaitingthem. Noformal0freeze/heldoutat05:48. NextactualcostcompletionthenfrozenG/seeds/runtimeforecast once; recordfirstfreeze+updateplan.
- ACTUALreportevidence now includesbothnegativeworkloads: originaldevelopment_limits.py restoredEXACTsourceSHA113186b29b6d5e50a80b7355ff37bcf7ca2b2341059b44104c2df7a143a22440 matchingactuallyexecutedreader0/H29; new2importsidenticalcheckedmath andonlylabelsactualcandidate/caserange. Original4testsrestoredbyte-exactH29SHA; newseparatelabeltest, no imported-testduplication. report_compact.py NEVERrun/notformalharness, extendedbeforefirstuse tosaveboth olddevelopmentCSV/JSON andMD+HTMLlimitations; report_views_compact numerical/plot functionsunchanged. Originalfrozenreport.py/mobile/implementationunchanged.
- H30 ACTUAL167PASS0.117s undercommonGPUlock (H29166 remainsoldversionproof). NEW negative-development-reader1 ACTUALOK/PID3505022completed, readsBOTHcompletehash-checked30pair sourcesundergpulock andgets0.9807017266880058/0.9878514144230027; captionsshowcorrectfamily/counts. Thisisactualevidencevalidation, NOTnativeAll/131/newquality. CurrentCPPstilladmitted26/noedits; whole30C7development1.47113938312 and2xtargetUNREACHED.
- CURRENT/README reflect1.471, newG64negative0.988 andactual167helpertests. Finalselected-sourcecleanFULLRelease/nativeAll, source/docsalignment, formalreport/actualplots/HTML728+360/filter/task/train-test/nooverflow/renderORIGINAL, localcommitstillpendingAFTERtimedjobs. Deadline1792769007 and3hreserveunchanged. No subagents/push/newappmonitor.


- 2026-10-23 05:41 UTC: H29 ACTUAL166PASS0.216s, controller3503393EXITED. Fourdevelopment_limits guards pass (completegeomeanrecompute,sourceSHA,timing-ratioidentity,partialtaskrejection); report_views_compact non-dashboard functions still byte-identical to priorimplementation. This isstudyhelperchecks only, NOTnativeAll/131. CURRENT/READMEtop nowcorrectC7complete1.47113938312 (P1k1.21274762237/P16k1.78458489190) andnegativeoldC2G640.9807. TRAINING17actualPID3502852live/noformal0freeze, timing12/prep10waiting; final5liveconditionalchainhealthy. NoCPPchange/currentadmitted26; exactwinnerfullRelease/nativeAll/actualformalquality/reportbrowser/commit stillpending. Originaldeadline/3hreserveunchanged.


- 2026-10-23 05:40 UTC: combined7 ACTUAL COMPLETE120searches/all30/bothP/G8/R1. All60fullAST/descriptors/allgenerationchampion pairs exact, all120search CPU16fitness+wrong0. Means P1024=1.212747622366923, P16384=1.7845848919014693, pooled=1.4711393831195734 (NOT2x/formal/freshquality). C7controller3496706EXITED. final5ACTUALselected26/beb5585d6cd478be304ff37795dd6bf303cdf5460f2221a95f1aaf07708d2c57+globaladjacency; differsfromTRAINING16source21, so NEWtraining17PID3502852,training-timing12PID3502853,preparation-timing10PID3502854 launched automatically. Preserveallold16costs; no formal0freeze/heldoutyet. Next actualtrain/costs then globalG/seeds/runtime freeze once, same originaldeadline3hreserve.
- Reporting preparation: CURRENT/README top updated to completeC2(oldbest) beforeC7finished; updatecurrentmeansfromC7next. report_compact.py has NEVERrun and nowincludes checked negativeTRAINING16supplement (development_limits.py, recomputes30ratios fromfullcomparisons+sourceSHA; newCSV/JSON; views/numericalprimarycodeUNCHANGED). Adds0.981x differentworkload limitationtoactualMD+HTML, separatefromformalwinner. Fourguardtests authored; H29queuedundercommonlock (inspectactualbeforeclaim). Originalreport.py/mobile/implementation remainunchanged. No packedcapacityproductionintegration/newCPPchange.


- 2026-10-23 05:29 UTC: H28 ACTUAL162PASS0.109s, PID3501005EXITED. winner_preset2.py relative-path/nooverwrite guards passed; useNEW2 foractualfinalexport, original1 unchanged. C7P1024ACTUALcomplete30pairs/60searches, allAST/descriptors/champions equal, developmentG8R1mean1.212747622366923. P16384last14pairscomplete+controller3496706LIVE; DO NOT poolpartial withP1k oroldC2P16. final5waitingcorrectedfull30, cost11ACTUALcomplete, prep9complete, TRAINING16completeoldG64. No formal0freeze yet. Packedcapacityprototype1 remains COPYONLY/notadmitted/NOsearchgain; mainproductionCPP26unchanged and3hreport/originaldeadlinepreserved. Closeoutsource-validator environment readiness inspected only: PythonTarFile.extractall supportsfilter, gittracked symlinksnone; this isNOTnativeAllregression. Source/fulltests/report actual browser/commit still pending.
# Latest state — 2026-10-23 05:19 UTC

- PROGRESS: admitted26 adjacency fixed8/bothP/G8 coarse COMPLETE48searches, all32relativecells/16pairs fidelityPASS/CPU16; ordinary26OFFvs24OFF0.9993913561995565,26ONvs24OFF1.0408693907849849 (about4.1%). Frozen>=2%vsBOTHcontrols PASS; admitted_generations64 is enabled-route count, NOT constructor-hit count. Actualprivate-adjacent1requested_phase_complete; combined7PID3496706 nowNEWfull30P1024 withfresh731controls, thenP16384. No selected whole30gain claimeduntil60cellscomplete. Best complete stillC2pooled1.44359227657; G64oldTRAINING16P1024ratio0.980701726688 separatelyreportednegative regime.
- NEW oldC2all30/bothP replay timing diagnosis RAW/replay-opportunity0 ACTUALOK, PID3499326EXITED. Hypothetically removing ALL replay-event duration gives scenario factors P10241.16546540677/P163841.12676620586/pooled1.14595245733 additional, NOTmeasuredspeed or rigorousbound. Largest eventfractions ReplaceSpaceNewline~.636/DoubleLetters~.615 (stringtasks), FilterOddP1024~.427. Neveradd GPUevents to enclosingwall/APIwait. This narrow replay path alone doesNOTdemonstrate2xwholematrix.
- NEW packed-capacity COPY-ONLY prototype1 ACTUALPASS/results[0,0], PID3499656EXITED:380160CPU/GPUsemantic-replay traces, allinitialactualphysicalcursors<=capacity, unlimitedlogicalbudgetdeclines, finalsemanticlogical/error/immutableprefixtruth matchesCPUafterreplay. Positiveappend64/cap64: baseline+adjacent128overflows each, packed0, actual64/reserved+charged2143. Syntheticboundaryoverflows73600/73600/63424, NOTrealbenchmarkreplayfraction/speed. CPPproductionNOTmutated; CURRENTADMITTEDSOURCEstill26. No packed-capacity flag inproduction/no main-source integration/no actualsearchtiming/quality forpacked, so neveradvertise it asselected optimization.
- Prototype0 ACTUALfailed [0,11] retainedfullsource/binary/logs; newprototype1differentcopiedoverlay corrects actual-copycapacity check AFTER no-write adjacencyshortcut and conservatively keepsmin(savedordinary,currentphysical) onscalarrewind. This letsphysicalreservation staywithinactualcapacity whilelogicalcharge stayssame. New1doesnot silentlyretry/overwrite0. Original26runtimeordinary-overflowcontract remainsunchanged. Anyfutureintegrationneedsfreshactualnative+globalcoarse+full30 andbudgetreview BEFOREformal0freeze; do not delay/alterselectedformalbasedonheldout. Current7/oldcostpipelinecontinues.
- Found/fixed a deliveryhelperrelative-path defect inNEW winner_preset2.py: resolvefreeze/outputpaths before recordingrepository-relativeorigin. Originalwinner_preset.py remainsfrozenunchanged; run_winner format remainscompatible. Two realpath/nooverwrite guards authored; H28PID3501005runsunderGPUlock (inspectactualstatus/log beforeclaim). USE winner_preset2.py forfinalexport afterREALfreeze. H27actual160/H26154/H25147remainvalid; no freshfullnativeAll/131claim.
- Finalpreferredreportreport_compact.py/projectiondashboard_data (H27guarded), includesallCSVunchanged and onlyvisibleaggregatesinHTML. StillACTUALfinalplots+728/360/taskfilter/taskselect/train-test/nooverflow/html_renderORIGINALrequired. Currentformal5conditionaltrainingreusepipeline unchanged; exact latestadmittedsource26 neededforfinalalignment. Originaldeadline1792769007/START1792682607/3hreserve unchanged. GoalACTIVE2x+independentqualityUNPROVEN. No appmonitor/subagents/push.


- 2026-10-23 04:56 UTC: TRAINING16 ACTUAL COMPLETE30/30tasks x2methods G64/P1024/seed0 onoldTRAINING cases19/21/30/79/81/200. All30 fullfinalAST/descriptors/allgenerationchampion pairs exact; all60nativechampion/final TRAINING auditsOK. Observed development G64geomean0.9807017266880058x (slightly SLOWER), NOT1.4436x/C1024matrix. Preserve/reportnegative regime; single-run variation not a significant regression claim. Evidence RAW/long-run-training-observation16-0.json. Timing11 nowauditing_old_training/prep9old_seed0_preparation; no newheldout, no test-guidedselection. Source26coarse1continues commonlock.
# Latest state — 2026-10-23 04:52 UTC

- PROGRESS: actual26 full Release targeted native15/15 PASS353.17s, results[0,0,0], phase-population296.24s with BOTH declinedSoA and admittedcontiguous ON/OFF across32gen/CPUchecks; newprimitive253440traces/64physical2143reserved witness alsoPASS. Native25 prior14/15failure remains preserved; actual runtime benchmark26 SHA beb5585d6cd478be304ff37795dd6bf303cdf5460f2221a95f1aaf07708d2c57 EXACTLY equals preservedfailed25 benchmark (same_benchmark_as_failed25=True). Correction was TESTONLY, production code byte identity proved, not assumed.26 now LATEST ADMITTED CURRENT CPP source map; use build-attempt26 as prior for finalalign. No fresh fullnativeAll/131 claim.
- Admission1 RAW/private-adjacent-admission1 ACTUALOK: complete26sourcehashes/pinnedbinary/independentlypassedoverlayexactidentity/control24/native15/staticcuobjdump resources verified, PID3496704EXITED. Nativecandidate1 PID3496703EXITEDok. Gate1 PID3496705 ACTUALcoarse/fresh24OFF/26OFF/26ON, last04:51 countsbaseline2/ordinary1/adjacent1OK; no wholemean or2xclaimfrompartials. Combined7PID3496706waitingcoarse; training16andcost11/prep9continue; final5PID3496708waitscomplete7 then conditionalexact-recipe trainingreuse. Noformal0freeze/freshheldout yet.
- H27 ACTUAL160PASS0.112s, PID3497926EXITEDok. PreviousH26154/H25147 valid. Added dashboard_data.py/report_views_compact.py/report_compact.py, preserving ALL originalfrozenreport files. HTML onlyprojects fields theexistingUIuses: speedcell/status and60 task/method countrows withJOINT/TRAIN/TEST/gen0/final/new/lost/audited/expected. Full per-seed rows remain unchanged inCSV/summary; numerical/statistical report functions+plots unchanged (guarded). Unknown/failed runs remain expected but never counted audited/successful. This avoids embedding4800+fullprovenance records into HTML and allows actualT3preview/render without truncating a multimegabytepage. Sixnewguardtests actualPASS aspartH27, NOT finalbrowservalidation.
- PREFERRED FINAL REPORT ENTRY NOW report_compact.py (sameCLI/reportcore, actualmodule+dashboard_data hashes recorded); still must renderACTUALformaldata, inspectplots+HTML728/360/filter/taskselect/train-test/nooverflow, html_renderORIGINALfinal. Do NOT use syntheticUI toclaimfinalchecks. Originalreport_implementation/mobile remainpreserved. Winnerpreset/runhelper and source/doc alignment guards passedH27 but actualfinalexport/align notrun.
- CurrentbestWHOLE30developmentstillC2 pooled1.443592276569684 / P10241.19197663135 / P163841.74832174236, all60AST/champions/CPU16agree, G8/R1oldTRAININGonly. Goal2x/freshqualityunproven; ACTIVE. START1792682607/DEADLINE1792769007/3hreportreserve unchanged. No otherbuild/test/heavyanalysis outsideGPUlock, no newappmonitor/subagents/push.

# Latest state — 2026-10-23 04:46 UTC

- PROGRESS: combined2 ACTUAL COMPLETE all30/bothP/G8/R1 =120searches, all60finalAST/descriptors/champions equal/CPU16pass. NEW best complete development means P1024=1.1919766313499849, P16384=1.7483217423578479, pooled=1.443592276569684 (previousC1=1.3899). Still NOT2x/formal/freshquality. Original START1792682607/DEADLINE1792769007/3hreserve unchanged; no appmonitor/subagents/push. ALL commands support worktree; heavy jobs GPUflock.
- ACTUAL25 FAILED native14/15 in57.02s, results[0,0,8]; not admitted. New primitive extended-payload test PASSED (all253440CPUvector traces +physical64/reserved2143 append/prepend witness executed); phase-population failed0.30s at newly added combined assertion. Source inspection proves fixture main globallysets PRIVATE_CELL_SOA=1, while new contiguous-only route MUSTdecline; assertion wrongly demandedenabled. Failed25 fullsnapshot/logs retained. Do not dismiss potential scoring differences until new26 actually passes. Actualfailed benchmark and phase-test binaries preserved RAW/failed25-artifacts0 withSHA; no timed25samples existed; private-adjacent0 gate andcombined6 correctly skip failedcomponent.
- NEW26 ONLY test correction, NO production CPP change: controller3496703, build-attempt26. Test separately checks existing-layoutOFF/ON scores+expectedSoAdecline, then forcescontiguousOFF/ON scores+enabledprofile, restoresbothflags. All32generation fixtures retainCPU/origin/ASTchecks. Worker verifies everyotherCPP SHA equalsfailed25; builds/tests/pins undercommonlock. Lastobserved04:44:38 build26results[0,0],9/15PASS, phase-populationACTIVErunning; not yetadmitted. If26passes, useACTUAL26source map aspriorADMITTEDforfinalalignment; iffails preserve source26 and manuallyrestoreverified24 beforealignment. NEVERclaim25admitted/current24mapmatches26.
- H25 ACTUAL147PASS0.080s; H26 ACTUAL154PASS0.092s (adds7newcorrected-recipe selection guards). Both exited. No fresh fullnativeAll/131 claim. Currentnewformalcore/code/reportstatistics unchanged. New154 covers conditional helper module compile and newselector, not native26 completion.
- IMPORTANTACTIVEexisting TRAINING16 PID3492294 was already running; preserved, no stop/restart/overwrite. training-timing11/preparation-timing9 continue. Old final-pipeline4 PID3492297 ONLY verifiedidle/nochildren/nofreeze, intentionallyterminated; recordRAW/queued-final5-replacement0.json. New final-pipeline5 PID3496708 waitscomplete7, then compares actual selectedbinarySHA+globalextra toTRAINING16. IfIDENTICAL, uses EXISTING16/timing11/prep9 directly as oldTRAININGcost evidence (no clone/remap, noformaldenominatorreuse). Ifnew7wins, launchesNEW training17/timing12/prep10 withnativeaudits. Realfreezeformal0 remainsonce/preheldout/fromactualcosts; oldfinal4 MUSTNOTrestart.
- NEW gate1 PID3496705 waitsactualnative26, then NEW48search24OFF/26OFF/26ON fixed8/bothP/G8/seed0; same frozen>=2%vsBOTHcontrols+exactAST/champions/CPU16rule. Combined7 PID3496706 waits6+gate1, measuresfreshall30/bothP/731controls ONLYifadmitted; oldbestremainsfallback. Provenance/staticresource admission1 PID3496704 waits26. Newfiles private_adjacent_pilot1,private_adjacent_pipeline1,combined7_pilot,combined_pipeline7,best_selection_private_adjacent1_recipe,training_best_private_adjacent1_recipe,final_pipeline5. Alloriginallaunchedsourcespreserved. No manual candidatefreeze or hiddenrerun.
- Execution-identity-diagnosis1 ACTUALOK, old8/P1024only, no newsearch/seed/heldout. ExactASTduplicates initial/final: Sum22/27,IsSorted3/8,FilterOdd48/542,Median13/609,CountOdds26/168,HouseRobber97/42,DoubleLetters70/167,Grade71/294. Allinitial/final ASTJSONoriginkeycounts0: previousinitialduplicatecomparisonALREADYexcludedgrammaroriginannotations. Countfactors assumeequalprogramcost and areNOTmeasuredspeed/runtimebounds. No cacheequality relaxed; origin affectscontracts/sites, execution-equivalence stillneedscompletecompiler/context/fuelproof. Analysis0 wrongschema discoveredbeforelockacquired, verified0rows/nativejobs intentionallyreplaced; failure+correctionrecords preserved.
- Requiredcloseoutstill: timedjobs/childrenexit -> validate_selected_source exactwinner FULLRelease/nativeAll -> align_selected_source withlatestactualADMITTEDprior -> align_selected_docs (8capturedblocks/conditionaladjacentnav; preservesunrelated) -> smalltools/repositorychecks -> report_implementation.py(newconditionalmechanismviews; unchangednumericcore) + actualwinner_preset export/run_winner -> inspectACTUALplots/HTML728+360/taskfilter/taskselect/train-test/nooverflow ->html_renderORIGINALfinal ->inspectdiff/untracked/affecteddocs/localcommitNOPUSH. Existinginterimdevelopment-preview1 remainsINTERIM/stale1.327 andnotfinal. Noformal0freeze/freshqualityatlastobservation; goalACTIVE/unachieved.


- 2026-10-23 04:37 UTC: next narrow opportunity investigated WITHOUT production mutation: compile_population passes nodes/constants/slot context/fuel into compile_phase_tree, NOT grammar origins; current score cache additionally compares origins. This is NOT yet execution-equivalence proof and no cache condition was relaxed. Existing initial duplicate stats8/P1024 are3..97 duplicate members, from complete prepared AST JSON. New execution-identity-diagnosis1 PID3495268 waits COMMONLOCK to count exact full-AST initial/finalG8 duplication and explicit origin-field presence on SAME old8TRAINING/1024; no new search/seed/heldout. Any N/unique factor is PROGRAM-COUNT reduction under equal-cost assumption ONLY, never measured speed or runtime upper bound (program costs vary).
- The new analysis0 wrongly assumed export root list; bounded actual prefix proved object/programs-array BEFORE it obtained GPUlock/read inputs. Original0 source/record preserved; pidfd verifiedblocked-on-flock+nochildren+0rows, intentionally terminated. Cancellation evidence execution-identity-diagnosis0-cancellation.json ACTUALok, no native job/rerun. New1 correct schema is a NEW attempt. One failed read-only cancellation precheck inspected wrong /proc/locks column, made NO mutation; corrected by matching inode across fields and verified actual build3492026 owner and analysis waiter before stop. Do not classify old0 as failed native experiment.

- 2026-10-23 04:25 UTC: inspected actual build cache: Release/CUDA86/benchmarksON/CUDAON/originalASGPsource+extensionON, matching contract. Fixed actual newly authored, NOT-yet-used run_winner.py failure receipt gap: spawn OSError now writes a failed first-attempt receipt/log and returns127, no retry; nonzero native outputs preserved and reexecution refused. Two new guards plus4-space formatting of new unstarted tests, before H25 acquired source snapshot (ACTUALwaiting/no source_sha256). No frozen launched harness changed; no native25 claim.

- 2026-10-23 04:23 UTC: current goal turn PROGRESS/verified-wait: exact ptxas3492275 EXITED via pidfd (not timeout/restart); actual25 libgagp_gpu archive and evolution plus extended-payload new fixture compiled. Last large bounded-region-results target still compiling under SAME build child3492082, no duplicate. Native25 tests and H25 not yet run/PASS, no performance claim. Added explicit light provenance observation RAW/private-adjacent-overlay-provenance0.json: all3 actually passed prototype sources unchanged; CURRENT25 builtins SHA EXACTLY equals passed copied-overlay SHAad83c77e983abf83ad31c91b95d4fe5e26db0e02832753593d70896c9e25bd58. This is source byte identity, not a native test. Goal2x/freshqualityunproven, deadline/reserve unchanged.

- 2026-10-23 04:16 UTC: prepared align_selected_docs.py +4guards and maintained-doc-blocks.json (8 exact study-owned blocks). NOT RUN/no maintained-doc mutation yet. After exact validated CPP promotion, removes only captured docs for flags absent from selected production source; retains disabled but implemented optional routes; adds adjacent contract/navigation only if its flag actually exists. Backs up current docs and rejects changed owned text/source. Preserves all unrelated text. Required final sequence: validate_selected_source -> align_selected_source using latest ADMITTED prior25 ifpass -> align_selected_docs -> small tools/repository checks -> report_implementation.py/winner_preset export/actual browser -> diff/commit. H25pending, no new pass orspeedclaim.

- 2026-10-23 04:11 UTC: final report version report_implementation.py/report_views_implementation.py adds only conditional adjacent-storage mechanism explanation; original report_mobile.py and numerical/statistical functions unchanged. Four new guards check disabled flags stay unadvertised and source equivalence. H25 pending; use this NEW version for actual final report, plus separate winner_preset export after real freeze. No formal artifacts or browser claim yet.

- 2026-10-23 04:10 UTC: authored winner_preset.py/run_winner.py plus6guards while native25 builds. Preset export requires REAL formal speedfreeze/full30/bothP/R2/samebinary/settings/16GiB; not exported or run yet. New invocation clears ambientGAGP, checksSHA/inputs/commonlock, refusesoverwrite and recordsoneattempt+receipt. No new benchmark/heldout use or speed/quality claim. H25 waiting lock includes new sources/tests when it starts.
# Latest state — 2026-10-23 04:06 UTC

- Goal ACTIVE, whole30 extra2x NOT achieved. Original START1792682607 / DEADLINE1792769007, 3h report reserve unchanged. No new monitor/subagents/push. ALL commands cwd support-20261015. All native/build/test/heavy work share GPU flock.
- NEW COMPLETE combined1: 120 searches = all30 x bothP x baseline/candidate, G8/R1 oldTRAINING. All60 pairs full final AST/descriptors/generation champions equal and CPU16 checks pass. Means P1024=1.1791333590506516, P16384=1.6383533646164903, pooled=1.3899054306434593. This supersedes earlier COMPLETE19pool_shape1.32719 only as development best; independent quality/formal2x still unproven. combined2 PID3473236 still all30P16384, last observed27completepairs+onecandidateLIVE; no partial pooled claim. combined3/4/5 rejected tails remain preserved, not duplicated.
- NEW independent private-append-prototype0 ACTUAL OK/results[0,0,0,0], PID3491429 EXITED. Reference126720 plus candidateOFF/ON253440 =380160 CPU/GPU exact traces, immutable prefixes/rewind/external allocations/divergent fillers/wideInt64/capacity and logical-overflow boundaries. Synthetic constructor kernel OFF0.579586ms/ON0.215552ms (2.68884x), NOT GAGP/evolution/search gain. Production source24 was unchanged during that prototype.
- NEW candidate25 integration now IN BUILD, NOT admitted yet: controller3492026, raw build-attempt25. Copies EXACT actually probed builtins overlay; new opt-in GAGP_GPU_PRIVATE_ADJACENT_CONCAT, only compact/private/non-SoA admitted. Preserves ordinary reservation/logical-charge/overflow and fuel/RNG; separate physical cursor reuses contiguous immutable spans. Host clears external private bits; whole-program capability excludes memoized list states. State flag propagated in VM and cache scorer. New native fixture asserts append+recursiveprepend physical64 vs reserved/logical2143, all old immutable prefixes; 253440 independent model traces. Existing32generation private owner fixture now compares OFF/ON full fitness/wrong/case counts plus existing nativeCPU auditing. Source snapshot frozen in build25; DO NOT edit CPP during active build/test.
- NEW actual queue replacement queued-final-chain16-replacement0 statusOK. Prior15 queues were pidfd-parked/rechecked idle/nochildren/zero-nativejobs/nofreeze, intentionally terminated, records preserved. Metadata-only replacement does not acquire or compete with native GPU timing; build remains under GPU lock. No original native job killed. Successors: harness25 PID3492291 (waiting lock); private-adjacent0 fixed8/bothP/24OFF/25OFF/25ON gate PID3492292; combined6 freshall30 gate PID3492293; training16 PID3492294; training-timing11 PID3492295; preparation-timing9 PID3492296; final-pipeline4 PID3492297. All LIVE/waiting. Older formal-pipeline3/training15 must NEVER restart.
- Coarse rule fixed BEFORE new measurements: >=2% global vs BOTH24OFF and25OFF plus exact final AST/descriptors/champions/CPU16. If native/coarse rejected, keep prior valid complete recipes; failures preserved. Only enabled candidate runs NEW complete60cells/fresh731 denominator on best complete prior global recipe. New immutable selector best_selection_private_adjacent_recipe, training_best_private_adjacent_recipe, combined6_pilot; original launched modules unchanged. Training16 waits actual harness25 pass. Final pipeline4 includes combined6/timing11/prep9 and all old prerequisites, then real formal0 freezes exactly once from oldTRAINING/runtime. No formal/freshqualityfreeze yet.
- Last ADMITTED production source map is24 SHA53ffc6f3114549d737a6ff0d0298ba98b9140b305be2371279f27a156ded8286, but CURRENT CPP is UNADMITTED25 under build. If25 passes, use its actual map as prior admitted for final alignment. If25 fails, preserve all failure snapshots and manually restore verified24 map before alignment; do not bypass source guard. No new native pass claim until actual results. H24previous120PASS remains historical; H25pending.
- Pending: inspect build25/check logs, fix only actual bugs in new attempts, inspect coarse/full30 results, then long-run training/nativecost/freeze/freshvalidation. Do not add further broad tuning ahead of quality/report reserves. Formal report_mobile.py uses numerically identical original report core and corrected mobile views, still must inspect ACTUAL final plots/728+360 and train-test interactions, html_render ORIGINAL final page. Interim development-preview1 is not final. Exact selected-source full Release/nativeAll + small repo/tools checks and local commit still pending.


- 2026-10-23 03:16 UTC：geometry粗測208/208完成，維持plain0／poolOFF；個別尚未啟動的全題尾段依已凍結gate取消，資料保留，非執行失敗。combined1已完成P1024的60次，P16384進行中；等待全域完整資料再選版本。H24研究檢查120PASS；開發CSV／PNG+PDF／互動頁已用實際stage1資料產生，728／360互動與無橫向溢出通過，屬interim而非正式／品質結論。新版正式mobile報告僅CSS與來源雜湊路徑變更，分析判準與原版一致已核對；正式頁面仍待真實資料驗證。2×未達，正式freeze與新seeds品質未開始；原截止時間不變。


- 2026-10-23 02:52 UTC：精確編輯距離／串列交錯排列五組粗測完成80/80（8題、兩種P、G8），完整AST／分數描述／逐代冠軍一致；短評分相對同版OFF僅1.00636×，交錯排列變慢，依凍結門檻拒絕兩項，不追加其全題尾段。當前CPP24已通過15/15針對性native（321.38s），H23研究檢查112PASS；不得當作freshfull131。既有全30開發最佳仍1.327189×（P1024 1.094712×／P16384 1.609036×），未達2×。幾何設定／既有組合對照仍執行，training15→timing10／prep8→final3等待實測全域選擇，尚無正式freeze／新資料品質。新開發報告讀取器實際重核stage1四種候選240格成功，保留缺失與失敗且不宣稱正式速度／品質通過。原截止時間不變。

# Latest state — 2026-10-23 02:06 UTC

- This goal turn is PROGRESS: actualMyersprototype22242CPU/GPUcomparisonsPASS and6.89912xsyntheticKERNELdiagnostic; source23actual15nativePASS+distinct96laneprobePASS; newexactscoringCPP24applied/building, joint5armcomparison/globalcombined5/final15chainstaged. GoalACTIVE/whole30extra2xUNREACHED. START1792682607/DEADLINE1792769007unchanged,3hreportreserve. No subagents/push/newmonitor/formalfreeze/freshheldout. AllheavyjobsGPUlock.
- CPP CURRENT24, NOT23. short-edit-candidate0 PID3483367 verified23source/admission/lane/mathproofcontrollersallEXITED+OK, exactnormalizedmathfunctionmatchesactuallytestedprototype, thenappliedprototypes/apply_short_edit.py underlock. build-attempt24 CURRENTcommand1/results[0],cmakechild3483394live; NO24nativePASS/binary/speedclaimyet. DoNOTeditCPPduringbuild/admission. Buildsame15targetscopeas23plusactualnewtestsinsidetargets, NOTfullnative/fresh131.
- CPP24 GAGP_GPU_SHORT_EDIT_DISTANCE: privateIntListLevenshteinonly; per-original-case session-owned sortedFULLInt64key/uint64positionmasks preparedfromSAMEdecodedhostpayloadcache,rawexpectedtag/i/b+decodedlength/rangechecks; internal-markeranswerbtrueDECLINESdictionary. Globalalignment, no byteconversion/quantization/truncation; m<=64wordupdate, longeroriginalDP, empty/equalexactshortcutsAFTERoldmaxlength+penaltyguards. Hostprep/uploadchargedinit/evolvewall;immutableanswermetadataoutsidepayloadworkspace,cap16GiBunchanged. Initinvalidatespriorfitnessrevision,clears/rebuildsmasks; originalcaseindexhonoredwithcaseorders. Othermetrics/nonprivateunchanged,sourceRNG/operators/GPUrepro/fuel/VMerrorsunchanged.
- Newnative24tests: private_short_edit_checks.cuh publicGPUAPIagainstnativeCPU+OFFcontrol,1024mixedcases/length0/1/2/31/32/33/63/64/65/fullInt64, capped+discounted/penalty3.5+1000/sortOFF+ON/reinitwithreversedcases. Extra32-generationprivateownerLevenshteinfixtureCPUauditsusingexistingallfreshphase/typed/window/stage guards. Actualsource23not24:15/15PASS319.04s, SHAaf4871c900e19f815b56fe160611bd96d946e9935888e08b84819dcc64ad1581; cell-soa-admission0OK,pinned/source/resourcesverified, laneprobePASS96distinctlanesx2layouts. No23searchspeedconclusion.
- Myersactualproof RAW/myers64-prototype0 statusOK[0,0],22242pairs exhaustivebinary0..6+6000randomwideInt64+word/empty/globalboundaries/textup2048, independenthostrow/hostword/GPUword/GPUrowallmatch. SyntheticCUDAevents20repeatsbit11.7763ms,row81.2462ms,ratio6.89912; EXCLUDESpatternprep/upload/VM/evolution, neverheadlineorASGPratio. PrimaryMyers1999 doi10.1145/316542.316550. Noedlibruntimedependency.
- TRANSPARENTharnessfailure: cell-soa0 PID3480484STOPPEDafterFIRSTbaseline22nativeprocessreturned0/CPU16fitness+wrong0, butparserreadNONEXISTENTrootexecution_profile(actualgenerations[*].execution_profile). Originalinvalid_outputrecord/raw/export/historykeptunchanged, no nativererun. cell-soa0-parser-failure0.json recordsactualhashes/return0/error/no reruns. combined4willrecordrejectedcomponent/run_fullFalseaftercombined3. Thisisnot23native/ownershipfailure. DoNOTresume/editoldSoA0frozenharnessorcorrectitsfailedrowinplace.
- NEW short-edit-admission0 PID3483587 waitsactual24native/source, thenstaticresourcesandcontrol23pinnedproof; NEW short-edit0 PID3483589 waits24admission+H22. Joint80searches=fixed8oldTRAININGtasks/bothP/G8/seed0, NEW23OFF/24OFF/24short/24SoA/24short+SoA,shape/poolON/block1. NewhelperreadsPER-GENprofiles;oldparserbugregressiontestadded. Oneglobalbestof3activearmsrequires>=2%vsBOTH23OFFand24OFF; exactAST/descs/champs/CPU16/activeapplicablegenerationsmandatory. Fresh23controlsareDIFFERENTbinaryfromoldfailed22attempt, nohiddenrerun/historicaldenominatorreuse.
- NEW combined5 PID3483745 waitscomplete/exitedshort-edit0+combined4. Failed24native/coarseorbenefitrejection->explicitno-newfull30,failedcomponentpreserved, validolderrecipesremain. Ifjointwins, composeONLYits2newglobalflagsontoBESTcomplete60-cellpriorrecipe,immutable24,NEWfull30bothP/fresh731controls. best_selection_short_edit_recipe.py allows24onlycomplete60/fidelity/settings/binaryhashchecked;7newguards. No unmeasuredflagsoverridingolderfallbacks. H22 PID3483588WAITINGlock, expected103checks(91+5parser/settings+7recipeguards);readACTUALcount/statuslater, NOTPASSyet. H21actual91PASS0.053s; H20actual84historical, nofreshfullsuiteclaim.
- ACTUAL final14replacementcomplete: queued-final-chain14-replacement0 statusOK; oldtraining13/timing8/prep6/final1EXITED/stoppedintentional0samples, alloriginalproofsretained. Activewaitingtraining14PID3482932,timing9PID3482933,prep7PID3482934,final2PID3482935. NEWqueued-final-chain15-replacement0 PID3483746WAITINGH22, NOTexecutedyet. Itverifiesold14idle/nochildren/0nativejobs/nofreezeunderGPUlock, preservesoriginals, terminatesonlyidlequeues, launchestraining15(best_selection_short_edit_recipe,priorcombined5,knownadmit22butactualselectedownbinary),timing10/prep8/final3. FIRSTnextturninspectactualreplacement/newPIDs;neverduplicate. Ifold14alreadystarted,preservejobsandinspect,donotforcecancel.
- final3explicitwaitscombined5/H22/training15costs, allowsrejectedcellSoA0/shortedit0controllersEXITasfailureswithoutclaimingPASS; freezeusescompletecombined5decision. OnlyonefinalG64TRAINING/timing/prep/globalfreezechain intended. Source24nativefailuredoesNOTforce24selection; fallbackownbinary/settingsstayexact. Onfirstformal0freezeverifyactualG/N/forecastandupdatePROGRESS+UIplanbeforefreshdata. Noformal0filesyetatlastread.
- Lastgeometrycoarse152/208OK,controller3463600healthyWAITINGlockwhile24buildactive; completewinnerNOTready. Existingcombined1/2/3waitingglobalgeometry;block1retained,parentcachecoarseON. Source24buildserializedbetweenjobs,noactivesampleinterrupted. Full30bestACTUALCOMPLETE remains19pool_shape1.327189036pooled, P10241.094711502/P163841.609036476,G8R1oldTRAINING. New6.9microisNOTwhole30claim.
- Dryrun24guardcaughtambiguousd_shape_representativesfreeanchor(2matches)BEFORECPPmutation; explicitshort-edit-dry-run0.json storesoldscriptSHA/error/0mutations/0samples, fixedtoinituploadtimestampcontext; seconddryrunpassed, no scientificattemptrepeated. Allactual24source/frozenapplyinputsnowmustremainunchanged.
- Reportrecipe_rowsdescribesSoAandshorteditONLYifactualfrozenflags1,privateLevenshteinscope/longDP/chargedinitexplicit; docs/design/payload.md +gagp-navigationupdatedforactualCPP24interfaces. ThesePython/doceditsmadeBEFOREH22executes; H22willcapturethem. Selected-sourcevalidationfullcleanbuildtimeout3600(originaldeadline+2hreportreserveunchanged)remainsAUTHORED/notexecuted. LateralignactualsupportCPPtoEXACTmeasuredselectedsource,usingCURRENTADMITTED24mapif24passes,otherwisepreservefailedsnapshotandmanualrestore; noold22assumption. Presentationsuntouched. Finalactualquality/fullsourcechecks/plots/T3preview728/360/render/localcommitpending.

---

# Latest state — 2026-10-23 01:31 UTC

- This goal turn is PROGRESS / verifiedlivebuildwait: independent exact Myer64 CPU+GPU prototype authored and queued, reportrecipe includesactualSoAflagonly, source23mainfitnessptxascompleted andlastnativeGPUtargetcompiling. Goal ACTIVE, whole30additional2xUNREACHED. START1792682607/DEADLINE1792769007unchanged,3hreportreserve. Noformal/newheldout, no subagents/push/newmonitor; allheavyjobsusecommonlock.
- CPP STILL23; no newCPPchanges thisturn. Build23 results[0], currentcommand1,lasttargetgagp_test_bounded_region_gpu_results compiled withactualptxasleaf3481842R (previousfitnessptxas3480491exitednormallyintocompletedlib/targetbuild). Cmakechild3480329/controller3480301live. Nativecheck2logabsent atlastread: NO15PASS or23binaryadmission/speedclaim. Do noteditCPPduringbuild/sourceadmission/distinct-laneprobe.
- H21 PID3480568stillWAITINGlock; replacement14 PID3480569stillWAITINGchecks; oldtraining13/final1queuesnotyetreplaced atlastread. SoAadmission3480483/coarse3480484/combined43480567/laneprobe3480811waiting. Firstnextactioninspectactualbuild/native/H21/replacement/new14PIDs/probeoutcomes, no duplicate/resume. Source23coarse positiveactivation/fidelity/globalbenefit gates unchanged. The distinct-laneprobe remainsanexplicitrootgatebefore23finaleligibility; frozencombined4/final2doNOTrequirethislaterprobe themselves.
- NEW myers64-prototype0 PID3481943 WAITINGsharedlock. Sourceprototypes/myers64.hpp +myers64_probe.cu, runner myers64_probe0.py. Independent one-word GLOBAL editdistance, patterns0..64, FULLsignedInt64dictionarykeys/masks, lastbit64uses1<<63, positiveglobalboundary1, unsupportedlength65declines. No truncation/substringmatch/bytealphabetcoercion. Intendedactualcheck22242pairs: allbinarysequences0..6 crossproduct16129,6000random/fullInt64 cases,112long/wordboundarycases(textup2048),1global-vs-substringcase. Compareindependenthostrowreference,hostbitvector,GPUbitvector,GPUrowrecurrence. CUDAevents20repeatsaresyntheticKERNELDIAGNOSTICS ONLY; exclusionspreparation/upload/VM/evolutionexplicit. NOTproductionimplementation/nativePASS/formalratio. Confirmactualstatus/outputbeforeanyclaim. Allattemptsimmutable/newnameonfailure.
- Researched PRIMARYalgorithm source MyersJACM1999 doi10.1145/316542.316550 / gersteinlab.org/courses/452/09-spring/pdf/Myers.pdf; officialMartinsos/edlib master sourceusedonlytoverifypositiveglobalboundaryvsfree-substringboundary. Ownprototype/formulanames; noedlibruntimedependency. Finalmethodexplanationsshouldciteprimarypaperifadopted. No tasktestlabelsread: onlyoldtrainfilebytecounts/splitmetadata inspected, notquality.pooltestcontents.
- NEXTmajorcandidateIFmathGPUprototypeactuallyPASS/diagnosticwarrants: exact privateIntListLevenshtein short-pattern path (m<=64, otherwise ORIGINALfullDPfallback), plus exactequal/emptyfastreturns afterexistingmaxlength/penaltyguards. Preserveactualfitness/wrong/fuel/errors/RNG/populations. No24CPP/applier/integrationauthored yet. Need new24admission/source/binary/pairedOFFcontrols andwhole30beforefinaleligibility, nevercallprototypekernelratiosearchspeedup.
- Proposed24contracttoimplementonlyafterproof: per-session/preparedTRAINING-case fullInt64patternmasks fromalreadydecodedimmutablehost_list_payloads, sortedkeys+uint64mask; rawexpectedValue+length identityboundtoeachcase/sessionrevision. Chargeallhostpreparation/uploadtocurrentinit/evolvewall. DRegionWorkspacecanpassconstpattern/entrypointers andcount; d_execute_bytecode_implknowsoriginalinputlocal_case evenwithcase_order/repeatedcases, so choosesmatchingdescriptor andpassesto d_score_resident. Leave generic/nonprivate/othermetrics/longpatterns unchanged; matchfullrawexpectedtag/i/b+length+boundsbeforefastpath. Ownstate/proofnotpublicper-programmetadata. Reinitmustinvalidate/clearcachedpatterns, includeCPU/nativechecksforcasepermutation/repeatedlabels/newinit/widevalues/0/63/64/65/capped+discounted/fallbacks. ImmutablepatternGPUbytesareinputmetadata,16GiBworkspacecapunchanged; accountinitcost/wholecardVRAM. Do not changeCPUreproductionorbenchmarkdenominator.
- Currentpipeline14onlywaitscombined4/SoA; itdoesNOTknowfutureMyerscandidate. IF24isactuallyworthglobaltest, beforefirstformal/freshdatafreeze safelyreconcileONLYverifiedidle/nochild/0samplequeuesusingNEWnamesandpreserveallattempts. Iftraining14alreadystarted,preserveit; no hiddenreplacement/rerun. Avoidendlessmicrotuningafterfailedthreshold; mainremainingcostisevaluation. BestCOMPLETEactualdevelopmentstill19pool_shape1.327189036pooled, P10241.094711502/P163841.609036476, G8R1 oldTRAINING; no2x/newquality.
- Reportviewsrecipe_rowsnowlistsGAGP_GPU_PRIVATE_CELL_SOAonlyifACTUALfrozencandidateflag1; noopforolderfallback. ThisdisplayeditmadeBEFOREH21starts, nofrozenharnesschanged. H20actual84PASSremainslatest; H21expected91mustbereadactual. Actualformalreport/plots/T3preview728/360/render/sourcefullregression/localsourcealignment+commitallpending.

- Source-validation harness correction BEFOREH21starts: fullclean Releasebuild command nowallows3600s insteaddefault1800s, whilecommand()stillcapsatORIGINALdeadline minus2hfinalreportreserve. Evidence:23VMptxasandbounded-resultsptxaseach>10min, so freshfullbuildneedsroom; noexecution/metric/binarysettingchanged, no source-validationattempt rerun. H20doesNOTvalidateupdatedhelper; pendingH21willhash/syntaxcheckcurrentfile.

---

# Latest state — 2026-10-23 01:09 UTC

- This goal turn is PROGRESS: NEW private compact-cell SoA implementation applied to CPP23 underGPUlock;15-target native build/checks running, plus new immutable coarse/global-selection/final14 chain authored and safely gated. Additional full30 2x remains UNREACHED. START1792682607/DEADLINE1792769007 unchanged,3hreportreserve. No subagents/push/newmonitor/freshheldout/formalfreeze.
- CPP CURRENT23, NOT22. cell-soa-candidate0 PID3480301 acquiredlock, verifiedcomplete22source map, appliedprototypes/apply_private_cell_soa.py, thenbuild-attempt23. Currentbuildstatusrunning/currentcommand1/results[0], cmakechild3480329 andactualptxasleaf3480491R observed. No23binary/nativePASS/speed result yet. Do NOTeditCPPduringbuild/admission. Controllerouterstatusapplying reflectsinitialapplication; authoritativebuildmanifesttracksrealcompile/checkstage.22binary/snapshotallolderresultsunchanged.
- NEW GAGP_GPU_PRIVATE_CELL_SOA=1 interleavescompact privateIntListcells byphysicalblockcolumn; lane t logicali mapsblockbase+i*width+t. Samecellcount/capacity/logicalallocation/fuel/overflow/replay/handles, sameGPUoperators/RNG. PrivateSpan carriescompactstride; immutableboxedinput/answertablescontiguous. Singleton/concat/index/slice/resident scoring honorstride. CooperativecontiguouscopyrequiresBOTHsource/targetstride1; stridedcopyusesexistingownerloop. Kernelphysicalblockownscolumnsacrossgridscheduling; noCPUrepro orworkspacecapreduction. Admissiondeclinesnoncompact/nonprivate paths; profilemarkerprivate-cell-soa[-declined]. Thisisimplementation/hypothesis, NOTmeasuredgain.
- Native23 targetscope15: previous13 +gagp_test_extended_payload_gpu +gagp_test_bounded_region_gpu_results. Extendedprivateoffsetfixture nowchecksone/32laneboxed/compactcontiguous/interleavedcells, fullorigin/window/65536-boundary/wideint/fuel-capacity/score/rewind contracts withoutcrosslanealias; phasepopulationfixtures enableSoA andretainCPU/allgenotype/evolution checks. BuildCellSoA assertsactual15/15/no skipsbeforepinning23. Target15notfullnativeALLsuite/fresh131. Docs/design/payload.md andgagp-navigation updatedforactualaffectedlayout; measuredsourcewillstillbepromotedonlyafterformal.
- NEW cell-soa-admission0 PID3480483 waitsactual23controllerexit+15nativepass, verifiesactualbinary/source map, control22pinnedprovenance, runsstaticcuobjdumpresourcesunderlock; no profiledheadline. NEW cell-soa0 PID3480484 waitsadmission, thenfixed8oldTRAININGtasks/bothP/G8/seed0,48searches22OFF/23OFF/23ON withfullAST/descriptors/champs/CPU16. Requires>=2%pooled16cells vsBOTHcontrols; actualpositiveSoAactivationrequired. Allfailurespreserved; no hiddenretry/heldout. Oldcomplete1.327pooled remainsbestrealfull30.
- NEW combined4 PID3480567 waitscell-soa0+combined3. Ifnew23native/coarsefailsorbenefitrejected, explicitrun_fullFalse/rejected-componentpreservesallfailed evidenceandretainsvalidpriorrecipes. Ifcoarsewins, composeSoAONLYonthebestCOMPLETE60-cellpriorglobalrecipe, immutable23, NEWfull30bothP/fresh731controls; neverattachnewflagtoanunmeasuredfallbackorforce22coldfusion. best_selection_soa_recipe.py permits23onlyaftercomplete60/fidelity/hashchecks;sevennewselectiontests rejectpartial/duplicate/fidelity/recipechangesandpreserveolderfallback.
- H21 PID3480568 WAITINGcommonlock, notPASSyet. Itwillcheckallcurrenttop-levelstudyPython/91expectedtests (actualcountmustberead). H20actual84PASS0.113s remainslatest. NEWqueued-final-chain14-replacement0 PID3480569 WAITINGH21. ItMUSTverifyunderGPUlockoldtraining13/timing8/prep6/final1areallidle/nochildren/0samples/nofreeze, preserveoriginalrecords/PIDproofs, terminateonlythoseidlequeues, thenlaunchtraining14/timing9/prep7/final2. AtlastreadreplacementNOTexecuted; old13chainstillactiveWAITING. NextturnFIRSTinspectreplacementstatus/newPIDs, neverduplicate. No existing native sample/intermediate23samplewillbeinterrupted.
- Newtraining_best_soa_recipe.py waitscombined4andselectscompletebestglobalrecipe; initialknownbuild22admissionargumentdoesnotforce22source, actualselectedbinary/settingsownmetadataarekept. Final2waitsH21/combined4/actualtraining14costs, allowspreservedrejectedcell-soacomponenttoexitwithoutbecomingeligible; formalextra-prerequisiteterminalcheckusescombined4decisionratherthanclaimfailedcomponentpassed. OnlyonenewG64TRAINING/timing/prep/freezechain, nofreshdata/seedsbeforeglobalfreeze. OnfirstfreezeverifyactualG/N/forecastandupdateUIplan.
- Latestpriorgeometrycount113ok+1running/208before23buildtookcommonlock. Itispausedonlybyserialization, notstopped: controller3463600live withchild3480011observedbeforebuild, currentchildrenmustberechecked. Globalgeometrywinnernotready. Existingcombined1/2/3stillwaittheirfrozenprerequisites; parentcachecoarsewinnerenabledTrue, block1retained. Do notcompeteheavyjobsorresample.
- NewsourcealignmenthelperNOTexecuted. If23isadmitted, itscompletecurrentmapistherequiredprior-buildmapforlateralignment; doNOTpass22justbecauseolderdocssay22. If23fails, preservethefailed23snapshotandrestore/selectmanuallybeforepromotionratherthanbypassingthehelper'sadmitted-current-mapguard. ActualselectedCPPmustmatchmeasuredsourceatfinalcommit; rejectedattempts/presentationspreserved. Formalreport/plots/nativefullsourcechecks/T3preview728/360/render/localcommitstillpending.

- Additional strict ownership probe private-cell-lanes0 PID3480811 is queued on admitted23/sharedlock: standalone96 DISTINCT full-width lane values,3blocks,contiguous/interleaved singleton/concat/window/resident scores/rewind/logical+physicaloverflow. Sourceprototypes/private_cell_lane_probe.cu, helpercell_soa_lane_probe0.py. This strengthens same-value primitive fixtures; noCPPmutation/pinnedsample rerun. Atlastread WAITING/notPASS. NEXT MUST inspectactualprobeoutcome beforeallowing23finalselection/freeze. Ifitfails, inspectactualcompile-harnessvsownershipcause,preserveattempt, requirecorrectedNEWproof before23eligibility; nevercallitpassedfrom15targetcompletion. Existingfrozencombined4/final2doNOTthemselvesrequirethislaterprobe, so rootmustclose this gate explicitly. H21allstudysyntax stillWAITINGlock; final14replacementNOTexecuted. Ptxas3480491 actualR99.9%,11:42CPU,~9.35GiBRSS atlastread; aliveprogressing, nothung/notnativepass.

---

# Latest state — 2026-10-23 00:54 UTC

- Goal ACTIVE; additional full30 geometric-mean 2x UNREACHED. START1792682607 / DEADLINE1792769007 unchanged, 3h reporting reserve. Support worktree only, no subagents/push/new monitor. All heavy work shares the GPU lock. No formal freeze/fresh dataseed20261022 or quality seeds25+ yet.
- stage1 COMPLETE300 searches:150/150 P1024 and150/150 P16384; complete full-AST/descriptors/champions/CPU16 fidelity. Best complete pooled development option pool_shape=1.327189036x, P1024=1.094711502x/P16384=1.609036476x, G8/R1 old TRAINING. stage=1.210167526x, pool=1.165762062x, pool_full128=1.256423878x. These are development results, not formal/new quality or2x. Older8shape remains eligible, including its faster small-P result.
- Repro-block0 PID3471682 COMPLETE128/128 searches, all fidelity checks pass; 2/4/8/16/32/64/128 blocks yield pooled1.00358/1.00483/0.99076/0.99517/0.99362/0.99541/0.99561 vsblock1. Frozen>=2% rule retains1. This setting is CUDA BLOCK SIZE, not cooperative threads per program.
- Parent-cache0 PID3472903 COMPLETE48/48 searches, all fidelity checks pass. 20OFF/21OFF/21ON fixed8/bothP: ordinary0.978648849/cached1.024510242 relative20OFF. Cached clears>=2% against BOTH controls, enabled for upcoming combined2 full30 only. Coarse is not a full30 result or permission to attach this flag to an older unmeasured fallback.
- CPP CURRENT22 SHA3ff82aadbc29cb0058839794c651c70da867f98fcbebec3b023f488c47f8bc38. Build22 actual13/13 targeted native PASS275.30s, results[0,0,0], provenance22/resources22/admission1 OK. No later CPP edits. This is NOT a full Release ALLtargets/fullnative/fresh131 pass. Exact selected source full regression remains pending.
- New22 GAGP_FUSED_COLD_EXECUTABLE removes duplicate serialization only after the SAME invocation validated full immutable cold identity/fresh certificate; registry read-snapshot and raw tags/bits/b, input ABI, public unpaired accessor verification, full GPU bytecode/region bounds remain. Fused-cold0 COMPLETE48/48 native-fidelity searches:22OFF0.989568054/22ON1.019103792 vs19reference; fails frozen>=2% over BOTHcontrols, selected baseline/enabledFalse. Do NOT round1.019 to1.02 or rerun to pass threshold. combined3 will explicitly skip its full30 tail aftercombined2 completes.
- Transparent harness correction: fused_cold_candidate0 applied/built/tested22 correctly but its post-build completion metadata mistakenly read21. Original controller and fused-cold-completion-metadata-bug0.json preserved. NEW fused-cold-admission1 PID3477208 verified ACTUAL22 source map/binary/13nativePASS and prior controller exit, corrected metadata in fused-cold-completion-metadata-correction0.json; no build/search repeated or wrong binary admitted. Use admission1, not old candidate0 completion, for22 admission.
- stage1 priority-drain0 COMPLETE: only two verified idle/no-child coarse controllers were temporarily pidfd-paused between samples; stage1 last2 samples completed, both resumed epoch1792713033.997. No active native sample paused/replaced. Complete proof scheduler-priority-drain0.json preserved.
- view-geometry1 PID3463600 healthy coarse13fixedtasks/bothP/8arms, latest99ok+1running of208; 5 full-native plain32 admission tasks already passed. Global poolOFF selected from complete60 stage/pool cells before geometry. No completed geometry winner yet. Actual current PID/children must be inspected, never duplicate. coarse-gate0 PID3469908 preserves/cancels only UNSTARTED individual full30 tails. combined1 PID3470379 then NEWfull30 bothP/fresh731controls onglobalrecipe20. combined2 PID3473236 waitsblock/cache/combined1, then cacheON21 composition NEWfull30/freshcontrols; it does not silently alter older fallback recipes. combined3 PID3477616 waitscombined2 and disabledfused selection, then records explicit no new full30.
- Old idle/no-child/0-native-sample training12/timing7/prep5/final0 queues were safely stopped with originals/PID proofs/hashes in queued-final-chain13-replacement0.json; never restart. ACTUAL new durable chain training13 PID3478030 (best_selection_final_recipe complete60-cell selector) -> training-timing8 PID3478031/preparation-timing6 PID3478032 -> final-pipeline1 PID3478033. All currently waiting for complete measured global recipes. Final1 requires H19/reference reader/actual22 source-resource proofs, waits started tails/profiles exit; freeze once from actual TRAINING costs, then formal0-speed/shared CPU references and formal0-quality. One final G64 training cost chain, no duplicate searches. On first actual freeze record G/seeds/cost forecast in PROGRESS and UIplan.
- nsys cold19 Grade/P16384 CPU profile actual complete. cpu-profile-analysis0 verified SQLite sha and actual callchains; native_population1108 inclusive samples (constructor662), import_layout308, grammar_admission1612 across process, bytecode_verify151, skeleton68; import runtime_cache_identity135 inclusive samples motivated22. These overlap and are NOT wall percentages. Parent21 GPU profiles complete2: FilterOdd eval99.3% of GPU kernel time; parent index+lookup~0.035%; Median mutation46.4%/eval34.5%/parent index+lookup~5%. Do not add API wait as independent overhead or mix profiler times into ratios.
- bottleneck-after-stage1-0 actualOK overcomplete60 candidate19pool_shape. Mean per-cell fractions P1024 admission0.1357/GPUinit0.1681/eval0.4674/repro0.0784/final0.0614; P16384 0.2522/0.0313/0.5463/0.0557/0.0897. Hypothetical half-eval geometry1.79446 is UNMEASURED/not a bound/result. final_eval includes native final AST restoration; output JSON/AST file writes occur after evolve wall. Old diagnostic bottleneck_analysis0 failed output-name collision before any work; traceback and correction preserved, not mistaken for old manifestPID3449191.
- H19 actual78 scientific harness checks PASS0.028s, not fulltools/native131. Readable report modules show baseline/candidate geometric-mean seconds + ratio identity, actual enabled mechanisms only, strict qualified aggregates/failure rows, same-initial-population speed reps vs independent quality seeds, exact shared CPU reference costs/proofs. Real formal report/plots/T3preview728/360/interactions/render remain pending.
- initial-duplicate-diagnosis0 PID3479803 ACTUALOK underGPUlock: complete initial AST equality on fixed8/P1024/old TRAINING only. Duplicate members /1024:sum22,sorted3,filter48,median13,count26,house97,double70,grade71. Mostly<5%; final-generation duplicates do not justify cold-import cache, so no23 implementation added or22 promoted. Input hashes/complete JSON keys preserved; no searches/heldout/time estimate.
- NEW align_selected_source.py authored, NOTexecuted: after actual selected-source full regression/timedcontrollers exit, verifiescurrentCPPagainstcomplete admitted prior22map, rejectsunknown/userchanges/trackedremovals/pathescapes, verifiesmirror/evidence, backsupalloldCPPbeforemutation, promotesexactselectedsourceonly, leavespinnedbinariesuntouched. Explicit--execute required as implementation switch; no permission request implied. Freshbuild/sourcevalidatedmirror mustnotreuseold22buildmtime. Sixmeaningfulguardtests added; H20 ACTUAL84PASS0.113s, alltop-levelstudysyntaxchecked underGPUlock. Thisisnotthefulltools/native131suite.
- User-readable progress CURRENT.zh-TW.md added: P/G/repetitions/completeall30development1.095/1.609/pooled1.327x, qualitynotyetverified, actualmechanisms/rejectedattempts. Formalreportstillpending. Latestgeometry107ok+1running/208, currentcontrollerhealthy.
- Final requirements unchanged: new full30/twoP/R2/G>=8 speed plus fresh full-final CPU reference/GPU audits, independent all30P1024G64/newdata/seeds25+ min10/frozen runtime scale; native train/test fitness+wrong/allchampions/gen0 vs subsequent final success/5pp seed-cluster bound and per-task uncertainty; source/binary/input/split proofs and sampled VRAM. Exact selected source must be promoted to actual CPP AFTER timed jobs with all rejected attempts preserved, full actual regression, docs/diff/untracked/local commit/no push; presentations/ untouched. If deadline constrains coverage, report actual partial coverage and never fabricate2x/quality equivalence.

---

- 2026-10-22 23:46 UTC：私有launch bound粗測完成但收益未達2%，維持0；21版2組nsys完成，hash/index非主要GPU成本。新combined2條件全30隊列等待block/cache與combined1，只有粗測有收益才追加；H17/H18實際71PASS。修正診斷輸出資料夾衝突為新attempt，無重跑搜尋；報告加實際搜尋秒數與範圍定義。正式freeze／新quality仍未開始，2×未達；詳RECOVERY。

- 2026-10-22 23:29 UTC：21版跨父代精確分數查找已9/9 nativePASS198.05s，SHA3add55e4…；H16實際64PASS。來源／資源核對及速度粗測條件隊列持續。沒有21倍率、新quality或2×聲明；詳RECOVERY。

- 2026-10-22 23:28 UTC：已實作21版跨父代精確分數查找（GPU hash定位後完整基因型比較，保留revision／代數／fuel核對），編譯成功、9項native回歸仍執行，尚未admit或取得倍率。20版繁殖block1–128的固定八題兩P粗測已啟動；21版20OFF/21OFF/21ON粗測等待native＋來源admission。全題19矩陣及20組合鏈繼續，未取消已啟動工作。正式freeze／新quality尚未開始，完整最佳仍1.1762／1.2615×，2×未達。新globalblock/cache若有收益，必須先做全30兩P與64代training成本，不能套到舊fallback後直接宣稱收益。詳RECOVERY最新段落。

- 2026-10-22 23:08 UTC：20版9/9 nativePASS197.34s，SHA3b071f8b...，provenance／resources通過；REG255→128／64只屬資源證據，速度尚待實測。13題project/simple獨立粗測未達2%門檻、拒絕納入；數值不作全30倍率。事件gate僅取消未啟動的個別全題尾段，保留所有粗測與已啟動資料，改combined1全30兩P→training12→timing7/preparation5→final-pipeline0。CPU參考精確跨方法重用5組native＋完整decode-context reader通過；P16k參考145s/GPU重算8.1s只減校驗成本，正式仍需當輪新CPU參考，actual0/ref使用量分列。H15實際64PASS；正式freeze／新quality未開始，整體2×未達。詳RECOVERY最新段落。

- 2026-10-22 21:57 UTC：實作私有串列評分核心的opt-in編譯bound32/512/1024；physical32／logical512、案例／fuel／GPU演化／16GiB不變。20編譯中，native回歸尚未admit；八題粗測→全30兩種P新對照已等待admission。19版全題／geometry／training9鏈維持，無新取消／重測。新報告讀取／圖表／freeze執行器已author，修正未執行報告讀取器的成本錯誤與native未齊全合格計數；10/11科學檢查與完整reader smoke等待lock，尚未PASS。完整最佳開發數據仍1.1762／1.2615×，正式freeze／新quality未開始。詳RECOVERY最新段落。

# 全30題額外兩倍加速 — 2026-10-22（進行中）

- 使用者目標：相對目前最快GAGP，全30題幾何平均再快2倍，保留演化能力。固定新窗口2026-10-22 15:23:27 UTC至10/23 15:23:27 UTC（台灣23:23:27），延續最多24小時；不重啟舊研究。
- 實際worktree `gagp-support-20261015`／`opt/list-output-5x-20261015`，CPU1T、GPU評分＋繁殖、16GiB workspace、AST2048/depth24/phase1024。沒有subagents或push。所有重工作使用共用GPU鎖。
- 舊最快binary `731697e...` 已獨立複製與hash凍結；主要倍率為本輪新配對舊GAGP／候選，不沿用歷史秒數或ASGP分母。
- 精確整數accumulator／合併計數的首批8題兩種P接近1×，保留負結果。新增重複整數案例重用與受保護除法／餘數捷徑；後者526052次GPU/CPU運算比對通過。build-attempt4的4項針對性測試通過145.42s，binary9cac63f1...；build3因測試來源快照競態而拒絕，沒有採用其binary。
- cases1的13題兩種P共78搜尋完成，案例重用幾何平均1.3226／1.4502×；全AST／descriptors／champions相同。較完整simple1全30題兩種P共180搜尋完成：case_only1.0724／1.1233×、simple1.0702／1.1149×，完整配對一致。全題尚遠離2×，不把13題子集當整體收益。
- build6的4項native檢查與科學harness8項通過。build7回歸因測試漏開DIRECT_ROOT與fallback profile斷言失敗，封存後只修測試設定；build8的5項native通過，shape1／register1全題開發對照排程中。下一候選build9實作依賴分組、register phase stack與實體threads縮減，原邏輯分數順序不變，等待整合回歸／全題實測。尚未正式凍結或接觸新heldout。
- [規格／狀態](results/twofold-optimization-20261022/README.md)、[恢復](results/twofold-optimization-20261022/RECOVERY.md)，raw `logs/twofold-optimization-20261022/`。原有未追蹤 `presentations/` 保留。

- 新增精確unchanged-offspring GPU分數重用：post-elite完整genotype相等證明＋session／resolved-payload revision與世代限制，實際root calls與沿用logical cases分列。Host scope preflight通過；build10／11編譯期間因來源檢查補shared-capacity／tiled scratch／late payload失效規則而主動中斷封存，沒有科學樣本或nativePASS。build12因新增CPU oracle少連library而失敗；修正CMake後build13的6項native PASS176.49s，binary12f31bbf...。尚無此功能全題加速結論；reuse2後續全30題對照已排程。Python harness最新21項通過。正式／新heldout／全Release回歸與報告仍未完成。

- 19:1x UTC：nsys指出Grade/P16384配置成本明顯（profiled cudaMalloc3.8185s，占CUDA API64.1%；不作倍率），新增專用GPU記憶體池獨立生命週期／排序測試PASS，整合build14編譯中。新測試history計數問題已排透明修正／新build15，尚無整合PASS或池化速度。單菁英O(P)GPU排序原型98組核對PASS，待build16整合。全30多組新對照已排程；既有shape/register/vm/training鏈仍健康，不重複樣本。2×尚未達成。

- 19:3x UTC：build14–16的新增fixture問題（fuel、未請求native path、37-case nested evolve清除其他payload roots）封存；runtime池化來源未改，17以完整1024-case fixture回歸中，尚未宣稱PASS或池化倍率。Python27項PASS；精確AST去重CPU判題四題smoke15個實際程式／36個觀測全部對回原逐一判題一致，可降低後續離線品質校驗成本。threads0另以同一舊binary測全域1／8／16 threads。正式／新heldout仍未開始。

- 19:35 UTC：池化build17 ACTUAL7/7 nativePASS179.95s（phase-population165.52s），SHA71c7e467...；完整1024-case fixture與原32代CPU檢查恢復通過。pool5全30題兩種P四組對照已啟動。O(P)菁英已接入新18並編譯／8項回歸中；elite5、training5與18provenance/resources有條件排程。仍無池化完整倍率、正式freeze或新heldout，2×未達成。

- 19:56 UTC：18的8/8 nativePASS179.13s，provenance/resources皆OK。shape1全題240搜尋完成，shape-only幾何平均1.1762／1.2615×（P1024／16384,G8,R1），AST／分數描述／冠軍一致，仍非2×。停止未開始的舊8／9後續鏈並保留0sample／取消記錄，轉測當前18的實體32／64／128 threads；pool5/elite5/threads0仍健康，training6＋timing1等待完整新對照後做G64舊training與校驗成本預測。品質harness33項PASS，正式freeze／新heldout仍未開始。

- 2026-10-22 20:44 UTC：pool5／elite5／smallwidth0在P16384第1代後配置16GiB workspace失敗，原attempt及失敗保留；training5/6與timing0/1停止，非完整矩陣。新增共用stage儲存區維持16GiB與固定current population，獨立100次配置／釋放原型通過，19整合9項native回歸仍在編譯，尚未admit。stage1新容量gate＋全30兩種P五組新對照、stage-width0全域width、training7＋timing2舊training長跑／核對成本已條件排程。新選擇器保留已完成8版shape候選，不把失敗矩陣納入。Full-final精確AST去重CPU核對4題smoke通過；新正式harness分開timing與120全population native audits，缺校驗不能宣稱達標。尚無正式freeze或新heldout；2×未達成。

- 2026-10-22 21:27 UTC：19 ACTUAL9/9 nativePASS180.18s，SHA2e264df8...；provenance/resources19皆OK。三題P16384容量15/15完成，完整配對AST／descriptors／champions與CPU16核對通過，整卡取樣21.24GiB，16GiB cap未降。全30P1024五組150搜尋完成，P16384進行中。P1024 poolON0.9958×相較poolOFF+stage1.0621×顯示cold成本；只取消尚未開始的width／geometry舊隊列並保存0sample記錄，改由完整60格先選globalpool，再joint測plain／relaxed32/64/128，最後全30單一winner新對照。現在有效鏈stage1→view-geometry1→training9→timing4／preparation-timing2；舊training7/8等已停止，不resume。科學harness51PASS，report reader四題既有audit smoke通過；native CPU歷史4.21h僅作預算預測，正式仍需新CPU核對。正式freeze／新heldout尚未開始，2×未達成。

---

# 大population與全30題GPU加速優化 — 2026-10-18（完成）

- 固定2026-10-18 11:18:51 UTC至10/19 11:18:51 UTC，未重設；worktree gagp-support-20261015／opt/list-output-5x-20261015，無subagents／push。全30題 × P1024／16384 × ASGP1T／原GPU／tuple GPU × G8 ×2次，共360搜尋與360 CPU final／逐代champion判題均完成、0執行失敗；最後搜尋01:39:03 UTC、判題02:10:25 UTC。全部controller／children退出，不resume。
- CPU1T、GPU評分與繁殖、16GiB workspace、physical2048／24／phase1024、C1024固定；所有ASGP分母新量測。P1024對ASGP原／優化幾何平均7.86／9.30×；P16384為11.10／12.43×。相對原GPU幾何平均1.18／1.12×，收益集中IsSorted與AllEqual（P1024再快8.24／7.80×；P16384皆5.25×），多數其他格接近1×，7／30與11／30負收益保留。
- 合格速度格60／60；2352檔案來源／binary／external source／linked inputs／命令／輸出hash與720個串行區間通過。240 GPU runs的1046400次最終個體fitness／wrong觀測0差異；120 execution pairs一致，其中60完整最終AST。整卡約200ms NVML最高21.116GiB（含桌面、可能漏尖峰），P16384全30題在24GiB卡完成，不作任意族群／世代保證。
- 搜尋累計6.083h、額外raw CPU核對4.222h、正式程序10.658h；冠軍CPU判題程序另0.107h，不能把搜尋倍率套用總成本。品質僅seed0／固定training／第一rep：P1024三方法皆final4/30、gen0為2、新增2；P16384 ASGP9、兩GPU8/30，gen0皆5、新增4／3。無新heldout，不宣稱泛化、完整算子等價、paper replication或解題加速比。
- 新benchmark blocksize入口已實作；小分組變慢，1024分組雖平均快6.4%但全AST差異而拒絕。compact-root原型實作／測試後20完整配對一致，平均僅0.9%未達預定3%，封存移除。正式採既有tuple路徑／block512，全部負結果與failed attempts保留。固定父代336請求＝296 root匹配＋40 mapping unavailable，不證明內部位置／mutation／RNG等價。
- 確切最終binary731697...完整Release／native131/131 PASS255.09s；後續study57、repository24通過；staged格式核對明列保留CSV CRLF／凍結檔EOF空行的規則，詳format-check.json（不同實際檢查，非新完整tools131）。兩張PNG檢視；最終T3 728／360各56互動檢查、手機test提示／頁尾／無頁面溢位通過，render原始final HTML。首版尺寸限制與次版低對比修正均封存，三版科學summary／CSV／PNG不變；唯一交付final-v3。
- [易讀結論](results/scaling-optimization-20261018/CURRENT.zh-TW.md)、[正式中文報告](results/scaling-optimization-20261018/final-v3/REPORT.zh-TW.md)、[逐題CSV](results/scaling-optimization-20261018/final-v3/cells.csv)、[互動頁](results/scaling-optimization-20261018/final-v3/dashboard.html)、[實際驗證](results/scaling-optimization-20261018/final-v3/validation.json)、[重現](results/scaling-optimization-20261018/README.md)、[恢復／封存](results/scaling-optimization-20261018/RECOVERY.md)。本輪暫時monitor最終交付停用，舊兩個monitor維持停用；本地提交、未push。

---

# 品質控制與長代改善24小時 — 2026-10-18（已完成）

- 新時間窗固定2026-10-18 01:17:45 UTC至2026-10-19 01:17:45 UTC，沒有重設。9題 × 20新seeds × 5方法、P1024／G64，共900搜尋＋1800 CPU train/test final/history判題全部完成，0程序失敗／未執行；369個準備工作成功。搜尋08:35:23 UTC、判題09:03:29 UTC結束（起點至此7.762h），所有controller／children已退出；不要resume。CPU1T、GPU評分與繁殖、16GiB workspace、physical2048/24/phase1024固定。
- 第1項完成：全30題初代30,720、中後代144快照147,456、演化後source/native27快照27,648候選觀測fitness/wrong皆0差異；正式final前16共11,520觀測亦0差異。Source/native抽樣偏好1011/491520不同，4題HeadIndex site count仍不等價。算子／rejection／行為多樣性證據已整理，未把品質落差唯一歸因為GPU評分錯誤。
- 第2項完成：預設關閉CPU training top-K/interval/near-zero機制；正式top8/i8檢查19,584個program觀測／3,916,800cases，fitness/wrong修正0，CPUwall80.644s占guard搜尋9.00%；相對operator搜尋wall配對中位增加9.71%。離線雙split判題另耗1155.238s程序wall。未看到品質收益，定位為校驗機制。
- 第3項完成：TupleSummary execution-only在新資料20配對seeds的G64搜尋相對原GPU，Is Sorted9.59×（95%8.89–9.87），All Equal9.94×（9.36–10.27）；對ASGP為6.30／4.28×。180對匯出champion AST／fitness軌跡及最終descriptors一致；不宣稱全population AST／任意輸入語意相同。倍率不混nsys時間。
- 品質未證明整體提升：ASGP final聯合成功41/180，四GPU方法皆39/180，gen0 champion成功皆0。Training-only自動選出site7於04:22:53 UTC凍結，按固定時間規則選20×G64，再建立新data seed20261018／seeds5–24。相對execution-only，site7成功數3題增加、2題減少，主要9題Holm後無支持提升或退步；沒有顯著差異不代表等價。保留execution與operator分開比較，沒有按test挑新設定。
- 完整性5160檔案雜湊、外部ASGP46來源、相同且不相交的每題train/test／相同初代及串行區間全部通過；各題train200/test1000，全卡200ms NVML取樣最高18.950GiB（含桌面，可能漏短尖峰）。正式同一binary SHA d347d95617aba1676af11b05d786bf06db882d9eddff6b97756207b3d8d9a48a，核心4ca0909。品質僅9題／P1024／本機，不能推廣全30品質、ASGP等價、論文重現或time-to-solution。
- Release全build PASS；完整native130/131唯一文件hash gate修正後gate＋adapter2/2，最後pipeline＋adapter2/2；tools122(4skip)、repo24。最終分析／驗證15、repository24通過，不宣稱單次fresh131全通過。三張實際PNG已檢視；T3 html_preview桌面728／手機360各71互動檢查通過。報告首版失敗／修訂均保留，科學數值／PNG／推論JSON不變。暫時monitor已停用、舊day24仍停用；本地提交、未push。
- [主要結論](results/quality-control-20261018/final/FINDINGS.zh-TW.md)、[完整中文報告](results/quality-control-20261018/final/REPORT.zh-TW.md)、[互動頁](results/quality-control-20261018/final/dashboard.html)、[逐run CSV](results/quality-control-20261018/final/runs.csv)、[配對統計](results/quality-control-20261018/final/paired_tests.csv)、[規格／重現](results/quality-control-20261018/README.md)、[恢復入口](results/quality-control-20261018/RECOVERY.md)。Raw logs／immutable binaries／全attempts：logs/quality-control-20261018/。工作區gagp-support-20261015／opt/list-output-5x-20261015，舊UI worktree未改動。

---

# 單機24小時實驗 — 2026-10-17（已完成）

- 使用者已授權開始。24h從2026-10-17 13:16:49 UTC起算，硬截止2026-10-18 13:16:49 UTC（台灣21:16:49）；重啟不重設預算。先以時間／資源pilot選定速度G16／品質G128，若12h／5h及保留5h的門檻不成立，正式前統一改G8／G64。
- 速度保留30題×五種P1024/2048/4096/8192/16384×ASGP1T／GPU-er-o，seed0，各一個cold process。品質保留全30題、P1024、配對初代與variation seeds0–4、unique train<=200／input-disjoint test<=1000；有限域例外明列，不能稱原論文重現或品質等價證明。
- 16GiB workspace、GPU繁殖、CPU1T、whole AST2048／physicaldepth24／phase1024固定。production runtime保持12c6849；本輪僅擴充benchmark adapter的opt-in案例數、初代seed、指定資料原生CPU判題與best-only export。舊資料與binary不改。
- Controller原PID3124080已退出，manifest為complete，沒有背景benchmark工作。原始資料／pinned binary／manifest／controller metadata在`logs/day24-20261017/`及同prefix controller檔案；不要resume已完成矩陣。從準備到全部核對9.234h，含報告檢查時間另見final/completion.json，仍在原24h截止內。
- 全30題、150組品質初代準備完成，均完整admit，未替換個體；30題train/test input overlap均0。有限域Train/Test unique例外：Even Squares79/20、Sum of Squares30/70、Fibonacci21/10、Tribonacci19/7、Fibonacci Array81/88，其餘25題200/1000。代表題速度pilot6/6、品質pilot12/12完整通過；正式品質的600/600指定train/test原生CPU判題亦完成。
- 2026-10-17 15:22:04 UTC正式前依凍結門檻全局選定速度G8／品質G64。Pilot校正預估：G16／G128速度26.876h、品質9.793h，超出12h／5h門檻；G8／G64估速度14.530h、品質5.380h，合計19.911h，選定時剩21.912h。這是保守預測而非完成保證；原硬截止及reserve guards保留，若時間不足須明列budget_not_run。全30題／五種P／五組品質seeds與其他設定不變，不再逐格調代數。
- 正式G8速度矩陣300/300方法runs完成且驗證通過，150/150配對格齊全，0失敗；五種P各30題全部完成，包含P16384全30題。所有GPU使用同一pinned binary、16GiB workspace及GPU繁殖。各P倍率中位數10.64／13.95／16.05／14.94／15.22×，幾何平均7.95／10.22／11.49／10.30／11.07×；147/150格較快。正式速度整卡200ms NVML取樣峰值21.114GiB，含pilot等工作最高21.132GiB；含桌面且可能漏短尖峰。
- P1024／G64／seeds0–4品質300/300搜尋與600/600指定train/test原生CPU判題完成，沒有程序失敗。ASGP final train/test全對58/150、GPU39/150，兩邊gen0 champion各10個已成功，新增成功48／29；至少一個seed成功的題目16／9題。跨題加總及每題5seeds僅屬描述性探索，顯示目前品質有落差，不能將搜尋倍率當作解題加速比，也不能單獨歸因於GPU有損執行。
- 完整性核對591個檔案雜湊（外部ASGP46來源檔案）、30題資料／初代配對、600正式命令及全部串行時間區間通過。初版報告保留在`logs/day24-20261017-report-initial`；修正pool/support標示、gen0 champion文案及補source provenance後，summary／CSV／PNG雜湊不變，沒有改指標或重測。
- 首對Double Letters P16384/C1024/G16＋final pilot通過：ASGP搜尋2307.765s、GPU96.318s；GPU16輪unscored=0，first16 final-program CPU audit為0 mismatch，無CPU繁殖fallback。整卡200ms NVML取樣峰值21.106GiB（含桌面，可能漏短尖峰）；只代表此題此設定，尚非全題G16支援證明。較大G16／G128 pilot獨立保留，不混入G8／G64正式主表。
- 暫時性T3 monitor原每30分鐘回到原對話核對進度，現已以`update_scheduled_task(enabled=false)`停用。ID及prompt存`logs/day24-20261017-monitor.json`。最終repository24項全通過，桌面728／手機360互動檢查通過；production runtime與12c6849差異為空。變更與結果只在本地、未push。
- [主要結論與限制](results/day24-20261017/final/FINDINGS.zh-TW.md)、[完整逐題報告](results/day24-20261017/final/REPORT.zh-TW.md)、[互動表](results/day24-20261017/final/dashboard.html)、[速度CSV](results/day24-20261017/final/speed.csv)、[品質CSV](results/day24-20261017/final/quality.csv)、[本輪規格與重現](results/day24-20261017/README.md)、[凍結protocol](results/day24-20261017/protocol.json)。先前四代／1024-node結果保留獨立，不混入新的長程表。核對採每次既有first16 final-program CPU audit及所有品質champions離線原生CPU判題；不宣稱新的全population parity證明。

---

# 全題統一2048-node容量驗證 — 2026-10-16（完成）

- 使用者要求統一放寬限制。新增benchmark `native_nodes`／`--native-nodes`，預設1024不變；新設定全30題統一whole-AST2048/depth24，ASGP phase depth7不變，GPU每phase1024節點、12GiB genotype guard與16GiB workspace保留。
- GPU layout不再把整體budget與單phase allocation混為同一上限；compacted code/constants依整體variable node budget檢查。Cold import明確拒絕超過1024節點的phase。新增>1024整體AST的四代GPU reproduction＋獨立CPU fitness核對，較小whole-budget與單phase容量拒絕均有覆蓋；既有32代GPU回歸通過135.06s。工具116項通過（4 skips）、repository24通過；其餘130項native通過105.98s（總計131/131）。
- 實際pilot揭露evolve在嘗試phase owner之前先執行generic1024-node preflight，已改為phase admission後才在fallback建立generic resources，保留generic硬限制與payload roots。另修正mutation donor須同時符合剩餘whole budget及destination phase容量；公共evolve入口、generic負向測試及大AST CPU/GPU audit均通過，完整native131/131再次PASS（239.39s）。先期失敗與舊binary保留。
- 正式runtime凍結`12c6849`；同一新binary完成全30題P16384/C1024/G4＋final evaluation，30/30成功。每題全部16384個最終個體的CPU fitness audit均0 mismatch，四代unscored=0；仍使用GPU繁殖與16GiB workspace。House五模式P8/G2亦5/5通過。兩個controller皆已退出，沒有背景benchmark工作。
- 24GiB RTX3090整卡NVML每200ms取樣峰值21.2978GiB（含桌面，可能漏掉短尖峰）。跨題倍率中位數14.38×、幾何平均10.12×，28/30題快於ASGP；每題單次cold process，非精確重複量測或收斂等價證明。House為17.27×、10.56GiB，最終最大AST1115，確實驗證大於舊1024上限的GPU演化。
- 全30題16384初始admission通過；所有新grammar只改max_nodes，初始source/cases不變，不替換個體。29題完整初始AST與舊artifact相同，House前8192個體相同。準備資料及舊pilot失敗保留在`logs/nodes2048-20261016/`；正式raw、不可變binary、硬體及取樣在`logs/nodes2048-searches-20261016/`。通過指完整執行與fitness核對，不表示全部隨機程式正確或全部題目已解出。
- [完整中文報告](results/nodes2048-20261016/final/REPORT.zh-TW.md)、[互動表](results/nodes2048-20261016/final/dashboard.html)、[原始秒數與counters](results/nodes2048-20261016/final/cells.csv)、[重現入口](results/nodes2048-20261016/README.md)。本輪新設定只重測最大P的30格；舊1024-node完整150格矩陣與失敗紀錄保留，不以新budget單格補進舊主表。工作區仍為`gagp-support-20261015`／branch `opt/list-output-5x-20261015`，變更只在本地、未push。

---

# 固定16GiB完整scaling完成 — 2026-10-16

- 已完成完整30題 × P1024/2048/4096/8192/16384 × G4+final evaluation／完整1024cases。CPU1T、seed0，所有GPU格同一binary、16GiB workspace；不混入8GiB時間。150格全部結束，149格GPU成功並有合格倍率；ASGP controls 150/150完成（121個同P、同inputs/settings且ASGP邏輯未變的控制組經驗證後重用，29個P16384另行實測）。controller已退出，沒有背景benchmark工作。
- P<=8192的120/120格通過，完整輸出AST、fitness及semantic/operator traces皆與既有有效資料一致；先前九筆僅8GiB可執行的題目現也全在16GiB通過。P16384成功29/30題，全部16384個最終個體 × 1024cases的CPU fitness audit均0 mismatch；所有GPU成功格unscored=0。
- 唯一限制：House Robber的frozen parent index8603轉譯為1086 AST nodes，超過固定1024 node budget；depth24不是原因。原16K preparation已失敗，先前誤把30個prepare任務complete當作全部admission成功，已更正。原None輸入attempt保留，以source/grammar/binary hashes及原prepare log註記initial_admission_limit；不是OOM或GPU kernel失敗，沒有合格倍率。未刪除／替換parent或採用放寬node budget的離線診斷資料。
- RTX3090 24GiB整卡NVML每200ms取樣峰值21.2734GiB，含桌面，可能漏掉短尖峰。P1024/2048/4096/8192/16384跨題倍率中位數9.77/12.61/14.89/13.23/13.90×，幾何平均7.12/8.76/10.04/9.32/9.92×；最後一組為29題，其餘30題。每格單次cold process，屬支援／粗略速度檢查，非精確重複量測或收斂等價證明。
- Runtime checkpoint `356bc55`：native phase owner上限8192擴到16384，保留12GiB genotype guard。按記憶體估算分階段配置／釋放repro暫存及eval scratch，不改搜尋參數、RNG、個體／案例數、fuel或16GiB scratch上限；generic metadata 100萬項guard未抬高。完整Release build及131/131 native tests通過，含32代staged/resident、in-place/out-of-place、private-list/window及CPU對照；tools115項（4 skips）、repository24 checks通過。
- [完整中文報告](results/scaling-all30-20261016/final-same16/REPORT.zh-TW.md)、[原始秒數與counters](results/scaling-all30-20261016/final-same16/cells.csv)、[互動表](results/scaling-all30-20261016/final-same16/dashboard.html)、[重現與恢復](results/scaling-all30-20261016/README.md)。`final-same16/`另含逐代CSV、PNG/PDF、硬體、驗證及來源雜湊。原始資料及pinned binary在`logs/all30-scaling-20261016/same16/`，舊失敗、probe及8GiB補測全部保留為歷史。
- T3更新後恢復原工作區`gagp-support-20261015`、branch `opt/list-output-5x-20261015`（起點047aa84）；初始UI工作區`t3code-63ba5a62`是較早分支，未改動。變更與結果只在本地，未push。

---

# All30 G4 scaling — 2026-10-16（執行中）

- 使用者授權完整30題×P1024/2048/4096/8192/16384×G4，可超過1小時。比較ASGP1T／最佳GPU-er-o，CPU1T、完整1024cases，不改runtime／pipeline。
- Runtime及設定沿用下方d250ac3 checkpoint；binary `logs/list-5x-20261015/pooled16-ten/bench`。準備與矩陣各自新目錄`logs/all30-scaling-20261016/{artifacts,matrix}`，外部launcher等待prepare完成後自動開始measure。原始worktrees／artifacts不改，不push。
- 最新P1024 GPU30/30完整G4與CPU audit通過；先期歷史分母摘要在`results/scaling-all30-20261016/p1024/`。正式五種P使用各自重新測得ASGP控制，不混用歷史分母。
- 30/30題新16384source已確認原1024 parents/cases prefix；P16384 native admission實際29/30通過，House在parent8603因node budget失敗（先前誤讀complete旗標，已更正）。正式矩陣P1024/2048/4096/8192皆30/30通過（120/150格），目前進入P16384。P8192九題使用獨立8GiB設定恢復完整執行，原16GiB失敗仍保留。原16GiB若allocation失敗才明列8／4GiB重試，保留所有failed attempts；其他錯誤不改設定掩蓋。每cell1800s、hostRSS32GiB保護，失敗無speedup。
- [執行與恢復入口](results/scaling-all30-20261016/README.md)，runner `results/scaling-all30-20261016/run_matrix.py`。每cell寫manifest／rawlog／JSON；續跑先查launcher與controller，未完成measure用`--resume`。GPU lock仍`/tmp/gagp-support-20261015-gpu.lock`。P16384 Filter先期已見constant node origin preparation item limit（16/8/4GiB皆失敗、不是OOM），僅作support evidence。預計數小時，不因啟動長跑就收尾。

- P8192 Filter16GiB的launch failure經nsys確認cudaLaunchKernel returnValue=2（memory allocation）；原失敗保留。獨立`capacity_retries.py`於同一鎖下測8/4GiB，九題均在8GiB完整G4與CPU audit通過（Filter／IsSorted／AllEqual／Cumulative／For／Double／Replace／Even／Negative）。其他相同文字不一律宣稱已證實OOM；以各自完整run結果判定。`report.py --capacity-results <manifest>`可合併有效設定並保留original status/log；實際資料工具檢查通過。

- 前四級checkpoint：`results/scaling-all30-20261016/p8192-checkpoint/`（120合格格，其餘pending）；P1024/2048/4096/8192跨題ratio中位數9.96／11.92／14.69／14.57×。仍非完整結果，不將pending標成功。

---

# List／sequence 優化 checkpoint — 2026-10-15–16

- 範圍：10×伸展目標、5×里程碑；CPU 1T＋GPU、原生五種模式、P1024／完整1024 cases／G4＋final eval。分母沿用各題 frozen ASGP 1T G4。沒有 sampling、solver、CPU fragment reproduction 或本輪新的 RNG／operator／fuel／數值變更。未 push、未使用 NCU、未跑高成本全30題／scaling精確矩陣；原始 worktree／frozen inputs／歷史結果未改。
- 工作區：`gagp-support-20261015`，branch `opt/list-output-5x-20261015`。保護點 c596217、原始 runtime 8249c0d、lossless 起點 3d4fafe。最佳 production **b568c7e**，測試 **b1760de**。Archived binary：`logs/list-5x-20261015/pooled16-ten/bench`，實際 source／binary／input hashes 在 manifest。
- 設定：`results/list-5x-20261015/settings-workspace-pool16.json`；省 VRAM 版本 `settings-workspace-pool.json` 使用8GiB。最佳設定16GiB是資源代價；預設仍512MiB，新 profiles 預設關閉。GPU測試依序取得 `/tmp/gagp-support-20261015-gpu.lock`。
- 完整 G4 加速比：Filter **9.88×**、For **8.14×**、Double **14.77×**（額外三個 cold processes 中位數）；Cumulative **12.99×**、Replace **11.55×**、Even **11.53×**、Negative **12.45×**、FibArray **5.39×**、Small **0.92×**、Grade **0.87×**（單次粗測）。五題超過10×、八題超過5×，未全題達標。FibArray 是 scalar-output bounded2D memo。For 同輪 ASGP9.041s vs frozen9.372s，約3.5%漂移已揭露，沒有換分母。
- 保留的 native 改善：排序／auto tiling／exact Concat與epoch caches；private content／offset IntLists；compact int64 cells；所有座標的 dense memo；trivial／Measure／bound phases；既有 two-window driver與64-bit typed loop；cold membership／executable reuse；>=256 cells 的 warp copy；單一 session-owned cells／frames／memo backing。未變更核心 pipeline，仍有 AST export／必要 admission。
- Workspace 真正消除重複配置：initial／replay 共用一塊 backing；整個 retained capacity＋當前其他 scratch 都計入 cap；先釋放才成長／縮容，包含 owning device／callback lease／reset。三個 G24 run 皆第一代配置1次、後23代0次。最終 free 留在 `evolve_wall_ms`；不以單次 snapshot（其 final free 在 session 析構）宣稱 pool e2e 收益。
- 同 binary、8GiB 完整G4 pool OFF→ON：Filter1.652→1.156s、For1.785→1.165、Even2.031→1.673、Double3.563→3.262、Small.477→.413，全部 AST／語意一致。新 compact＋warp＋pool 前提下，8→16GiB 的 Double3.309→2.687、Replace3.144→2.558、Even1.699→1.588s；完整 A/B 在結果 JSON。
- 支援／語意：private profile 明確把歷史 hash alias／65536 transport identity 改為生成 list 的 content identity；container Eq、IntList memo keys 等明確 decline。完整 typeflow／當代 packed bytecode 的證明不等於 grammar membership。65536 external／65537 generated backing、encoded observations 及 generic fallback 保留；詳見 `docs/design/payload.md`。後續 executor／copy／pool 保持此已揭露 profile 的語意。
- 正確性：最終十題 seed0 的全部1024 AST／population／語意 trace 對原 native 控制一致；CPU top16 audit=0。Filter／Negative／Small 額外 seeds1、2 G4及seed0 G24對 lossless 完整一致。G24 lossless→best：20.13→4.82s、25.36→6.16s、3.796→.809s。短程不證明收斂品質等價；後期時間仍增加。
- 記憶體：G24 process RSS 抽樣最高約580／456／644MiB，包含 export／audit。長序列題目的 device process 抽樣最高18,664MiB，包含 context／genotype／scratch，不是 device peak；短 Small 可能錯過 backing 存活期。沒有宣稱任意長跑或常數記憶體已解決。
- 最終驗證：完整 Release build；**131/131 native PASS，175.64s**；repository24、fixed-ASGP tools31 PASS；13題×snapshot及原五模式 P8／完整1024／G2＝**78 cells PASS**，snapshot mismatch=0。Copy helper memcheck／synccheck均0 errors；pool另有容量／dirty／callback／replay／32代CPU對照測試。原三題 pre-pool cold paired controls 約慢3%／3%／13%，已列限制；最後五模式檢查仍涵蓋三題。沒有挪用舊 GAGP CPU e2e 時間當最新控制。
- 最新 nsys：Filter eval占GPU kernel時間97.2%；malloc／free各166次（先前185），API總時間約5／21ms。兩個 trace cap不同，不能作因果 wall attribution；採用依據是未 profile 的同 binary完整搜尋。長題剩餘成本仍是 list 搬移／VM；Small／Grade admission本身已超過各自5×總預算，需另一個cold ownership問題的實驗。
- 淘汰／未做：移除 global hash index、capacity scale、cell SoA（Even慢15%）、不安全的 untiled32-slot shrink；保留安全的 tiled-only32 slots。額外 phase-pattern coverage不足、frame cap收益弱；大型rope／DAG沒有最小原型收益證據。本輪所有實驗、build與測試已完成，沒有背景 GPU 工作；歷史失敗 artifacts 留作紀錄，無 live rejected flags。
- 重現：`PYTHONPATH=tools python3 benchmarks/fixed_asgp/optimization/results/list-5x-20261015/run_search.py --out <NEW> --binary logs/list-5x-20261015/pooled16-ten/bench --settings benchmarks/fixed_asgp/optimization/results/list-5x-20261015/settings-workspace-pool16.json`。**不要 prepare frozen inputs**。續跑先以新的高價值假設做最小實驗，不自動重跑全基準或復活淘汰原型。
- [完整中文報告](results/list-5x-20261015/REPORT.zh-TW.md)、[最終數據](results/list-5x-20261015/final.json)、[驗證](results/list-5x-20261015/validation-final.json)、[英文圖表](results/list-5x-20261015/final-speedup.png)。Raw logs／manifests／binaries：`logs/list-5x-20261015/`。

---

# All-task repair continuation — 2026-10-15

**RESUMED: prioritize list/string-output tasks, target ASGP1T/GPU-er-o >=5x on current P1024/C1024/G4 complete-search contract.** Keep CPU1T/native pipeline/full cases. Final all30 G4, original-three long and five-mode P8 smokes are complete and frozen; do not rerun them merely to establish a baseline. Full baseline rebuild/CTest pending. New outputs under `logs/list-5x-20261015/`. First hypothesis: bounded larger initial arenas reduce costly replay, but initial warp scheduling may negate the gain; test before adoption.

- Integration worktree `gagp-support-20261015`, branch `fix/asgp-all-task-runtime-20261015`; protected original `241e374`. Current runtime equals `8249c0d` (verified empty `git diff 8249c0d..96152cb -- cpp`); later commits reject prototypes/update docs. Native five modes, CPU1T + GPU reproduction retained; original worktrees/frozen inputs/historical results untouched, no push.
- Current best settings: `results/repair-20261015/settings-final.json`, adding adaptive replay, 8192MiB bounded GPU workspace and exact small-int Concat hash lookup to prior settings. Default workspace512MiB and optional flags unchanged. Scratch cap excludes immutable/executable/context; observed device sample9992MiB is not peak. Smallhash P1024 Filter fixed evaluator5554.75→4793.95ms, same full-case fitness/status; full-search result pending.
- Correctness: prior all30 known-witness/source-output sample/CPU-GPU snapshots + P8/C1024/G2 five modes pass. All30 P1024 admission. Original23 complete G4 plus seven list tasks now complete G1 with CPUaudit0; uniform final G4 running. Negative CPU1T P1024G1 completes70.754s,274MiB RSS, replacing prior P8 95.36GiB failure; not a long-run memory or fastCPU proof.
- Retained fixes include logical/physical allocation separation, bounded tiled/adaptive GPU replay, indexed private arena with scalar rollback repair, exact fusedConcat/smallhash, source Stock compact/lazy lowering+constant-pool normalization. Cumulative/list-producing DC comparison uses disclosed symmetric `list-combine-v1` extension. Fitness/elite1 align; native physicaldepth24/balanced weights/operator/fuel rules still differ from source logicaldepth7.
- Rejected and removed: localarena P1024OOM; arena-window/compact-root<1%; immutable-window hash cache no consistentgain; exact endpointtrim<~2%; readonly profile not applicable; index transpose~0.5–0.7%; Slice/Singleton smallhash no incrementalgain. Raw evidence retained, no live rejected flags. Privateoffsethandles deferred: canonical identity/collision semantics and lifetime proof need a distinct larger execution profile.
- Optional sourcecap7, uniformphase and .7crossgate are NOT final headline settings. Cap7 fails closed on six unsupported grammars; House quality mixed/worse in two seeds. New cap certification included inside walltimer since6ac07f2. Do not substitute these altered-search times or historical ASGP denominators into final native results.
- Completed: `/tmp/gagp-final-all30.py`, output `logs/all-task-repair-20261015/final-all30-g4`; archived8249c0d binary, P1024/C1024/G4/seed0, own ASGP G4 controls reused plus two same-round drift controls. No precision/full scaling matrix. Final-three-long (32/32/64) and all30 five-mode P8 smoke also complete; full build/CTest pending user resume. Scripts mirrored under results/repair-20261015/scripts; final scripts accept NEW `--out`. Never prepare original inputs; GPUlock `/tmp/gagp-support-20261015-gpu.lock`.
- Latest full native131/131 at01f4f65 before smallhash; exact smallhash/transpose/Slicehash focused extended tests pass, rejected-feature tests removed only with their implementation. Tools115 run/4skipped, repository24 passed. Final full native and final five-mode validation pending.

---

# All-task runtime repair — 2026-10-15 (in progress)

- Branch `fix/asgp-all-task-runtime-20261015`, worktree `gagp-support-20261015`; protected start `checkpoint/pre-all-task-runtime-20261015`=`241e374`. Original inputs/worktrees/results untouched; no push this round. CPU1T, native five modes/GPU reproduction, full1024cases; no expensive precision matrix.
- Correctness checkpoint `0b1c99b`: all30 full1024 source-parent admission and known runtime witnesses; P8/full1024/G2 all5 modes **30/30 strict support passes**,360cells/443.09s. Includes source/native sampled output/status and CPU/GPU fitness+case/error/timeout parity. This is not all30 P1024 speed/long-run proof. Full native130/130; tools108pass/4skip; repository24pass. Original Negative frozen failure independently repaired.
- Added Stock fixed-skeleton constant-pool canonicalization `6bb241c` (restores existing native GPU phase path; different operator trajectory from previous AST fallback), fused exact Concat `abcbecb`, arena32 launch bounds `071ba22`. Current candidate `e55a673` also has optional extended aliases, initial overflow diagnostics, adaptive replay capacities; **adaptive performance/validation pending**, not current best claim.
- Baseline/settings: raw `logs/all-task-repair-20261015/support-balanced` and `benchmark-all-g4`; compiled runtime0b1c99b. Optional native physicaldepth24 and balanced-source-production-v1 weights. Symmetric opt-in ASGP/GAGP `list-combine-v1` changes list-combine language; original source build remains available. Aligned fitness/elite1 is NOT identical search space: ASGP maintains source offspring depth7, native physical24 is different. A separate exact projected-depth prototype is under test; not adopted.
- `benchmark-all-g4`: all30 requested P1024/full1024/G4/seed0, ownASGP controls,23complete/7GPUprocess timeouts60s,966.4s. No qualified ratio for timeouts. One-shot coarse times, some CPU compiler overlap; no precision claims. See [before](results/repair-20261015/benchmark-before/searches.csv).
- Original3 longer own-controls, same current balanced/depth24 contract: Sum32 ASGP49261.072/GPU2290.113ms=21.510x; House32 27251.040/1466.871ms=18.578x; Median64 2813.294/931.593ms=3.020x. Single samples, not historical denominator reuse. Historical best not restored.
- Quality3seeds/G4/P1024 +heldout: original3 balanced screen and Stock phase-path screen complete. Sum/Median finalbest unchanged; House seed-dependent. Stock GPU1288–1291ms versus previous2382ms G4 AST path; CPU finalfitness audits0. No convergence-equivalence claim. [Balanced](results/repair-20261015/quality-balanced/), [Stock](results/repair-20261015/quality-stock/).
- P64 fixedsnapshots: Filter fused Concat3890.5ms vs bound32 baseline5513.6ms (~29%less), both exact CPU statuses/fitness. Arena-window and compact-root prototypes <1%standalone/noise; **reverted**, flags/tests/docs removed. Archived bf352a4 binary/raw evidence remains historical only.32boundvs1024bound ~4–5%coarse improvement.
- GlobalOFF P64 faster for list outputs, but **all7 list tasks fail P1024 launchOOM**; local route is not adopted. Extended alias pair changes global mode, not an alias0/1 control; no isolated alias speedup claim. Bounded global initial arena remains required. List eval/replay dominates; initial arena/replay workspace capped512MiB excluding immutable input/executable and unrelated CUDA storage.
- In progress: G1 P1024 slow-task own-controls with120s processcap (new outputs); overflow lane-distribution diagnostic; adaptive replay starts16kvalues and grows only exhausted capacities; projected source-depth profile gate. No omission of difficult programs/cases or CPU fallback.
- Raw artifacts `logs/all-task-repair-20261015/`; compact evidence [repair-20261015](results/repair-20261015/). Preserve failed/timeout/localOOM runs as evidence. GPUlock `/tmp/gagp-support-20261015-gpu.lock`. All speedups use unprofiled search-wall; process-wall includes separate input/admission/audit costs. Diagnostics are not subtracted from formal wall time.
- Reproduce baseline support: `PYTHONPATH=tools python3 -m gagp_tools.experiments.fixed_asgp.low_budget support --out <NEW> --budget 650 --timeout 35 --native-depth 24 --source-balanced --settings benchmarks/fixed_asgp/optimization/results/repair-20261015/settings.json`. Later stages inherit immutable artifacts. Never prepare old inputs or use P8 smoke ratios as P1024 results.

---

# ASGP 30-task support and aligned metrics — 2026-10-15 (validation complete)

- Integration `feat/asgp-support-contract-20261015`, worktree `gagp-support-20261015`; protected checkpoint `dc99120`. Runtime checkpoint `e2da6ad`, diagnostic/migration/test followups through `8163e47`. Main fast-forward delivery; preserve the dirty original main checkout rather than resetting it. Original chat worktree and historical inputs/binaries remain unchanged.
- Three Astra-medium agents; native five modes, CPU1T and GPU reproduction retained. Added configurable task fitness, elite1 alignment, wrong-count ranking, exact int64 execution, generic protected truncating primitives, source-derived grammar adapters, bounded CPU payload lifetime, separate GPU extended arena and safe memo admission. Named search-rule/resource differences remain; `fully_aligned=false`.
- 30/30 P1024 admission; 150/150 P8/full1024/G2 five-mode searches complete. Combined strict gate23/30: six source/native resource-status gaps (including CumulativeSum source-language limitation), plus NegativeToZero GPU arena mismatch. Known witnesses29/30. Do not call all30 fair or fully supported.
- P1024/full1024 quality: three tasks x3seeds x2gens x5modes,45searches+45heldout audits passed in197.6s. No clear short-run degradation; Sum already solved atgen0 and Median unchanged. No convergence equivalence claim; final native audit16programs only.
- One-shot two-generation search wall (seconds), ASGP/CPU/GPU-e/GPU-er/GPU-er-o: Sum6.068/20.500/1.089/1.152/1.074; House3.053/8.111/.943/.521/.518; Median.171/1.453/.764/.769/.779. New grammar/metric/input contract, own controls; no old25x/30x denominator reuse. Median GPU slower. Native AST fallback causes Sum/Median host decode cost; House retains native phase path.
- Build complete. Native128 covered (126firstpass+2corrected retests),3focused external regressions; tools100(4skips),repository24. Preserved old migration fixtures; changed wrong int64 rounding expectations to exact low-bit checks. No full precision repeats/scaling/long-run benchmark.
- [Chinese report](results/support-20261015/REPORT.zh-TW.md), raw compact summaries/manifests/settings/tests under same directory. Full logs/binary/exports: `logs/support-20261015/{native-support,quality,benchmark}`. Failed macro-lowering prototype removed; builtin lowering preserves physical source-op count. Payload metadata reuse has no isolated speedup claim.
- Reproduce `PYTHONPATH=tools python3 -m gagp_tools.experiments.fixed_asgp.low_budget support --out logs/support-NEW --budget 600 --timeout 25`; then `quality`/`benchmark --artifacts logs/support-NEW --out <NEW>`. DefaultsP1024/C1024/G2 for latter stages. GPU lock `/tmp/gagp-support-20261015-gpu.lock`; never prepare old inputs.
- Next: admit repeated-hole guards/captures into existing compact GPU phase pipeline without changing source semantics; align list lifetime/resource budgets; broaden quality only after support gates. No active experiment or automatic continuation required.

---

# Expanded native benchmark — 2026-10-14 (complete)

- Runtime unchanged from db1dd55/2938d69; source checkpoint41f2566, binary SHA b9952b911df5f22f1ed361472663a114994882278bcededfbe3f8efd3d3d7980. Original worktree/frozen inputs preserved; no push.
- Count Odds / Last Index: P1024/C1024, CPU1T,16generations×3seeds×5native modes=30completed searches. Full-search medians ASGP31.352/24.826s, GPU-er-o1.005/1.017s →31.21x/24.41x. Complete CLI ratios13.57x/11.82x. New common-fitness denominators; original Last discounted control separately27.440s, not substituted.
- Final independent exact accuracy ASGP19.43%/22.66%, GPU-er-o7.42%/15.33%. Additional archived-training-best audits12.60%/22.66%; no convergence equivalence. Native GAGP can lose best; ASGP elite1. Neither task solved in these16gens. Native pipeline/CPU1T retained; no solver or known-solution injection.
- All30 formal +30 final/history quality +30 archived-best quality +3 native-policy +3 native-quality cells completed. Initial fullpopulation snapshots retain66/21ASGP-CPU mismatches and1/0CPU-GPU mismatch. Known-solutions and finaltop16GPU/CPU audits pass; scope not full exact parity. Last repeated-square numeric precision discrepancy independently reproduced; others not fully diagnosed.
- Stock exact clamped memo transition capability probes reject both candidate encodings. Negative knownsolutionGPU fails; CPU randomP8 preflight stopped at95.36GiB RSS. No qualified ratios for either. No rerun of unsafe randomlist evolution. Explicit expected failures/skips preserved, not called support.
- Release benchmark build; focused native CTest passes (2successes,2expected failures,1skip); tools7/repository24/report6 pass. Original3tasks/five modes smoke preserved. Formal processes16.16min; elapsedstage18.44min including shared-lock waits. No scaling or long-run claim this round.
- [Report](results/expanded-20261014/REPORT.zh-TW.md), [settings](results/expanded-20261014/settings.json), [contract](results/expanded-20261014/contract.json), [all seed timings](results/expanded-20261014/final/per-seed.csv), [all480 generation rows](results/expanded-20261014/final/generations.csv), [quality](results/expanded-20261014/final/best-ever.csv). Rawlogs/artifacts `logs/optimization/expanded-20261014/`. Serial `run.py <pilot|formal|quality|best-quality|native-controls>`; `summarize.py` + `summarize_best.py` regenerate figures into a NEW directory. Exact commands in report; do not prepare old inputs.
- Next work: loss of historical best / equal-wall-clock quality, explicit integer semantics, bounded list payload/GPU evaluation, clamped memo contracts. These are identified followups, not changes implemented here. All jobs complete.

---

# Sum25 / House25 / Median5 — 2026-10-14 (complete)

- Best runtime `2938d69`; final test-only correction `c0d24a9`; branch `opt/gpu-generation-20261001`, worktree `gagp-opt-20261001`. Candidate binary SHA`e4c8e8ebebab92a6cc81be1e8df31d0a43d9dc9fb6f9dacc12e0f302c36e519b`. Start checkpoint `checkpoint/pre-sum25-20261014`=`320d8bf`; original worktree clean at`75d7f83`, archived baseline/frozen inputs preserved. No push.
- Native pipeline, CPU1T and GPU reproduction unchanged; no RNG/operator/numeric/fuel/grammar changes. All64 original flags plus `GAGP_MEASURE_PHASE_CACHE=1`, `GAGP_PER_PHASE_STACK_BOUND=1`, `GAGP_HYBRID_PHASE_EXECUTOR=1`, `GAGP_GPU_WINDOW_LEAF_DIRECT=1`, `GAGP_WINDOW_SOA=1`, `GAGP_DYNAMIC_PROGRAM_QUEUE=1`. [Complete flags/identity/denominators](results/sum25-20261014/contract.json).
- Nine fresh unprofiled cold searches per task: Sum32/seed0 **1762.552080ms /27.832x**, House32/seed0 **1009.139088ms /27.418x**, Median64/seed2 **807.146462ms /7.218x**. All nine individually meet25x/25x/5x. Existing `evolve_wall_ms` includes cold evolution/all generations/final evaluation; parsing/independent audit/JSON output remain outside. CLI process medians3.507/2.237/1.980s, reported separately.
- Same-round old binary3 controls:4303.359/1011.774/806.421ms. Strong improvement is Sum2.44x; House−0.26%/Median+0.09% time differences are noise, not new speedup claims. MedianASGPdrift+2.68% does not replace frozen denominator. GPU-e/ER each3new repeats; CPU historical full-search controls explicitly reused (unchanged code), all5modes smoke/scaling rerun.
- Three Astra-medium worktrees explored phase execution/window continuations/scheduling. Kept exact pure-Measure reuse, per-phase certificates, hybrid VM fallback, leaf-direct, queue/SoA. Removed rejected top-cache and entry-direct; Index inspected only. No CPU fragment route. [Contracts](../../../docs/design/gpu-reproduction.md).
- Final evidence:90search samples,54exact semantic/final/history AST comparisons; three seeds×128gens per task before/after identical. Full initial snapshots exact;4inheritedHouseCPU/GPUdifferences unchanged. Sum gen0 already solved; top16long CPU audit only; no improved convergence claim. Sumseed0last16medians202.04→46.91ms. RSS+roughly12–18MiB,128gens not an unbounded-history proof.
- Release build; native126/127 initial plus corrected fixture1/1 (all127 covered); repository24/tool5; main CUDA12.6memcheck0errors/racecheck0hazards. Stale agent test object concealed invalid4096-frame fixture; coverage retracted and replaced with fresh97program/65case/128frame fixture. Capacity safety remains. All60fixed-parent scaling cells P1024/2048/4096/8192×3tasks×5modes pass, own controls rerun, noOOM.
- [Chinese report](results/sum25-20261014/REPORT.zh-TW.md), [final JSON/manifest](results/sum25-20261014/final/summary.json), [per-generation CSV](results/sum25-20261014/final/generations.csv), [scaling](results/sum25-20261014/final/scaling.csv), [tests](results/sum25-20261014/validation.json), [nsys diagnostic](results/sum25-20261014/profile.json). No NCU; profiled time never used in ratios. Sum eval89%ofGPUkernel time; House eval and Median reproduction/lowering remain bottlenecks.
- Reproduce: `python3 benchmarks/fixed_asgp/optimization/results/sum25-20261014/run_validation.py <timing|quality|other-modes|snapshot|scaling> --out <NEW stage directory>` then adjacent `summarize.py --runs <stage parent> --out <NEW report directory>`. Raw `logs/optimization/sum25-20261014/`; shared GPU lock `/tmp/gagp-parallel-20261013-benchmark.lock`. No prepare. All agents/jobs complete; no pending experiment. Next higher-target work must remeasure remaining eval/phase cost or Median lowering, preserving native pipeline.

---

# Three-task rerun — 2026-10-14 (complete)

- No runtime change: validated `be455a4` binary SHA764ab8df9dbea4b2cb9e439a8e2608f7b1bd7fd9aa9f7ecd8bf4c4a6e09b53a0 and full64flags. CPU1T, P1024/C1024; no profiler/input preparation/early-success stop.
- Sum32/seed0, House32/seed0, Median64/seed2; all5native modes×3fresh complete cold searches=45runs. Allcontrols rerun. GPU-er-o medians4339.178/1045.019/857.690ms; same-round ASGP ratios11.305x/26.477x/6.793x. Frozen historical House/Median ratios26.495x/6.838x kept separate. Fastest GPU happens to beER by a tiny margin; not evidence that overlap hurts.
- Fresh fixed-parent all3tasks×5modes=15cells, each warmup+3samples:GPU-er-o45.844/30.940/6.604ms, same-round ratios41.38x/41.96x/8.63x; these do not establish complete-search targets.
- Quality limitation:Suminitialgeneration0 alreadyfitness0 for allmodes. HousefinalfitnessASGP−4234/CPU,e−6544/ER,o−7146;Median−578/−642. Three timing repeats use one seed/task, not three independent quality seeds. Fullcases/unscored/audit checks passed in runner. Five fixed-tool tests and24repository checks passed; no runtime rebuild needed.
- [Report](results/three-task-20261014/REPORT.zh-TW.md), [raw summary](results/three-task-20261014/summary.json), [per-generation CSV](results/three-task-20261014/generations.csv), [English chart](results/three-task-20261014/search-comparison.png). Raw root`logs/optimization/three-task-20261014/`; alljobs complete.
- Reproduce with `python3 benchmarks/fixed_asgp/optimization/results/three-task-20261014/run.py --out <NEW>`; summarize via sibling `summarize.py --raw <runs> --out <NEW summary>`. Existing short_search now accepts explicitSum32/seed0; defaultHouse/Medianunchanged. No push.

---

# Bloat cost optimization — 2026-10-14 (complete)

- Best runtime `be455a4`, branch `opt/gpu-generation-20261001`, worktree `gagp-opt-20261001`. Protected prior checkpoint `checkpoint/pre-bloat-cost-20261014` = `71dc916`. CPU1T, native GPU reproduction, full P1024/C1024; no RNG/operator/fuel changes, no push this round.
- Keep all previous manifest flags; add `GAGP_GPU_DEFERRED_CROSSOVER_COPY=1`, `GAGP_GPU_FUSED_SPLICE_DEPTH=1`, `GAGP_GPU_COMPACT_COMPILE_FRAMES=1`. Runtime eliminates overwritten parent copies and duplicate depth scan, reduces lowering stack2816→768bytes. Full phase lowering still runs; no persistent DAG/subtree executable cache claim.
- Three Astra-medium agents in separate worktrees explored four routes: executable-only CSE, incremental metadata/lowering, physical sharing/deferred copies, live workspace. Root adopted only measured native changes. Builds/GPU experiments serialized with shared lock.
- Nine interleaved complete cold searches: House32/seed0 **1047.911452ms /26.421x**, Median64/seed2 **858.233274ms /6.834x**. Same-round prior best1067.850610/914.303899ms:1.87%/6.13% less time. Frozen ASGP denominators27687.368225/5864.915193ms unchanged. House30x unmet (requires922.912274ms);Median5x met;CPU still far fromASGP.
- 18 short paired and6 task/seed128-generation comparisons: semantic counters, fitness, final and history AST exact (history elapsed timestamps excluded). All P×C evaluations retained. Long CPU audit covers16programs, not fullpopulation. Tests127/127 + expanded variation; repository24/24 passed.
- Rejected/removed executable CSE: packing exceeds eval savings, Median regresses badly. Rejected/removed live memo backing removal including bounded allocation reuse: House still regresses. Patches/raw evidence archived, no active runtime flags. [Incremental/sharing](results/bloat-cost-20261014/incremental-sharing.json), [CSE](results/bloat-cost-20261014/rejected-executable-cse.json), [workspace](results/bloat-liveness-20261014.json).
- Raw root `logs/optimization/bloat-cost-20261014/validation/runs/`; [report](results/bloat-cost-20261014/REPORT.zh-TW.md). Reproduce: `python3 benchmarks/fixed_asgp/optimization/results/bloat-cost-20261014/run_validation.py --out logs/optimization/bloat-rerun-NEW`; summarize with sibling `summarize_final.py --runs <fresh runs>`. No input preparation/overwrite.
- Remaining:House eval/bloat;Median reproduction/lowering;CPU evaluation/reproduction. House128 seed0 early/late16medians20.926/171.181ms. No measured case for extending rejected prototypes or building persistent DAG this round.
- Final validation complete:40/40 fixed-parent scaling cells (P1024/2048/4096/8192, all5modes, controls rerun), Sum all5 regression,10five-mode smoke cells,3focused CUDA memchecks0errors. [final.json](results/bloat-cost-20261014/final.json) records raw repeats/flags/input/binary identity; [per-generation CSV](results/bloat-cost-20261014/generation-times.csv). All agents and benchmark jobs complete; no pending continuation.

---

# Astra parallel optimization — 2026-10-14 (complete)

- Best runtime `5a3be05`, branch `opt/gpu-generation-20261001`, worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`. Protected start `ce0d811`; original workspace, frozen inputs and historical binaries untouched. No push.
- Authoritative binary/full settings/input hashes and raw repeats: [final.json](results/astra-20261014/final.json), raw `logs/optimization/astra-20261014/final/gpu-nine/manifest.json`. All previous-best flags remain required. Add `GAGP_GPU_FUSED_DONOR_SITES=1`, `GAGP_CPU_FUSED_ADMISSION=1`, `GAGP_CPU_COMPACT_ADMISSION=1`, `GAGP_GPU_CONSTRUCTIVE_EXPORT=1`, `GAGP_REUSE_IMPORT_CERTIFICATE=1`, `GAGP_PREPARED_CASE_VIEWS=1`; CPU1T only.
- P1024/full1024, changing House32/seed0 and Median64/seed2: GPU-er-o9 medians **1064.776693ms /26.002981x**, **909.163542ms /6.450891x** against frozen ASGP search denominators27687.368225/5864.915193ms. Prior1317.052/1136.319ms:19.15%/19.99% less time. House30x remains unmet; another13.3% time reduction needed.
- All5 modes: CPU3 medians82103.390/42153.159ms, GPU-e3 8398.843/24131.383ms, GPU-er3 1063.300/915.630ms. ASGP3 same-round27576.167/5899.860ms. CPU remains far from ASGP. CPU/ASGP controls reuse their unchanged archived binary and CPU implementation; GPU-e rerun after GPU-only view change. CLI process ratios are lower than search ratios; report includes both.
- Kept native architecture/RNG/operators/numerics/fuel. Removed duplicated donor annotation, fused admissibility+witness traversal, continued scalar proofs across compaction, reused private cold-import admission/executable, omitted optional reverse-parsed export cache, prepared private list-parameter views. Fused admission's speculative-work guard accounting differs explicitly; no new search-rule profile retained.
- Rejected/removed runtime: GPU superinstructions, power2 division, exact guarded32 division, CPU cached fusion (including misleading regressed-OFF artifact), size-near crossover. Evidence/patches archived. Equal-wallclock heldout rejects bloat change; don't revive CPU fragment route.
- Final validation:127 native tests,24 repository checks,89 tools (4 skipped); focused CUDA12.6 memchecks0errors.36 complete short/128-generation trace+AST comparisons exact; fixedP1024/C1024 snapshot exact before/after, inherited4House CPU/GPU mismatches unchanged. Three seeds×128gens per task remain identical; not a convergence-equivalence proof.
- Scaling40/40: all5 modes×2tasks×P1024/2048/4096/8192, controls rerun, noOOM. Separate frozen-parent9 medians32.228752/6.956174ms, not the search target. SumP1024 all5 regression complete. Long-run bloat and history/JSON memory remain; peak RSS1.16–2.28GiB is not proof of bounded host history.
- Remaining:House eval;Median GPU reproduction/lowering;CPU evaluation plus Median CPU reproduction. Final list-view step is modest (pairedHouse−1.67%,Median+1.16%); no guaranteed all-workload speedup. No evidence to extend rejected static/bloat prototypes this round.
- [Chinese report](results/astra-20261014/REPORT.zh-TW.md), [per-generation CSV](results/astra-20261014/generation-times.csv), [validation](results/astra-20261014/validation.json). Reproduce via `python3 benchmarks/fixed_asgp/optimization/results/astra-20261014/run_final.py --out logs/optimization/astra-rerun-NEW`; summary via sibling `summarize.py`. Never prepare original inputs. All agents/builds/experiments completed; no pending jobs or context-dependent continuation.

---

# Parallel worktree optimization — 2026-10-13 (complete)

- Protected original workspace checkpoint `75d7f83`; optimization input checkpoint `9ddab1d`. Original workspace remains unchanged after its checkpoint; no push.
- Best runtime `2d7dac3`, branch `opt/gpu-generation-20261001`, worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`. GPU runtime unchanged from `b60a336`; final addition is exact CPU all-fit compatible-pair bounds.
- Authoritative binary/settings/input identities: `logs/optimization/parallel-20261013/followup-main/manifest.json`. All best flags including CPU1T and `GAGP_CPU_ALL_FIT_PAIRS=1` are also in [latest/final.json](results/parallel-20261013/latest/final.json).
- Primary contract: House32/seed0, Median64/seed2, P1024/full1024 cases. Cold admission/import, every complete generation, final evaluation/export included in search wall; full process adds input parsing and audit/JSON output.
- Final GPU-o9 medians: House1317.052ms /21.022x, Median1136.319ms /5.161x against same-round ASGP3 medians27687.368/5864.915ms. CPU3 medians84247.491/44762.427ms, still3.043x/7.632x slower than ASGP. House30x and CPU-near-ASGP are NOT achieved.
- Separate frozen-parent formal9:32.926999/7.277462ms, historical frozen ratios38.935x/8.489x. Do not substitute these for changing-search ratios.
- Final scaling:40/40 cells (two tasks x four populations x five modes), all controls rerun. Earlier Median8192 OOM retained; other user's GPU workload later released memory naturally and final rerun passed. SumP1024 five-mode regression passed.
- Validation:127 native tests before compactTask,6 affected after compactTask,2 affected after all-fit;24 repository and89 tools (4 skipped); focused CPU sanitizers/GPU memcheck. Final30 main traces and final fitness/sizes/ASTs exactly match pre-all-fit checkpoint, frozen input hashes unchanged.
- Quality:counted CPU pair sampling changes seeded trajectories (Median chosen seed best-462→-642; House improves). All-fit changes no RNG. GPU128gens x3seeds and Median512 memory diagnostic pass, but bloat/history/output memory remain; short quality is not convergence equivalence.
- Retained:CPU typed/view/memo/compact driver and selected-site preparation; GPU batch import/export, static grammar/executable reuse, packed carrier, capability buckets/workspace, comparison/ring memo, mutation scratch and compact Tasks. Native GPU selection/reproduction stays GPU, one application CPU thread.
- Rejected and removed:sampled sites,size ties,rootfilter,copy/compaction variants,shared/cached/register stacks,fuel/atomic variants,lazy CPU LOAD bindings,GPU per-slot executable reuse. Raw evidence retained, no experiment entry points.
- Remaining bottlenecks:House eval;Median CPU admission/reproduction and GPU mutation/lowering;long-run bloat. No measured basis for another small stack/cache/binding variation. A future static typed phase execution plan needs a separate costed prototype; do not resume rejected CPU fragment architecture.
- [Full report, raw repeats, scope, limits and reproduction command](results/parallel-20261013/REPORT.zh-TW.md). Summary: `python3 benchmarks/fixed_asgp/optimization/results/parallel-20261013/summarize.py --followup`. Archived raw root `logs/optimization/parallel-20261013/`; prior b60a336 evidence remains immutable under final-main/final-fixed.
- All agents, builds and benchmark processes completed. No pending experiment or automatic continuation is required to recover this checkpoint.

---

# Native 1T optimization checkpoint — 2026-10-02

Active objective: preserve CPU / GPU-e / GPU-er / GPU-er-o architecture, use
CPU 1T + GPU, target fastest native GPU >30× frozen ASGP-1T on Sum/House;
Median remains fixed-overhead workload. Do not resume retired CPU fragment route.

## Long-search follow-up — 2026-10-13 (complete)

House64 / Median128, all5 modes, seeds0/1/2, P1024/C1024; then timing-budgeted
Median256 reruns. Unchanged archived binary and flags, CPU1T. No runtime edits.
39 runs complete in54.16min, 5184 generations; all30 primary plus9 Median256
extensions. 150 checkpoint records /39 unique programs independently rechecked,
native scores reproduce and GAGP CPU/GPU match. Historical16-gen and extension
128-gen prefixes match; full CPU/e population parity is not claimed.
[Results and figures](results/long-search-20261013/REPORT.zh-TW.md): House64
fastest GPU13.27x, Median128 1.49x search-wall. Quality is seed-dependent: House
ASGP seed0 solves atgen37; Median CPU/e seed2 held-out61.13% vs ER/o33.40%.
Raw checkpoint: logs/optimization/long-search-20261013/manifest.json.
[Contract](results/long-search-20261013/CONTRACT.zh-TW.md). Runner long_search.py;
post-run checkpoint audits long_search_audit.py; plots long_search_report.py.
Runner refuses to overwrite its output; reproduce into a fresh directory.
No runtime modifications; source/binary/input identities and validation archived.

## Multi-generation validation — 2026-10-13

[Six all-mode comparison figures](results/multigen-20261013/all-mode-comparison/README.md):
English labels, common-generation fitness/time and four ASGP speedup bar charts;
long-run timing uses seed 0 only. Reproducer: multigen_comparison_plots.py --results
benchmarks/fixed_asgp/optimization/results/multigen-20261013.

Runtime Z/settings unchanged; benchmark adds five search entries and opt-in trace.
[ASGP vs GAGP overview](results/multigen-20261013/OVERVIEW.zh-TW.md) fixes GPU-er-o
as the primary comparator; regenerate with multigen_overview.py --results
benchmarks/fixed_asgp/optimization/results/multigen-20261013. Archived CSV unchanged.
[Report, plots and CSV](results/multigen-20261013/REPORT.zh-TW.md): P1024/C1024,
3 seeds,16 generations+final,45 aligned primary runs,15 original Median controls,
30 timing-selected extensions (up to1024 generations);90 processes ~40.6min total.
Median scoring aligned to ASGP exact using cap1 for admitted Int output only.
Search-wall fastest GPU: Sum3.142s/9.97x,House1.279s/10.78x,Median1.022s/0.76x.
These are changing-population search results, not the frozen30x headline.
House improves in all3seeds at10s; Median is variable. Long House seed0 generation
cost grows11.51x. ASGP translated Median has76 distinct semantic mismatches;
negative-index behavior confirmed. Native ASGP quality/heldout used, not ported
scores. GAGP CPU/e mean differs in10 generation records; no full parity claim.
4109 unique recorded champions pass GAGP CPU/GPU full-case audit; native ASGP
snapshots reproduce search scores. Focused native integration,5tool/24repo checks pass.
Raw/binaries/commands: logs/optimization/multigen-20261013-v2/,
multigen-20261013-exact/,multigen-20261013-analysis/. Runner multigen.py; independent
ported/native audits and report scripts alongside it. No runtime optimization
or operator change in this validation. Custom metric/ranking API remains proposed.

## Current best and active experiment

Validated best opt-in runtime Z: `d1712a5`, X plus `GAGP_COUNTED_SITE_DRAW=1`.
GPU count/rank site sampling retains uniform candidates but changes RNG trajectory.
Interleaved3 processes x1 warm+3 measured repeats per setting:
- Sum: uncounted50.603 -> counted48.884881ms (range48.580490–50.016281),37.66x.
- House:42.306 ->40.384029ms (39.790332–42.125251),31.75x; all9 below42.734ms.
- Median:15.565 ->12.213825ms (11.871306–12.702482),5.06x.
Frozen denominators1841.160175/1282.011816/61.777141ms unchanged. These are full
generations on frozen parents, not identical evolving populations or search-wall
speedups. Cold owner admission363.6/333.0/369.8ms remains included in search wall.
Base R flags below plus `GAGP_GPU_PHASE_POPULATION=1 GAGP_OWNED_PHASE_PACK=1
GAGP_OWNED_PHASE_CAPS=1 GAGP_COORDINATE_EXECUTOR=1 GAGP_COUNTED_SITE_DRAW=1`.
No CPU workers. Generic native fallback/all original modes remain.
Archive/manifest/raw9 repeats: `logs/optimization/native-counted-validation/`.
Runner from repo root: `python3 logs/optimization/native-counted-validation/run.py
STAGE`, stages paired,screen,quality,snapshot,cpu-views,scaling. Registered cells
resume; original inputs and historical results never overwritten. Archived bench
SHA and frozen input SHA live in manifest; runtime is d1712a5 (later docs-only
spec-freeze manifest correction does not change executable).

Validation done:3 seeds x4 generations; long3 seeds x64 Sum/96 House/512 Median.
Common5-second completed prefixes including cold initialization:
- Sum base/new best=[0,0,0]/[0,0,0].
- House base[-7570,-7465,-7570],new[-5492,-4337,-5843].
- Median base[-26620,-28478,-25038],new[-26445,-16159,-18062].
All top16 final CPU audits exact; complete pairs,zero timeout/fallback/unscored.
Runtime error rate at5s did not increase; they remain real penalized errors, not
skipped pairs. Unique final genotypes generally>1000/1024. New long-run populations
bloat: final means Sum~300+,House~550+,Median~800 nodes; late generation times
are larger, so do not transfer frozen-parent headline to whole searches.
Independent1024 heldout cases per task (same ASGP ranges,new seed,no exact
training inputs):18 selected best programs all CPU/GPU exact,zero runtime errors;
House/Median fitness advantages remain. Raw heldout/summary.json,quality-summary.json.
A separate512-generation Median RSS diagnostic shows~1011MiB at5–15s increasing
to~1075MiB at25–30s,peak1313MiB including final audit/report; GPU genotype857MiB.
Owner buffers retain only live generations, but requested history-best AST/proofs
still grow with generation count. This is a disclosed retained-output cost, not
an assertion of constant total-process memory. Raw memory/rss.csv and manifest.

All3072 frozen rows exactly match historical allocator reference. House retains
4 inherited CPU/GPU differences (347,359,622,766), not new; don't claim all3072
CPU/GPU equal. X reporting checkpoint ce0e430 had House43.634ms/29.4x; its earlier
30.54x result was not robust. Z's final-source snapshots now also match reference.
Full127-target suite with CPU view opt-in:126 passed; spec-freeze hash test caught
changed grammar-definition contract doc. Corrected only its manifest hash; failed
repository target rerun passes,24 repository and5 fixed-tool checks pass. No test
removed. Weighted/reset fixed skeletons explicitly decline profile. Negative tests
confirm existing physical admission bounds, including zero-charge resources;
no redundant runtime budget scan was added. Y construction-site code is removed.
CPU window view adopted opt-in after interleaved9 repeats: Sum16960.628->9465.923ms
(1.792x); House7952.051->7957.459 (+0.068%),Median687.083->686.402ms. All non-time
outputs match. Add `GAGP_CPU_REGION_VIEWS=1` to best flags; generic fallback remains.
3072 evolved seed0 final programs (64/96/512 generations) independently admitted,
full1024-case CPU(view enabled)/GPU reevaluation exact versus stored final fitness.
32-case behavior-probe distinct counts27/205/76; these are not full-domain equivalence.
CUDA12.6 memcheck of32-generation P16 full-case owner test:0 errors.
Latest House nsys kernel means including warm/cold launches: eval29.827ms,
mutation2.015,crossover including selection1.643,lowering1.367,offset scan0.531,
final genotype compare0.323. Cold import annotation0.914 is initialization only.
These are diagnostics, not unprofiled headline arithmetic. Raw profile/ files.
Tracked evidence: results/native-counted-validation.json; full commands/raw logs
remain in archive. Final5-mode scaling is complete:60/60 cells,full1024 cases,all four populations.
P1024 CPU scaling reuses identical same-binary third-process CPU-view controls,
explicitly tagged reused_from_tag; remaining controls freshly measured. No NCU.

### Historical exact-trajectory reference (R)

Best validated GPU source: `5508a60` (Q+R immutable crossover handoff), with
locally extracted jemalloc 5.2.1-4ubuntu1. Use `round2.py` FLAGS and process env:
```
LD_PRELOAD=$PWD/logs/optimization/native-allocator/extracted/usr/lib/x86_64-linux-gnu/libjemalloc.so.2
MALLOC_CONF=background_thread:false,narenas:1
```
No system installation; allocator confirms no background workers and one arena.
Previous N (`d72b5f4`) nine-repeat medians Sum279.706 / House295.679 / Median288.718ms;
same-round allocator controls329.388 /348.488 /339.451ms.
Frozen headline6.583x Sum /4.336x House, not30x. All3072 snapshots,3 seeds x4/32
generation results and final ASTs match. Four focused native/safety tests pass.
All120 tests pass with N and the O window-view candidate; scaling still pending. Use `round2.py` flags including `GAGP_OWNED_PREPARATION=1`.
Archive/repeats/commands `logs/optimization/native-owned-validation/`;
[validation](results/native-owned-validation.json).
Dependency hashes at `logs/optimization/native-allocator/dependency.json`.
Earlier allocator and selected-site results remain in their historical manifests.

K selects logical sites before materializing contracts and uses owned scalar
parent continuations. Full fallback for multiple root requests, projected
budgets, registry constants, staged payloads or explicit multiple workers.
GPU operators/RNG/cases/admission semantics unchanged. Two GPU copybacks/AST
rebuilds remain. J's execution-only admission is incorporated as K's internal
step; its standalone flag/entry is removed (small isolated benefit).

Rejected L: exact CPU list-slice cache.64K entries: Sum17.441→22.109s,
13.09M hits/23.47M misses,358 capacity clears, zero mutable-payload invalidations.
524K diagnostic:35.24M hits/1.32M misses (96.4%), but snapshot process still17.88s
versus ~18.5s reference with additional memory. High hit rate gives too little
benefit to retain this mutable-registry memo policy. All3 tasks' CPU generation
fields and3072 CPU/GPU snapshot records match; safety tests passed. Feature,
flag, exclusive API/stats/tests removed; raw binaries/patch/counters retained in
`logs/optimization/native-cpu-slice/`. Short registered-list Slice bounds fix and
its negative test remain. Next CPU direction needs cheaper view execution, not
another cache capacity sweep.
Retained M: allocator reduces all three GPU generation times15-18%; all nine
32-generation searches preserve non-time results/exports. Full120-test pass.
New Median NVTX diagnostic (373ms profiled vs334ms unprofiled):13,915 runtime
identities92.5ms; membership Matcher21.5ms self, witness22.0ms self;
2 decode passes128.4ms inclusive; selected analysis63.4ms inclusive.
Raw timeline/attribution: `logs/optimization/native-selected-profile/`.
Retained N (`d72b5f4`, `GAGP_OWNED_PREPARATION=1`): an owned immutable scalar population can share its admission
witness/VerifiedAst/identity through native preprocess/pack, avoiding repeated
serialization and proof copies while preserving public input checks. Keep GPU
operators, RNG, membership and both copybacks unchanged for this experiment.
Quick paired full generation: Sum325.464->284.576 /House350.142->297.930 /
Median330.887->289.507ms. Four focused tests pass (including owner/input mutation,
request/domain/payload/index/lifetime checks). Raw `logs/optimization/native-owned-preparation/`;
9-repeat, fixed snapshot and3-seed4/32-gen validation `native-owned-validation/`.
Formal9-repeat and3-seed4/32-gen comparisons pass; retained.
Candidate O (`GAGP_CPU_REGION_VIEWS=1`): invocation-local IntList views
for statically proved region phases, eliminating intermediate Slice registry
hash/copy work; original CPU VM/region control flow and numeric/fuel remain.
5 focused parity/region/session tests and full120 suite pass. Initial P1024 CPU
Sum16942.169->9241.978ms, House7806.760->7848.474, Median712.673->751.051.
All CPU generation non-time fields and3072 CPU/GPU snapshots match.
Coordinate views had no supported performance benefit; implementation is now
restricted by `SequenceWindows` structure (not task name). Formal9 CPU repeats,
full-case snapshots and5-mode quick controls running in
`logs/optimization/native-cpu-window-validation/`. Initial all-region prototype
binary/results retained in `logs/optimization/native-cpu-views/`.
No CPU workers, no view escape into root code; memo list states unsupported.
Temporary slice registry contents are intentionally not materialized in this
profile (same scalar-content aim as GPU views); no claimed speedup yet.

## Current fair baseline

Source parent `9ce96a9`; thread-policy checkpoint follows. `GAGP_HOST_THREADS=1`
is now the default; `2..20` is optional host parallelism, excluded from fair
headlines. One-thread overlap runs preparation on the calling thread after GPU
launch, before synchronization. Existing native fast-profile flags below remain.

Exploratory P1024/full1024 baseline (one process, 1 warmup + 3 repeats):
Sum GPU-overlap 724.686 ms, House 853.414 ms, Median 792.708 ms.
These supersede historical multiworker times **for the 1T contract**; they do not
claim a performance improvement. Raw five-mode measurements and binary:
`logs/optimization/native-1t-20261002/`; compact manifest:
[1T baseline](results/native-1t-baseline.json).

Validation: complete Release build; all 120 native tests passed across full run
and focused reruns after adapting two worker-count expectations. Explicit 4-worker
resource/cache/parity tests also passed; tool tests 5, repository tests 24 passed.
A clone trace plus pthread_create interposer/backtrace smoke records three CUDA
driver thread creations, no GAGP host workers. Logs are alongside the baseline.

Candidate A (`GAGP_REUSE_ADMISSION_COMPILE=1`): retain admission's existing
lowering in its opaque native certificate, scalar constants only. Exploratory
Sum640.959 / House736.418 / Median699.902 ms; all non-timing fields equal to 1T
baseline. Compile drops to 12.8–13.9 ms. Tests reject stale AST/constants/input
layout/fuel and decline registry-dependent constants. Needs formal and changing
search validation before promotion. Raw `logs/optimization/native-admission-compile`.

NVTX diagnosis of candidate A, Median: site analysis self178.6 ms, runtime
identity105.9 ms/11024 calls, bytecode+region verification96.7 ms; full profiled
722.7 ms. Nested ranges are not additive. Raw source-only instrumentation and
restored sources at `logs/optimization/native-1t-host-profile/`.

Retained candidate C: 1T decode uses ordered admission in the owning run context;
no discarded worker caches or speculative child warming. Formal 3 processes ×
(1 warmup + 3 repeats), interleaved with 1T controls: Sum552.771 / House637.124 /
Median590.220 ms, reductions22.2% /25.1% /21.1%. All 3072 snapshot program records
match (full cases, GPU counts and CPU fitness); all nine 3-seed×4-generation
search comparisons match non-timing fields and exported ASTs. Quality limits:
short runs only, no convergence/long-run assertion. Raw commands/binaries/repeats:
`logs/optimization/native-checkpoint-validation/`; summary
[validation](results/native-1t-reuse-validation.json).

Rejected B: grammar-wide local-resource eligibility cache. Sum657.988 /
House752.135 /Median697.685 ms versus candidate A640.959 /736.418 /699.902;
no reliable gain. The cache change is reverted, raw data in
`logs/optimization/native-static-charges/`.

Rejected D: compact-before-admission. No performance gain (571.8/656.3/604.5 ms)
and a new negative test caught unused duplicate-name validation being hidden
by compaction. The implementation, flag and proposed spec change are reverted;
the acceptance tests remain and pass on the retained implementation. Raw rejected
patch/timings in `logs/optimization/native-compact-admission/`; no maintained entry.

Candidate E: analysis-local site contract reuse (`GAGP_REUSE_SITE_CONTRACTS=1`).
Same verified scope/nonterminal/template/group/closure reuses the exact key;
repeated-hole intersections retain full construction. Quick Sum505.950 /
House595.256 /Median542.493 ms, unchanged non-timing results. Differential
contract fixtures plus reproduction/bounded variation pass. Formal repeats and
changing-search checks pending. Raw `logs/optimization/native-site-contracts/`.
Detailed Median NVTX: 141886 contract constructions (~94.7 ms self), runtime
identity90.2 ms, root/region bytecode verification91.8 ms. Instrumented timings
are diagnosis only. Next: shared immutable verifier locals between instructions,
copy on Store/merge; preserve traversal and admission/error semantics.

Candidate F: verifier local environments share ownership between unchanged
instruction states; Store and control-flow merge detach before mutation. CFG
order, validation rules and diagnostics unchanged. Quick Sum455.695 /
House498.343 /Median484.025 ms after E; all non-timing fields match. Four focused
tests pass; independent old-source differential checks match all 100000 ordinary
bytecode and 20000 bounded-region results including exact diagnostics and resource
summaries. Full Release build passes. Raw `logs/optimization/native-verifier-locals/`.
E+F formal validation passed; archived in
`logs/optimization/native-contract-verifier-validation/`. Superseded by I then K.

Rejected G: retaining full sites in native certificates. Fixed generation slowed
~30–47 ms, and 32-generation searches slowed3–4% with RSS+120–140 MB. All short
and32-generation fields/exports matched, but copying/retaining sites lost.
Implementation/flag/exclusive tests removed; raw rejected patch and evidence:
`logs/optimization/native-certified-sites/`. Best runtime unchanged.
Rejected H: retain the run analysis context (also tested a smaller2P capacity).
All short and32-generation results/ASTs matched. After correctly charging final
cache destruction, fixed-gen changes were only ~1–3%, and32-generation process
wall Sum18.78→19.90s, House17.69→17.32s, Median20.53→19.63s. Mixed/small benefit
and Sum regression do not justify this extra lifetime policy. Implementation,
flags, exclusive eligibility/ownership tests and guide removed. Raw corrected
patch/results `logs/optimization/native-run-analysis-accounted/`; earlier timing
notices remain in `native-run-analysis` and `native-run-analysis-2p`.

Timing audit adds `evolve_wall_ms` outside the actual evolve call. Historical
`evolve_call_ms` is an internal timer excluding function-local destructors.
Keep both and original generation boundaries. Fixed benchmark smoke covers it.

Latest best-version NVTX: Median profiled563.5 ms, identity89.4, site build92.7,
key construction9.3, root/region verifier39.8 ms. Separate scopes are nested.
Raw `logs/optimization/native-refreshed-profile/`. Process-local SIGPROF PC samples
(no workers/system settings) in `logs/optimization/native-cpu-pc-profile/` show
allocation/free pressure and repeated linear descriptor lookups; CPU Sum also
spends substantial samples hashing/registering list slices. Full-process samples
include setup, not a precise per-generation attribution. Descriptor indexing (I) was subsequently validated:415.657/430.558/437.677 ms,
all full snapshots and short searches match. Raw `native-node-index-validation`.

Current best runtime `ed19fde`, archived binary/flags/commands in
`logs/optimization/native-contract-verifier-validation/manifest.json`.
Enable retained flags plus `GAGP_HOST_THREADS=1 GAGP_REUSE_ADMISSION_COMPILE=1
GAGP_REUSE_SITE_CONTRACTS=1`; round2 runner supplies these.
Formal nine-repeat Sum465.603 /House505.974 /Median499.036 ms versus same-round
controls562.207 /650.451 /590.818. Every snapshot/non-timing search field and
exported AST matches across 3 seeds ×4 generations. All120 native tests pass
across full run and repository rerun (admission contract's freeze hash updated).
[Raw-repeat summary](results/native-contract-verifier-validation.json).
Next: reprofile, then retain certified site analysis across actual generations.
Do not merely persist the compatibility registry: its numeric IDs are local and
its 65536-key bound must not become a long-run cumulative admission restriction.
CPU perf sampling remains blocked by perf_event_paranoid=4; optional request
pending. No system settings changed.

---

Worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`, branch
`opt/gpu-generation-20261001`. The maintained mainline is GPU evaluation plus
GPU reproduction. CPU controls remain part of the five-mode fixed benchmark.

## Removed route

CPU fragment evolution/reproduction, its phase-bank predecessor, exclusive
owners/executable certificates, owned packing and capacity buckets, probe modes,
runner options and exclusive tests have been deleted. They are not isolated
alternatives, fallback reproduction backends, or future recommended work.

Historical measurements remain unchanged; their status is recorded in the
[archive notice](results/README.md). The old CPU-fragment timings are withdrawn
from the main target assessment. Original worktree modifications, frozen inputs,
archived binaries and historical logs remain protected. No branch reset or push.

## Retained native settings

Presence enables these flags (including a value of `0`):
```
GAGP_CUDA_DEVICE=0
GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1
GAGP_COMPACT_FRAMES=1 GAGP_CONSTANT_PHASE=1 GAGP_POPULATION_HANDOFF=1
GAGP_DIRECT_PHASE=1 GAGP_DIRECT_ROOT=1 GAGP_WINDOW_EXECUTOR=1
GAGP_UNBOXED_WINDOW_FRAMES=1 GAGP_BOUND_ADD_PHASE=1
```
Direct phase/root, window execution, unboxed frames, bound Add, case sorting,
compact workspaces and native generation-owned analysis handoff remain. The
native bytecode verifier, grammar admission/certificates, donor preparation and
GPU reproduction/decode paths retain their safety contracts and tests.

Most recent nine-repeat native medians at P1024/full1024 cases, before cleanup:
Sum205.448, House207.503, Median178.636 ms. These are historical native values,
not new cleanup measurements; 40× is not achieved. See native rows in
[round2-final.json](results/round2-final.json). Historical control binary
`logs/optimization/final_bench` and frozen inputs must not be overwritten.

## Reproduce maintained paths

Build the existing Release CUDA configuration:
```
cmake --build cpp/build/release -j10
```
The runner supplies the native flags above. Use a new output directory each time:
```
python3 benchmarks/fixed_asgp/optimization/round2.py formal NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --baseline logs/optimization/final_bench
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --generations 32
python3 benchmarks/fixed_asgp/optimization/round2.py scaling NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench
```
Scaling retains `asgp_1t`, `gagp_cpu`, `gpu_eval`, `gpu_repro`, `gpu_overlap` at
P1024/2048/4096/8192, using the existing frozen P8192 prefixes and all 1024 cases.
`snapshot` and `search` are native diagnostic modes. Never rerun prepare against
original inputs. GPU runs are serial; profile only for diagnosis, not headlines.

## Cleanup validation

Logs and dependency/protection audit:
`logs/optimization/cpu-fragment-removal-20261002/`.
Full Release build and 119 native tests passed. Tools: 89 tests (4 optional skips);
repository: 24 tests passed. Three tasks × five modes × (one warmup + three measured runs)
at P32/full 1024 cases matched every non-timing field in the pre-cleanup archived
native binary. This is smoke/regression evidence, not a new performance baseline.
Removed modes reject with `unknown mode`; no deleted symbols remain in native
libraries and no removed entrypoints remain in maintained source/guides/runners.
All original-file hashes match. [Validation record](results/cpu-fragment-removal-20261002.json).
The cleaned binary is archived at
`logs/optimization/cpu-fragment-removal-20261002/bench`.
No new architecture or optimization experiment is part of this cleanup.

Next high-value GPU question: can native GPU offspring be lowered directly into
existing proven phase bytecode, without CPU AST reconstruction between passes?
Start with a bounded prefix-phase lowering prototype and compare it against the
existing compiler (code, constants, fuel, outputs). This is necessary groundwork,
not a speedup claim or another reproduction backend. Grammar membership must
remain a distinct proof; type-correct phase lowering alone does not establish it.

P prototype: device prefix-phase lowering is now runnable in
`cpp/build/release/gagp_test_gpu_phase_compile [PREPARED_JSON GRAMMAR_JSON]`.
512 generated expressions (including branch/fuel cases) +8 negative cases pass;
all14336 phases from3 frozen P1024 populations match CPU compiler instructions,
constants and fuel. CUDA12.6 memcheck:0 errors. System `/usr/bin/compute-sanitizer`
misses its injection library; use `/usr/local/cuda-12.6/bin/compute-sanitizer`.
Warm batch medians ~0.62/0.89/0.55ms (Sum/House/Median); cold launch~2.9-3.2ms,
H2D~0.36-0.40ms,D2H~2.0-2.5ms. Host diagnostic setup~212-230ms includes JSON,
independent admission, reference CPU compilation and normalization; not a fair
end-to-end replacement measurement.40 registers,2816-byte indexed stack,0 spills.
[Raw manifest](results/native-gpu-phase-compile.json). Isolated test-only path;
no grammar trust, operators, evaluator or generation timing changed. This is
useful groundwork, not an adopted generation speedup.

O CPU window candidate follow-up: O2 lazy allocation and O3 separate VM
instantiations did not remove generic regressions. O4 moved private view handling
to scoped builtins; O5 delayed conversion but slowed recursive execution. O6
restricted eager setup to proven constant-false window predicates; O7 outlined
ownership/cleanup from the ordinary VM. O7 nine repeats: Sum16877.975->9520.944ms,
House7789.829->7982.784ms (+2.5%), Median707.697->714.291ms (+0.9%). GPU controls
remain ~280/292/284ms. This CPU candidate is NOT promoted; artifacts, all nine
raw repeats and source experiments are under `native-cpu-window-final-check/`
and preceding `native-cpu-window-*` directories. Current source also packs shared
CPU case bindings once per evaluator call (focused parity passed; separate timing
pending). No application workers were added.

P2 GPU donor construction: current opt-in `GAGP_GPU_DONORS=1` extends owned
selected-site native GPU mutation. Production/scope/domain tables build lazily
inside timed preparation; GPU makes fresh donors, native GPU applies mutation,
final canonical child admission remains. Unsupported jobs use native donors;
new donor RNG/budget-allocation trajectory is explicit, not canonical replay.
All4096 phase donors from3 frozen grammars independently admit/lower; default
and scope-permutation fixtures plus native deferred-preparation regression pass.
Current prototype source is not promoted and still needs full quality/scaling.

INVALID experiment `logs/optimization/native-gpu-donors-integration/`: a misplaced
deferred-batch loop produced no donors, so296 mutations fell back. The203/214/206ms
numbers were retracted; INVALID.txt preserves the reason. Accounting invariant
and actual native preparation test now prevent this failure. Corrected quick run
`native-gpu-donors-corrected/`: Sum280.360->288.195, House293.288->292.745,
Median292.184->281.072ms;1092/1064/1112 GPU donors,92/120/72 native fallback donors,
zero offspring fallback, generation or admission rejection. Not a stable overall
improvement. P2b packs lexical scope maps into24 bits (8 slots), reducing compiler
local stack8624->3168 bytes, zero spills; active A/B and selective batch trace:
`logs/optimization/native-gpu-donors-compact/`.

Refreshed N NVTX diagnostic: `native-owned-profile/`,323.803ms profiled versus
~289ms unprofiled Median.7771 identities49.172ms;2 decode passes129.812ms inclusive;
1320 admissions116.728ms inclusive;Matcher21.642ms self,Witness22.223ms self;
compile39.889ms inclusive, selected analysis41.973ms inclusive. Do not subtract
these annotated/overlapped times from formal totals.

### P2c checkpoint — GPU grammar donors, compact output

- Native GPU selection/crossover/mutation remain; opt-in `GAGP_GPU_DONORS=1`
  constructs supported fresh donors on GPU, then uses existing native splicing
  and canonical final admission. No CPU workers. Unsupported requests explicitly
  use the native donor preparation. New production/budget RNG trajectory.
- Nine unprofiled repeats against N, interleaved 3 processes × warm + 3:
  Sum 278.274330 → 262.496018 ms; House 292.987050 → 270.175429 ms;
  Median 283.401071 → 263.846364 ms. Raw/manifest/summary:
  `logs/optimization/native-gpu-donors-validation/`.
- Full 123-test suite passed. Three seeds × four generations: complete 1,048,576
  pairs/generation, zero timeouts/unscored, top-16 CPU fitness exact. New trajectories
  retain diversity but Sum grows larger programs and total search is slower;
  **not promoted**. 32-generation comparison underway in `native-gpu-donors-long/`.
- Compact output removes unused-capacity D2H/host allocation, not evaluated work.
  Earlier `native-gpu-donors-integration/` timings are INVALID (deferred batch was
  misplaced; mutation fallback); accounting invariant and 128-donor regression
  now exercise actual construction. Never cite that directory as a speedup.
- Next hypothesis: compile_population serializes each admitted AST for the cache
  key and again to validate its executable; one private combined lookup should
  eliminate the duplicate while retaining mutation/input/fuel validation.

### Q — single evaluation identity

- `GAGP_SINGLE_EVAL_IDENTITY=1` computes the current runtime identity once and
  returns the matching sealed executable in the same private lookup. External
  AST mutation, input order and fuel still invalidate reuse. No RNG/operator change.
- Focused membership/reproduction/parity tests pass; all non-time search fields
  and exports match for three tasks × four generations. Raw quick and nine-repeat
  results: `logs/optimization/native-single-identity{,-formal}/`.
- Next: preserve ownership between the already-admitted GPU crossover output
  and mutation preparation; no admission deletion at external boundaries.

### R — immutable crossover-to-mutation handoff

- `GAGP_OWNED_CROSSOVER_HANDOFF=1`, with Q, removes repeated certification/copy
  of the private, just-admitted crossover population. Both GPU operator passes,
  AST decodes and final canonical admission remain. No RNG/language change.
- Nine repeats vs N: Sum281.756493→269.700719, House297.769050→278.160918,
  Median288.717111→268.244850 ms. All three seeds ×32 generations match every
  non-time search field and final exported AST, including top16 CPU reevaluation.
  Search wall improves about5–8%; raw `native-owned-handoff-validation/`.
- Four focused tests pass, including repeated-hole/capture, wrong source/context,
  invalid output and destroyed-parent lifetime. Full123 suite passes; all3072
  complete-case CPU/GPU snapshot rows match N.
- Updated host/CUDA diagnostic underway in `native-owned-handoff-profile/`.
- P2 GPU donor32-generation tests did not justify promotion: most whole searches
  slower with larger programs despite fixed-parent gain. Keep it off the best
  flags; it is a GPU construction experiment, not a headline performance claim.

Q+R activation: N flags plus `GAGP_SINGLE_EVAL_IDENTITY=1` and
`GAGP_OWNED_CROSSOVER_HANDOFF=1`; keep GPU_DONORS and CPU_REGION_VIEWS unset.
Profile302.222ms (instrumented Median, not formal268.245ms): runtime identities
5736calls/34.001ms; decode2calls/128.005ms inclusive; matcher21.762ms self;
witness22.347ms self; compile16.341ms self; constant streams5.209ms.
Median fitness kernel2.113ms on final profiled generation. Remaining host proof
work dominates. Full123 suite and3072 snapshot rows pass. Scaling pending.

### S — incremental canonical crossover matcher prototype

- `GAGP_TRANSPORT_CROSSOVER_PROOF=1` uses immutable parents plus actual copied
  node/value correspondence to seed unchanged/donor subtree decisions. Affected
  ancestors are removed and re-matched in declaration order. Native verification,
  witness/resource construction and lowering remain; no RNG/language change.
- Zero-node alias roots, noninjective scope maps, coupled occurrence sites and
  inner-binder donors decline to ordinary admission. First disjoint-root profile
  admitted0 benchmark children (`native-proof-transport/`), not an improvement.
- Actual incremental matcher handles982/984/998 crossover children. Initial
  table/AST copies cancelled savings; moving owned temporaries gives quick
  paired271.820→262.111 /275.211→267.552 /266.850→263.308ms. Search4-generation
  times mixed; **not promoted**, formal/long comparison pending. Raw
  `native-incremental-matcher{,-move}/`.
- Focused4 tests pass; added512-child canonical witness check includes ambiguous
  ancestor alternatives and explicit alias fallback. Final ASTs remain exact.
- Remaining eval candidate: bounded one-coordinate memo lookup currently scans
  all occupied cells. Test direct indexing with a register validity bitmap,
  keeping complete state, lazy execution, fuel/base/capacity order unchanged.

S decision: **rejected from active runtime**, reproducible source checkpoint
`ed0448a`; patch/binary/raw in `native-incremental-matcher-validation/`. Nine
paired medians R→S: Sum261.695204→261.260983, House274.157456→270.559995,
Median272.448157→268.385679ms. Larger quick gains did not repeat. Sum gain0.2%,
House/Median1–2% with overlapping ranges; insufficient for added proof transport
complexity. Dedicated512-child ambiguity/alias witness tests passed, and the
prototype remains useful evidence, not the recommended path. Removed its flag,
private admission path, exclusive test and active guide/navigation entries.
R remains best; T dense coordinate memo now awaiting serial build/evaluation.

### T — dense coordinate memo: rejected

- One-Int-state memo domain <=128 slots; direct indexing plus per-invocation
  validity bitmap, with original fuel/base/capacity order. Three focused tests
  pass, including raw values/errors/fuel and boundary checks; all1024 House
  full-case CPU/GPU snapshot rows exact.
- Two paired orders give House272.33→278.59 and275.00→284.09ms; flag-off
  candidate272.46/276.44ms. Sum/Median changes are order-dependent noise.
  No end-to-end benefit: removed active code/flag/exclusive tests and guide.
  Patch, binary, raw JSON retained in `logs/optimization/native-dense-memo/`;
  tracked summary `results/native-dense-memo.json`.
- R remains best. Next hypothesis: table compaction after successful admission
  need not repeat matching, witness building and lowering when only unused
  scalar constants are removed and names/scopes are unchanged. Validate first,
  then transform owned data and bind its proof before publication.

### U — post-admission constant compaction: rejected

- Minimal owned path retains proof after full original-input validation, only
  remapping scalar constant indices; changed names use full readmission.
  Focused membership/reproduction tests pass including malformed unused entries,
  canonical witness comparison and stale public certificate rejection.
- Quick medians R→U:262.10→264.00 /268.41→274.96 /263.29→267.04ms.
  Diagnostic5280/5280 admissions have already compact constant/name tables:
  native GPU copyback already eliminates this work. No gain; source/flag and
  exclusive tests removed. Existing shared compaction safety tests retained.
- Corrected attribution: R Median1320 matcher/lowering calls under actual
  offspring decode; other1184 calls belong to donor preparation. This is not
  evidence of duplicate full offspring admission.
- Raw `native-admitted-compaction/`. Diagnostic runner overwrote its own quick
  archived binary (quick original SHA remains in manifest); diagnostic source
  patch retained. Quick timing is rejected evidence, not a reproducible best.

### V1 — GPU expression subtree variation foundation

- Device-owned prefix trees carry distinct nonterminal sites (not type-only
  tags). GPU crossover copies site metadata; new GPU mutation donors annotate
  construction once. No reannotation of existing offspring in operators.
  Independent diagnostic reconstruction checks every transported site separately.
- Prototype profile: pure Int/Bool/IntList expressions, scalar Int/Bool constants,
  identity scope maps, default variation/resource policy, no aliases/inner
  binders/templates/repeated holes inside phases. Unsupported combinations
  explicitly decline. This is NOT CPU fragment reproduction, a finite phase bank,
  a native benchmark mode, or a complete-generation performance claim.
- 1024 phases ×32 operator rounds for all16 three-task expression nonterminals;
 5120 independently admitted exports per root, first/final1024 GPU bytecodes
  exactly match CPU instructions/constants/fuel. Fresh mutation uses destination
  remaining node/depth budget:32768/32768 admitted per root. Crossover capacity
  rejections explicit. Distinct audited/final genotypes recorded separately.
- Cross+fresh-mutation+lowering warm CUDA-event medians0.95–2.43ms per1024 phases
  on these profiles. No selection fitness/evaluation/setup/export included, so
  not a generation speedup. Raw `native-gpu-phase-variation/{sum,house,median}-v3.log`.
- Scalar recursive fixture: memory sanitizer0 errors, node/index/NT/scope/capacity
  guards, exact transported metadata checked. Finite-language scope fixture
  exposed an overly broad diversity assertion; assertion retained for recursive
  built-in fixture, finite languages report actual uniqueness and validate all
  members. Scope fixture supports BA/Root, declines permutation AB and Float.
- Next: whole-program static skeleton admission, frozen import/export and resource
  accounting, then native GPU evaluator integration and actual selection/search.
  R remains validated best; this foundation is not promoted.

V2 cold layout adapter: `gpu/phase_layout.hpp` independently admits inputs,
extracts phase-aligned independent holes, and exports ASTs without carried proof.
All3072 frozen parents and the full1024-member R seed2 Median32-generation final
population roundtrip with exactly equal compiled instructions/constants/fuel,
bindings and plans. A separate two-coordinate memo grammar fixture also passes;
coupled/inner-lexical fixture explicitly declines. Raw `layout-*.log` alongsideV1.
This proves representability (including native later Median), not search quality.
Next implementation: run-owned double-buffered GPU phase population, completed
fitness-driven tournament/crossover/mutation, GPU lowering, timed code copyback
and native evaluator integration. No CPU reproduction. Best still R.


### V3 — native resident GPU generation, opt-in integration

- `GAGP_GPU_PHASE_POPULATION=1` integrates V1/V2 into existing GPU eval/repro
  modes. Selection/cross/fresh mutation/lowering all GPU, one host thread.
  Between operators and generations no AST rebuild/reverse membership or CPU
  compile; live bytecode copyback/integration remains. Existing native evaluator
  still independently verifies/repacks bytecode each evaluation.
- Explicit unsupported-profile fallback recorded; one admitted history-best
  export/gen and requested final outputs timed. Bounded double buffers, no
  ancestor storage. Default backend unchanged. Operator/RNG differences in
  `docs/design/gpu-reproduction.md`; final changed-child counts not attempts.
- Independent two-coordinate memo fixture:32 generations,P16,full1024cases,
  every exported genome independently admitted/CPU reevaluated exactly;26 unique,
  device storage5,249,984→5,251,568 bytes. Oversized declared capacity explicitly
  rejected, separate authored128-cell variant succeeds. Input/export isolation,
  input-name/index/incomplete-fitness guards, native evolution adapter and explicit
  unsupported-policy fallback pass (`test-integration.log`).
- One-process quick (warmup+3) full P1024 generation medians Sum64.918 /House61.889
  /Median30.656ms; cold import/admission343.993/310.496/352.181ms. Whole-call walls
  434.745/396.574/410.360ms. NOT promoted: formal repeats/longer quality pending.
- Three seeds ×4 generations: full1,048,576 cases/gen,0 unscored, top16 full-case
  CPU reevaluation exact. Final unique counts Sum964–982,House792–845,Median993–994.
  Initial error distribution unchanged; subsequent errors fall. Raw/archive
  `logs/optimization/native-gpu-phase-integration/` (manifest,quick,quality,ASTs).
- Next: evaluator packing remains16–20ms. Hypothesis: a private owner-bound
  certified bytecode view can reuse cold root/region verification plus checked GPU
  phase compiler bounds, without a public verified flag or mutable external
  metadata. Then measure and test31+ generations, wall-clock quality and scaling.
  R remains historical validated best; V3 is a substantial unpromoted candidate.
- V3 focused CTest3/3 pass; integrated32-generation fixture compute-sanitizer
  memcheck0 errors. Raw `native-gpu-phase-integration/{ctest,memcheck}.log`.

### V4 — private executable proof reuse, candidate

- `GAGP_OWNED_PHASE_PACK=1` in the GPU genotype owner uses a private synchronous
  borrowed view. Cold native verification covers immutable root/plan/bindings;
  checked GPU lowering supplies new phase type/instruction/stack proofs. No
  public verified flag; external `eval_programs` still verifies fully. Packing
  retains fuel and transport checks, and view capability analysis still runs.
- 32-generation independent fixture: owned GPU fitness and case counts exactly
  equal ordinary verified packing and independent exported CPU programs.
- Quick warmup+3 medians Sum51.961 /House47.949 /Median17.354ms;
  packing4.004/4.296/4.020ms (V3~16–20ms). Init359/325/364ms; whole-call436/398/409ms.
  Sum~35.4x,House~26.7x frozen denominator; not a formal target claim yet.
- House nsys kernel diagnostic averages (includes cold/warm launches): eval32.234,
  mutation3.297,crossover2.266,lowering1.329,serial offsets0.521,comparison0.355ms.
  Raw `native-gpu-owned-pack/profile/`; these are not headline times.
- `native-gpu-owned-pack/quality/run.py` running3 seeds×32gens, full1024-case
  diagnostics and all1024 final CPU re-evals for seed0,top16 for others. Audit
  fields explicitly record count/mismatches; output remains the original top16.
- Next hypothesis: a one-coordinate structural executor can avoid the generic
  multidimensional state/transition/frame loop while keeping original lazy
  recursion, linear memo lookup, phase expressions and fuel/error order. It is
  structurally selected, not task-selected. Do not retry rejected dense memo.

V4 quality completed:3 seeds×32 generations. Seed0 all1024 final genomes per
workload independently admitted and CPU reevaluated on full cases with0 fitness
mismatches; other seeds top16 exact. Full1,048,576 pairs/gen,0 unscored.
Search walls Sum5.419–6.175s,House2.624–2.818s,Median1.408–1.791s. Last-gen best
Sum0 all seeds,House -7043/-8431/-7460,Median -28478/-19364/-26258. Not convergence
or equal-wall equivalence. Sum mean final nodes217 versus R137 (seed0), and its
late generation166.6ms: initial-snapshot speedup must not represent whole search.
RSS1.29–1.62GB includes final population exports/audits; bounded-memory long run
still pending. Raw `native-gpu-owned-pack/quality/` includes all exports/resources.

### W — structural single-coordinate executor

- `GAGP_COORDINATE_EXECUTOR=1`: one Int coordinate/result,literal bounds,1–2
  offset requests,no preparations/bound operands. Unboxed frames; same lazy
  phases,linear memo,fuel/boundary/capacity behavior. Generic fallback otherwise.
- Initial full House test caught wrong frame byte stride in kernel slicing;
  fixed separately from allocation and covered by37 distinct programs ×1024
  cases. Initial failed binary/log retained under `native-coordinate-executor/`.
  New focused matrix covers inclusive/exclusive bounds,duplicate requests,memo
  on/off,zero/tight capacity and fuel,plus existing lazy-capture/2D fixtures.
  Nonmemoized positive cell-limit test explicitly rejects malformed metadata.
- Corrected v2 focused `gagp_test_bounded_region_gpu` passes. Same-binary flag-off/on
  three processes each, warmup+3, interleaved House medians48.302→44.118ms.
  Off range47.330–59.015,on43.190–44.915ms. ~29.1x frozen ASGP,not30x yet.
  Sum/Median quick51.859/17.401ms. Raw/archive/9repeats
  `native-coordinate-executor/v2/validation.json`; memcheck running.
- Next: reuse typed-view capability proof from immutable root and checked GPU
  phase compiler; retain actual case-type and payload checks. No public proof flag.

W full `gagp_test_bounded_region_gpu` compute-sanitizer memcheck:0 errors.

### X — private typed-view capability reuse

- `GAGP_OWNED_PHASE_CAPS=1`: cold immutable root/phase capability proof is tied
  to ordered grammar input types. Checked GPU phase compiler proves its closed
  typed operation/forward-branch subset; runtime still checks actual case-type
  signature,payload contents,stack/local thresholds. Different case types fall
  back to ordinary analysis.32-gen owned/ordinary/CPU fixture passes including
  a mismatched case-type session, which must use mixed fallback.
- Three independent processes,warmup+3 each, P1024/full1024 cases: Sum50.126518ms
  (49.546908–52.783495),House41.976280 (41.468189–43.851836),Median15.701990
  (15.545705–16.123876). Frozen headline medians~36.73x/~30.54x/~3.93x.
  House tail still crosses30x threshold. Raw9 repeats and immutable binary in
  `native-owned-capabilities/{formal.json,manifest.json}`. Cold/whole-call costs
  separately present in every row; not an equivalent full-search speedup.
- Next: GPU mutation currently generates construction origins then traverses the
  new tree again to annotate grammar sites. Record those sites at construction
  and independently audit them; preserve RNG/operator outputs, remove this work.
  Final whole-suite/scaling/equal-wall quality still required before promotion.

### Y — constructor-emitted site metadata: rejected

- GPU constructor recorded sites while generating donors, eliminating later
  annotation. Independent site reconstruction and32-gen CPU reevaluation pass.
  CUDA resources decreased from4784B/80 registers to3248B/76 registers,no spills.
- Same-binary flag A/B,3 processes each,warmup+3: House42.026227→43.539508ms;
  ranges41.422101–42.780943 vs43.086235–44.247228. Both evaluator and reproduction
  GPU times shifted, so no defensible instruction-level causal speedup claim.
- Rejected: restored X implementation and tests, removed active experimental
  switch. Patch,immutable binary,all raw repeats retained under
  `native-construction-sites/`. X remains candidate. Do not retry merely because
  the compiler resource report looks smaller.
- Freeze candidate for final validation. Add actual generation-completion times
  including cold initialization, and optional already-retained history exports,
  to compare completed prefixes at an identical5-second search budget. Longer
  fixed-generation runs preserve operators and all cases; no early-stop shortcut.
  Predeclared screening thresholds for this run:Sum fitness0,House>=-8000,
  Median>=-25000. Three seeds only; not convergence equivalence.

Final native rerun entry (same tested serial runner, parameterized output path):
`python3 benchmarks/fixed_asgp/optimization/native_candidate.py daily --out
logs/optimization/native-daily-check`. `paired` repeats three independent processes,
`scaling` covers all populations. Existing archived experiment still resumes with
its original `logs/optimization/native-counted-validation/run.py`; runtime binary
unchanged across final scaling and the later test-only profile-name correction. No prepare/input overwrite.

Final acceptance:
- Full suite127/127 with adopted evaluator/CPU-view settings; the explicit GPU
  genotype tests select their distinct operator profile.5 fixed-tool,24 repository
  checks pass. Numeric boundary profile-name assertion was corrected to include
  the eligible coordinate executor; exact values/fuel/errors were not relaxed.
- Full60-cell matrix and240 warm/measured rows retained in
  results/native-counted-scaling.csv and native-counted-scaling-raw.csv.
  Same-round fastest GPU ratios for P1024/2048/4096/8192:
  Sum37.77/39.80/44.73/48.81;House32.94/36.87/40.35/39.91;
  Median4.99/4.40/5.73/5.46. These use matrix controls, not headline denominators.
- P1024 headline remains9-repeat48.884881/40.384029/12.213825ms and frozen
  ASGP ratios37.66/31.75/5.06. Cold import333–370ms is not included in those
  generation rows; it is charged once per search, separately reported and
  included in all equal-wall-clock comparisons. Real64/96/512-generation
  searches took11.158–14.761 /7.609–9.318 /29.926–31.689s; initial admission
  occupied2.49–3.28% /3.55–4.38% /1.17–1.26%. Do not infer30x whole searches.
- Runtime/archived benchmark SHA unchanged after the full matrix. Parameterized
  checked-in runner resume verified without changing manifest or outputs.
- Objective reached for Sum/House; Median substantially improved. No further
  runtime experiment pending. Remaining work, if a new round is requested:
  reduce eval interpreter cost, CPU reproduction in GPU-e, padded GPU genotype
  allocation/cold import, and retained-history proof memory. These are not
  completed optimizations. Failed dense memo/construction-site/hash-only paths
  remain rejected as recorded above. CPU fragment route remains removed.

## CPU perf access and focused diagnosis — 2026-10-02

Host owner enabled perf_event_paranoid=1; own-process cycles:u sampling works.
No installation, system change, worker thread or runtime change by this experiment.
Best checkpoint remains beecdf5 / runtime d1712a5 with the recorded best flags.
Archive: logs/optimization/native-perf-20261002 (commands, flags, binary SHA,
raw perf.data, script, reports and summarizer). Tracked compact evidence:
results/native-perf-20261002.json. All non-time benchmark fields match controls.

Sum CPU whole-process self cycles: interpreter43.59%, region driver14.26%,
phase binding11.18%, builtins8.94%, ValueStack push7.43%. This motivates
precompiled phase bindings / less interpreter frame and Value movement.
Median GPU-e deeper999Hz/32768-byte callchain capture:1296 reproduction samples
out of2345; within recognized reproduction stacks inclusive variation analysis
86.08%, certification72.40%, witness18.19%, runtime identity15.67%, serialization
13.05%, compile11.45%. Inclusive percentages overlap; do not add or translate
them directly into generation milliseconds. Unknown leaf14.76% remains within
the reproduction subset; unwind coverage is incomplete. Initial shallow capture
is retained as such, not used to claim precise per-generation attribution.
Next host experiment should remove repeated analysis/certification inside existing
native operators, preserving CPU1T, grammar admission and GPU-er/o reproduction.
No speedup or new implementation is claimed by this diagnostic checkpoint.

## Parallel architecture-preserving optimization — 2026-10-13

Checkpoint: runtime/experiment tree `9ddab1d`; chat worktree snapshot `75d7f83`.
No push. Original frozen inputs and archives remain unchanged.
Target: CPU1T approaches ASGP1T; native GPU-er-o search-wall House >30x,
Median >5x. Historical frozen-generation ratios are not this target.
Short comparison: House32/seed0, Median64/seed2, P1024/C1024; all five modes.
Primary metric retains `evolve_wall_ms` (cold evolution init, every generation,
final eval). Input import and post-search audit/export are outside this metric;
process wall includes them and is reported separately. Flags and penalties match
long-search manifest; House1000, Median1. No algorithm or evaluation changes.
Runner: `python3 benchmarks/fixed_asgp/optimization/short_search.py --binary
logs/optimization/long-search-20261013/bench --out logs/optimization/parallel-20261013/baseline`.
Use fresh output directories; `--binary`, `--reference-root`, `--modes`,
`--generations`, `--seeds`, `--flag` support focused comparisons. Every benchmark
process takes `/tmp/gagp-parallel-20261013-benchmark.lock`; agents use the same
lock for GPU tests/profiling. Initial baseline may overlap compilation; important
candidates require quiet-machine interleaved confirmation.
Worktrees/branches: `parallel-cpu-eval-20261013`, `parallel-cpu-repro-20261013`,
`parallel-gpu-20261013`, under the existing worktrees directory and `opt/` branches.
Hypotheses: direct CPU phase binding; deferred native CPU site analysis; GPU
per-program compact execution / changed-phase lowering after cost attribution.
No CPU fragment backend, CPU workers, reduced cases, or benchmark solver.
Status: baseline in progress; no new runtime improvement claimed.

Transport candidate: `GAGP_BATCH_PHASE_IMPORT=1 GAGP_BATCH_PHASE_EXPORT=1`.
Cold/final compact transport preserves exact 4-generation traces/final ASTs;
32-generation full-case owner test also compares bulk/per-member export.
House4 median search752→662ms; Median4 797→~644ms (early exploratory).
Main House32 2245→2144ms; Median64 2058→1986ms, single runs; modest gain,
not target achievement. Raw: `logs/optimization/parallel-20261013/{import-on,
import-off,io-on,io-main}`; compact evidence `results/parallel-20261013/transport-experiment.json`.
Median CPU128 seed2 full population, champion-368, imports to GPU native profile
without fallback and preserves initial full-case fitness; representation supports
this known better solution. Probe is support evidence, not speed comparison.

Static grammar candidate: perf cold trace identified repeated `compile_phase_grammar`
inside each import (8.7% inclusive whole-process cycles in one diagnostic, not
8.7% of search-wall). `GAGP_REUSE_PHASE_GRAMMAR=1` now reuses pure grammar
compilation in an owner-bound cold cache. No admission/witness bypass. With
batched I/O, House32 search repeats2049/2045/2074ms; Median64 1793/1855/1793ms.
All traces and final ASTs exactly match original baseline. Focused owner/layout
tests pass. Evidence `results/parallel-20261013/static-grammar-experiment.json`.
### Parallel GPU exploration — 2026-10-13

Worktree `parallel-gpu-20261013`, checkpoint base `9ddab1d`.
Retained candidates: parallel lowering-offset scan; owner capability buckets
sharing packed tables with index-mapped output; opt-in smaller reproduction
blocks. No RNG/operator/numeric/fuel/language change. Copying subset bytecode
prototype was removed: Median host regrouping outweighed kernel savings.
Focused main House32/s0 and Median64/s2, P1024/full1024 cases: indexed buckets
and 1-thread reproduction blocks give1870.241/1773.683ms search wall versus
same-build prefix-only2189.835/1995.761ms (single process; formal confirmation
pending). All10 candidate/control pairs retain identical per-generation quality,
operator/error counters and final fitness/node rows. Full32-generation owner
fixture and257-member mixed-stack fixture pass independent CPU reevaluation.
NSYS Median16 diagnostic: offset scan0.821→0.0115ms; crossover1.758→0.636ms;
mutation4.852→4.056ms; generic eval4.024ms becomes compact2.353 plus
fallback0.248ms. Profiled components are not subtracted from unprofiled totals.
Raw paths/hashes/timings: `results/parallel-gpu-20261013/experiments.json`;
NSYS archives: worktree `logs/parallel-gpu/diagnostic{,-index}/`.
Next: integrate with root cold-import/export changes, repeat combined candidate,
then address remaining mutation and House evaluator cost. No benchmark controls
or original inputs were modified.

Rejected semantic experiment: fitness ties prefer smaller GPU genotype. Full
cases kept, but Median3seeds all end-642/mean~20nodes and427–448unique, whereas
control seed0 reaches-497 and~1000unique. House2/3seeds worse andseed1slower.
Median~1.03s is not accepted as5x goal evidence. Code/flag/docs removed; archived
binaries/raw outputs retained only as historical evidence. Record:
`results/parallel-20261013/rejected-size-ties.json`.
Combined exact-behavior GPU main3repeats: House median1662ms, Median1484ms;
raw `logs/optimization/parallel-20261013/combined-gpu-main`. Goals still unmet.

GPU static donor weights: `GAGP_GPU_STATIC_WEIGHTS=1` proves depth/capacity-
independent normalizing maxima at grammar import and pre-divides weights.
Compared with the combined capability-bucket/transport/grammar-cache candidate,
three independent complete-search repeats retain exact quality/operator/error
traces and final fitness/node rows. Raw repeats and medians are in
`results/parallel-gpu-20261013/static-weights.json`. The device construction
matrix across depths/capacities passes exact node/origin/constant comparisons;
32-generation independent CPU reevaluation also passes. No language, operator,
RNG or resource-limit change. Remaining House kernel cost is dominant; focused
NCU reports low eligible warps and substantial dynamically indexed local memory,
so a separate shared operand-stack prototype is being evaluated, not yet adopted.

Bucket workspace candidate: compact/generic grids planned independently under
the same512MiB sharedscratchceiling. Samebinary3process medians House1648→1583ms,
Median1396→1361ms; unchanged traces/finalpopulation. Mixed257program/32generation
CPUaudit test passes. Setting`GAGP_BUCKET_WORKSPACE=1`; evidence
`results/parallel-20261013/bucket-workspace.json`. Improvement modest, supports
capability-dependent concurrency without reducing execution limits.
House scalar structural comparison: `GAGP_BOUND_COMPARE_PHASE=1` recognizes
LOAD/Int CONST/comparison phases under typed proof and one non-parameter binding.
Three-process full-search results and exact trace checks are recorded in
`results/parallel-gpu-20261013/bound-comparison.json`; focused tests cover
LT/LE/GT/GE, both operand orders, optional Return, >2^53 conversion and fuel.
Shared-stack8 and cached-top interpreter prototypes were tested and rejected:
House same-binary1637-scale controls became1767→2006ms and1961→2144ms,
respectively (their disabled binaries also regressed). All prototype code and
switches were removed; patches/binaries/results remain under `logs/parallel-gpu/`.
They are not recommended paths. Next bounded experiment: phase-local fuel
ownership, then a forward-only phase fuel upper bound if profiling supports it.

Coordinate ring memo: `GAGP_COORDINATE_RING_MEMO=1` requires the typed coordinate
executor, memoization, first offset -1, other offsets -1..-4, and a statically
proved lower-interval base predicate. Four scalar values replace the linear
lookup; original entry/base/fuel/frame/insertion-capacity behavior remains.
Unsupported shapes keep generic lookup; workspace allocation is unchanged.
Six same-binary search repeats per setting show about2% House32 search-wall
and7% diagnostic kernel improvement. Exact final AST/trace comparison,
independent proof review and exhaustive focused fuel/capacity tests pass.
Evidence: `benchmarks/fixed_asgp/optimization/results/parallel-20261013/ring-memo.json`.
Bucket workspace candidate: compact/generic grids planned independently under
the same512MiB sharedscratchceiling. Samebinary3process medians House1648→1583ms,
Median1396→1361ms; unchanged traces/finalpopulation. Mixed257program/32generation
CPUaudit test passes. Setting`GAGP_BUCKET_WORKSPACE=1`; evidence
`results/parallel-20261013/bucket-workspace.json`. Improvement modest, supports
capability-dependent concurrency without reducing execution limits.

GPU follow-ups rejected (2026-10-13): phase-local fuel ownership plus a
forward-only phase fuel upper bound passed existing fuel/CPU-audit tests but
three-process House medians1471.377→1461.782ms overlapped, Median1398.421→1411.805ms
regressed. Per-warp diagnostic-count aggregation preserved all trace counters
and passed bounded-region tests, but House1471.947→1450.040ms and
Median1366.080→1377.148ms did not establish a useful total-time gain; kernel
differences were below1%. Both prototypes and switches were removed, patches
and raw outputs archived in `logs/parallel-gpu/`. Recorded results:
`results/parallel-gpu-20261013/rejected-followups.json`. Next: inspect remaining
cold native import/final export cost; root separately tests structurally proved
coordinate memo storage. No phase semantics, counters, or operator changes adopted.

Cold import reuse: `GAGP_REUSE_IMPORTED_EXECUTABLE=1` eliminates the second
AST verification/lowering after fresh grammar admission, using the evaluator's
actual input order. Secondary imports no longer copy already cached immutable
profile tables only to discard them. All input admission, GPU bytecode checks
and output membership checks remain. Three-process House1478.364→1408.281ms,
Median1394.884→1340.333ms; raw repeats and cold-prefix times:
`results/parallel-gpu-20261013/cold-reuse.json`. All quality/operator/error traces
and final fitness/node rows are exact. Existing32-generation CPU/grammar audit,
mixed257-member test, and new reversed input-order test pass. No changed
execution, grammar, RNG or operators. Next: eliminate repeated DInstr→Instr→DInstr
conversion and static repacking through private owner-bound packed artifacts.

Rejected full register-bank8 interpreter: compiler generated heavy spilling;
House samebinaryOFF1832→ON1949ms and disabledbinary already regressed from
~1404ms. Median1391→1373ms does not offset House loss. Focused parity/owner
tests passed, but no code/flag is retained. Rawpatch/binary/compilerreports:
`results/parallel-20261013/rejected-register-stack.json`. This differs from the
previous cached-top/shared-stack tests and also fails to remove interpreter cost.
Private packed carrier: `GAGP_OWNED_PHASE_PACKED=1` eliminates the per-generation
DInstr→Instr/fuel→DInstr round trip and static structure repacking. Checked GPU
compiler outputs update private phase ranges and refreshed optimization hints;
reference bytecode is materialized on demand, including different-input-type
fallback before capability analysis. Three-process House1460.209→1397.288ms,
Median1410.296→1212.252ms; initialization adds about21ms and remains counted.
Raw repeats, per-stage medians, RSS/device bytes and identical final AST hashes:
`results/parallel-gpu-20261013/owned-packed.json`. No RNG, operator, grammar,
numeric, fuel or capacity changes. Transfers and a per-evaluation carrier copy
remain; this is host-work elimination, not a resident executable claim. Focused
32-generation CPU/admission, mixed257-member, wrong-type fallback and pattern
invalidation tests cover the changed path. Next integrated bottleneck is House
evaluation; root independently explores a scalar operand-bank prototype.

Exact compact GPU generator tasks retained: pending entries16→8bytes using
existing expression/depth/scope bounds, with explicit pre-assignment ID and
reservation checks. No new runtime switch or grammar/RNG/operator change.
Three-process House1361.463→1367.503ms is neutral; Median1177.709→1160.406ms
improves1.47%, non-overlapping ranges. Reproduction-kernel medians improve about3%;
mutation registers86→84, peaklocalframe remains4784B and spills remainzero.
`results/parallel-gpu-20261013/compact-generator-task.json` records all repeats
and identical finalAST hashes. Reference-task differential tests cover scalar
and scoped grammar roots, maxdepth64, expressionID65536, eight-slot permutations,
invalid IDs/counts and complete generated-node/fuel/constant/status equality.
32-generation independent CPU/grammar checks pass. Packed-carrier quickquality
3seeds×4generations also completed (`logs/parallel-gpu/packed-quality`). All
subagent builds/performance runs are now stopped for root's quiet integrated
validation; no additional direction is queued on this branch.

2026-10-13 follow-up: same-slot GPU executable reuse rejected. Based on integrated
runtime `b60a336`, the minimal prototype retained only previous compact code and
constants, double-buffered certificates, and selected-parent slot lineage. Edits
invalidated that slot; first generation compiled fully. There was already no
crossover-intermediate bytecode compile to remove. The 40-generation, two-slot
exact differential passed with both mutation-buffer modes, repeated selected
parents, rejected mutation input, independent CPU export checks and existing
regressions. Three-process full-search medians OFF→ON: House1361.168→1352.552ms
(0.63%), Median1168.505→1160.476ms (0.69%); ranges overlap. Same-window archived
`b60a336` measured1362.134/1164.541ms, also comparable. Four-generation NSYS compile
totals fell4.064→3.482ms /3.614→3.304ms, but this diagnostic reduction did not
establish useful full-search improvement. Do not subtract it from unprofiled
search times. Both compiler variants used40registers,2816Bstack, zero spills.

The prototype, flag and exclusive tests are removed; no runtime/settings changes
remain. Evidence: `results/parallel-gpu-20261013/rejected-executable-reuse.json`.
Archived binaries, raw repeats, profiler CSVs, test log and rejected patch remain
under the GPU agent worktree's `logs/parallel-gpu/reuse-executable-*`. Its local
build contains the archived rejected experiment until rebuilt; the root's
validated runtime binary is unaffected. No dirty-work-queue extension is planned:
its additional complexity is unsupported by this full-search result.

2026-10-14 fused native GPU donor sites retained opt-in:
`GAGP_GPU_FUSED_DONOR_SITES=1` writes exact construction-site metadata while
emitting each mutation donor. Imported trees still use independent annotation;
splice/resource validation and the single post-variation lowering remain.
A dedicated mutation specialization removes the annotation traversal and reduces
in-place local frame4784→1712B, registers86→76, no spills (sm86).
Same-binary3-process House32 median1377.781→1358.585ms (1.39%); Median64
1188.736→1152.144ms (3.08%). Same-window archived-best1379.784/1184.140ms
is comparable to disabled. All final ASTs and quality/operator/error traces are
exact across archive/OFF/ON. Five scoped/unscoped generation, variation and
32-generation population tests pass. Evidence:
`results/parallel-astra-20261014/fused-donor-sites.json`. This changes construction
work only; size-near crossover is a separate search-rule experiment.

2026-10-14 size-near crossover rejected. The separate native GPU search profile
kept parent/destination/source-A streams and, on an independent 50% gate, drew
source B from the same compatible donor population, choosing nearer subtree size
(ties A). No added rejection, cap, fitness tie-break, task dispatch or mutation
change. Analytic sampler, scoped variation and32-generation CPU/grammar tests
passed. Three fixed seeds at House32/Median64, then both tasks128 generations,
retained fullP1024/C1024 coverage. House128 final fitness OFF→ON:
-4427/-865/-4535→-8280/-8303/-5480; House32 timing did not materially improve.
Median128 -478/-642/-642→-529/-642/-642. Actual untimed seed0/16-generation
novelty remained high (House final unique1009→992, novel versus all parents936→888;
Median1022→1017,943→929), so changed-child counts did not conceal the old small-tree
collapse. This still failed search-quality requirements; no parameter sweep follows.

All size-near runtime flags, exclusive probe/tests and documentation are removed.
Patch/binary/full raw traces and novelty exports remain in the donor worktree's
`logs/bloat/`; evidence is `results/parallel-astra-20261014/rejected-size-near.json`.
Root supplies supplemental preregistered equal-wallclock/heldout analysis. Fused
construction-site optimization is independent and remains retained.
2026-10-14 native CPU admission follow-up: two independent opt-in candidates.
`GAGP_CPU_FUSED_ADMISSION=1` fuses unique exact-head membership into witness
construction; ambiguous/alias heads retain recursive matching and repeated-hole
checks remain. Three-process full GPU-e House32/Median64 medians 9613.752 → 9348.258 /
26767.027 → 25443.351 ms. The speculative matching-work guard accounting changes
explicitly, including public unframed single-root reconstruction; strict
`require_membership` is unchanged. Evidence: `results/fused-admission-20261014.json`.

`GAGP_CPU_COMPACT_ADMISSION=1` instead validates/lowers fresh scalar single-root
inputs before stable compaction, then seals the same canonical witness to the
compacted AST when names are unchanged. Name-table changes rerun admission;
registry-backed constants, attached provenance and multiple roots keep the
ordinary path. No guard/RNG/grammar/resource/execution changes for this flag.
The old rejected GPU compaction experiment had zero hits; CPU splice output has
3045/13244 House and 2605/13122 Median constant compactions in 8 generations,
with zero name changes. This removes actual second reconstruction/verification.

Same-binary three-process GPU-e medians OFF/compact/both:
House 9746.720/8594.850/8287.609 ms; Median 26804.613/25124.282/23868.246 ms.
Every final/history AST, fitness/operator/error row is exact. Frozen CPU1T
three-process warmup+3 total medians OFF→both House 3370.624 → 3379.262 ms (neutral),
Median 549.086 → 531.677 ms; reproduction 235.714 → 211.529 / 217.208 → 193.454 ms.
One combined full CPU validation takes 82.929/41.725 s versus archived medians
84.247/44.762 s, not a paired full-CPU speedup claim. Native GPU-overlap three-process
medians 1379.315 → 1356.892 / 1188.305 → 1167.503 ms; cold admission saves only 7.64/6.50 ms.
Targets remain unmet: House CPU is evaluator-dominated and the GPU whole-search
improvement is small. No default or best-settings adoption is made here.

Eight focused native tests pass. Extra compaction coverage compares retained
versus independent lowering for every fuel 0..1000, checks invalid unused entries,
stale proofs and name fallback. Archived-binary strict admission and independent
full 1024-case CPU/GPU audit pass all 1056 House and 1088 Median final/history
programs; combined exports are structurally identical to those audited artifacts.
Raw preserved binaries/manifests/profiles/resources live in the admission worktree's
`logs/local-admission/`; tracked evidence is `results/compact-admission-20261014.json`.

2026-10-14 private constructive export retained opt-in:
`GAGP_GPU_CONSTRUCTIVE_EXPORT=1` returns the owner-constructed AST without an
optional reverse-parsed canonical derivation/compiled cache. Cold admission,
private grammar/domain generation, exact nonterminal/scope splices and physical
resource checks establish membership; existing export origin/opcode/fuel/index
validation and metadata compaction remain. No external trusted bit/certificate
or generic import shortcut is introduced. `ProgramGenome::derivation` is optional.

Same-binary3-process House32 search1377.520→1228.279ms (10.83%), Median64
1186.501→1057.472ms (10.87%); process2.636→2.480s /2.393→2.245s.
Final-evaluation/materialization medians190.052→39.112ms /138.506→14.714ms.
All6 OFF/ON finalAST exports and semantic/operator/error traces are exact.
The32-generation all-member CPU/grammar test runs with certificates disabled;
new cache/no-cache and modified external replay tests pass. Both fullP1024 final
populations additionally pass archived-runtime strict grammar import without
certificates and fullC1024 CPU/GPU evaluation, matching every search fitness.
Evidence: `results/parallel-astra-20261014/constructive-export.json`. Root's
separate import-certificate reuse and integrated timing are not conflated here.

2026-10-14 prepared immutable GPU IntList parameter views retained opt-in:
`GAGP_PREPARED_CASE_VIEWS=1` prepares a separate case table after the existing
complete-payload/type-flow proof, and only if a packed region declares an IntList
parameter. Offsets are rebuilt for each evaluation's payload table; original
lengths/presence masks and lazy fallback remain. No device VM, fuel, search rule,
reproduction backend or task dispatch changes. Build/pack/upload costs remain timed.

Alternating OFF/ON, ON/OFF, OFF/ON three-pair House32 medians1192.607→1172.735ms
(1.67% improvement), Median64 1008.864→1020.615ms (1.16% regression). House summed
eval kernel617.187→576.899ms; generation804.596→780.143ms, with extra host packing
and upload. All6 paired final AST and semantic/operator/error traces are exact.
The larger grouped-run apparent win is not attributed to this optimization.
Pre-experiment archived control in the same broader window was1211.302/1026.301ms.

Earlier unconditional and shared-list-presence-only stages are rejected: Median
DOES have a shared list input, but carries it as already-converted state and has
no parameter captures. Its extra table preparation therefore had no kernel benefit.
The final gate inspects packed region parameter types, never benchmark names.
Focused fitness/bounded-region parity passes; final captured-list tests pass
CUDA12.6 compute-sanitizer with0errors. Coverage includes private marker sanitization,
empty/list lengths, shifted constant-payload tables, fuel, missing/wrong captures,
and existing state-only sequence paths. Evidence:
`results/parallel-astra-20261014/prepared-case-views.json`. Root's combined nine-cell
and scaling runs determine final adoption; no further candidate directions planned.

2026-10-14 ring workspace liveness experiments rejected:
The proven four-register coordinate memo ring never reads global memo history,
while generic capability buckets still require that history. Removing dead
backing was correct, but exact-sized workspace allocation changed whenever the
live generic bucket count changed. Fixed-grid three-pair House32 median
1063.828 -> 1146.781 ms; summed upload/allocation roughly 29 -> 105 ms with neutral
kernel time. The original expanded-grid variant's regression was also dominated
by this allocation churn, not evidence of a large concurrency-only penalty.

A bounded geometric-capacity reuse variant preserved the original grid and
512 MiB total allocation ceiling, freeing the old layout before replacement.
Its three-pair House32 median 1074.895 -> 1092.077 ms; kernel 573.176 -> 572.886 ms,
upload 29.457 -> 48.462 ms. Median64 uses the window executor, so its apparent
922.079 ->911.414 ms change is control drift, not a live-ring benefit.
No runtime flag/code or test-only fixture is retained. All three rejected
patches and binaries remain archived under the liveness worktree's logs.

All paired full population/history ASTs and semantic/operator/error rows match.
Focused fitness/bounded-region/native population parity passes; a temporary
257-member mixed compact/generic unary-ring fixture checks full 1024-case parity
and reproduced offspring against independent CPU execution. CUDA 12.6
compute-sanitizer reports 0 errors. Structured results, raw paths, binary hashes,
and original archived controls: `results/bloat-liveness-20261014.json`.


2026-10-14 Sum scheduling/layout candidate (opt-in, pending integration):
`GAGP_DYNAMIC_PROGRAM_QUEUE=1` lets each physical region block claim its next
logical program row after completing all cases. It retains its own workspace,
original row scatter and each program's reduction order. Compact/generic queues
are separate. `GAGP_WINDOW_SOA=1` transposes unboxed window frame fields across
case lanes without changing frame sizes, declared limits or the 512 MiB budget.
Unsupported/generic frames retain their existing layout. Both flags are off by default.

Three alternating full Sum32/seed0 P1024/C1024 pairs: search median
4345.244 -> 4180.848 ms (3.78%); summed evaluation kernels
3599.167 -> 3455.570 ms (3.99%). Archived control4329.454 ms; initial same-binary
OFF/queue/SoA/both screens4395.224/4268.687/4296.499/4172.046 ms.
Every final/history AST and semantic/operator/error row matches across all11runs.
This is a small candidate improvement, not attainment of25x or cross-task evidence.

Validation correction: the reported new17-program/65-case forced-queue fixture
coverage was invalid. Its source was edited during compilation; the old-source
object completed after the edit, preventing subsequent timestamp-based rebuilds.
The resulting test binary lacked the new assertion strings. Existing tests in
that binary passed and memcheck reported0errors, but neither establishes coverage
of the new queue/SoA fixture. Its4096-frame limit also violates the existing GPU
capacity128. Root is replacing it with97programs×65cases and128legalframes,
then rebuilding and running fresh memcheck. Production capacity must not change.
Full-search benchmark binaries and all11exact trace/AST comparisons are unaffected.
The final host guard reserves16bytes for queue shared state; archived timing
binaries precede that guard-only change. Raw patches/binaries/manifests remain in
the scheduling worktree's logs; tracked evidence:
`results/sum25-scheduling-20261014.json`.


2026-10-14 per-phase compiler-bound metadata candidate:
`GAGP_PER_PHASE_STACK_BOUND=1` retains each dynamic phase's actual checked GPU
compiler stack certificate, using the existing packed-phase row mapping. Fixed
phases retain their original conservative cold verified bounds; whole-program
bounds and capability bucket admission stay unchanged. This lets small phases
within a generic bucket use the existing typed phase VM. No executable, fuel,
operator, RNG or architecture changes. `GAGP_GENERIC_PHASE_VM` remains dominant.

One isolated same-binary full Sum32/seed0 pair: OFF4345.325 -> ON3755.088ms
(13.58%); summed kernels3594.498 ->3042.660ms. Final/history AST and all semantic,
operator and error rows are exact. Root's measure cache/hybrid combinations and
repeated three-task timing are separate pending work, not part of this claim.

The257-row mixed-stack regression compares fresh compiler metadata against a
conservative owner, independentCPU fitness and a second forced-generic generation.
It exposed an existing debug-override bug: capability rebucketing ignored the
forced generic phase bound and could launch the compact interpreter. The bucket
gate now honors `GAGP_GENERIC_PHASE_VM`; the focused native population test passes.
Evidence: `results/sum25-phase-bounds-20261014.json`.
