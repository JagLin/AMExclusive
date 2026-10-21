# TRACKING — asio-output（feat/asio-output）

**交接（2026-10-21）**：ASIO 輸出已上線並 push（branch `feat/asio-output` = origin，HEAD `db7217d`），本機與 Win10 都能用；GUAC32、JPLAY 實機出聲，Win10 串流 48k/24-bit 長播無斷音。設定頁有輸出裝置選單。進入暫時收尾。現在卡在 Win10 串流換曲偶爾少第一個音：mod 層計數無遺失，等使用者做 A/B/C 對照再定方向。
- 先讀：OQ-12、ADR-15、F-37、F-35、F-36，其次 ADR-8、ADR-9、ADR-10、OQ-1、OQ-2、F-20、F-25
- 別再撿：F-14（AVCF 不是佇列／重複觸發）、F-16（ConvertBuffer／FillBuffer 沒被用）、F-6（Agent 不是 AppContainer）、F-17（公開 API 拿不到 AVCF 的 pre-SRC PCM）
- 線上 living doc（同一段歷史的人讀版）：F-31

## §0 這個 feature 是什麼

在 AMExclusive（Apple Music for Windows 注入式 mod）的獨占輸出核心 audio_v2 加一個 ASIO 輸出後端，與既有 WASAPI 獨占並存、可在 Apple Music 設定頁選擇。使用者原話：「這完全是我自用的程式, 最終目的是接到我的jplay asio與guac32」。bit-perfect 與 fail-closed 不變式沿用 audio_v2 原規則。

## §1 ADR

### ADR-1 抽出 `IExclusiveSink`，coordinator 以成員持有各後端
- 決定：新增 `ExclusiveSink.h`（`IIntegerPcmSource`、`IExclusiveSink`、`ExclusiveSink*` 中性型別）；`AudioCoreCoordinator` 持有 `wasapiSink_`／`asioSink_` 成員，透過 `IExclusiveSink*` 存取，`Enable` 時選用。
- 放棄：`unique_ptr` 動態配置（`Enable` 需維持無配置、`noexcept`）。
- 理由：型別名 `SinkState` 會與 coordinator 方法 `SinkState()` 撞名（changes meaning），故全部加 `ExclusiveSink` 前綴。
- 狀態：已落地 `53ee205`；WASAPI baseline 驗證行為不變（F-9）。使用者同意 commit。

### ADR-2 ASIO SDK 用 Steinberg 專有授權版，只放本機
- 決定：SDK 放 `third_party/asiosdk/`（已 gitignore，`c32a1da`）；CMake 找得到才啟用，找不到則 `AsioSink` 編成永遠不可用。
- 放棄：GPLv3 版進 repo（安裝檔會變 GPLv3、與上游 MIT 分歧）；自寫介面宣告（ABI 風險、法律灰色）。
- 使用者原話：「這完全是我自用的程式, 最終目的是接到我的jplay asio與guac32, 前者是我自行購買的撥放軟體, 後者是搭配音響硬體使用, 所以應該沒有授權問題?」
- 狀態：已落地。

### ADR-3 ASIO sink 自帶 STA worker
- 決定：驅動建立與 `init`／`createBuffers`／`start`／`stop` 全在專屬 STA 執行緒（message-only 視窗＋message loop）；音訊資料不經過它，`bufferSwitch` 直接從 queue 取。
- 使用者原話：「第 2 題照你的建議，ASIO sink 自帶 STA 執行緒。」
- 狀態：已落地 `d6152f7`。

### ADR-4 沿用既有原生輸出接管，不另開 gate
- 決定：`PrepareForExclusiveHandoff` → `OpenEndpoint` → 失敗退回原生 → `Start` → `CommitCutover` 對後端無關，ASIO 直接接第 2 步；裝置改由 ini 的 ASIO 驅動決定。
- 使用者原話：「第 3 題照你的建議，沿用現有接管機制。」
- 狀態：已落地。

### ADR-5 驅動常駐、換曲只重建 buffer、閒置 5 秒釋放
- 決定：`Close()` 只 stop＋disposeBuffers；取樣率不同才 `setSampleRate`；閒置 5 秒、換驅動、`kAsioResetRequest` 或 `setSampleRate` 失敗才完整釋放（重載一次仍失敗就 Faulted，不重取樣）。
- 使用者原話：「第 4 題照建議，驅動常駐、閒置 5 秒後釋放。」
- 狀態：已落地；閒置釋放有 log 佐證，取樣率切換未實測（OQ-1）。

