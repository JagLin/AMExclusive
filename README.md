<p align="center">
  <img src="assets/ame-logo.svg" alt="AME." width="480">
</p>

# AMExclusive

### Make Apple Music on Windows a “Pro” Hi-Fi Player

[中文](README.zh-CN.md)

[Website](https://amexclusive.tgwgroup.ltd/en/) · [Documentation](https://amexclusive.tgwgroup.ltd/en/docs/)

> **About this fork.** This is [JagLin/AMExclusive](https://github.com/JagLin/AMExclusive), a personal fork of [AntiOblivionis/AMExclusive](https://github.com/AntiOblivionis/AMExclusive). It adds an **ASIO output backend** next to upstream's WASAPI exclusive output, and an **output device picker** in Apple Music's playback settings. Upstream changes are merged in regularly. The fork is maintained for the author's own setup; no prebuilt binaries are published, and ASIO support requires building locally with the Steinberg ASIO SDK (see [Build locally](#build-locally)). The website and documentation links above describe upstream.

## Description

AMExclusive (AME) is an open-source project designed specifically for Apple Music on Windows. It adds WASAPI exclusive audio output and forces Apple Music to use the highest available audio quality, with the goal of turning Apple Music into a more capable Hi-Fi player that can achieve bit-perfect output on supported hardware and configurations.

## Development and Test Environment

- Windows 11 x64
- Apple Music for Windows file version `1.6.4.90`
- Verified package/framework build `1.1540.23042.0` x64

This project has only been validated with the development and test versions listed above. Its behavior on other Apple Music versions is not guaranteed.

This fork's ASIO output has additionally been used with:

- ASIO drivers: **GUAC32** (reports "Combo384 ASIO") and **JPLAY Driver**.
- Windows 10 22H2 (build 19045) with Apple Music package `1.1540.23041.0`, streaming only. The installer is built with the Windows 11 SDK but runs on Windows 10. That package build is one step older than the verified one above; it worked in testing, but the mod does not check the Apple Music version.

## Features

1. **WASAPI exclusive output**  
   Sends audio directly to the selected Windows output device in exclusive mode, avoiding the normal shared-mode Windows audio mixing path.

2. **Forced highest audio quality**  
   Removes Apple Music's native low-bandwidth quality downgrade behavior and keeps waiting for the highest available lossless stream instead of automatically falling back to a lower audio specification.

3. **Bit-perfect output**  
   Preserves the source sample format through the supported playback path and submits integer PCM directly to WASAPI, allowing bit-perfect playback when the source, device, and supported output format match.

4. **ASIO output (this fork)**  
   Sends the same integer PCM to an ASIO driver instead of WASAPI. The device sample rate is set to the track's rate. ASIO drivers usually fix their own buffer size (JPLAY, for example, follows its DAC Link setting), so the **Hardware buffer** value is only a request there; the mod sizes its internal buffering from the period the driver actually reports. Requires a build that includes the ASIO SDK.

5. **Output device picker (this fork)**  
   **Settings > Playback** gains an **Output device** list: WASAPI (the current Windows default device) or any ASIO driver registered on the PC. A new selection takes effect from the next song.

## Known Limitations (this fork)

- **Imported local files and Sound Check.** If a local track already has Sound Check data in the library but **Sound Check** is off, Apple Music plays it through a different player path that the mod cannot capture, and exclusive output is silent. Keep **Sound Check** on for local files: the mod captures PCM before the Sound Check gain is applied, so output on that path stays unmodified. Streaming is not affected.
- **ASIO SDK.** Builds without the SDK, including GitHub Actions builds, still list ASIO drivers in the picker, but selecting one shows "The selected ASIO driver could not start".
- Sample-rate changes between consecutive tracks and bit-perfect verification on the ASIO path have only been tested lightly.

## Recommended Settings

In Apple Music, open **Settings > Playback** and use the following settings:

- Turn **Crossfade** off.
- Turn **Sound Enhancer** off.
- Turn **Sound Check** off for streaming. For imported local files, keep it on (see [Known Limitations](#known-limitations-this-fork)).
- Turn **Lossless Audio** on.
- Set both audio-quality selectors below Lossless Audio to **Hi-Res Lossless (ALAC)**.
- Turn **Dolby Atmos** off.
- Turn **Exclusive Mode** on.
- Choose the **Output device** (this fork): WASAPI or an ASIO driver.
- Enter an integer from `1` through `100` milliseconds in **Hardware buffer** field and select **Apply**. The setting is used when exclusive output opens for the next song. The default is `20` ms; try a higher value if encountering playback problems.
- Turn **Spatial sound** off.

With a separate DAC and amplifier, keep the Apple Music app volume, Windows output volume, and DAC output volume at maximum, and adjust listening volume only on the amplifier. With an integrated DAC/amp, keep the Apple Music app volume and Windows output volume at maximum, and adjust listening volume using the DAC/amp hardware control. Before applying these settings for the first time, lower the amplifier or DAC/amp hardware volume to avoid an unexpectedly high sound level.

## Build and Install

### Build with GitHub Actions

> **This fork:** the workflow builds **without ASIO**. The Steinberg ASIO SDK is not in the repository, so the runner compiles the ASIO backend as permanently unavailable; the resulting installer offers WASAPI exclusive output only. Putting the SDK into CI would publish SDK-derived binaries as downloadable artifacts, which this fork deliberately avoids. The workflow has not yet been run on this fork. For ASIO, [build locally](#build-locally).

1. Open [Actions → Build Setup](https://github.com/JagLin/AMExclusive/actions/workflows/build-setup.yml). To build your own changes, fork the repository and enable Actions in your fork first.
2. Select **Run workflow**, choose the branch, and run it.
3. When the run succeeds, download **AMExclusive-Setup-x64** from **Artifacts** and unzip it to obtain `AMExclusive-Setup.exe`.

The workflow runs only when manually triggered. It builds Windows x64 Release and checks the embedded installer payload; it does not publish a GitHub Release. No Apple Music installation is needed on the runner: it downloads pinned Microsoft Windows App SDK / WinUI metadata packages from NuGet and verifies `Microsoft.UI.Xaml.winmd` against a pinned SHA-256 before building. Apple Music is still required on the PC where you install AMExclusive. Artifacts are retained for 14 days; downloading them requires signing in to GitHub.

### Build locally

Building is supported only on Windows x64. Install:

- Visual Studio 2022 or newer, or the MSVC Build Tools, with the C++ desktop workload.
- A Windows 11 SDK. The build uses its `cppwinrt.exe`.
- CMake 3.24 or newer. The copy bundled with Visual Studio works; `Build-Mod.ps1` finds it.
- PowerShell.
- **For ASIO output (this fork):** the Steinberg ASIO SDK. Download it from [Steinberg Developers](https://www.steinberg.net/developers/) and extract it to `third_party/asiosdk/`, so that `third_party/asiosdk/common/iasiodrv.h` exists. The directory is git-ignored.

  CMake reports `ASIO output enabled with SDK at …` when it finds the SDK, or `ASIO SDK not found …; ASIO output disabled` when it does not. Without the SDK the build still succeeds, but with WASAPI output only.

  The SDK is not part of this repository. Steinberg offers it under its own proprietary licence or under GPLv3, and you accept one of them when you download it. Installers built with the SDK contain SDK code, so do not redistribute them unless you comply with the licence you chose.

By default, the build script reads the required WinUI metadata from the locally installed Apple Music package. The default package path in `mod/CMakeLists.txt` is pinned to build `1.1540.23042.0`. If your installed build differs, use the standalone metadata route below.

```powershell
cd mod
.\scripts\Build-Mod.ps1 -Configuration Release
```

Apple Music does not need to be installed on the build machine if you provide standalone metadata instead:

```powershell
cd mod
.\scripts\Get-WinUIMetadata.ps1 -OutputDirectory .\build\winui-metadata
.\scripts\Build-Mod.ps1 -Configuration Release -WinUIMetadataDirectory .\build\winui-metadata
```

The installer will be generated at:

```text
mod\build\dist\AMExclusive-Setup.exe
```

To install or upgrade, run `AMExclusive-Setup.exe` and choose `1`. Then open Apple Music, go to **Settings > Playback**, enable **Exclusive mode**, and pick the **Output device**. Upgrading keeps the existing settings, including the selected output device.

To uninstall, run the same setup and choose `2`, or uninstall **AMExclusive** from **Windows Settings > Apps > Installed apps**.

## Acknowledgements

Thanks to the [LINUX DO community](https://linux.do/) for the discussion, support, and feedback provided during the development and testing of this project.

This project uses the following open-source projects:

- [MinHook](https://github.com/TsudaKageyu/minhook) — provides the Windows x64/x86 API hooking foundation.
- [rigtorp/SPSCQueue](https://github.com/rigtorp/SPSCQueue) — provides the bounded single-producer, single-consumer queue used on the real-time audio path.

Thanks to these projects and their contributors. Complete version, copyright, and license information is available in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## License and Disclaimer

Project-authored source code is released under the [MIT License](LICENSE). Third-party components retain their respective upstream licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

AMExclusive is an independent and unofficial open-source project. It is not affiliated with, endorsed by, or licensed by Apple Inc. Apple Music is a trademark of Apple Inc.

This fork does not include or redistribute the Steinberg ASIO SDK. ASIO is a trademark of Steinberg Media Technologies GmbH.

This project is not guaranteed to operate correctly across different software, hardware, driver, or audio-device configurations. Its use may cause unforeseen consequences, and users assume the associated risks.

This project does not distribute Apple software, Apple binaries, decrypted media, or DRM-free reference files, and it does not provide functionality intended to bypass DRM protection.
