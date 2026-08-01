; LiveSub 安装脚本（NSIS 3.x，Unicode + 简体中文界面）
; 生成 LiveSub-Setup.exe：安装程序 + 完成页自动下载模型
Unicode true
Icon "..\icons\livesub.ico"
Name "LiveSub 直播实时字幕"
OutFile "LiveSub-Setup.exe"
InstallDir "$LOCALAPPDATA\LiveSub"
InstallDirRegKey HKCU "Software\LiveSub" "InstallDir"
RequestExecutionLevel user

; 界面
!include "MUI2.nsh"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
; 完成页：勾选运行 model-dl.exe 下载模型
!define MUI_FINISHPAGE_RUN "$INSTDIR\model-dl.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动模型下载器（大模型 1.7B 约 2.8GB / 小模型 0.6B 约 1.1GB，首次使用需要）"
!define MUI_FINISHPAGE_RUN_CHECKED
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_LANGUAGE "SimpChinese"

Section "安装"
    SetOutPath "$INSTDIR"

    ; 程序与运行库（资源位于 ../dist/LiveSub/）
    File "..\dist\LiveSub\livesub.exe"
    File "..\dist\LiveSub\model-dl.exe"
    File "..\dist\LiveSub\llama.dll"
    File "..\dist\LiveSub\mtmd.dll"
    File "..\dist\LiveSub\ggml.dll"
    File "..\dist\LiveSub\ggml-base.dll"
    File "..\dist\LiveSub\ggml-vulkan.dll"
    File "..\dist\LiveSub\llama-common.dll"
    File "..\dist\LiveSub\libomp140.x86_64.dll"
    File "..\dist\LiveSub\libgcc_s_seh-1.dll"
    File "..\dist\LiveSub\libstdc++-6.dll"
    File "..\dist\LiveSub\libwinpthread-1.dll"
    File "..\dist\LiveSub\ggml-cpu-x64.dll"
    File "..\dist\LiveSub\ggml-cpu-haswell.dll"
    File "..\dist\LiveSub\ggml-cpu-ivybridge.dll"
    File "..\dist\LiveSub\ggml-cpu-piledriver.dll"
    File "..\dist\LiveSub\ggml-cpu-sandybridge.dll"
    File "..\dist\LiveSub\ggml-cpu-skylakex.dll"
    File "..\dist\LiveSub\ggml-cpu-alderlake.dll"
    File "..\dist\LiveSub\ggml-cpu-cannonlake.dll"
    File "..\dist\LiveSub\ggml-cpu-cascadelake.dll"
    File "..\dist\LiveSub\ggml-cpu-cooperlake.dll"
    File "..\dist\LiveSub\ggml-cpu-icelake.dll"
    File "..\dist\LiveSub\ggml-cpu-sapphirerapids.dll"
    File "..\dist\LiveSub\ggml-cpu-zen4.dll"

    ; 配置模板
    File /oname=config.ini "..\dist\LiveSub\config.ini"

    ; 模型目录（下载器会填充）
    CreateDirectory "$INSTDIR\model"

    ; 快捷方式
    CreateDirectory "$SMPROGRAMS\LiveSub"
    CreateShortcut "$SMPROGRAMS\LiveSub\LiveSub 直播字幕.lnk" "$INSTDIR\livesub.exe"
    CreateShortcut "$SMPROGRAMS\LiveSub\模型下载器.lnk" "$INSTDIR\model-dl.exe"
    CreateShortcut "$SMPROGRAMS\LiveSub\卸载 LiveSub.lnk" "$INSTDIR\uninstall.exe"
    CreateShortcut "$DESKTOP\LiveSub 直播字幕.lnk" "$INSTDIR\livesub.exe"

    ; 卸载信息
    WriteUninstaller "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "Software\LiveSub" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LiveSub" "DisplayName" "LiveSub 直播实时字幕"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LiveSub" "UninstallString" "$INSTDIR\uninstall.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LiveSub" "DisplayVersion" "0.2.0"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LiveSub" "Publisher" "Thatgfsj"
SectionEnd

Section "Uninstall"
    Delete "$INSTDIR\*.*"
    RMDir /r "$INSTDIR\model"
    RMDir "$INSTDIR"
    Delete "$SMPROGRAMS\LiveSub\LiveSub 直播字幕.lnk"
    Delete "$SMPROGRAMS\LiveSub\模型下载器.lnk"
    Delete "$SMPROGRAMS\LiveSub\卸载 LiveSub.lnk"
    RMDir "$SMPROGRAMS\LiveSub"
    Delete "$DESKTOP\LiveSub 直播字幕.lnk"
    DeleteRegKey HKCU "Software\LiveSub"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LiveSub"
SectionEnd
