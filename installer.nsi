Name "TLEscope"
OutFile "dist\TLEscope-Installer.exe"
InstallDir "$PROGRAMFILES\TLEscope"

# version metadata (injected by the Makefile via -DVERSION_NUM / -DVERSION_STR)
!ifndef VERSION_NUM
!define VERSION_NUM "0.0.0.0"
!endif
!ifndef VERSION_STR
!define VERSION_STR "0.0.0.0"
!endif

VIProductVersion "${VERSION_NUM}"
VIAddVersionKey "ProductName" "TLEscope"
VIAddVersionKey "CompanyName" "TLEscope"
VIAddVersionKey "FileDescription" "TLEscope satellite tracking installer"
VIAddVersionKey "FileVersion" "${VERSION_NUM}"
VIAddVersionKey "ProductVersion" "${VERSION_STR}"
VIAddVersionKey "LegalCopyright" "Copyright (c) TLEscope contributors"

# installer icons
Icon "dist\TLEscope-Win-Portable\logo.ico"
UninstallIcon "dist\TLEscope-Win-Portable\logo.ico"

# default UI pages
Page Directory
Page InstFiles

Section "Install"
	# install read-only binaries to Program Files
	SetOutPath "$INSTDIR"
	File "dist\TLEscope-Win-Portable\TLEscope.exe"
	File "dist\TLEscope-Win-Portable\*.dll"
	WriteUninstaller "$INSTDIR\uninstall.exe"
	
	# install assets and config to AppData
	# setting the OutPath sets the working directory for shortcuts created below
	SetOutPath "$APPDATA\TLEscope"
	File /r "dist\TLEscope-Win-Portable\themes"
	File "dist\TLEscope-Win-Portable\logo.png"
	File "dist\TLEscope-Win-Portable\logo.ico"
	File /nonfatal "dist\TLEscope-Win-Portable\settings.json"
	File /nonfatal "dist\TLEscope-Win-Portable\data.tle"
	File /nonfatal "dist\TLEscope-Win-Portable\persistence.bin"
	
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