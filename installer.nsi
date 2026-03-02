Name "TLEscope"
OutFile "dist\TLEscope-Installer.exe"
InstallDir "$PROGRAMFILES\TLEscope"

# installer icons
Icon "dist\TLEscope-Windows\logo.ico"
UninstallIcon "dist\TLEscope-Windows\logo.ico"

# default UI pages
Page Directory
Page InstFiles

Section "Install"
    # install read-only binaries to Program Files
    SetOutPath "$INSTDIR"
    File "dist\TLEscope-Windows\TLEscope.exe"
    File "dist\TLEscope-Windows\*.dll"
    WriteUninstaller "$INSTDIR\uninstall.exe"
    
    # install assets and config to AppData
    # setting the OutPath sets the working directory for shortcuts created below
    SetOutPath "$APPDATA\TLEscope"
    File /r "dist\TLEscope-Windows\themes"
    File "dist\TLEscope-Windows\logo.png"
    File "dist\TLEscope-Windows\logo.ico"
    File /nonfatal "dist\TLEscope-Windows\settings.json"
    File /nonfatal "dist\TLEscope-Windows\data.tle"
    File /nonfatal "dist\TLEscope-Windows\persistence.bin"
    
    # create shortcuts
    # target: Program Files executable.
    CreateShortcut "$DESKTOP\TLEscope.lnk" "$INSTDIR\TLEscope.exe" "" "$APPDATA\TLEscope\logo.ico"
    CreateDirectory "$SMPROGRAMS\TLEscope"
    CreateShortcut "$SMPROGRAMS\TLEscope\TLEscope.lnk" "$INSTDIR\TLEscope.exe" "" "$APPDATA\TLEscope\logo.ico"
SectionEnd

Section "Uninstall"
    # remove Program Files binaries
    RMDir /r "$INSTDIR"
    
    # remove AppData configuration and themes
    RMDir /r "$APPDATA\TLEscope"
    
    # remove shortcuts
    Delete "$DESKTOP\TLEscope.lnk"
    RMDir /r "$SMPROGRAMS\TLEscope"
SectionEnd