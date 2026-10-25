Unicode True
!include "MUI2.nsh"

Name "Boykisser Avatar"
OutFile "release\Boykisser-Avatar-Setup-1.0.1.exe"
InstallDir "$LOCALAPPDATA\Programs\Boykisser Avatar"
InstallDirRegKey HKCU "Software\Boykisser Avatar" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma

!define MUI_ABORTWARNING
!define MUI_ICON "build\icon.ico"
!define MUI_UNICON "build\icon.ico"

!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\BoykisserAvatar.exe"
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetOutPath "$INSTDIR"
    File /r "dist\*"

    CreateDirectory "$SMPROGRAMS\Boykisser Avatar"
    CreateShortcut "$SMPROGRAMS\Boykisser Avatar\Boykisser Avatar.lnk" "$INSTDIR\BoykisserAvatar.exe"
    CreateShortcut "$DESKTOP\Boykisser Avatar.lnk" "$INSTDIR\BoykisserAvatar.exe"

    WriteUninstaller "$INSTDIR\Uninstall.exe"
    WriteRegStr HKCU "Software\Boykisser Avatar" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Boykisser Avatar" "DisplayName" "Boykisser Avatar"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Boykisser Avatar" "DisplayVersion" "1.0.1"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Boykisser Avatar" "Publisher" "realmxrza"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Boykisser Avatar" "DisplayIcon" "$INSTDIR\BoykisserAvatar.exe"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Boykisser Avatar" "UninstallString" "$INSTDIR\Uninstall.exe"
SectionEnd

Section "Uninstall"
    Delete "$DESKTOP\Boykisser Avatar.lnk"
    Delete "$SMPROGRAMS\Boykisser Avatar\Boykisser Avatar.lnk"
    RMDir "$SMPROGRAMS\Boykisser Avatar"
    RMDir /r "$INSTDIR"
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\Boykisser Avatar"
    DeleteRegKey HKCU "Software\Boykisser Avatar"
SectionEnd
