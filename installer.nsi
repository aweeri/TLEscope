Unicode true

!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

Name "TLEscope"
OutFile "dist\TLEscope-Installer.exe"

# version metadata (injected by the Makefile via -DVERSION_NUM / -DVERSION_STR)
!ifndef VERSION_NUM
!define VERSION_NUM "0.0.0.0"
!endif
!ifndef VERSION_STR
!define VERSION_STR "0.0.0.0"
!endif

VIProductVersion "${VERSION_NUM}"
VIAddVersionKey "ProductName" "TLEscope"
VIAddVersionKey "CompanyName" "TLEscope contributors"
VIAddVersionKey "FileDescription" "TLEscope satellite tracking installer"
VIAddVersionKey "FileVersion" "${VERSION_NUM}"
VIAddVersionKey "ProductVersion" "${VERSION_STR}"
VIAddVersionKey "LegalCopyright" "Copyright (c) TLEscope contributors"

# per-user install so the folder stays writable: config is written next to the exe
InstallDir "$LOCALAPPDATA\Programs\TLEscope"
InstallDirRegKey HKCU "Software\TLEscope" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "TLEscope"

!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\TLEscope"

# installer icons: build/logo.ico is generated at build time by
# scripts/gen_icon.cpp from the hardcoded logo code; no binary icon is committed.
!define MUI_ICON "build\logo.ico"
!define MUI_UNICON "build\logo.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\TLEscope.exe"
!define MUI_FINISHPAGE_RUN_TEXT "Run TLEscope"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "LICENSE"
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

Section "Install"
    SetShellVarContext current
    SetOutPath "$INSTDIR"

    # themes must sit next to the exe, they are loaded from the working directory
    File "dist\TLEscope-Win-Portable\TLEscope.exe"
    File "dist\TLEscope-Win-Portable\*.dll"
    File /r "dist\TLEscope-Win-Portable\themes"

    WriteUninstaller "$INSTDIR\uninstall.exe"

    # remember the folder for upgrades, and register an Add/Remove Programs entry
    WriteRegStr HKCU "Software\TLEscope" "InstallDir" "$INSTDIR"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayName" "TLEscope"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayVersion" "${VERSION_STR}"
    WriteRegStr HKCU "${UNINST_KEY}" "Publisher" "TLEscope contributors"
    WriteRegStr HKCU "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\TLEscope.exe"
    WriteRegStr HKCU "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr HKCU "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
    WriteRegStr HKCU "${UNINST_KEY}" "QuietUninstallString" '"$INSTDIR\uninstall.exe" /S'
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoModify" 1
    WriteRegDWORD HKCU "${UNINST_KEY}" "NoRepair" 1
    ${GetSize} "$INSTDIR" "/S=0K" $0 $1 $2
    WriteRegDWORD HKCU "${UNINST_KEY}" "EstimatedSize" "$0"

    # last argument is the working directory: must stay the install folder
    CreateDirectory "$SMPROGRAMS\TLEscope"
    CreateShortcut "$SMPROGRAMS\TLEscope\TLEscope.lnk" "$INSTDIR\TLEscope.exe" "" "$INSTDIR\TLEscope.exe" 0 SW_SHOWNORMAL "" "$INSTDIR"
    CreateShortcut "$DESKTOP\TLEscope.lnk" "$INSTDIR\TLEscope.exe" "" "$INSTDIR\TLEscope.exe" 0 SW_SHOWNORMAL "" "$INSTDIR"
SectionEnd

Section "Uninstall"
    SetShellVarContext current

    ${IfNot} ${Silent}
        MessageBox MB_YESNO|MB_ICONQUESTION "Remove TLEscope and its settings?" IDYES uninst_go
        Abort
        uninst_go:
    ${EndIf}

    Delete "$DESKTOP\TLEscope.lnk"
    RMDir /r "$SMPROGRAMS\TLEscope"

    DeleteRegKey HKCU "${UNINST_KEY}"
    DeleteRegKey HKCU "Software\TLEscope"

    # settings live next to the exe, so this removes them too
    RMDir /r "$INSTDIR"
SectionEnd