### ADR-6 追蹤上游：fetch-only remote 並合併
- 決定：`upstream` = `AntiOblivionis/AMExclusive`，push URL 設 `DISABLED`；上游先合進 `main` 再合進 feature branch。
- 使用者原話：「我的理解是我需要上游的更新來維護對apple music的逆向工程, 所以應該是新增並合併」
- 狀態：已落地（`1eddee7`、`d059b23` 合進 `c67c2f3`）。

### ADR-7 git 身分用 repo local、取自 gh 帳號
- 決定：`user.name=Jag Lin`、`user.email=jaglin42@gmail.com`（repo local）。
- 使用者原話：「git config建立repo local的 就用gh auth這個我的身份」
- 狀態：已落地。`53ee205` 為設定前以上一個 commit 的身分（JagLin／nicedate@gmail.com）提交。

### ADR-8 本機檔不再投入，AVCF 接管擱置
- 決定：不逆向 AVCF 路徑；改以「保持音量平衡開啟」避開（F-20）；驗證改用串流。
- 使用者原話：「重新匯入的第一次總是可以撥放的 我在想可能先放棄local file這條路, 不要浪費時間, 紀錄進度後等我處理訂閱再回來」；「local我本來就有foobar/jplay」
- 放棄：跨執行緒延後綁定＋診斷 hook（綁得上但拿不到資料，F-17），草稿存 `notes/asio-output/avcf-crossthread-bind.patch`（原本放 `docs/avcf-crossthread-bind.patch`，依 ADR-14 搬到 notes/）。
- 狀態：生效。

### ADR-9 輸出裝置選單內嵌在 Apple Music 設定頁
- 決定：硬體緩衝區卡片下方加「输出设备」`ComboBox`；清單＝「WASAPI（默认播放设备）」＋`HKLM\SOFTWARE\ASIO` 所有驅動；下一首生效；Broker 擁有並寫 ini，Agent 只從 status 套用；IPC `kProtocolMajor` 2→3。
- 使用者選擇：生效時機「下一首生效（建議）」、清單「WASAPI＋所有 ASIO 驅動（建議）」；位置：「我測試過了 先維持內嵌吧」。
- 放棄：外部設定工具（Broker 已接受 Controller 角色送同一訊息，日後可加）；立即切換。
- 備註：最初放進 Apple Music 是模型自行判斷，事後才向使用者說明並取得同意。
- 狀態：已落地 `cf9ba62`，實機切換 4 次驗證。

### ADR-10 緩衝目標配合驅動實際 period，而非要求使用者改 DAC Link
- 決定：pump 水位改為 max(0.3 s, 2P)～max(0.5 s, 3P)，P = sink 開啟後回報的實際 buffer；ASIO 等待後湊滿 2P 才恢復。
- 使用者原話：「jplaysetting是一個特別的設定 叫做dac link, 單位是Hz, 可以從1左右到1000Hz, 官方的說明是請配合後面轉導的裝置設定, 如果我們前面可以buffer配合那應該是最好?」
- 狀態：已落地 `db7217d`，Win10 驗證（F-25）。

### ADR-11 ASIO 資料不足時的處理（假設：模型決定，未逐字拍板）
- 決定：ASIO 無法拒絕一個 period，不足時寫零（計入 `controlledSilenceFrames`），並延後 1–2 個 callback 才停驅動，確保最後一段真實樣本播完；啟動時前導一個 half 靜音。不重播舊資料、不重取樣。
- 依據：SDK 對 `bufferSwitch` 的說明（意譯）：沒及時填好的那半個 buffer 依然會送去硬體播放。
- 狀態：已落地 `d6152f7`；使用者在說明後未反對。

### ADR-12 暫停時丟棄已封存的本機尾段
- 決定：暫停時若有 EOS drain 待處理就 `RetireCaptureLocked(true)`；暫停中的 dispose 不視為 EOS。
- 使用者驗收：「測完了 大部分的暫停撥放沒問題」
- 狀態：已落地 `c1751b7`，驗證見 F-21。

### ADR-13 staging 搬移加鎖
- 決定：每個 `LocalPcmQueue` 加 `drainMutex`，順序 drainMutex → mutex；`OnPcmOutput` 無鎖故不會互等。
- 使用者原話：「恩..好 試試看」
- 狀態：已落地 `857463b`，初步通過（F-23）。

### ADR-14 工作紀錄放 `notes/`，進 repo
- 決定：TRACKING 與相關草稿放 `notes/<feature>/` 並 commit；不放 `docs/`。
- 使用者原話：「可以移到不是docs的資料夾 這樣就沒有容易跟上游衝突的問題」
- 放棄：`docs/<feature>/`（skill 慣例，但上游 `.gitignore` 排除 `/docs/`，硬加會與上游衝突）；只留本機。
- 依據：上游 tree 只有 `.github`、`assets`、`mod`、`third_party`，`notes/` 不在 `.gitignore`。
- 狀態：已落地（2026-10-21）。

