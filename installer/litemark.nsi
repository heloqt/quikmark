Unicode true

!include "MUI2.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "0.1.0"
!endif
!ifndef SOURCE_EXE
  !error "SOURCE_EXE is required"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "liteMark-setup.exe"
!endif
!ifndef LICENSE_FILE
  !define LICENSE_FILE "LICENSE"
!endif

Name "liteMark"
Caption "liteMark ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\liteMark"
InstallDirRegKey HKCU "Software\liteMark" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

!define MUI_ABORTWARNING
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

Section "liteMark" SEC_MAIN
  SetOutPath "$INSTDIR"
  File "/oname=liteMark.exe" "${SOURCE_EXE}"
  File "${LICENSE_FILE}"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "Software\liteMark" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\liteMark" "Version" "${APP_VERSION}"

  ReadRegStr $0 HKCU "Software\Classes\.md" ""
  WriteRegStr HKCU "Software\liteMark\Backup" ".md" "$0"
  ReadRegStr $0 HKCU "Software\Classes\.markdown" ""
  WriteRegStr HKCU "Software\liteMark\Backup" ".markdown" "$0"

  WriteRegStr HKCU "Software\Classes\.md" "" "liteMark.Markdown"
  WriteRegStr HKCU "Software\Classes\.markdown" "" "liteMark.Markdown"
  WriteRegStr HKCU "Software\Classes\liteMark.Markdown" "" "Markdown Document"
  WriteRegStr HKCU "Software\Classes\liteMark.Markdown\DefaultIcon" "" "$INSTDIR\liteMark.exe,0"
  WriteRegStr HKCU "Software\Classes\liteMark.Markdown\shell\open\command" "" '"$INSTDIR\liteMark.exe" "%1"'

  WriteRegStr HKCU "Software\Classes\Applications\liteMark.exe" "FriendlyAppName" "liteMark"
  WriteRegStr HKCU "Software\Classes\Applications\liteMark.exe\shell\open\command" "" '"$INSTDIR\liteMark.exe" "%1"'
  WriteRegStr HKCU "Software\Classes\Applications\liteMark.exe\SupportedTypes" ".md" ""
  WriteRegStr HKCU "Software\Classes\Applications\liteMark.exe\SupportedTypes" ".markdown" ""

  CreateDirectory "$SMPROGRAMS\liteMark"
  CreateShortcut "$SMPROGRAMS\liteMark\liteMark.lnk" "$INSTDIR\liteMark.exe"
  CreateShortcut "$SMPROGRAMS\liteMark\Uninstall.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "DisplayName" "liteMark"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "Publisher" "liteMark"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "NoRepair" 1

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "Uninstall"
  Delete "$INSTDIR\liteMark.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\liteMark\liteMark.lnk"
  Delete "$SMPROGRAMS\liteMark\Uninstall.lnk"
  RMDir "$SMPROGRAMS\liteMark"

  ReadRegStr $0 HKCU "Software\Classes\.md" ""
  StrCmp $0 "liteMark.Markdown" 0 md_done
  ReadRegStr $1 HKCU "Software\liteMark\Backup" ".md"
  StrCmp $1 "" 0 md_restore
  DeleteRegValue HKCU "Software\Classes\.md" ""
  Goto md_done
md_restore:
  WriteRegStr HKCU "Software\Classes\.md" "" "$1"
md_done:

  ReadRegStr $0 HKCU "Software\Classes\.markdown" ""
  StrCmp $0 "liteMark.Markdown" 0 markdown_done
  ReadRegStr $1 HKCU "Software\liteMark\Backup" ".markdown"
  StrCmp $1 "" 0 markdown_restore
  DeleteRegValue HKCU "Software\Classes\.markdown" ""
  Goto markdown_done
markdown_restore:
  WriteRegStr HKCU "Software\Classes\.markdown" "" "$1"
markdown_done:

  DeleteRegKey HKCU "Software\Classes\liteMark.Markdown"
  DeleteRegKey HKCU "Software\Classes\Applications\liteMark.exe"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark"
  DeleteRegKey HKCU "Software\liteMark"

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
