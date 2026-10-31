Unicode true

!include "MUI2.nsh"

!ifndef APP_VERSION
  !define APP_VERSION "1.2.2"
!endif
!ifndef SOURCE_EXE
  !error "SOURCE_EXE is required"
!endif
!ifndef OUTPUT_FILE
  !define OUTPUT_FILE "quikmark-setup.exe"
!endif
!ifndef LICENSE_FILE
  !define LICENSE_FILE "LICENSE"
!endif
!ifndef USAGE_FILE
  !error "USAGE_FILE is required"
!endif
!ifndef USAGE_EN_FILE
  !error "USAGE_EN_FILE is required"
!endif
!ifndef LOCALE_DIR
  !error "LOCALE_DIR is required"
!endif
!ifndef NOTICE_FILE
  !error "NOTICE_FILE is required"
!endif

Name "QuikMark"
Caption "QuikMark ${APP_VERSION}"
OutFile "${OUTPUT_FILE}"
InstallDir "$LOCALAPPDATA\Programs\QuikMark"
InstallDirRegKey HKCU "Software\QuikMark" "InstallDir"
RequestExecutionLevel user
SetCompressor /SOLID lzma
ShowInstDetails show
ShowUninstDetails show

!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\quikmark.exe"
!define MUI_FINISHPAGE_RUN_TEXT "运行 QuikMark"
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
  Delete "$SMPROGRAMS\liteMark\liteMark.lnk"
  Delete "$SMPROGRAMS\liteMark\使用说明.lnk"
  Delete "$SMPROGRAMS\liteMark\Uninstall.lnk"
  RMDir "$SMPROGRAMS\liteMark"

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
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\liteMark"
  DeleteRegKey HKCU "Software\liteMark\Backup"
  Delete "$LOCALAPPDATA\Programs\liteMark\liteMark.exe"
  Delete "$LOCALAPPDATA\Programs\liteMark\LICENSE"
  Delete "$LOCALAPPDATA\Programs\liteMark\THIRD_PARTY_NOTICES.md"
  Delete "$LOCALAPPDATA\Programs\liteMark\使用说明.md"
  Delete "$LOCALAPPDATA\Programs\liteMark\uninstall.exe"
  RMDir "$LOCALAPPDATA\Programs\liteMark"
  DeleteRegKey HKCU "Software\liteMark"
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

Section "QuikMark 程序" SEC_MAIN
  SetOutPath "$INSTDIR"
  File "/oname=quikmark.exe" "${SOURCE_EXE}"
  File "${LICENSE_FILE}"
  File "/oname=THIRD_PARTY_NOTICES.md" "${NOTICE_FILE}"
  File "/oname=使用说明.md" "${USAGE_FILE}"
  File "/oname=Usage.md" "${USAGE_EN_FILE}"
  SetOutPath "$INSTDIR\locale"
  File /r "${LOCALE_DIR}\*.*"
  SetOutPath "$INSTDIR"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "Software\QuikMark" "InstallDir" "$INSTDIR"
  WriteRegStr HKCU "Software\QuikMark" "Version" "${APP_VERSION}"

  CreateDirectory "$SMPROGRAMS\QuikMark"
  CreateShortcut "$SMPROGRAMS\QuikMark\QuikMark.lnk" "$INSTDIR\quikmark.exe"
  CreateShortcut "$SMPROGRAMS\QuikMark\使用说明.lnk" "$INSTDIR\quikmark.exe" '"$INSTDIR\使用说明.md"'
  CreateShortcut "$SMPROGRAMS\QuikMark\Uninstall.lnk" "$INSTDIR\uninstall.exe"

  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QuikMark" "DisplayName" "QuikMark"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QuikMark" "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QuikMark" "Publisher" "QuikMark"
  WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QuikMark" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QuikMark" "NoModify" 1
  WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QuikMark" "NoRepair" 1
SectionEnd

Section "桌面快捷方式" SEC_DESKTOP
  CreateShortcut "$DESKTOP\QuikMark.lnk" "$INSTDIR\quikmark.exe"
  WriteRegDWORD HKCU "Software\QuikMark" "DesktopShortcut" 1
SectionEnd