### ADR-15 舊曲收尾期間暫存新曲的 P0
- 決定：舊 generation 的 drain 還佔著 sink 時，`TryBindGraphLocked` 不再單純 return，而是對串流 successor 預約一個暫存區（`AudioCoreRuntime` 的 `successorStage_`：8 秒 @192 kHz，約 12 MB，第一次用到時配置一次，之後不再 resize）。successor 的 P0 在 unbound 時先複製進去；綁定後由同一個 converter callback 先回放暫存區，回放完才接即時 PCM，所以 queue 仍然只有一個 producer。暫存的 frames 也算進 pump 水位。
- 遇到以下情況丟棄暫存，退回原本只收即時資料的行為：overflow、successor 被 Reset／Dispose、使用者 pause、skip、seek、graph 拆除。
- 使用者原話：「照你的建議修取樣率切換換曲時新曲開頭被吃掉的問題。」
- 放棄：
  - drain 期間暫停 Apple 的 pump 讓解碼器停住。Apple render thread 等不到 event 時會怎樣不明，有觸發 device lost 的風險。
  - 丟掉舊曲不足一個 period 的尾段、直接綁新曲。這會截斷音樂。
  - source wait 時讓 ASIO driver 繼續跑、補零。這會插入靜音，而且仍會少約 0.5 秒。
- 狀態：已 commit 並 push（2026-10-22，commit 標題「fix(audio_v2): stage successor P0 while the previous track drains」），待 Win10 實測（OQ-12）。log 驗證點：`v2 sink runtime successor stage armed` 與 `... replayed stagedFrames=… replayedFrames=…`。

### ADR-16 broker 綁定的 Apple Music 結束時改綁仍在的程序
- 決定：`Broker.cpp` 的 `ProcessWatcher` 偵測到綁定的 `AppleMusic.exe` 結束（或 wait 失敗）時，先找其他仍在執行的 `AppleMusic.exe`，找到就改綁並記 `companion rebound`；只有找不到時才照原本的方式退出。UI 注入會在下一輪自動補到新的程序上。
- 使用者原話：「修 broker 綁到短命 AppleMusic.exe 就退出的問題：綁定的程序結束時，如果還有其他 AppleMusic.exe 就改綁而不是退出。」
- 依據：F-40。
- 狀態：已 commit 並 push（2026-10-22，commit 標題「fix(broker): follow a surviving AppleMusic.exe instead of exiting」），本機已安裝，使用者初步使用正常；改綁路徑本身還沒在 log 中觸發過。

## §2 Open Questions

| ID | 問題 | 開立 |
|---|---|---|
| OQ-1 | 換曲與取樣率切換（44.1 ↔ 96 kHz、ADR-5 的 `setSampleRate` 路徑）未實測；留意上游 issue #5（不同取樣率換曲後有時沒聲音）。GUAC32 的串流也未測。 | 2026-10-21 |
| OQ-2 | mod 沒有 Apple Music 版本檢查（F-27）；建議版本不符就不注入並提示。已列入 living doc 待辦。 | 2026-10-21 |
| OQ-3 | 恢復播放後約 30 ms fault（`0x80004005`）是否已被 ADR-13 根治：JPLAY 本機檔約 5 次通過，串流下未刻意測。 | 2026-10-21 |
| OQ-4 | 播放途中打開獨占開關後再播，Apple 連解碼器都沒建立就卡住（04:31 session），未查。 | 2026-10-21 |
| OQ-5 | 關掉 Apple Music 後 `AMPLibraryAgent` 常留背景，重開時兩個 agent 同時連 broker，影響未查。 | 2026-10-21 |
| OQ-6 | AVCF 本機檔接管（F-17、F-18）：便宜的下一步是診斷 unit 808 輸入是否經公開 render callback（屬性 23）；否則需 vtable hook 並記入 `ApplePrivateOffsets.h`。依 ADR-8 擱置。 | 2026-10-21 |
| OQ-7 | 設定頁是否顯示實際 buffer（例「實際：440 frames（10 ms）」）並在固定大小驅動時標不適用——模型提議，使用者未表態。 | 2026-10-21 |
| OQ-8 | UI log 每 5 秒 `GetAppProperty(MiniPlayerWindow) failed hr=0x8000000b`，推測 MiniPlayer 未開時的輪詢，未確認。 | 2026-10-21 |
| OQ-9 | 未做逐樣本 bit-perfect 比對（`BitPerfectVerifier` 只在 deep diagnostics 啟用）。 | 2026-10-21 |
| OQ-10 | 串流驗證完整後是否對上游開 PR。 | 2026-10-21 |
| OQ-11 | `mod\build\dist\AMExclusive-Setup.exe` 仍是上游舊版（03:16），未重跑 `Build-Mod.ps1`（F-28）。 | 2026-10-21 |
| OQ-12 | Win10 串流連續播放時，自然換曲的第一個音偶爾消失（F-35、F-36）。取樣率切換的那一種已找到成因（F-37），修正見 ADR-15，待 Win10 實測；同取樣率的那一種掉在哪一層仍未定。使用者「改天再測試」。待做對照：同一組相鄰曲目、各設定多試幾次——A 關閉獨占、B 獨占＋WASAPI、C 獨占＋JPLAY。A 也掉 → Apple 無縫播放本身；B 掉 A 不掉 → Apple 或 mod，下一步加診斷把換曲前後幾秒的實際輸出寫成 WAV 比對波形；只有 C 掉 → JPLAY 引擎。可另開 `native_trace=true`（手動加進 ini）並記下出問題的換曲時間或曲名，對照沒問題的換曲。 | 2026-10-21 |

