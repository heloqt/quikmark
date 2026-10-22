Unicode true

!include "MUI2.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "1.0.0"
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
!ifndef USAGE_FILE
  !error "USAGE_FILE is required"
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
!define MUI_FINISHPAGE_RUN "$INSTDIR\liteMark.exe"
!define MUI_FINISHPAGE_RUN_TEXT "运行 liteMark"
!define MUI_FINISHPAGE_RUN_NOTCHECKED
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"

Section "-清理旧版本关联"
  ReadRegDWORD $2 HKCU "Software\liteMark" "DesktopShortcut"
  StrCmp $2 1 0 cleanup_desktop_done
  Delete "$DESKTOP\liteMark.lnk"
cleanup_desktop_done:
  DeleteRegValue HKCU "Software\liteMark" "DesktopShortcut"

  ReadRegStr $0 HKCU "Software\Classes\.md" ""
  StrCmp $0 "liteMark.Markdown" 0 cleanup_md_done
  ReadRegStr $1 HKCU "Software\liteMark\Backup" ".md"
  StrCmp $1 "" 0 cleanup_md_restore
  DeleteRegValue HKCU "Software\Classes\.md" ""
  Goto cleanup_md_done
cleanup_md_restore:
  WriteRegStr HKCU "Software\Classes\.md" "" "$1"
cleanup_md_done:

  ReadRegStr $0 HKCU "Software\Classes\.markdown" ""
  StrCmp $0 "liteMark.Markdown" 0 cleanup_markdown_done
  ReadRegStr $1 HKCU "Software\liteMark\Backup" ".markdown"
  StrCmp $1 "" 0 cleanup_markdown_restore
  DeleteRegValue HKCU "Software\Classes\.markdown" ""
  Goto cleanup_markdown_done
cleanup_markdown_restore:
  WriteRegStr HKCU "Software\Classes\.markdown" "" "$1"
cleanup_markdown_done:

  DeleteRegKey HKCU "Software\Classes\liteMark.Markdown"
  DeleteRegKey HKCU "Software\Classes\Applications\liteMark.exe"
  DeleteRegKey HKCU "Software\liteMark\Backup"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "liteMark 程序" SEC_MAIN
  SetOutPath "$INSTDIR"
  File "/oname=liteMark.exe" "${SOURCE_EXE}"
  File "${LICENSE_FILE}"
  File "/oname=使用说明.md" "${USAGE_FILE}"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "Software\liteMark" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\liteMark" "Version" "${APP_VERSION}"

  CreateDirectory "$SMPROGRAMS\liteMark"
  CreateShortcut "$SMPROGRAMS\liteMark\liteMark.lnk" "$INSTDIR\liteMark.exe"
  CreateShortcut "$SMPROGRAMS\liteMark\使用说明.lnk" "$INSTDIR\使用说明.md"
  CreateShortcut "$SMPROGRAMS\liteMark\Uninstall.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "DisplayName" "liteMark"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "Publisher" "liteMark"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark" "NoRepair" 1
SectionEnd

Section "桌面快捷方式" SEC_DESKTOP
  CreateShortcut "$DESKTOP\liteMark.lnk" "$INSTDIR\liteMark.exe"
  WriteRegDWORD HKCU "Software\liteMark" "DesktopShortcut" 1
SectionEnd

Section "关联 Markdown 文件" SEC_ASSOC
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
  WriteRegDWORD HKCU "Software\liteMark" "FileAssociations" 1
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_MAIN} "安装 liteMark 程序并创建开始菜单快捷方式。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} "在桌面创建 liteMark 快捷方式。默认启用。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_ASSOC} "使用 liteMark 打开 .md 和 .markdown 文件；卸载时恢复原关联。"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  SectionSetFlags ${SEC_MAIN} ${SF_SELECTED}|${SF_RO}
FunctionEnd

Section "Uninstall"
  Delete "$INSTDIR\liteMark.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\使用说明.md"
  Delete "$INSTDIR\uninstall.exe"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\liteMark\liteMark.lnk"
  Delete "$SMPROGRAMS\liteMark\使用说明.lnk"
  Delete "$SMPROGRAMS\liteMark\Uninstall.lnk"
  RMDir "$SMPROGRAMS\liteMark"

  ReadRegDWORD $0 HKCU "Software\liteMark" "DesktopShortcut"
  StrCmp $0 1 0 desktop_shortcut_done
  Delete "$DESKTOP\liteMark.lnk"
desktop_shortcut_done:

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
