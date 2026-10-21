<p align="center">
  <img src="assets/ame-logo.svg" alt="AME." width="480">
</p>

# AMExclusive

### Make Apple Music on Windows a “Pro” Hi-Fi Player

[English](README.md) · [繁體中文](README.zh-TW.md)

[官网](https://amexclusive.tgwgroup.ltd/zh/) · [使用文档](https://amexclusive.tgwgroup.ltd/zh/docs/)

> **关于本 fork**：这里是 [JagLin/AMExclusive](https://github.com/JagLin/AMExclusive)，是 [AntiOblivionis/AMExclusive](https://github.com/AntiOblivionis/AMExclusive) 的个人 fork。在上游的 WASAPI 独占输出之外，本 fork 新增了 **ASIO 输出后端**，并在 Apple Music 播放设置中加入了**输出设备选单**。上游的修改会定期合并进来。本 fork 为作者自用，不发布预编译安装器；ASIO 支持需要搭配 Steinberg ASIO SDK 在本地编译（见[本地编译](#本地编译)）。上方的官网与文档链接描述的是上游版本。

## 描述

AMExclusive（AME）是一个专为 Windows 版 Apple Music 设计的开源项目，旨在为 Apple Music 提供 WASAPI 独占音频输出以及强制最高音频规格输出的能力，并以此将 Apple Music 优化为一款在受支持硬件与配置下可实现 bit-perfect 输出的“专业”Hi-Fi 音乐播放器。

## 开发与测试环境

- Windows 11 x64
- Windows 版 Apple Music 文件版本 `1.6.4.90`
- 已验证包/框架版本 `1.1540.23042.0` x64

本项目仅在上述开发与测试版本中完成验证，不保证在其他 Apple Music 版本上的运行效果。

本 fork 的 ASIO 输出另外在以下环境中使用过：

- ASIO 驱动：**GUAC32**（显示为 “Combo384 ASIO”）与 **JPLAY Driver**。
- Windows 10 22H2（build 19045），Apple Music 包版本 `1.1540.23041.0`，仅限流媒体。安装器虽然使用 Windows 11 SDK 编译，但可以在 Windows 10 上运行。这个包版本比上方已验证的版本早一个 build，测试中可以正常使用，但 mod 本身不会检查 Apple Music 版本。

## 特性

1. **WASAPI 独占输出**  
   以独占模式直接向当前 Windows 输出设备提交音频，绕过常规共享模式下的 Windows 音频混音路径。

2. **强制最高音频规格**  
   去除 Apple Music 原生“低带宽自动降低音频规格”的逻辑，在最高可用无损规格尚未就绪时继续加载等待，而不是自动回退到更低音频规格。

3. **Bit-perfect 输出**  
   在受支持的播放路径中保持源音频规格，并将整数 PCM 直接提交给 WASAPI；当音源、输出设备与目标格式匹配时，可实现 bit-perfect 播放。

4. **ASIO 输出（本 fork）**  
   将同样的整数 PCM 提交给 ASIO 驱动，而不是 WASAPI，并把设备采样率设为当前曲目的采样率。ASIO 驱动通常会自行固定 buffer 大小（例如 JPLAY 跟随其 DAC Link 设置），因此 **硬件缓冲区** 的数值在这里只是请求值；mod 会按驱动实际回报的 period 调整内部缓冲。需要使用包含 ASIO SDK 的编译版本。

5. **输出设备选单（本 fork）**  
   **设置 > 播放** 中新增 **输出设备** 列表，可选择 WASAPI（当前 Windows 默认设备）或本机已注册的任一 ASIO 驱动。新的选择从下一首歌曲开始生效。

## 已知限制（本 fork）

- **导入的本地文件与音量平衡**：如果某首本地曲目在资料库中已经有音量平衡数据，但 **音量平衡** 处于关闭状态，Apple Music 会改用 mod 无法截取的另一条播放路径，此时独占输出没有声音。播放本地文件时请保持 **音量平衡** 开启：mod 在音量平衡增益生效之前截取 PCM，所以这条路径的输出不会被修改。流媒体不受影响。
- **ASIO SDK**：未包含 SDK 的编译版本（包括 GitHub Actions 编译的版本）仍会在选单中列出 ASIO 驱动，但选择后会提示 “所选的 ASIO 驱动无法启动”。
- 连续曲目之间的采样率切换，以及 ASIO 路径的 bit-perfect 验证，目前只做过少量测试。

## 推荐设置

在 Apple Music 的 **设置 > 播放** 中：

- 关闭 **交叉渐入渐出**。
- 关闭 **声音增强器**。
- 播放流媒体时关闭 **音量平衡**；播放导入的本地文件时请保持开启（见[已知限制](#已知限制本-fork)）。
- 开启 **无损音频**。
- 将无损音频下方的两项音频规格均设为 **高解析度无损（ALAC）**。
- 关闭 **杜比全景声**。
- 开启 **独占模式**。
- 选择 **输出设备**（本 fork）：WASAPI 或某个 ASIO 驱动。
- 在 **硬件缓冲区** 中输入 `1`–`100` 毫秒的整数并点击 **确定**。该数值会在下一首歌曲打开独占输出时应用。默认值为 `20` 毫秒，遇到播放问题时可尝试增加数值。
- 关闭 **空间音效**。

使用独立 DAC 和放大器时，将 Apple Music 应用内音量、Windows 输出音量和 DAC 输出音量保持在最大值，只通过放大器调整实际播放音量。使用 DAC/AMP 一体机时，将 Apple Music 应用内音量和 Windows 输出音量保持在最大值，通过 DAC/AMP 一体机的硬件音量控制调整实际播放音量。首次应用这些设置前，请先降低放大器或 DAC/AMP 一体机的硬件音量，避免突然出现过高声压。

## 编译与安装

### 使用 GitHub Actions 编译

> **本 fork**：这个工作流编译出的版本**不含 ASIO**。仓库中没有 Steinberg ASIO SDK，因此构建机会把 ASIO 后端编译成永远不可用，产出的安装器只提供 WASAPI 独占输出。若把 SDK 放进 CI，等于把含 SDK 代码的二进制文件作为可下载的构建产物公开，本 fork 刻意避免这样做。这个工作流尚未在本 fork 上运行过。需要 ASIO 时请[本地编译](#本地编译)。

1. 打开 [Actions → Build Setup](https://github.com/JagLin/AMExclusive/actions/workflows/build-setup.yml)。编译自己的修改时，先 Fork 仓库，并在自己的仓库中启用 Actions。
2. 点击 **Run workflow**，选择分支并运行。
3. 成功后，在该次运行的 **Artifacts** 中下载 **AMExclusive-Setup-x64**，解压得到 `AMExclusive-Setup.exe`。

工作流仅手动触发，编译 Windows x64 Release 并检查安装器内嵌文件，不自动发布 Release。构建机不需要安装 Apple Music：工作流会从 NuGet 下载固定版本的 Microsoft Windows App SDK / WinUI 元数据包，并在编译前使用固定 SHA-256 校验 `Microsoft.UI.Xaml.winmd`。安装 AMExclusive 的电脑仍需安装 Apple Music。构建产物保留 14 天，下载需登录 GitHub。

### 本地编译

仅支持在 Windows x64 上编译。需要安装：

- Visual Studio 2022 或更新版本，或 MSVC Build Tools，并包含 C++ 桌面开发工作负载。
- Windows 11 SDK（编译会用到其中的 `cppwinrt.exe`）。
- CMake 3.24 或更新版本。Visual Studio 自带的 CMake 即可，`Build-Mod.ps1` 会自动找到它。
- PowerShell。
- **ASIO 输出（本 fork）**：Steinberg ASIO SDK。请从 [Steinberg Developers](https://www.steinberg.net/developers/) 下载，解压到 `third_party/asiosdk/`，确保 `third_party/asiosdk/common/iasiodrv.h` 存在。这个目录已被 git 忽略。

  CMake 找到 SDK 时会显示 `ASIO output enabled with SDK at …`，找不到时会显示 `ASIO SDK not found …; ASIO output disabled`。没有 SDK 时编译仍会成功，但只能使用 WASAPI 输出。

  SDK 不属于本仓库。Steinberg 以其专有许可证或 GPLv3 提供 SDK，下载时需要选择其中一种并接受。用 SDK 编译出的安装器包含 SDK 代码，除非遵守你所选许可证的条件，否则请勿再分发。

默认情况下，构建脚本会从本机安装的 Windows 版 Apple Music 中读取所需 WinUI 元数据。`mod/CMakeLists.txt` 中默认的包路径固定为 `1.1540.23042.0` 版本；如果本机安装的版本不同，请改用下方的独立元数据方式：

```powershell
cd mod
.\scripts\Build-Mod.ps1 -Configuration Release
```

构建机没有安装 Apple Music 时，也可以先准备独立元数据目录再编译：

```powershell
cd mod
.\scripts\Get-WinUIMetadata.ps1 -OutputDirectory .\build\winui-metadata
.\scripts\Build-Mod.ps1 -Configuration Release -WinUIMetadataDirectory .\build\winui-metadata
```

安装器输出位置：

```text
mod\build\dist\AMExclusive-Setup.exe
```

安装或升级时，运行 `AMExclusive-Setup.exe` 并选择 `1`。随后打开 Apple Music，进入 **设置 > 播放**，开启 **独占模式** 并选择 **输出设备**。升级安装会保留现有设置，包括已选的输出设备。

卸载时，运行同一个 Setup 并选择 `2`，或在 **Windows 设置 > 应用 > 已安装的应用** 中卸载 **AMExclusive**。

## 致谢

感谢 [LINUX DO 社区](https://linux.do/) 在项目开发与测试过程中提供的交流、支持与反馈。

本项目使用了以下开源项目：

- [MinHook](https://github.com/TsudaKageyu/minhook) — 提供 Windows x64/x86 API Hook 基础能力。
- [rigtorp/SPSCQueue](https://github.com/rigtorp/SPSCQueue) — 提供音频实时路径使用的单生产者、单消费者有界队列。

感谢上述项目及其贡献者。完整版本、版权和许可证信息见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 协议与声明

项目自行编写的源码使用 [MIT License](LICENSE)。第三方组件继续遵循其各自的上游许可证，详见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

AMExclusive 是一个独立且非官方的开源项目，与 Apple Inc. 没有任何关联，也未获得其许可或背书。Apple Music 是 Apple Inc. 的商标。

本 fork 不包含也不再分发 Steinberg ASIO SDK。ASIO 是 Steinberg Media Technologies GmbH 的商标。

本项目不保证在不同软件、硬件、驱动程序或音频设备条件下能够正常运行。使用本项目可能造成不可预知的后果，使用者应自行承担相关风险。

本项目不分发 Apple 软件、Apple 二进制文件、解密后的媒体或 DRM-free 参考文件，也不提供用于绕过 DRM 保护的功能。