## §3 方法論

（尚無；由 distill 寫入）

## §4 已知事實

**主題索引**
- 建置與工具：F-1、F-2、F-29、F-30
- 版本與相容性：F-3、F-4、F-26、F-27、F-28
- 驅動與硬體：F-5、F-6、F-7、F-8
- WASAPI baseline：F-9
- 原生輸出與 AVCF：F-12、F-13、F-14、F-15、F-16、F-17、F-18、F-19、F-20、F-39
- 暫停／恢復：F-10、F-11、F-21、F-22、F-23
- 緩衝：F-24、F-25
- 串流換曲：F-35、F-36、F-37、F-38
- 安裝：F-28、F-30
- 程序生命週期：F-40
- 外部資訊與參考：F-31、F-32、F-33
- 使用者偏好：F-34

### F-1 建置環境
VS Community 2026 18.11.1、MSVC 14.51.36231、Windows SDK 10.0.28000.0（含 cppwinrt）、VS 內建 CMake 4.3.1-msvc1（generator `Visual Studio 18 2026`，不在 PATH，`Build-Mod.ps1` 自動找到）、gh 2.102.0、PowerShell 7.6.7。開頭「`vswhere.exe` 不是內部或外部命令」訊息無害。
- 發現脈絡：2026-10-21 環境檢查與 baseline 建置。

### F-2 SDK 與 CI
`AMMOD_ASIO_SDK_DIR` 預設 `third_party/asiosdk`，存在 `common/iasiodrv.h` 才加 include；SDK 標頭以 `#pragma warning(push, 0)` 包住。CI 無 SDK 時照常建置。
- 發現脈絡：實作 ADR-2。

### F-3 Apple Music 版本
本機 package `1.1540.23042.0`（file `1.6.4.90`，即 mod 驗證版）。使用者的 Win10（19045 ubr 6456）是 `1.1540.23041.0`，串流、音質鎖、ASIO 都正常。使用者原話：「win10的版號少了c version 1」。
- 發現脈絡：2026-10-21 分析 Win10 log。

### F-4 Windows 相容
建置產物 225 個系統 API，最新為 `SetProcessDpiAwarenessContext`／`SetThreadDpiAwarenessContext`（Win10 1703）；高解析 timer 有退回；CRT 靜態連結；安裝用內建 PowerShell 5.1。AppModel-Runtime EventID 201 欄位只在本機 25H2 確認；Win10 實際注入成功。
- 發現脈絡：使用者問 Win10 能否安裝時的靜態檢查，後經 Win10 實測。

### F-5 GUAC32 驅動
回報「Combo384 ASIO 1.10」；內部從 `start()` 以 WASAPI 獨占開同一個 GUAC32 端點；固定 440 frames、Int32LSB；`start()` 約 25 ms。對 GUAC32 而言 ASIO 與 WASAPI 獨占本質相同。
- 發現脈絡：首次 ASIO 測試卡死時（F-8）。

### F-6 Agent 不是 AppContainer（負向事實）
曾假設 `AMPLibraryAgent.exe` 是 AppContainer、可能無法載入驅動——不成立：manifest 為 `Windows.FullTrustApplication`＋`runFullTrust`。真正限制是驅動皆 Apartment threading、mod worker 為 MTA（→ ADR-3）。
- 發現脈絡：2026-10-21 處理 open question 2 時讀 AppxManifest。

