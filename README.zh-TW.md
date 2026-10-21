<p align="center">
  <img src="assets/ame-logo.svg" alt="AME." width="480">
</p>

# AMExclusive

### Make Apple Music on Windows a “Pro” Hi-Fi Player

[English](README.md) · [简体中文](README.zh-CN.md)

[上游官網](https://amexclusive.tgwgroup.ltd/zh/) · [上游使用文件](https://amexclusive.tgwgroup.ltd/zh/docs/)（簡體中文）

> **關於本 fork**：這裡是 [JagLin/AMExclusive](https://github.com/JagLin/AMExclusive)，是 [AntiOblivionis/AMExclusive](https://github.com/AntiOblivionis/AMExclusive) 的個人 fork。在上游的 WASAPI 獨佔輸出之外，本 fork 新增了 **ASIO 輸出後端**，並在 Apple Music 的播放設定中加入**輸出裝置選單**。上游的修改會定期合併進來。本 fork 為作者自用，不發佈預先編譯好的安裝程式；ASIO 支援需要搭配 Steinberg ASIO SDK 在本機建置（見[本機建置](#本機建置)）。上方的官網與文件連結描述的是上游版本。

## 說明

AMExclusive（AME）是專為 Windows 版 Apple Music 設計的開源專案，為 Apple Music 加上 WASAPI 獨佔音訊輸出，並強制使用最高可用的音質，目標是讓 Apple Music 在受支援的硬體與設定下，成為能做到 bit-perfect 輸出的 Hi-Fi 播放器。

## 開發與測試環境

- Windows 11 x64
- Windows 版 Apple Music 檔案版本 `1.6.4.90`
- 已驗證的套件／框架版本 `1.1540.23042.0` x64

本專案只在上述開發與測試版本完成驗證，不保證在其他 Apple Music 版本上的行為。

本 fork 的 ASIO 輸出另外在以下環境使用過：

- ASIO 驅動程式：**GUAC32**（顯示為 “Combo384 ASIO”）與 **JPLAY Driver**。
- Windows 10 22H2（build 19045），Apple Music 套件版本 `1.1540.23041.0`，僅限串流。安裝程式雖然以 Windows 11 SDK 編譯，但可以在 Windows 10 上執行。這個套件版本比上方已驗證的版本早一個 build，測試中可以正常使用，但 mod 本身不會檢查 Apple Music 版本。

## 功能

1. **WASAPI 獨佔輸出**  
   以獨佔模式直接把音訊送到所選的 Windows 輸出裝置，不經過一般共用模式下的 Windows 混音路徑。

2. **強制最高音質**  
   移除 Apple Music 原生「頻寬不足時自動降低音質」的行為；最高可用的無損串流還沒準備好時會繼續等待，而不是自動退回較低的規格。

3. **Bit-perfect 輸出**  
   在受支援的播放路徑中保持音源的取樣格式，並把整數 PCM 直接交給 WASAPI；音源、裝置與輸出格式相符時，可以做到 bit-perfect 播放。

4. **ASIO 輸出（本 fork）**  
   把同樣的整數 PCM 交給 ASIO 驅動程式而不是 WASAPI，並把裝置取樣率設成目前曲目的取樣率。ASIO 驅動程式通常會自行固定 buffer 大小（例如 JPLAY 跟隨它的 DAC Link 設定），所以 **硬件缓冲区** 的數值在這裡只是請求值；mod 會依驅動程式實際回報的 period 調整內部緩衝。需要使用包含 ASIO SDK 的建置版本。

5. **輸出裝置選單（本 fork）**  
   **設定 > 播放** 中多了 **输出设备** 清單，可以選擇 WASAPI（目前的 Windows 預設裝置），或這台電腦上已註冊的任一 ASIO 驅動程式。新的選擇從下一首歌開始生效。

> mod 自己加進設定頁的控制項（**独占模式**、**硬件缓冲区**、**确定**、**输出设备**）在中文介面下一律以簡體中文顯示，英文介面下則顯示英文。

## 已知限制（本 fork）

- **匯入的本機檔案與音量平衡**：如果某首本機曲目在資料庫裡已經有音量平衡資料，但 **音量平衡**（Sound Check）是關閉的，Apple Music 會改走另一條 mod 無法擷取的播放路徑，獨佔輸出就會沒有聲音。播放本機檔案時請保持 **音量平衡** 開啟：mod 在音量平衡的增益套用之前就擷取 PCM，所以這條路徑的輸出不會被修改。串流不受影響。
- **ASIO SDK**：沒有包含 SDK 的建置版本（包括 GitHub Actions 建置的版本）仍會在選單中列出 ASIO 驅動程式，但選了之後會出現「所选的 ASIO 驱动无法启动」。
- 連續曲目之間的取樣率切換，以及 ASIO 路徑的 bit-perfect 驗證，目前都只做過少量測試。

## 建議設定

在 Apple Music 的 **設定 > 播放** 中使用以下設定（括號內為英文介面的名稱，繁體中文介面的實際字樣可能略有不同）：

- 關閉 **交叉淡入淡出**（Crossfade）。
- 關閉 **聲音增強器**（Sound Enhancer）。
- 播放串流時關閉 **音量平衡**（Sound Check）；播放匯入的本機檔案時請保持開啟（見[已知限制](#已知限制本-fork)）。
- 開啟 **無損音訊**（Lossless Audio）。
- 把無損音訊下方的兩個音質選項都設為 **高解析度無損（ALAC）**（Hi-Res Lossless (ALAC)）。
- 關閉 **杜比全景聲**（Dolby Atmos）。
- 開啟 **独占模式**（Exclusive Mode）。
- 選擇 **输出设备**（Output device，本 fork）：WASAPI 或某個 ASIO 驅動程式。
- 在 **硬件缓冲区**（Hardware buffer）輸入 `1`–`100` 毫秒的整數，再按 **确定**（Apply）。這個值會在下一首歌開啟獨佔輸出時套用。預設是 `20` 毫秒，播放有問題時可以試著調高。
- 關閉 **空間音訊**（Spatial sound）。

使用分離式 DAC 與擴大機時，把 Apple Music 的 App 音量、Windows 輸出音量和 DAC 輸出音量都保持在最大，只用擴大機調整實際聆聽音量。使用 DAC／耳擴一體機時，把 Apple Music 的 App 音量和 Windows 輸出音量保持在最大，用一體機的硬體音量控制調整實際聆聽音量。第一次套用這些設定前，請先調低擴大機或一體機的硬體音量，避免突然出現過大的音量。

## 建置與安裝

### 使用 GitHub Actions 建置

> **本 fork**：這個 workflow 建置出的版本**不含 ASIO**。repo 裡沒有 Steinberg ASIO SDK，所以建置機會把 ASIO 後端編成永遠無法使用，產出的安裝程式只提供 WASAPI 獨佔輸出。如果把 SDK 放進 CI，等於把含有 SDK 程式碼的二進位檔當作可下載的建置產物公開，本 fork 刻意避免這麼做。這個 workflow 尚未在本 fork 上執行過。需要 ASIO 時請[在本機建置](#本機建置)。

1. 開啟 [Actions → Build Setup](https://github.com/JagLin/AMExclusive/actions/workflows/build-setup.yml)。要建置自己的修改，請先 fork 這個 repo，並在自己的 repo 中啟用 Actions。
2. 按 **Run workflow**，選擇分支後執行。
3. 執行成功後，從該次執行的 **Artifacts** 下載 **AMExclusive-Setup-x64**，解壓縮得到 `AMExclusive-Setup.exe`。

這個 workflow 只能手動觸發，會建置 Windows x64 Release 並檢查安裝程式內嵌的檔案，不會發佈 GitHub Release。建置機不需要安裝 Apple Music：workflow 會從 NuGet 下載固定版本的 Microsoft Windows App SDK／WinUI metadata 套件，並在建置前用固定的 SHA-256 驗證 `Microsoft.UI.Xaml.winmd`。要安裝 AMExclusive 的電腦仍然需要安裝 Apple Music。建置產物保留 14 天，下載時需要登入 GitHub。

### 本機建置

只支援在 Windows x64 上建置。需要安裝：

- Visual Studio 2022 或更新版本，或 MSVC Build Tools，並包含 C++ 桌面開發工作負載。
- Windows 11 SDK（建置時會用到其中的 `cppwinrt.exe`）。
- CMake 3.24 或更新版本。Visual Studio 內建的 CMake 即可，`Build-Mod.ps1` 會自動找到它。
- PowerShell。
- **ASIO 輸出（本 fork）**：Steinberg ASIO SDK。請從 [Steinberg Developers](https://www.steinberg.net/developers/) 下載，解壓縮到 `third_party/asiosdk/`，確認 `third_party/asiosdk/common/iasiodrv.h` 存在。這個資料夾已被 git 忽略。

  CMake 找到 SDK 時會顯示 `ASIO output enabled with SDK at …`，找不到時會顯示 `ASIO SDK not found …; ASIO output disabled`。沒有 SDK 時建置仍會成功，但只能使用 WASAPI 輸出。

  SDK 不屬於本 repo。Steinberg 以自家的專有授權或 GPLv3 提供 SDK，下載時需要選擇其中一種並接受。用 SDK 建置出的安裝程式含有 SDK 程式碼，除非你遵守所選授權的條件，否則請勿再散布。

建置腳本預設會從本機安裝的 Windows 版 Apple Music 套件讀取所需的 WinUI metadata。`mod/CMakeLists.txt` 中預設的套件路徑固定為 `1.1540.23042.0` 版本；如果本機安裝的版本不同，請改用下方的獨立 metadata 方式。

```powershell
cd mod
.\scripts\Build-Mod.ps1 -Configuration Release
```

建置機沒有安裝 Apple Music 時，也可以先準備獨立的 metadata 資料夾再建置：

```powershell
cd mod
.\scripts\Get-WinUIMetadata.ps1 -OutputDirectory .\build\winui-metadata
.\scripts\Build-Mod.ps1 -Configuration Release -WinUIMetadataDirectory .\build\winui-metadata
```

安裝程式輸出位置：

```text
mod\build\dist\AMExclusive-Setup.exe
```

安裝或升級時，執行 `AMExclusive-Setup.exe` 並選擇 `1`。接著開啟 Apple Music，進入 **設定 > 播放**，開啟 **独占模式** 並選擇 **输出设备**。升級安裝會保留現有設定，包括已選的輸出裝置。

解除安裝時，執行同一個安裝程式並選擇 `2`，或在 **Windows 設定 > 應用程式 > 已安裝的應用程式** 中解除安裝 **AMExclusive**。

## 致謝

感謝 [LINUX DO 社群](https://linux.do/) 在本專案開發與測試過程中提供的討論、支持與回饋。

本專案使用了以下開源專案：

- [MinHook](https://github.com/TsudaKageyu/minhook) — 提供 Windows x64/x86 API hook 的基礎能力。
- [rigtorp/SPSCQueue](https://github.com/rigtorp/SPSCQueue) — 提供即時音訊路徑使用的單一生產者、單一消費者有界佇列。

感謝上述專案及其貢獻者。完整的版本、著作權與授權資訊見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 授權與聲明

本專案自行撰寫的原始碼以 [MIT License](LICENSE) 釋出。第三方元件沿用各自的上游授權，詳見 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

AMExclusive 是獨立、非官方的開源專案，與 Apple Inc. 沒有任何關聯，也未獲得其授權或背書。Apple Music 是 Apple Inc. 的商標。

本 fork 不包含也不再散布 Steinberg ASIO SDK。ASIO 是 Steinberg Media Technologies GmbH 的商標。

本專案不保證在不同的軟體、硬體、驅動程式或音訊裝置設定下都能正常運作。使用本專案可能造成無法預期的後果，使用者須自行承擔相關風險。

本專案不散布 Apple 軟體、Apple 二進位檔、解密後的媒體或無 DRM 的參考檔案，也不提供用來繞過 DRM 保護的功能。