Section "关联 Markdown 文件" SEC_ASSOC
  ReadRegStr $0 HKCU "Software\Classes\.md" ""
  WriteRegStr HKCU "Software\QuikMark\Backup" ".md" "$0"
  ReadRegStr $0 HKCU "Software\Classes\.markdown" ""
  WriteRegStr HKCU "Software\QuikMark\Backup" ".markdown" "$0"

  WriteRegStr HKCU "Software\Classes\.md" "" "QuikMark.Markdown"
  WriteRegStr HKCU "Software\Classes\.markdown" "" "QuikMark.Markdown"
  WriteRegStr HKCU "Software\Classes\QuikMark.Markdown" "" "Markdown Document"
  WriteRegStr HKCU "Software\Classes\QuikMark.Markdown\DefaultIcon" "" "$INSTDIR\quikmark.exe,0"
  WriteRegStr HKCU "Software\Classes\QuikMark.Markdown\shell\open\command" "" '"$INSTDIR\quikmark.exe" "%1"'

  WriteRegStr HKCU "Software\Classes\Applications\quikmark.exe" "FriendlyAppName" "QuikMark"
  WriteRegStr HKCU "Software\Classes\Applications\quikmark.exe\shell\open\command" "" '"$INSTDIR\quikmark.exe" "%1"'
  WriteRegStr HKCU "Software\Classes\Applications\quikmark.exe\SupportedTypes" ".md" ""
  WriteRegStr HKCU "Software\Classes\Applications\quikmark.exe\SupportedTypes" ".markdown" ""
  WriteRegDWORD HKCU "Software\QuikMark" "FileAssociations" 1
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_MAIN} "安装 QuikMark 程序并创建开始菜单快捷方式。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_DESKTOP} "在桌面创建 QuikMark 快捷方式。默认启用。"
  !insertmacro MUI_DESCRIPTION_TEXT ${SEC_ASSOC} "使用 QuikMark 打开 .md 和 .markdown 文件；卸载时恢复原关联。"
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Function .onInit
  SectionSetFlags ${SEC_MAIN} ${SF_SELECTED}|${SF_RO}
FunctionEnd

Section "Uninstall"
  Delete "$INSTDIR\quikmark.exe"
  Delete "$INSTDIR\LICENSE"
  Delete "$INSTDIR\THIRD_PARTY_NOTICES.md"
  Delete "$INSTDIR\使用说明.md"
  Delete "$INSTDIR\Usage.md"
  Delete "$INSTDIR\uninstall.exe"
  RMDir /r "$INSTDIR\locale"
  RMDir "$INSTDIR"

  Delete "$SMPROGRAMS\QuikMark\QuikMark.lnk"
  Delete "$SMPROGRAMS\QuikMark\使用说明.lnk"
  Delete "$SMPROGRAMS\QuikMark\Uninstall.lnk"
  RMDir "$SMPROGRAMS\QuikMark"

  ReadRegDWORD $0 HKCU "Software\QuikMark" "DesktopShortcut"
  StrCmp $0 1 0 desktop_shortcut_done
  Delete "$DESKTOP\QuikMark.lnk"
desktop_shortcut_done:

  ReadRegStr $0 HKCU "Software\Classes\.md" ""
  StrCmp $0 "QuikMark.Markdown" 0 md_done
  ReadRegStr $1 HKCU "Software\QuikMark\Backup" ".md"
  StrCmp $1 "" 0 md_restore
  DeleteRegValue HKCU "Software\Classes\.md" ""
  Goto md_done
md_restore:
  WriteRegStr HKCU "Software\Classes\.md" "" "$1"
md_done:

  ReadRegStr $0 HKCU "Software\Classes\.markdown" ""
  StrCmp $0 "QuikMark.Markdown" 0 markdown_done
  ReadRegStr $1 HKCU "Software\QuikMark\Backup" ".markdown"
  StrCmp $1 "" 0 markdown_restore
  DeleteRegValue HKCU "Software\Classes\.markdown" ""
  Goto markdown_done
markdown_restore:
  WriteRegStr HKCU "Software\Classes\.markdown" "" "$1"
markdown_done:

  DeleteRegKey HKCU "Software\Classes\QuikMark.Markdown"
  DeleteRegKey HKCU "Software\Classes\Applications\quikmark.exe"
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\QuikMark"
  DeleteRegKey HKCU "Software\QuikMark"

  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0)'
SectionEnd