### F-7 JPLAY 驅動
`JPLAY Driver`（JPLAYDriver v96）是到 JPLAY 引擎的橋接（channel 名 JPLAYStreamer，需 JPLAYService）。buffer 由驅動固定，依 JPLAY「DAC Link」（Hz，約 1–1000）：本機 8192 frames（約 186 ms）、Win10 24576 frames（約 0.557 s）。`start()` 約 150 ms；回報 outputLatency 64 不是真實延遲。
- 發現脈絡：JPLAY 實測與 Win10 斷音分析。

### F-8 驅動自開的 WASAPI client 被誤包裝會死結
驅動在 STA worker 上 Activate 的 IAudioClient 被 `NativeAudioClientProxy` 當成 Apple 原生 client 包裝，回呼 runtime 需 controlMutex_，而 runtime 正等 `start()` → 互等卡死。解法：worker 全程 `ScopedNativeProxyBypass`，另加 10 秒命令逾時。
- 發現脈絡：2026-10-21 04:13 首次 ASIO 測試沒聲音、暫停無反應。

### F-9 WASAPI baseline
本機 WAV 44.1k/16 經 WASAPI 獨占到 GUAC32：93.7 秒、underrun／drop／mapping failure 皆 0；pre-SRC 擷取 int16，輸出 24-in-32；接管約 31 ms。含 `IExclusiveSink` 重構。
- 發現脈絡：2026-10-21 03:50 baseline。

### F-10 本機檔暫停時 Apple 先拆解碼器
按下暫停時 Apple 約比 IPC 暫停意圖早 40 ms dispose 轉換器，原本被分類為曲目結束、封存 queue；恢復時先播舊尾段，再由 Apple 從較早的時鐘位置重新解碼 → 聽到倒轉（0.3–5.8 秒）。
- 發現脈絡：使用者回報「暫停後重播有時會倒轉一兩秒」。

### F-11 歌曲列上的播放鈕記為 selection_arm
使用者在歌曲列上暫停／繼續時，log 記為 `transport intent selection_arm`，不是 `play_pause`。
- 發現脈絡：19:02 驗證時找不到 play_pause。

### F-12 獨占模式開啟時 Apple 的共享輸出是純虛擬的
上游 `f51e2b6` 設計：UI 啟用時 Apple 的 shared client 不接 Windows 混音器，由軟體 pump 驅動；mod 接管失敗就是靜音（fail-closed）。虛擬端接受任何格式，所以 AVCF 會要到裝置上限 384 kHz；關掉獨占則拿到真實 48k。
- 發現脈絡：使用者確認「關掉後很正常」的對照實驗。

### F-13 AVCF 路徑的特徵
caller 在 CoreMedia（`0x7FFE…`）而非 `AMPLibraryAgent.exe`（`0x7FF6…`）；有 `AVCFPlayerItem` 呼叫；多段 float 圖、輸出 384 kHz；AudioUnit 先初始化、P0 converter 後建立且在另一執行緒啟動輸出單元。
- 發現脈絡：04:21 起本機檔沒聲音。

### F-14 AVCF 不是佇列／重複播放觸發（負向事實）
曾假設專輯佇列或重複播放讓 Apple 改走 AVCF——不成立：清空佇列後直接點歌仍走 AVCF。
- 發現脈絡：04:26 測試。

### F-15 綁定以外的 AVCF 卡點
跨執行緒延後綁定成功（`graph bound cross-thread`）但 `capturedFrames=0`，狀態卡在「嘗試啟用」。
- 發現脈絡：04:37 測試。

### F-16 ConvertBuffer／FillBuffer 沒被使用（負向事實）
曾假設 AVCF 用 `AudioConverterConvertBuffer` 或 `FillBuffer` 驅動同取樣率轉換——不成立：診斷 hook 0 次呼叫。Apple 的 CoreAudioToolbox 也沒有匯出 `ConvertComplexBuffer`。
- 發現脈絡：04:42 native_trace 診斷。

### F-17 AVCF 的 pre-SRC PCM 只在 CoreAudioToolbox 內部（負向事實）
P0 converter 4165 從建立到 dispose 未經任何公開 API；公開 `AudioConverterFillComplexBuffer`（RVA 0x4E6400）以 0x4F6310（表 0x82B010）把 handle 轉物件，再呼叫 vtable `+0x40`（設 input proc）與 `+0x38`（轉換）。公開層只看得到 converter 4168 與 unit 808 的 384 kHz float（已重取樣）。
- 發現脈絡：04:42 診斷＋反組譯（package 1.1540.23042.0）。

