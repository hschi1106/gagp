# GAGP 全30題額外加速與品質驗證

**本輪未達2×。** 全30題／P1024與16384／C1024／G8／每方法2次的搜尋計時觀測為 **1.477×**；P1024為1.217×、P16384為1.793×。另有1組額外原生核對中斷未知，整體驗證保留partial。

新資料P1024／G64／C19–200的加速為1.0004×，幾乎沒有加速。300組品質配對的冠軍軌跡一致；兩版最終train+test成功均為90/300，其中67組是演化後新增成功。10個seed群組不足以證明5pp統計不劣性。

- [結果與閱讀說明](CURRENT.zh-TW.md)：P/G/C、完整計時與已核對子集、品質與限制。
- [完整中文報告](final2/REPORT.zh-TW.md) · [互動表](final2/dashboard.html)
- [速度CSV](final2/speed_cells.csv) · [品質CSV](final2/quality_runs.csv)
- [速度PNG](final2/speed.png)／[PDF](final2/speed.pdf) · [品質PNG](final2/quality.png)／[PDF](final2/quality.pdf)
- [凍結執行設定](final2/winner_preset.json) · [dry-run檢查與呼叫範例](RUNNER_CHECK.json) · [來源與切分核驗](final2/validation.json)
- [UI檢查](UI_VALIDATION.json) · [圖表檢查](PLOT_REVIEW.json) · [呈現版本來源](final2/presentation-provenance.json)
- [原始規格](protocol.json) · [所有嘗試與恢復紀錄](RECOVERY.md)

原始統計報告 `final/` 保留；`final1/`、`final2/` 只修正實際預覽發現的欄寬、數字換行及閱讀說明，CSV／統計／PNG／PDF與父報告逐檔一致。正式交付頁面為 `final2/`。

實際驗證：乾淨Release建置通過；首次完整原生135/136，缺少歷史文件連結檔案的repository contract補入同雜湊原檔後定向1/1通過，原失敗保留，不能稱為一次新的136/136。研究工具194項、操作工具122項（4跳過）、repository24項通過。

固定窗口：2026-10-22 15:23:27 UTC至2026-10-23 15:23:27 UTC（台灣10/23 23:23:27），沒有重設。原始資料、來源快照與全部失敗保留於 `logs/twofold-optimization-20261022/`。沒有重跑／替換中斷樣本，也沒有用新test結果重選候選。
