# CLAUDE.md

AMExclusive（AME）：Apple Music for Windows 的注入式 mod，提供 WASAPI 獨占輸出與最高音質鎖定（ALAC Hi-Res），目標 bit-perfect。架構圖解見 `OVERVIEW.html`。

本 repo 是 `JagLin/AMExclusive`，fork 自 `AntiOblivionis/AMExclusive`。

## 建置

只能在 Windows x64 建置（CMake 會擋非 WIN32）。WSL 內無法編譯或執行，只能讀寫原始碼。

```powershell
cd mod
.\scripts\Get-WinUIMetadata.ps1 -OutputDirectory .\build\winui-metadata
.\scripts\Build-Mod.ps1 -Configuration Release -WinUIMetadataDirectory .\build\winui-metadata
# 產物：mod\build\dist\AMExclusive-Setup.exe
```

- 需要 MSVC、Windows 11 SDK（`cppwinrt.exe`）、CMake ≥ 3.24、PowerShell。
- CI：`.github/workflows/build-setup.yml`，只能手動觸發。
- 沒有自動化測試。驗證方式是實機安裝後看 `%PUBLIC%\AMExclusive\*.log`。

## 版本綁定

只在 Apple Music `1.6.4.90` / package `1.1540.23042.0` x64 驗證過。所有 Apple 私有 ABI（RVA、struct offset、呼叫堆疊指紋、ALAC cookie 位置）都集中在 `mod/src/ApplePrivateOffsets.h`，新增私有依賴時也放在這裡。`CMakeLists.txt` 的 `APPLE_MUSIC_PACKAGE_DIR` 預設值也綁這個版本。

## 程序與元件

```
工作排程器 (AppModel-Runtime EventID 201) → am-exclusive-broker.exe
  ├─ 注入 am-exclusive-ui.dll   → AppleMusic.exe       (Role::Ui)
  └─ 注入 am-exclusive-hook.dll → AMPLibraryAgent.exe  (Role::Agent，實際播放)
  三者以 named pipe 通訊（IpcProtocol.h）
```

| 路徑 | 職責 |
|---|---|
| `mod/src/Broker.cpp` | 生命週期綁定單一 AppleMusic.exe PID；ProcessWatcher 注入；pipe server；持有使用者 intent 並寫入 ini；錯誤 TaskDialog |
| `mod/src/UiAdapter.cpp` | C++/WinRT 走訪 WinUI 視覺樹，在 `PlaybackSettingsPage` 插入開關與 buffer 輸入框；transport 手勢 → `TransportIntent` |
| `mod/src/HookDll.cpp` | MinHook 安裝點、Broker IPC client、native trace 診斷 |
| `mod/src/LosslessQualityLock.cpp` | 嚴格 ALAC subtype filter（音質鎖） |
| `mod/src/audio_v2/` | 獨占輸出核心：registry → tap → queue → mapper → sink（WASAPI 或 ASIO），加上 native render gate |
| `mod/src/MediaSessionControl.cpp` | SMTC 輔助 exe（`reject-current` / `restart-current`） |
| `mod/src/SetupInstaller.cpp`、`mod/scripts/` | 單檔安裝程式與 PowerShell 安裝 / 解除安裝 / 排程註冊 |

## Hook DLL 的兩條獨立路徑

1. **音質鎖**：hook `AVCFPlayerItemSetVariantPreferences`、`AVCFPlayerItemSetPreferredMaximumAudioSampleRate`、`FigAlternateEligibleLosslessAudioFilterCreate`。這條路徑刻意**不受** UI 開關控制，不要把它接到 `AudioCoreGate`。
2. **獨占輸出（audio core v2）**：hook CoreAudioToolbox 的 `AudioConverter*` / `AudioUnit*` / `AudioOutputUnit*`，以及 `CoCreateInstance` → `IMMDeviceEnumerator` / `IMMDevice::Activate`。目前 `kNativeAcquisitionPassthrough = true`：Apple 的排程照常運作，mod 鏡像 float32 P0，再接手實體端點。

## audio_v2 不可破壞的不變式