### F-18 公開資料
網路上找不到 Windows 版 Apple Music AVCF／CoreMedia 本機檔路徑的討論；iLounge：Sound Check 關閉時新匯入歌曲不產生正規化資料。
- 發現脈絡：2026-10-21 使用者要求搜尋。

### F-19 音量平衡值只存在資料庫
WAV 檔未被修改、無 `iTunNORM`；資料庫在 `Music\Apple Music\Apple Music Library.musiclibrary\Library.musicdb`。設定 key `optimizeSongVolume` 在 `%LOCALAPPDATA%\Packages\AppleInc.AppleMusicWin_nzyj5cx40ttqa\LocalCache\Roaming\Apple Computer\Preferences\AMPLibraryAgent.exe.plist`（binary plist），重裝 Apple Music 會遺失。使用者觀察：「進了apple music資料庫之後資訊會多一個音量平衡掛在檔案資訊裡面」。
- 發現脈絡：追查 AVCF 原因。

### F-20 AVCF 觸發規則（已驗證）
歌曲在資料庫已有音量平衡資料、但播放時音量平衡關閉 → 走 AVCF。新匯入的第一次播放還沒有資料 → 一般路徑；播幾次後 Apple 寫入資料 → AVCF。保持音量平衡開啟即可走一般路徑，且 mod 在增益前擷取原始 int16（`integerizedSamples=0`、`sourceMappingFailures=0`），輸出不受影響。
- 發現脈絡：19:02 開啟音量平衡實測。

### F-21 暫停修正驗證
GUAC32 本機檔 7 次暫停／繼續，每次丟棄 2.5–3.3 秒封存資料，無倒轉。
- 發現脈絡：04:55 測試。

### F-22 恢復後 30 ms fault 的成因
恢復綁定時 `PromoteV2LocalQueue`（輸出單元啟動執行緒）與 Apple 解碼執行緒同時呼叫無鎖的 `DrainV2LocalQueue`，兩個生產者寫入 SPSC 主 queue，frame 順序錯亂 → `QueuedPcmSource` 檢查失敗 → sink fail-closed。JPLAY 首次要 8192 frames，較易撞到。
- 發現脈絡：04:56（GUAC32）與 04:59（JPLAY）fault。

### F-23 搬移鎖初步驗證
19:02 JPLAY 本機檔約 5 次暫停／繼續與一次拖動，0 fault（先前 3 次中 1 次失敗）。
- 發現脈絡：使用者回報「暫停與繼續我也有測試 看起來還可接受」。

### F-24 硬體緩衝區設定的實際作用
它是「請求」的 period：WASAPI 直接採用；ASIO 從驅動允許範圍挑最近值，GUAC32（440）與 JPLAY（DAC Link）都忽略；另影響啟用前預存量 max(5 × 請求 period, 0.1 s)。不再影響播放中的 pump 水位（ADR-10）。
- 發現脈絡：使用者問「20ms buffer現在的意義」。

### F-25 大 buffer 驅動造成週期斷音（已修）
原 pump 水位 0.3–0.5 s 低於 JPLAY 24576 frames（0.557 s），每 25–35 秒 queue 見底，補 1.1–1.7 秒靜音。`db7217d` 後 Win10 串流 48k/24-bit 播 5.5 分鐘：`sourceWaits=0`、`underruns=0`，queue 穩在 0.8–1.4 s。
- 發現脈絡：使用者回報「好像會每幾十秒有一兩秒沒聲音」。

### F-26 Win10 機器
Windows 10 22H2（19045），預設裝置「數位輸出 (2- FEZZ)」，有 Apple Music 訂閱（無損開啟），JPLAY 24576 frames。
- 發現脈絡：Win10 log。

### F-27 沒有版本檢查
`ApplePrivateOffsets.h` 定義了 `kAppleMusicFileVersion`／`kPackageVersion`，但程式未使用；版本不符仍照常注入。
- 發現脈絡：使用者問重裝 Apple Music 要做什麼。

