;For testing the script
;SetCompress Off

;Enables Unicode installer to clear ANSI deprecation message
;Unicode true
;Version, Icon and URL
;!define SUFFIX "0.8.0-win64"
!define URL "https://naev.org"
!define MUI_ICON "logo.ico"
;!define MUI_UNICON "logo.ico"

;Miscellaneous defines
!define MULTIUSER_EXECUTIONLEVEL Highest
!define MULTIUSER_MUI
!define MULTIUSER_INSTALLMODE_COMMANDLINE
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_KEY "Software\Naev"
!define MULTIUSER_INSTALLMODE_DEFAULT_REGISTRY_VALUENAME ""
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_KEY "Software\Naev"
!define MULTIUSER_INSTALLMODE_INSTDIR_REGISTRY_VALUENAME ""
!define MULTIUSER_INSTALLMODE_INSTDIR "Naev"

;Needed include files
!include "MultiUser.nsh"
!include "MUI2.nsh"
!include "nsDialogs.nsh"
!include "LogicLib.nsh"
;--------------------------------
;General

;Name and file
Name "Naev"
OutFile "naev-${SUFFIX}.exe"

;--------------------------------
;Variables

Var StartMenuFolder

;--------------------------------
;Interface Settings

;!define MUI_WELCOMEFINISHPAGE_BITMAP - A 164x314 px bitmap could go here.
!define MUI_ABORTWARNING

;--------------------------------
;Language Selection Dialog Settings

;Remember the installer language
!define MUI_LANGDLL_REGISTRY_ROOT "SHCTX"
!define MUI_LANGDLL_REGISTRY_KEY "Software\Naev"
!define MUI_LANGDLL_REGISTRY_VALUENAME "Installer Language"

;--------------------------------
;Pages

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "legal\gpl-3.0.txt"
!insertmacro MULTIUSER_PAGE_INSTALLMODE
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY

;Start Menu Folder Page Configuration
!define MUI_STARTMENUPAGE_REGISTRY_ROOT "SHCTX"
!define MUI_STARTMENUPAGE_REGISTRY_KEY "Software\Naev"
!define MUI_STARTMENUPAGE_REGISTRY_VALUENAME "Start Menu Folder"
!define MUI_STARTMENUPAGE_DEFAULTFOLDER "Naev"

!insertmacro MUI_PAGE_STARTMENU Application $StartMenuFolder

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN $INSTDIR\naev-${SUFFIX}.exe
!define MUI_FINISHPAGE_RUN_PARAMETERS
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
;Languages

!insertmacro MUI_LANGUAGE "English" ;first language is the default language

;--------------------------------
;Installer Sections

Var PortID

Section "Naev Engine and Data" BinarySection

   SectionIn RO

   SetOutPath "$INSTDIR"
   File /r bin\*
   File logo.ico

   IntOp $PortID $PortID & ${SF_SELECTED}

   ${If} $PortID = 0 ;this means that the section 'portable' was not selected
   ;Store installation folder
   WriteRegStr SHCTX "Software\Naev" "" $INSTDIR

   ;Create uninstaller
   WriteUninstaller "$INSTDIR\Uninstall.exe"

   ;Add uninstall information
   WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "DisplayName" "Naev"
   WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "DisplayIcon" "$\"$INSTDIR\naev-${SUFFIX}.exe$\""
   WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "UninstallString" "$\"$INSTDIR\Uninstall.exe$\""
   WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "QuietUninstallString" "$\"$INSTDIR\Uninstall.exe$\" /S"
   WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "URLInfoAbout" "${URL}"
   WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "DisplayVersion" "${SUFFIX}"
   WriteRegStr SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "Publisher" "Naev Team"
   WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "NoModify" 1
   WriteRegDWORD SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev" "NoRepair" 1

   !insertmacro MUI_STARTMENU_WRITE_BEGIN Application

      ;Create shortcuts
      CreateDirectory "$SMPROGRAMS\$StartMenuFolder"
      CreateShortCut "$SMPROGRAMS\$StartMenuFolder\Naev.lnk" "$INSTDIR\naev-${SUFFIX}.exe"
      CreateShortCut "$DESKTOP\Naev.lnk" "$INSTDIR\naev-${SUFFIX}.exe"

   !insertmacro MUI_STARTMENU_WRITE_END
   ${Else}
   File "datapath.lua"
   ${EndUnless}

SectionEnd

Section /o "Do a portable install" Portable
SectionEnd

;--------------------------------
;Installer Functions

Function .onInit

   !insertmacro MULTIUSER_INIT
   !insertmacro MUI_LANGDLL_DISPLAY

   ReadRegStr $INSTDIR SHCTX "Software\Naev" ""
   ${Unless} ${Errors}
      ;If we get here we're already installed
     MessageBox MB_YESNO|MB_ICONEXCLAMATION "Naev is already installed! Would you like to remove the old install first?" IDNO skip
     ExecWait '"$INSTDIR\Uninstall.exe"' $0
     ${Unless} $0 = 0 ;note: = not ==
        MessageBox MB_OK|MB_ICONSTOP "The uninstall failed!"
     ${EndUnless}
     skip:
   ${EndUnless}

FunctionEnd

;TODO: someone please email me with a better way to do this
;--Sudarshan S
Function .onSelChange
   SectionGetFlags ${Portable} $PortID
FunctionEnd

;--------------------------------
;Descriptions

   ;Assign descriptions to sections
   !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
      !insertmacro MUI_DESCRIPTION_TEXT ${BinarySection} "Naev engine and data."
     !insertmacro MUI_DESCRIPTION_TEXT ${Portable} "Perform a portable install. No uninstaller or registry entries are created and you can run off a pen drive"
   !insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
;Uninstaller Section

Section "Uninstall"

   Delete "$INSTDIR\Uninstall.exe"
   Delete "$INSTDIR\*"
   RMDir /r "$INSTDIR\dat"
   RMDir "$INSTDIR"

   !insertmacro MUI_STARTMENU_GETFOLDER Application $StartMenuFolder

   Delete "$SMPROGRAMS\$StartMenuFolder\Naev.lnk"
   RMDir "$SMPROGRAMS\$StartMenuFolder"
   Delete "$DESKTOP\Naev.lnk"

   DeleteRegKey SHCTX "Software\Microsoft\Windows\CurrentVersion\Uninstall\Naev"
   DeleteRegKey SHCTX "Software\Naev"

SectionEnd

;--------------------------------
;Uninstaller Functions

Function un.onInit

   !insertmacro MULTIUSER_UNINIT
   !insertmacro MUI_UNGETLANGUAGE

FunctionEnd