- **Callback 執行緒不配置記憶體、不等待、不拿鎖**：`ApplePcmTap` / registry lookup 只做 atomic 讀取和 SPSC push。所有端點所有權轉換只在 `AudioCoreRuntime` 的 worker 執行緒進行。
- **Fail-closed，不修改樣本**：`ExactSampleMapper` 對整數來源只接受精確的 lattice 值，不做 rounding、dither、增益或重取樣。`LocalFloatIntegerizer` 只用在本機 float32 來源，而且刻意跟前者分開。
- **不偽造樣本**：source 不足時回傳 `kAudioSourceWouldBlock`，sink 暫停，不補靜音。只有明確的 end-of-stream 才能送出不足一個 period 的資料。
- **Generation token**：`AudioCoreGate` 每次切換都遞增 generation，舊 token 立即失效。只有 `CommitCutover`（sink 已 Start 且已 prebuffer）會讓原生輸出被壓掉。
- **Intent 所有權**：持久化的使用者 intent 由 Broker 擁有。Agent 的 `StatusChanged` / `AttemptFailed` 只能結束當次嘗試，不能清除 intent。
- 抽象邊界：`IIntegerPcmSource` 與 `IExclusiveSink`（`ExclusiveSink.h`）。`AudioCoreCoordinator` 以成員持有 `WasapiExclusiveSink` 與 `AsioSink`，`Enable` 時依輸出後端選用。
- `AsioSink`：驅動呼叫全在專屬 STA worker（全程 `ScopedNativeProxyBypass`，有些驅動內部自開 WASAPI client）；`bufferSwitch` 不足時只能寫零並計入 `controlledSilenceFrames`。Steinberg SDK 只放本機 `third_party/asiosdk/`（gitignore），沒有 SDK 時編成永遠不可用。
- pump 的 queue 水位依 sink 實際 period（`IExclusiveSink::BufferFrames`）調整；ASIO 驅動常無視請求的 buffer（JPLAY 依 DAC Link 可到 24576 frames）。

## IPC

- `Message` 是單一 `#pragma pack(1)`、trivially-copyable 的結構，必須 < 4096 bytes。改 layout 時要遞增 `kProtocolMajor`，三端 binary 一起發佈。
- pipe 名稱與 DACL 綁定目前使用者的 SID；Broker 會比對 `GetNamedPipeClientProcessId` 與 `senderPid`。

## 設定與日誌（安裝目錄 `%PUBLIC%\AMExclusive`）

- `am-exclusive.ini` 的 `[mod]` 區段：`mode=exclusive|probe`、`period_100ns`（預設 200000，對應 UI 上的 1–100 ms）、`native_trace`（診斷）、`output=wasapi|asio`、`asio_driver`（`HKLM\SOFTWARE\ASIO` 下的鍵名）。`output`／`asio_driver` 由 Broker 擁有，UI 設定頁的「输出设备」選單經 IPC 寫入，Agent 只從 status 套用。新增 ini key 時，要一併更新 `Install-Mod.ps1` 的保留邏輯。
- 日誌：`am-exclusive-broker.log`、`am-exclusive.log`（hook）、`am-exclusive-ui.log`、`am-exclusive-recovery.log`，都用 `BoundedLog.h` 輪替。

## 程式風格

- C++20、MSVC `/W4 /permissive- /guard:cf`，命名空間 `ammod::*`（`ipc`、`quality`、`audio_v2`、`apple_private`）。
- 匿名命名空間內的全域變數加 `g_` 前綴，成員變數加 `_` 後綴；hook 函式命名為 `Hook<Target>`，原函式指標命名為 `g_original<Target>` / `g_v2Original<Target>`。
- 只要不需要丟例外，就標 `noexcept`；錯誤一律回傳 `HRESULT` / `AppleOSStatus`，不丟例外。
- 註解說明「為什麼」和不變式，而不是逐行描述做了什麼；修改時沿用這種寫法。
- UI 字串以 `Localized(chinese, english)` 依 Apple Music 的 app runtime 語言切換（中文時用簡體字串，其餘用英文）。

## 被 .gitignore 排除的路徑

`/docs/`、`/scripts/`（根目錄）、`/mod/tests/`、`/evidence/`、`/report/`、`/artifacts/`、`/third_party/asiosdk/`、`third_party/` 下的 PortAudio / PlayPcmWin / mpv 等。要提交的文件不要放進這些目錄。

## 本 fork 的工作紀錄

`notes/<feature>/TRACKING.md` 記錄本 fork 各 feature 的交接、決定（含使用者原話）、未決問題與已知事實。上游沒有 `notes/`，不會與上游衝突；不要改放 `docs/`。接手某個 feature 前先讀它的檔頭交接段。目前：`notes/asio-output/`（ASIO 輸出與輸出裝置選單）。