### F-28 安裝檔
`mod\build\Release\AMExclusive-Setup.exe` 為單檔安裝程式（11 個內嵌資源），隨 cmake 建置更新；`mod\build\dist\` 只在 `Build-Mod.ps1` 時更新，目前是 03:16 上游版。重裝同版 Apple Music 時 mod、ini、排程都保留，只需重開音量平衡（F-19）。
- 發現脈絡：使用者問重裝與 Win10 安裝。

### F-29 shell 工具陷阱
Git Bash 的 `sed -i`／`perl -pi` 會把 CRLF 原始碼改成 LF（本 repo 為 CRLF）；沒有 Python（`python3` 是 Store alias）。
- 發現脈絡：重構與診斷 hook 編輯時。

### F-30 `Install-Mod.ps1` 行為
`-SourceDirectory` 需絕對路徑（相對路徑以程序 cwd 解析）；會強制關閉 Apple Music；保留 `mode`、`period_100ns`、`output`、`asio_driver`，不保留 `native_trace`。
- 發現脈絡：安裝時失敗一次。

### F-31 living doc
「AMExclusive 開發狀態（feat/asio-output）」：https://claude.ai/code/artifact/1be7c81f-b92f-41c0-89f3-e322ab003da3 ，線上文件、不在 repo。
- 發現脈絡：session 開始時建立。

### F-32 另一個 ASIO fork
`yeluo53/AMExclusiveASIO`：基於上游 v1.1、自寫 ASIO 介面、移除 WASAPI sink、只在 RME ADI-2 Pro 驗證，未處理本機檔或 AVCF。
- 發現脈絡：搜尋 AVCF 討論時掃上游 fork。

### F-33 commit 清單
`53ee205` IExclusiveSink、`c32a1da` gitignore SDK、`1eddee7`／`d059b23` 合併上游 `c67c2f3`、`d6152f7` ASIO、`c1751b7` 暫停修正、`857463b` 搬移鎖、`cf9ba62` 輸出裝置選單、`db7217d` pump 水位、`09893bc` 工作紀錄、`5541739` 換曲少音紀錄、successor stage 修正（ADR-15）、README 更新、broker 改綁（ADR-16）。皆已 push 到 origin。
- 發現脈絡：收尾整理。

### F-34 使用者偏好
- 「除了專有名詞不需要硬是翻譯, 其他的輸出用中文」，已寫入全域 `~/.claude/CLAUDE.md`（繁體）。
- open questions 要「一個一個處理」、白話解說。
- 本機音樂用 foobar／JPLAY，Apple Music 主要為串流。
- 發現脈絡：session 中使用者指示。

### F-35 串流自然換曲時 mod 沒有丟資料
Win10 log（pid 3968，`db7217d`，JPLAY 24576 frames）三次自然換曲（23:59:43、00:03:36、00:07:14）：Apple 沿用同一個 decoder converter（4279），每次先記 `v2 streaming producer EOF candidate converter=4279 accepted=1`，下一個非零 P0 callback 取消候選、新曲接在尾段後；約 1.6–2.2 秒後才出現下一曲的 `AVCFPlayerItemSetPreferredMaximumAudioSampleRate`／`FigAlternateEligibleLosslessAudioFilterCreate`。換曲前後每 5 秒 `capturedFrames` 與 `acceptedFrames` 同步增加 240,832，`droppedFrames`、`sourceWaits`、`underruns` 全為 0（capture 層 reject 計數只在 deepDiagnostics 時才累加，production 恆為 0，原本把它當證據是錯的，2026-10-22 更正），`controlledSilenceFrames` 停在啟動時的 24576；換曲附近沒有 reset／discontinuity log。計數只證明數量連續，不證明內容（順序、對齊）正確。
- 發現脈絡：使用者回報「曲目自然切換的第一秒會消失」後比對 log。

### F-36 換曲的第一個音是真的消失，不是延遲（負向事實）
「只是延遲／被大 buffer 推後」的假設不成立。使用者實聽：「I listened: the first note at a track change really is missing, not just late.」並補充「但好像不是每次都會發生」。
- 發現脈絡：模型先提延遲假設，使用者實測否決，2026-10-21。

### F-37 取樣率切換時，舊曲尾段重啟 JPLAY，吃掉新曲開頭約 3 秒
Win10 log（`am-exclusive(1).log`，pid 3968）00:44:49 這次 48k→44.1k 換曲，使用者聽到開頭不見：
- 49.895 EOF candidate（converter 4870）。到 51.06 queue 只剩 15488 frames，不滿一個 period（24576），`AsioSink` 進入 source wait（`sourceWaits` 0→1、`controlledSilenceFrames` +24576），停掉 driver。
- 51.332 Apple native Stop，seal 尾段（`endOfStreamDrainPending_`）。51.37 新 graph（44.1k，converter 4917）啟動，Apple 開始即時解新曲。
- seal 後尾段可以 `CanProvide`，sink 為了播這 0.32 秒的尾段重新 `driver_->start()`。JPLAY 在 Win10 start 要約 3 秒，再加兩個 tail callback，舊 sink 到 54.628 才 Stop/Close。
- drain 期間 `TryBindGraphLocked` 不綁新 graph（原註解假設這段只有「a few milliseconds」）。新曲從 51.37 到約 54.3 的 PCM 沒進 queue；54.629 綁定後，57.68 active 時 `capturedFrames=148051`（3.36 秒），而新 graph 已跑了 6.31 秒，約少 2.9 秒。
- 對照組 00:37:14（44.1k→48k）沒事：seal 時 sink 還在跑，尾段直接送出，0.13 秒後就 Stop，16.112 綁新 graph，沒有遺失。
- 偶發的原因：要看 EOF candidate 到 Apple native Stop 這段時間（這兩次 1.27–1.44 秒）內，queue 會不會先降到不滿一個 period。JPLAY 的 period 大、start 又慢，條件就容易成立。
- 同取樣率、同 converter 的換曲（F-35）不走這條路，那幾次的遺失仍未解釋。
- 發現脈絡：使用者回報「剛剛突然又發生了一次 我猜在log偏末端」，2026-10-21。

### F-38 JPLAY 在 Win10 的 `start()` 約 3 秒
同一 log 裡 `StartEnter`→`StartExit`：00:37:16.214→19.388（3.17 秒）、00:44:54.733→57.681（2.95 秒）。本機約 150 ms（F-7）。換曲重開 sink 時這段 queue 會持續累積，不會掉資料；但只要有東西在等 driver 重啟（例如 F-37），就會被拖住。
- 發現脈絡：分析 F-37 時量到。

### F-39 「已有音量平衡資料」不足以觸發 AVCF（F-20 條件不完整）
本機 Win11、`8a13e22`、JPLAY 8192 frames，使用者表示音量平衡設定是關的：
- 「12. My Soul,Your Beats!」（WAV 16-bit／44.1 kHz）在 02:22、02:23 走 AVCF（384000 Hz converter）沒有聲音；第二次 Apple 連解碼器都沒建立。
- 02:27 重新匯入（歌曲資訊的「加入日期」為 2026-10-22 2:27）。之後 02:28、02:29、02:30、02:33 共四次播放，其中三次是新的 agent 程序，全部走一般路徑（`staging queue created` → `bound active` → `exclusive sink active`），沒有 384000。
- 這時歌曲資訊「檔案」分頁已顯示 `音量 -6.5 dB`。這個欄位就是使用者先前注意到的音量平衡值（F-19）。也就是說，資料已經存在，音量平衡也是關的，卻沒有走 AVCF。F-20 的「已有資料 + 關閉 → AVCF」不是充分條件；重新匯入後要怎樣才會再切到 AVCF，仍未知。
- 歌曲資訊「選項」分頁的「音量調整」滑桿（每首歌各自的增益，中間是 0%、最右是 +100%）：使用者觀察到「拉到最大能播」，拉到 0% 也能播。所以滑桿位置不是開關（負向事實）。模型推測（假設，未驗證）：按「確定」會重寫資料庫紀錄，效果可能跟重新匯入一樣，會把路徑重置回一般路徑。
- 使用者原話：「如果音樂庫裡面單曲的音量平衡是最大 好像就算關掉設定的音量平衡也能正常撥放」、「拉到中間是0% 奇怪 然後又能播了」。
- 發現脈絡：使用者回報 Win11 本機「好像不能撥放了」，2026-10-22。

### F-40 同時啟動兩個 AppleMusic.exe 時，broker 綁到短命的那個並退出
本機 Win11 02:43:05 同時出現 `AppleMusic.exe` pid 76724 與 68092：
- broker 先掃到 76724 就綁上，也只對它注入 UI。
- 02:43:10 76724 結束；broker 依「綁定的程序結束就退出」的規則跟著退出。agent（37044）因此記 `broker IPC disconnected; local runtime retired`。
- 之後 Apple 在 68092／37044 上正常播放，但 mod 完全沒接手：telemetry 一直是 `capturedFrames=0`，聲音走 Apple 自己的輸出。
- 存活的 68092 自己觸發的那次 broker 啟動，因為單一實例 mutex（`Local\AMExclusive.Broker.<SID>`）已被前一個 broker 持有，直接 return 0，所以沒有人接手它。
- 這段綁定邏輯沿用上游，跟 ASIO 無關。之前「點封面後沒正常啟動獨佔，關掉 Apple Music 重開就好」那次，可能也是同一個原因（未確認）。
- 修正見 ADR-16。
- 發現脈絡：使用者回報「現在的撥放似乎攔截失敗」，2026-10-22。
