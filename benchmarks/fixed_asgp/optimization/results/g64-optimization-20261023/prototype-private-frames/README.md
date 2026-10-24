# Packed private-window frames：隔離原型

這個原型未整合進正式GAGP，不列入本輪速度或品質結果。

已實際通過的檢查：

| 範圍 | 實際結果 | 原始紀錄 |
|---|---|---|
| CPU storage，包含dirty-storage／tag／64-bit payload | 最佳化及ASan／UBSan版本通過 | logs/g64-optimization-20261023/packed-oracle0 |
| CUDA AoS／SoA storage、53個lanes、canary與重用 | 建置及執行通過 | logs/g64-optimization-20261023/packed-cuda-oracle1 |
| CPU root／child／stack-local leaf存取介面 | 最佳化及ASan／UBSan版本通過 | logs/g64-optimization-20261023/packed-access-oracle0 |
| 真實device executor的12個入口 | NVCC device編譯與symbol檢查通過，**沒有launch kernel** | logs/g64-optimization-20261023/packed-executor-compile0 |

正式closeout結束後才執行上述檢查，使用同一GPU lock。CUDA輔助啟動attempt0漏設PYTHONPATH，在import前失敗；原始失敗紀錄保留，attempt1僅修正啟動環境。正式樣本沒有重跑。

原型將私有list輸出的通用compact frame由72／88 bytes壓成40／48 bytes，分開儲存完整64-bit payload及tag／bool metadata，保留所有已定義ValueTag。純Int結果原本已有unboxed frame；這個比例不能套用到所有題目。Global scratch必須按Storage大小配置，不能使用含參照的LocalFrame大小。

目前缺少host准入、配置、dispatch、initial／replay routing整合，以及實際CUDA VM執行、fuel／型別與錯誤順序、髒workspace重用、跨代CPU parity和正式速度測量。儲存／存取檢查或編譯成功都不能替代這些證據，也不能當作1.3×達標。

實作檔案為private_window_frames_gpu.hpp、private_window_frame_views.hpp及private_window_executor_access.cuh；對應storage_oracle.cpp／.cu、access_oracle.cpp與executor_compile_probe.cu。正式CPP沒有include這些檔案。後續若整合，必須使用獨立開發與新資料驗證，不能由本輪test結果重選設定。
