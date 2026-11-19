# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Required Plugins:
# AppAssocReg
# CertCheck
# InetBgDL
# ShellLink
# UAC

; Set verbosity to 3 (e.g. no script) to lessen the noise in the build logs
!verbose 3

SetDatablockOptimize on
SetCompress off
CRCCheck on

RequestExecutionLevel user

ManifestSupportedOS all
ManifestDPIAware true

!addplugindir ./

Var CheckboxShortcuts
Var CheckboxInstallMaintSvc
Var CheckboxCleanupProfile

Var FontFamilyName

Var HasRequiredSpaceAvailable
Var IsDownloadFinished
Var DownloadSizeBytes
Var DownloadReset
Var ExistingTopDir
Var SpaceAvailableBytes
Var InitialInstallDir
Var HandleDownload
Var InstallCounterStep
Var InstallTotalSteps
Var ProgressCompleted
Var UsingHighContrastMode
Var DownloadRequestsBlockedByServer

Var ExitCode
Var FirefoxLaunchCode

Var StartDownloadPhaseTickCount
; Since the Intro and Options pages can be displayed multiple times the total
; seconds spent on each of these pages is reported.
Var IntroPhaseSeconds
Var OptionsPhaseSeconds
; The tick count for the last download.
Var StartLastDownloadTickCount
; The number of seconds from the start of the download phase until the first
; bytes are received. This is only recorded for first request so it is possible
; to determine connection issues for the first request.
Var DownloadFirstTransferSeconds
; The last four tick counts are for the end of a phase in the installation page.
Var EndDownloadPhaseTickCount
Var EndPreInstallPhaseTickCount
Var EndInstallPhaseTickCount
Var EndFinishPhaseTickCount

Var InitialInstallRequirementsCode
Var HadExistingProfile
Var ExistingVersion
Var ExistingBuildID
Var DownloadedBytes
Var DownloadRetryCount

; After a failure, did the user choose to open the download page as a fallback?
Var OpenedDownloadPage

Var DownloadServerIP
Var PostSigningData
Var NotificationHelperStopExitCode
Var PreviousInstallDir
Var ProfileCleanupPromptType
Var AppLaunchWaitTickCount
Var TimerHandle

; Set AbortInstallation to "true" to prevent the installation "Pages" from starting, while still allowing
; SendPing to complete.
Var AbortInstallation

!define ARCH_X86 1
!define ARCH_AMD64 2
!define ARCH_AARCH64 3
Var ArchToInstall

; Uncomment the following to prevent pinging the metrics server when testing
; the stub installer
;!define STUB_DEBUG

!define StubURLVersion "v14"

; Successful install exit code
!define ERR_SUCCESS 0

; Default failure exit code. Seeing this in telemetry indicates that the stub
; installer has exited unsuccessfully, but no reason has been specified
!define ERR_UNKNOWN 1

; The user cancelled the installer before the download phase started
!define ERR_USER_CANCELLED_BEFORE_DOWNLOAD 2

/**
 * The following errors prefixed with ERR_DOWNLOAD apply to the download phase.
 */
; The download was cancelled by the user
!define ERR_DOWNLOAD_CANCEL 10

; Too many attempts to download. The maximum attempts is defined in
; DownloadMaxRetries.
!define ERR_DOWNLOAD_TOO_MANY_RETRIES 11

/**
 * The following errors prefixed with ERR_PREINSTALL apply to the pre-install
 * check phase.
 */
; Unable to acquire a file handle to the downloaded file
!define ERR_PREINSTALL_INVALID_HANDLE 20

; The downloaded file's certificate is not trusted by the certificate store.
!define ERR_PREINSTALL_CERT_UNTRUSTED 21

; The downloaded file's certificate attribute values were incorrect.
!define ERR_PREINSTALL_CERT_ATTRIBUTES 22

; The downloaded file's certificate is not trusted by the certificate store and
; certificate attribute values were incorrect.
!define ERR_PREINSTALL_CERT_UNTRUSTED_AND_ATTRIBUTES 23

; Timed out while waiting for the certificate checks to run.
!define ERR_PREINSTALL_CERT_TIMEOUT 24

; System does not meet the minimum hardware requirements
!define ERR_PREINSTALL_SYS_HW_REQ 25

; System does not meet the minimum OS version requirements
!define ERR_PREINSTALL_SYS_OS_REQ 26

; Insufficient storage space is available in the target location
!define ERR_PREINSTALL_SPACE 27

; Target location is not writable
!define ERR_PREINSTALL_NOT_WRITABLE 28

/**
 * The following errors prefixed with ERR_INSTALL apply to the install phase.
 */
; The installation timed out. The installation timeout is defined by the number
; of progress steps defined in InstallTotalSteps and the install timer
; interval defined in InstallIntervalMS
!define ERR_INSTALL_TIMEOUT 30

; Maximum times to retry the download before displaying an error
!define DownloadMaxRetries 9

; Interval before retrying to download. 3 seconds is used along with 10
; attempted downloads (the first attempt along with 9 retries) to give a
; minimum of 30 seconds or retrying before giving up.
!define DownloadRetryIntervalMS 3000

; Interval for the download timer
!define DownloadIntervalMS 200

; Timeout for the certificate check
!define PreinstallCertCheckMaxWaitSec 30

; Interval for the install timer
!define InstallIntervalMS 100

; Number of steps for the install progress.
; This might not be enough when installing on a slow network drive so it will
; fallback to downloading the full installer if it reaches this number.

; Approximately 240 seconds with a 100 millisecond timer.
!define InstallCleanTotalSteps 2400

; Approximately 255 seconds with a 100 millisecond timer.
!define InstallPaveOverTotalSteps 2550

; Blurb duty cycle
!define BlurbDisplayMS 19500
!define BlurbBlankMS 500

; Interval between checks for the application window and progress bar updates.
!define AppLaunchWaitIntervalMS 100

; Total time to wait for the application to start before just exiting.
!define AppLaunchWaitTimeoutMS 10000

; Maximum value of the download/install/launch progress bar, and the end values
; for each individual stage.
!define PROGRESS_BAR_TOTAL_STEPS 500
!define PROGRESS_BAR_DOWNLOAD_END_STEP 300
!define PROGRESS_BAR_INSTALL_END_STEP 475
!define PROGRESS_BAR_APP_LAUNCH_END_STEP 500

; Amount of physical memory required for the 64-bit build to be selected (2 GB).
; Machines with this or less RAM get the 32-bit build, even with a 64-bit OS.
!define RAM_NEEDED_FOR_64BIT 0x80000000

; Attempt to elevate Standard Users in addition to users that
; are a member of the Administrators group.
!define NONADMIN_ELEVATE

!define CONFIG_INI "config.ini"
!define PARTNER_INI "$EXEDIR\partner.ini"

!ifndef FILE_SHARE_READ
  !define FILE_SHARE_READ 1
!endif
!ifndef GENERIC_READ
  !define GENERIC_READ 0x80000000
!endif
!ifndef OPEN_EXISTING
  !define OPEN_EXISTING 3
!endif
!ifndef INVALID_HANDLE_VALUE
  !define INVALID_HANDLE_VALUE -1
!endif

!define DefaultInstDir32bit "$PROGRAMFILES32\${BrandFullName}"
!define DefaultInstDir64bit "$PROGRAMFILES64\${BrandFullName}"

!insertmacro GetParameters
!insertmacro GetOptions
!insertmacro LineFind
!insertmacro StrFilter

!include "locales.nsi"
!include "branding.nsi"

!include "defines.nsi"

; Must be included after defines.nsi
!include "locale-fonts.nsh"

; The OFFICIAL define is a workaround to support different urls for Release and
; Beta since they share the same branding when building with other branches that
; set the update channel to beta.
!ifdef OFFICIAL
!ifdef BETA_UPDATE_CHANNEL
!undef URLStubDownloadX86
!undef URLStubDownloadAMD64
!undef URLStubDownloadAArch64
!define URLStubDownloadX86 "https://download.mozilla.org/?os=win&lang=${AB_CD}&product=firefox-beta-latest"
!define URLStubDownloadAMD64 "https://download.mozilla.org/?os=win64&lang=${AB_CD}&product=firefox-beta-latest"
!define URLStubDownloadAArch64 "https://download.mozilla.org/?os=win64-aarch64&lang=${AB_CD}&product=firefox-beta-latest"
!undef URLManualDownload
!define URLManualDownload "https://www.mozilla.org/${AB_CD}/firefox/installer-help/?channel=beta&installer_lang=${AB_CD}"
!undef Channel
!define Channel "beta"
!endif
!endif

!include "common.nsh"


!insertmacro CopyPostSigningData
!insertmacro ElevateUAC
!insertmacro GetLongPath
!insertmacro GetPathFromString
!insertmacro GetParent
!insertmacro GetSingleInstallPath
!insertmacro InitHashAppModelId
!insertmacro IsUserAdmin
!insertmacro RemovePrecompleteEntries
!insertmacro SetBrandNameVars
!insertmacro ITBL3Create
!insertmacro UnloadUAC

!define TELEMETRY_STUB_INSTALLER
!include telemetry.nsh

VIAddVersionKey "FileDescription" "${BrandShortName} Installer"
VIAddVersionKey "OriginalFilename" "setup-stub.exe"

Name "$BrandFullName"

XPStyle on
BrandingText " "
ChangeUI IDD_INST "nsisui.exe"

!ifdef ${AB_CD}_rtl
  LoadLanguageFile "locale-rtl.nlf"
!else
  LoadLanguageFile "locale.nlf"
!endif

!include "nsisstrings.nlf"

Caption "$(INSTALLER_WIN_CAPTION)"

!macro _RegisterAllCustomFunctions
  GetFunctionAddress $0 getUIString
  WebBrowser::RegisterCustomFunction $0 "getUIString"

  GetFunctionAddress $0 getTextDirection
  WebBrowser::RegisterCustomFunction $0 "getTextDirection"

  GetFunctionAddress $0 getFontName
  WebBrowser::RegisterCustomFunction $0 "getFontName"

  GetFunctionAddress $0 getIsHighContrast
  WebBrowser::RegisterCustomFunction $0 "getIsHighContrast"

  GetFunctionAddress $0 gotoInstallPage
  WebBrowser::RegisterCustomFunction $0 "gotoInstallPage"

  GetFunctionAddress $0 getProgressBarPercent
  WebBrowser::RegisterCustomFunction $0 "getProgressBarPercent"
!macroend
!define RegisterAllCustomFunctions "!insertmacro _RegisterAllCustomFunctions"

!macro _StartTimer _INTERVAL_MS _FUNCTION_NAME
  Push $0
  GetFunctionAddress $0 ${_FUNCTION_NAME}
  WebBrowser::CreateTimer $0 ${_INTERVAL_MS}
  Pop $TimerHandle
  Pop $0
!macroend
!define StartTimer "!insertmacro _StartTimer"

Function gotoInstallPage
  Pop $0
  StrCpy $CheckboxCleanupProfile $0

  StrCpy $R9 1
  Call RelativeGotoPage
  Push $0
FunctionEnd

Function getProgressBarPercent
  ; Custom functions always get one parameter, which we don't use here.
  ; But we will use $0 as a scratch accumulator register.
  Pop $0
  ; This math is getting the progess bar completion fraction and converting it
  ; to a percentage, but we implement that with the operations in the reverse
  ; of the intuitive order so that our integer math doesn't truncate to zero.
  IntOp $0 $ProgressCompleted * 100
  IntOp $0 $0 / ${PROGRESS_BAR_TOTAL_STEPS}
  Push $0
FunctionEnd

Function getTextDirection
  Pop $0
  !ifdef ${AB_CD}_rtl
    Push "rtl"
  !else
    Push "ltr"
  !endif
FunctionEnd

Function getFontName
  Pop $0
  Push $FontFamilyName
FunctionEnd

Function getIsHighContrast
  Pop $0
  Push $UsingHighContrastMode
FunctionEnd

Function getUIString
  Pop $0
  ${Select} $0
    ${Case} "cleanup_header"
      ${If} $ProfileCleanupPromptType == 1
        Push "$(STUB_CLEANUP_REINSTALL_HEADER)"
      ${Else}
        Push "$(STUB_CLEANUP_PAVEOVER_HEADER)"
      ${EndIf}
    ${Case} "cleanup_button"
      ${If} $ProfileCleanupPromptType == 1
        Push "$(STUB_CLEANUP_REINSTALL_BUTTON)"
      ${Else}
        Push "$(STUB_CLEANUP_PAVEOVER_BUTTON)"
      ${EndIf}
    ${Case} "cleanup_checkbox"
      Push "$(STUB_CLEANUP_CHECKBOX_LABEL)"
    ${Case} "installing_label"
      Push "$(STUB_INSTALLING_LABEL)"
    ${Case} "installing_blurb_0"
      !ifdef DEV_EDITION
        Push "$(STUB_BLURB_FIRST_DEVEDITION)"
      !else
        Push "$(STUB_BLURB_FIRST)"
      !endif
    ${Case} "installing_blurb_1"
      !ifdef DEV_EDITION
        Push "$(STUB_BLURB_SECOND_DEVEDITION)"
      !else
        Push "$(STUB_BLURB_SECOND)"
      !endif
    ${Case} "installing_blurb_2"
      !ifdef DEV_EDITION
        Push "$(STUB_BLURB_THIRD_DEVEDITION)"
      !else
        Push "$(STUB_BLURB_THIRD)"
      !endif
    ${Default}
      Push ""
  ${EndSelect}
FunctionEnd

Function createInstall
  ${If} $AbortInstallation != "false"
    ; Skip the installation, but first send the telemetry
    Call SendPing
    ; Abort in this context skips the "page"
    Abort
  ${EndIf}
  GetDlgItem $0 $HWNDPARENT 1 ; Install button
  EnableWindow $0 0
  ShowWindow $0 ${SW_HIDE}

  GetDlgItem $0 $HWNDPARENT 3 ; Back button
  EnableWindow $0 0
  ShowWindow $0 ${SW_HIDE}

  GetDlgItem $0 $HWNDPARENT 2 ; Cancel button
  ; Hide the Cancel button, but don't disable it (or else it won't be possible
  ; to close the window)
  ShowWindow $0 ${SW_HIDE}

  ; Get keyboard focus on the parent
  System::Call "user32::SetFocus(p$HWNDPARENT)"

  ; Set $DownloadReset to true so the first download tick count is measured.
  StrCpy $DownloadReset "true"
  StrCpy $IsDownloadFinished "false"
  StrCpy $DownloadRetryCount "0"
  StrCpy $DownloadedBytes "0"
  StrCpy $StartLastDownloadTickCount ""
  StrCpy $DownloadFirstTransferSeconds ""
  StrCpy $OpenedDownloadPage "0"

  ClearErrors
  ReadINIStr $ExistingVersion "$INSTDIR\application.ini" "App" "Version"
  ${If} ${Errors}
    StrCpy $ExistingVersion "0"
  ${EndIf}

  ClearErrors
  ReadINIStr $ExistingBuildID "$INSTDIR\application.ini" "App" "BuildID"
  ${If} ${Errors}
    StrCpy $ExistingBuildID "0"
  ${EndIf}

  Call GetHadExistingProfile
  Pop $HadExistingProfile

  StrCpy $DownloadServerIP ""

  System::Call "kernel32::GetTickCount()l .s"
  Pop $StartDownloadPhaseTickCount

  ${If} ${FileExists} "$INSTDIR\uninstall\uninstall.log"
    StrCpy $InstallTotalSteps ${InstallPaveOverTotalSteps}
  ${Else}
    StrCpy $InstallTotalSteps ${InstallCleanTotalSteps}
  ${EndIf}

  ${ITBL3Create}
  ${ITBL3SetProgressState} "${TBPF_INDETERMINATE}"

  ; Make sure the file we're about to try to download to doesn't already exist,
  ; so we don't end up trying to "resume" on top of the wrong file.
  Delete "$PLUGINSDIR\download.exe"

  ${StartTimer} ${DownloadIntervalMS} StartDownload

  ${RegisterAllCustomFunctions}

  File /oname=$PLUGINSDIR\installing.html "installing.html"
  File /oname=$PLUGINSDIR\installing_page.css "installing_page.css"
  File /oname=$PLUGINSDIR\installing.js "installing.js"
  WebBrowser::ShowPage "$PLUGINSDIR\installing.html"
FunctionEnd

Function GetHadExistingProfile
  ${GetLocalAppDataFolder} $0
  ${If} ${FileExists} "$0\Mozilla\Firefox"
    Push 1
  ${Else}
    Push 0
  ${EndIf}
FunctionEnd

Function StartDownload
  WebBrowser::CancelTimer $TimerHandle

  Call GetDownloadURL
  Pop $0
  InetBgDL::Get "$0" "$PLUGINSDIR\download.exe" \
                /CONNECTTIMEOUT 120 /RECEIVETIMEOUT 120 /END

  ${StartTimer} ${DownloadIntervalMS} OnDownload

  ${If} ${FileExists} "$INSTDIR\${TO_BE_DELETED}"
    RmDir /r "$INSTDIR\${TO_BE_DELETED}"
  ${EndIf}
FunctionEnd

Function SetProgressBars
  ${ITBL3SetProgressValue} "$ProgressCompleted" "${PROGRESS_BAR_TOTAL_STEPS}"
FunctionEnd

Function OnDownload
  InetBgDL::GetStats
  # $0 = HTTP status code, 0=Completed
  # $1 = Completed files
  # $2 = Remaining files
  # $3 = Number of downloaded bytes for the current file
  # $4 = Size of current file (Empty string if the size is unknown)
  # /RESET must be used if status $0 > 299 (e.g. failure), even if resuming
  # When status is $0 =< 299 it is handled by InetBgDL
  StrCpy $DownloadServerIP "$5"
  ${If} $0 > 299
    WebBrowser::CancelTimer $TimerHandle

    ; Download server web access filtering indicates a blocked request by returning
    ; a status of 406 - Not Acceptable
    ${If} $0 = 406
      IntOp $DownloadRequestsBlockedByServer $DownloadRequestsBlockedByServer + 1
    ${EndIf}
    IntOp $DownloadRetryCount $DownloadRetryCount + 1
    ${If} $DownloadRetryCount >= ${DownloadMaxRetries}
      StrCpy $ExitCode "${ERR_DOWNLOAD_TOO_MANY_RETRIES}"
      ; Use a timer so the UI has a chance to update
      ${StartTimer} ${InstallIntervalMS} DisplayDownloadError
      Return
    ${EndIf}

    ; 1000 is a special code meaning InetBgDL lost the connection before it got
    ; all the bytes it was expecting. We'll try to resume the transfer in that
    ; case (assuming we aren't out of retries), so don't treat it as a reset
    ; or clear the progress bar.
    ${If} $0 != 1000
      ${If} "$DownloadReset" != "true"
        StrCpy $DownloadedBytes "0"
        ${ITBL3SetProgressState} "${TBPF_INDETERMINATE}"
      ${EndIf}
      StrCpy $DownloadSizeBytes ""
      StrCpy $DownloadReset "true"
      Delete "$PLUGINSDIR\download.exe"
    ${EndIf}

    InetBgDL::Get /RESET /END
    ${StartTimer} ${DownloadRetryIntervalMS} StartDownload
    Return
  ${EndIf}

  ${If} "$DownloadReset" == "true"
    System::Call "kernel32::GetTickCount()l .s"
    Pop $StartLastDownloadTickCount
    StrCpy $DownloadReset "false"
    ; The seconds elapsed from the start of the download phase until the first
    ; bytes are received are only recorded for the first request so it is
    ; possible to determine connection issues for the first request.
    ${If} "$DownloadFirstTransferSeconds" == ""
      ; Get the seconds elapsed from the start of the download phase until the
      ; first bytes are received.
      ${GetSecondsElapsed} "$StartDownloadPhaseTickCount" "$StartLastDownloadTickCount" $DownloadFirstTransferSeconds
    ${EndIf}
  ${EndIf}

  ${If} "$DownloadSizeBytes" == ""
  ${AndIf} "$4" != ""
    StrCpy $DownloadSizeBytes "$4"
    StrCpy $ProgressCompleted 0
  ${EndIf}

  ; Don't update the status until after the download starts
  ${If} $2 != 0
  ${AndIf} "$4" == ""
    Return
  ${EndIf}

  ${If} $IsDownloadFinished != "true"
    ${If} $2 == 0
      WebBrowser::CancelTimer $TimerHandle
      StrCpy $IsDownloadFinished "true"
      System::Call "kernel32::GetTickCount()l .s"
      Pop $EndDownloadPhaseTickCount

      ${If} "$DownloadSizeBytes" == ""
        ; It's possible for the download to finish before we were able to
        ; get the size while it was downloading, and InetBgDL doesn't report
        ; it afterwards. Use the size of the finished file.
        ClearErrors
        FileOpen $5 "$PLUGINSDIR\download.exe" r
        ${IfNot} ${Errors}
          FileSeek $5 0 END $DownloadSizeBytes
          FileClose $5
        ${EndIf}
      ${EndIf}
      StrCpy $DownloadedBytes "$DownloadSizeBytes"

      ; Update the progress bars first in the UI change so they take affect
      ; before other UI changes.
      StrCpy $ProgressCompleted "${PROGRESS_BAR_DOWNLOAD_END_STEP}"
      Call SetProgressBars

      ; Disable the Cancel button during the install
      GetDlgItem $5 $HWNDPARENT 2
      EnableWindow $5 0

      ; Open a handle to prevent modification of the full installer
      StrCpy $R9 "${INVALID_HANDLE_VALUE}"
      System::Call 'kernel32::CreateFileW(w "$PLUGINSDIR\download.exe", \
                                          i ${GENERIC_READ}, \
                                          i ${FILE_SHARE_READ}, i 0, \
                                          i ${OPEN_EXISTING}, i 0, i 0) i .R9'
      StrCpy $HandleDownload "$R9"

      ${If} $HandleDownload == ${INVALID_HANDLE_VALUE}
        StrCpy $ExitCode "${ERR_PREINSTALL_INVALID_HANDLE}"
        System::Call "kernel32::GetTickCount()l .s"
        Pop $EndPreInstallPhaseTickCount
        ; Use a timer so the UI has a chance to update
        ${StartTimer} ${InstallIntervalMS} DisplayDownloadError
      ${Else}
        CertCheck::CheckPETrustAndInfoAsync "$PLUGINSDIR\download.exe" \
          "${CertNameDownload}" "${CertIssuerDownload}"
        ${StartTimer} ${DownloadIntervalMS} OnCertCheck
      ${EndIf}
    ${Else}
      StrCpy $DownloadedBytes "$3"
      System::Int64Op $DownloadedBytes * ${PROGRESS_BAR_DOWNLOAD_END_STEP}
      Pop $ProgressCompleted
      System::Int64Op $ProgressCompleted / $DownloadSizeBytes
      Pop $ProgressCompleted
      Call SetProgressBars
    ${EndIf}
  ${EndIf}
FunctionEnd

Function OnCertCheck
  System::Call "kernel32::GetTickCount()l .s"
  Pop $EndPreInstallPhaseTickCount

  CertCheck::GetStatus
  Pop $0
  ${If} $0 == 0
    ${GetSecondsElapsed} "$EndDownloadPhaseTickCount" "$EndPreInstallPhaseTickCount" $0
    ${If} $0 >= ${PreinstallCertCheckMaxWaitSec}
      WebBrowser::CancelTimer $TimerHandle
      StrCpy $ExitCode "${ERR_PREINSTALL_CERT_TIMEOUT}"
      ; Use a timer so the UI has a chance to update
      ${StartTimer} ${InstallIntervalMS} DisplayDownloadError
    ${EndIf}
    Return
  ${EndIf}
  Pop $0
  Pop $1

  ${If} $0 == 0
  ${AndIf} $1 == 0
    StrCpy $ExitCode "${ERR_PREINSTALL_CERT_UNTRUSTED_AND_ATTRIBUTES}"
  ${ElseIf} $0 == 0
    StrCpy $ExitCode "${ERR_PREINSTALL_CERT_UNTRUSTED}"
  ${ElseIf} $1 == 0
    StrCpy $ExitCode "${ERR_PREINSTALL_CERT_ATTRIBUTES}"
  ${EndIf}

  WebBrowser::CancelTimer $TimerHandle

  ${If} $0 == 0
  ${OrIf} $1 == 0
    ; Use a timer so the UI has a chance to update
    ${StartTimer} ${InstallIntervalMS} DisplayDownloadError
    Return
  ${EndIf}

  Call LaunchFullInstaller
FunctionEnd

Function LaunchFullInstaller
  ; Instead of extracting the files we use the downloaded installer to
  ; install in case it needs to perform operations that the stub doesn't
  ; know about.
  WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "InstallDirectoryPath" "$INSTDIR"

  ; Always create a start menu shortcut, so the user always has some way
  ; to access the application.
  WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "StartMenuShortcuts" "true"

  ; Either avoid or force adding a taskbar pin and desktop shortcut
  ; based on the checkbox value.
  ${If} $CheckboxShortcuts == 0
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "TaskbarShortcut" "false"
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopShortcut" "false"
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopLauncher" "false"
  ${Else}
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "TaskbarShortcut" "true"
!ifdef DESKTOP_LAUNCHER_ENABLED
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopShortcut" "false"
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopLauncher" "true"
!else
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopShortcut" "true"
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "DesktopLauncher" "false"
!endif
  ${EndIf}

!ifdef MOZ_MAINTENANCE_SERVICE
  ${If} $CheckboxInstallMaintSvc == 1
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "MaintenanceService" "true"
  ${Else}
    WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "MaintenanceService" "false"
  ${EndIf}
!else
  WriteINIStr "$PLUGINSDIR\${CONFIG_INI}" "Install" "MaintenanceService" "false"
!endif

  ; Delete the taskbar shortcut history to ensure we do the right thing based on
  ; the config file above.
  ${GetShortcutsLogPath} $0
  Delete "$0"

!ifdef MOZ_PUSH_NOTIFICATION_HELPER
  ${SignalPushNotificationHelperStop} $NotificationHelperStopExitCode
!endif

  ${RemovePrecompleteEntries} "false"

  ; Delete the install.log and let the full installer create it. When the
  ; installer closes it we can detect that it has completed.
  Delete "$INSTDIR\install.log"

  ; Delete firefox.exe.moz-upgrade and firefox.exe.moz-delete if it exists
  ; since it being present will require an OS restart for the full
  ; installer.
  Delete "$INSTDIR\${FileMainEXE}.moz-upgrade"
  Delete "$INSTDIR\${FileMainEXE}.moz-delete"

  System::Call "kernel32::GetTickCount()l .s"
  Pop $EndPreInstallPhaseTickCount

  Exec "$\"$PLUGINSDIR\download.exe$\" /LaunchedFromStub /INI=$PLUGINSDIR\${CONFIG_INI}"
  ${StartTimer} ${InstallIntervalMS} CheckInstall
FunctionEnd

Function SendPing
  HideWindow

  ; Get the tick count for the completion of all phases.
  System::Call "kernel32::GetTickCount()l .s"
  Pop $EndFinishPhaseTickCount

  ; When the value of $IsDownloadFinished is false the download was started
  ; but didn't finish. In this case the tick count stored in
  ; $EndFinishPhaseTickCount is used to determine how long the download was
  ; in progress.
  ${If} "$IsDownloadFinished" == "false"
    StrCpy $EndDownloadPhaseTickCount "$EndFinishPhaseTickCount"
    ; Cancel the download in progress
    InetBgDL::Get /RESET /END
  ${EndIf}

  ; When $DownloadFirstTransferSeconds equals an empty string the download
  ; never successfully started so set the value to 0. It will be possible to
  ; determine that the download didn't successfully start from the seconds for
  ; the last download.
  ${If} "$DownloadFirstTransferSeconds" == ""
    StrCpy $DownloadFirstTransferSeconds "0"
  ${EndIf}

  ; When $StartLastDownloadTickCount equals an empty string the download never
  ; successfully started so set the value to $EndDownloadPhaseTickCount to
  ; compute the correct value.
  ${If} $StartLastDownloadTickCount == ""
    ; This could happen if the download never successfully starts
    StrCpy $StartLastDownloadTickCount "$EndDownloadPhaseTickCount"
  ${EndIf}

  ; When $EndPreInstallPhaseTickCount equals 0 the installation phase was
  ; never completed so set its value to $EndFinishPhaseTickCount to compute
  ; the correct value.
  ${If} "$EndPreInstallPhaseTickCount" == "0"
    StrCpy $EndPreInstallPhaseTickCount "$EndFinishPhaseTickCount"
  ${EndIf}

  ; When $EndInstallPhaseTickCount equals 0 the installation phase was never
  ; completed so set its value to $EndFinishPhaseTickCount to compute the
  ; correct value.
  ${If} "$EndInstallPhaseTickCount" == "0"
    StrCpy $EndInstallPhaseTickCount "$EndFinishPhaseTickCount"
  ${EndIf}

  GetFunctionAddress $0 PrepareStubInstallPing
  Push $0
  Call SendTelemetryPing

  ; The following will exit the installer
  SetAutoClose true
  StrCpy $R9 "2"
  Call RelativeGotoPage
FunctionEnd

Function GetHadOldInstall
  ${If} "$PreviousInstallDir" != ""
    Push 1
  ${Else}
    Push 0
  ${EndIf}
FunctionEnd

Function CheckInstall
  IntOp $InstallCounterStep $InstallCounterStep + 1
  ${If} $InstallCounterStep >= $InstallTotalSteps
    WebBrowser::CancelTimer $TimerHandle
    ; Close the handle that prevents modification of the full installer
    System::Call 'kernel32::CloseHandle(i $HandleDownload)'
    StrCpy $ExitCode "${ERR_INSTALL_TIMEOUT}"
    ; Use a timer so the UI has a chance to update
    ${StartTimer} ${InstallIntervalMS} DisplayDownloadError
    Return
  ${EndIf}

  ${If} $ProgressCompleted < ${PROGRESS_BAR_INSTALL_END_STEP}
    IntOp $0 ${PROGRESS_BAR_INSTALL_END_STEP} - ${PROGRESS_BAR_DOWNLOAD_END_STEP}
    IntOp $0 $InstallCounterStep * $0
    IntOp $0 $0 / $InstallTotalSteps
    IntOp $ProgressCompleted ${PROGRESS_BAR_DOWNLOAD_END_STEP} + $0
    Call SetProgressBars
  ${EndIf}

  ${If} ${FileExists} "$INSTDIR\install.log"
    Delete "$INSTDIR\install.tmp"
    CopyFiles /SILENT "$INSTDIR\install.log" "$INSTDIR\install.tmp"

    ; The unfocus and refocus that happens approximately here is caused by the
    ; installer calling RefreshShellIcons to refresh the shortcut icons.

    ; When the full installer completes the installation the install.log will no
    ; longer be in use.
    ClearErrors
    Delete "$INSTDIR\install.log"
    ${Unless} ${Errors}
      WebBrowser::CancelTimer $TimerHandle
      ; Close the handle that prevents modification of the full installer
      System::Call 'kernel32::CloseHandle(i $HandleDownload)'
      Rename "$INSTDIR\install.tmp" "$INSTDIR\install.log"
      Delete "$PLUGINSDIR\download.exe"
      Delete "$PLUGINSDIR\${CONFIG_INI}"
      System::Call "kernel32::GetTickCount()l .s"
      Pop $EndInstallPhaseTickCount
      Call FinishInstall
    ${EndUnless}
  ${EndIf}
FunctionEnd

Function FinishInstall
  StrCpy $ProgressCompleted "${PROGRESS_BAR_INSTALL_END_STEP}"
  Call SetProgressBars

  ${If} ${FileExists} "$INSTDIR\${FileMainEXE}.moz-upgrade"
    Delete "$INSTDIR\${FileMainEXE}"
    Rename "$INSTDIR\${FileMainEXE}.moz-upgrade" "$INSTDIR\${FileMainEXE}"
  ${EndIf}

  StrCpy $ExitCode "${ERR_SUCCESS}"

  ${CopyPostSigningData}
  Pop $PostSigningData

  Call IsInstallerLaunchedByDesktopLauncher
  Pop $0
  ${If} $0 == 1
    Push "desktoplauncher"
    Call SetDlsourceFieldInPostSigningData
    Call UpdateInstalledPostSigningDataFile
  ${EndIf}

  Call LaunchApp
FunctionEnd

Function IsInstallerLaunchedByDesktopLauncher
  ${GetParameters} $0
  ClearErrors
  StrCpy $1 ""
  ${GetOptions} "$0" "/LaunchedBy:" "$1"
  ${IfNot} ${Errors}
  ${AndIf} $1 == "desktoplauncher"
    Push 1
  ${Else}
    Push 0
  ${EndIf}
FunctionEnd

Function SetDlsourceFieldInPostSigningData
  Pop $R0
  StrCpy $R1 "dlsource"
  StrCpy $R2 "%3D" ; =
  StrCpy $PostSigningData "$R1$R2$R0"
FunctionEnd

Function UpdateInstalledPostSigningDataFile
  ClearErrors
  FileOpen $R0 "$INSTDIR\postSigningData" w
  ${If} ${Errors}
    StrCpy $PostSigningData "error:filewrite"
    Return
  ${EndIf}
  FileWrite $R0 "$PostSigningData"
  FileClose $R0
FunctionEnd

Function RelativeGotoPage
  IntCmp $R9 0 0 Move Move
  StrCmp $R9 "X" 0 Move
  StrCpy $R9 "120"

  Move:
  SendMessage $HWNDPARENT "0x408" "$R9" ""
FunctionEnd

Function CheckSpace
  ${If} "$ExistingTopDir" != ""
    StrLen $0 "$ExistingTopDir"
    StrLen $1 "$INSTDIR"
    ${If} $0 <= $1
      StrCpy $2 "$INSTDIR" $3
      ${If} "$2" == "$ExistingTopDir"
        Return
      ${EndIf}
    ${EndIf}
  ${EndIf}

  StrCpy $ExistingTopDir "$INSTDIR"
  ${DoUntil} ${FileExists} "$ExistingTopDir"
    ${GetParent} "$ExistingTopDir" $ExistingTopDir
    ${If} "$ExistingTopDir" == ""
      StrCpy $SpaceAvailableBytes "0"
      StrCpy $HasRequiredSpaceAvailable "false"
      Return
    ${EndIf}
  ${Loop}

  ${GetLongPath} "$ExistingTopDir" $ExistingTopDir

  ; GetDiskFreeSpaceExW requires a backslash.
  StrCpy $0 "$ExistingTopDir" "" -1 ; the last character
  ${If} "$0" != "\"
    StrCpy $0 "\"
  ${Else}
    StrCpy $0 ""
  ${EndIf}

  System::Call 'kernel32::GetDiskFreeSpaceExW(w, *l, *l, *l) i("$ExistingTopDir$0", .r1, .r2, .r3) .'
  StrCpy $SpaceAvailableBytes "$1"

  System::Int64Op $SpaceAvailableBytes / 1048576
  Pop $1
  System::Int64Op $1 > ${APPROXIMATE_REQUIRED_SPACE_MB}
  Pop $1
  ${If} $1 == 1
    StrCpy $HasRequiredSpaceAvailable "true"
  ${Else}
    StrCpy $HasRequiredSpaceAvailable "false"
  ${EndIf}
FunctionEnd



Function LaunchApp
  StrCpy $FirefoxLaunchCode "2"

  ; Set the current working directory to the installation directory
  SetOutPath "$INSTDIR"
  ClearErrors
  ${GetParameters} $0
  ${GetOptions} "$0" "/UAC:" $1
  ${If} ${Errors}
    ClearErrors
    ${If} $CheckboxCleanupProfile == 1
      ${ExecAndWaitForInputIdle} "$\"$INSTDIR\${FileMainEXE}$\" -reset-profile -migration -first-startup"
    ${Else}
      ${ExecAndWaitForInputIdle} "$\"$INSTDIR\${FileMainEXE}$\" -first-startup"
    ${EndIf}
    ${If} ${Errors}
      StrCpy $FirefoxLaunchCode "0"
    ${EndIf}
  ${Else}
    StrCpy $R1 $CheckboxCleanupProfile
    GetFunctionAddress $0 LaunchAppFromElevatedProcess
    UAC::ExecCodeSegment $0
  ${EndIf}

  StrCpy $AppLaunchWaitTickCount 0
  ${StartTimer} ${AppLaunchWaitIntervalMS} WaitForAppLaunch
FunctionEnd

Function LaunchAppFromElevatedProcess
  ; Set the current working directory to the installation directory
  SetOutPath "$INSTDIR"
  ClearErrors
  ${If} $R1 == 1
    ${ExecAndWaitForInputIdle} "$\"$INSTDIR\${FileMainEXE}$\" -reset-profile -migration -first-startup"
  ${Else}
    ${ExecAndWaitForInputIdle} "$\"$INSTDIR\${FileMainEXE}$\" -first-startup"
  ${EndIf}
  ${If} ${Errors}
    StrCpy $FirefoxLaunchCode "0"
  ${EndIf}
FunctionEnd

Function WaitForAppLaunch
  FindWindow $0 "${MainWindowClass}"
  FindWindow $1 "${DialogWindowClass}"
  ${If} $0 <> 0
  ${OrIf} $1 <> 0
    WebBrowser::CancelTimer $TimerHandle
    StrCpy $ProgressCompleted "${PROGRESS_BAR_APP_LAUNCH_END_STEP}"
    Call SetProgressBars
    Call SendPing
    Return
  ${EndIf}

  IntOp $AppLaunchWaitTickCount $AppLaunchWaitTickCount + 1
  IntOp $0 $AppLaunchWaitTickCount * ${AppLaunchWaitIntervalMS}
  ${If} $0 >= ${AppLaunchWaitTimeoutMS}
    ; We've waited an unreasonably long time, so just exit.
    WebBrowser::CancelTimer $TimerHandle
    Call SendPing
    Return
  ${EndIf}

  ${If} $ProgressCompleted < ${PROGRESS_BAR_APP_LAUNCH_END_STEP}
    IntOp $ProgressCompleted $ProgressCompleted + 1
    Call SetProgressBars
  ${EndIf}
FunctionEnd

Function DisplayDownloadError
  WebBrowser::CancelTimer $TimerHandle
  ; To better display the error state on the taskbar set the progress completed
  ; value to the total value.
  ${ITBL3SetProgressValue} "100" "100"
  ${ITBL3SetProgressState} "${TBPF_ERROR}"

  MessageBox MB_OKCANCEL|MB_ICONSTOP "$(ERROR_DOWNLOAD_CONT)" IDCANCEL +2 IDOK +1
  Call LaunchHelpPage
  Call SendPing
FunctionEnd

Function LaunchHelpPage
  StrCpy $OpenedDownloadPage "1" ; Already initialized to 0
  ClearErrors
  ${GetParameters} $0
  ${GetOptions} "$0" "/UAC:" $1
  ${If} ${Errors}
    Call OpenManualDownloadURL
  ${Else}
    GetFunctionAddress $0 OpenManualDownloadURL
    UAC::ExecCodeSegment $0
  ${EndIf}
FunctionEnd

Function OpenManualDownloadURL
  ClearErrors
  ReadINIStr $0 "${PARTNER_INI}" "DownloadURL" "FallbackPage"
  ${IfNot} ${Errors}
    ExecShell "open" "$0"
  ${Else}
    ExecShell "open" "${URLManualDownload}&installer_arch=$ArchToInstall${URLManualDownloadAppend}"
  ${EndIf}
FunctionEnd

Function ShouldPromptForProfileCleanup
  ; This will be our return value.
  StrCpy $ProfileCleanupPromptType 0

  ; Only consider installations of the same architecture we're installing.
  ${If} $ArchToInstall == ${ARCH_AMD64}
  ${OrIf} $ArchToInstall == ${ARCH_AARCH64}
    SetRegView 64
  ${Else}
    SetRegView 32
  ${EndIf}

  ; Make sure $APPDATA is the user's AppData and not ProgramData.
  ; We'll set this back to all at the end of the function.
  SetShellVarContext current

  ${FindInstallSpecificProfile}
  Pop $R0

  ${If} $R0 == ""
    ; We don't have an install-specific profile, so look for an old-style
    ; default profile instead by checking each numbered Profile section.
    StrCpy $0 0
    ${Do}
      ClearErrors
      ; Check if the section exists by reading a value that must be present.
      ReadINIStr $1 "$APPDATA\Mozilla\Firefox\profiles.ini" "Profile$0" "Path"
      ${If} ${Errors}
        ; We've run out of profile sections.
        ${Break}
      ${EndIf}

      ClearErrors
      ReadINIStr $1 "$APPDATA\Mozilla\Firefox\profiles.ini" "Profile$0" "Default"
      ${IfNot} ${Errors}
      ${AndIf} $1 == "1"
        ; We've found the default profile
        ReadINIStr $1 "$APPDATA\Mozilla\Firefox\profiles.ini" "Profile$0" "Path"
        ReadINIStr $2 "$APPDATA\Mozilla\Firefox\profiles.ini" "Profile$0" "IsRelative"
        ${If} $2 == "1"
          StrCpy $R0 "$APPDATA\Mozilla\Firefox\$1"
        ${Else}
          StrCpy $R0 "$1"
        ${EndIf}
        ${Break}
      ${EndIf}

      IntOp $0 $0 + 1
    ${Loop}
  ${EndIf}

  GetFullPathName $R0 $R0

  ${If} $R0 == ""
    ; No profile to clean up, so don't show the cleanup prompt.
    GoTo end
  ${EndIf}

  ; We have at least one profile present. If we don't have any installations,
  ; then we need to show the re-install prompt. We'll say there's an
  ; installation present if HKCR\FirefoxURL* exists and points to a real path.
  StrCpy $0 0
  StrCpy $R9 ""
  ${Do}
    ClearErrors
    EnumRegKey $1 HKCR "" $0
    ${If} ${Errors}
    ${OrIf} $1 == ""
      ${Break}
    ${EndIf}
    ${WordFind} "$1" "-" "+1{" $2
    ${If} $2 == "FirefoxURL"
      ClearErrors
      ReadRegStr $2 HKCR "$1\DefaultIcon" ""
      ${IfNot} ${Errors}
        ${GetPathFromString} $2 $1
        ${If} ${FileExists} $1
          StrCpy $R9 $1
          ${Break}
        ${EndIf}
      ${EndIf}
    ${EndIf}
    IntOp $0 $0 + 1
  ${Loop}
  ${If} $R9 == ""
    StrCpy $ProfileCleanupPromptType 1
    GoTo end
  ${EndIf}

  ; Okay, there's at least one install, let's see if it's for this channel.
  SetShellVarContext all
  ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $0
  ${If} $0 == "false"
    SetShellVarContext current
    ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $0
    ${If} $0 == "false"
      ; Existing installs are not for this channel. Don't show any prompt.
      GoTo end
    ${EndIf}
  ${EndIf}

  ; Find out what version the default profile was last used on.
  ${If} ${FileExists} "$R0\compatibility.ini"
    ClearErrors
    ReadINIStr $0 "$R0\compatibility.ini" "Compatibility" "LastVersion"
    ${If} ${Errors}
      GoTo end
    ${EndIf}
    ${WordFind} $0 "." "+1{" $0

    ; We don't know what version we're about to install because we haven't
    ; downloaded it yet. Find out what the latest version released on this
    ; channel is and assume we'll be installing that one.
    Call GetLatestReleasedVersion
    ${If} ${Errors}
      ; Use this stub installer's version as a fallback when we can't get the
      ; real current version; this may be behind, but it's better than nothing.
      StrCpy $1 ${AppVersion}
    ${EndIf}

    ${WordFind} $1 "." "+1{" $1
    IntOp $1 $1 - 2

    ${If} $1 > $0
      ; Default profile was last used more than two versions ago, so we need
      ; to show the paveover version of the profile cleanup prompt.
      StrCpy $ProfileCleanupPromptType 2
    ${EndIf}
  ${EndIf}

  end:
  SetRegView lastused
  SetShellVarContext all
FunctionEnd

Function GetLatestReleasedVersion
  ClearErrors
  Push $0 ; InetBgDl::GetStats uses $0 for the HTTP error code
  ; $1 is our return value, so don't save it
  Push $2 ; InetBgDl::GetStats uses $2 to tell us when the transfer is done
  Push $3 ; $3 - $5 are also set by InetBgDl::GetStats, but we don't use them
  Push $4
  Push $5
  Push $6 ; This is our response timeout counter

  InetBgDL::Get /RESET /END
  InetBgDL::Get "https://product-details.mozilla.org/1.0/firefox_versions.json" \
                "$PLUGINSDIR\firefox_versions.json" \
                /CONNECTTIMEOUT 120 /RECEIVETIMEOUT 120 /END

  ; Wait for the response, but only give it half a second since this is on the
  ; installer startup path (we haven't even shown a window yet).
  StrCpy $6 0
  ${Do}
    Sleep 100
    InetBgDL::GetStats
    IntOp $6 $6 + 1

    ${If} $2 == 0
      ${Break}
    ${ElseIf} $6 >= 5
      InetBgDL::Get /RESET /END
      SetErrors
      GoTo end
    ${EndIf}
  ${Loop}

  StrCpy $1 0
  nsJSON::Set /file "$PLUGINSDIR\firefox_versions.json"
  IfErrors end
  ${Select} ${Channel}
  ${Case} "unofficial"
    StrCpy $1 "FIREFOX_NIGHTLY"
  ${Case} "nightly"
    StrCpy $1 "FIREFOX_NIGHTLY"
  ${Case} "aurora"
    StrCpy $1 "FIREFOX_DEVEDITION"
  ${Case} "beta"
    StrCpy $1 "LATEST_FIREFOX_RELEASED_DEVEL_VERSION"
  ${Case} "release"
    StrCpy $1 "LATEST_FIREFOX_VERSION"
  ${EndSelect}
  nsJSON::Get $1 /end

  end:
  ${If} ${Errors}
  ${OrIf} $1 == 0
    SetErrors
    StrCpy $1 0
  ${Else}
    Pop $1
  ${EndIf}

  Pop $6
  Pop $5
  Pop $4
  Pop $3
  Pop $2
  Pop $0
FunctionEnd

; Determine which architecture build we should download and install.
; AArch64 is always selected if it's the native architecture of the machine.
; Otherwise, we check a few things to determine if AMD64 is appropriate:
; 1) Running a 64-bit OS (we've already checked the OS version).
; 2) An amount of RAM strictly greater than RAM_NEEDED_FOR_64BIT
; 3) No third-party products installed that cause issues with the 64-bit build.
;    Currently this includes Lenovo OneKey Theater and Lenovo Energy Management.
; We also make sure that the partner.ini file contains a download URL for the
; selected architecture, when a partner.ini file eixsts.
; If any of those checks fail, the 32-bit x86 build is selected.
Function GetArchToInstall
  StrCpy $ArchToInstall ${ARCH_X86}

  ${If} ${IsNativeARM64}
    StrCpy $ArchToInstall ${ARCH_AARCH64}
    GoTo downloadUrlCheck
  ${EndIf}

  ${IfNot} ${IsNativeAMD64}
    Return
  ${EndIf}

  System::Call "*(i 64, i, l 0, l, l, l, l, l, l)p.r1"
  System::Call "Kernel32::GlobalMemoryStatusEx(p r1)"
  System::Call "*$1(i, i, l.r2, l, l, l, l, l, l)"
  System::Free $1
  ${If} $2 L<= ${RAM_NEEDED_FOR_64BIT}
    Return
  ${EndIf}

  ; Lenovo OneKey Theater can theoretically be in a directory other than this
  ; one, because some installer versions let you change it, but it's unlikely.
  ${If} ${FileExists} "$PROGRAMFILES32\Lenovo\Onekey Theater\windowsapihookdll64.dll"
    Return
  ${EndIf}

  ${If} ${FileExists} "$PROGRAMFILES32\Lenovo\Energy Management\Energy Management.exe"
    Return
  ${EndIf}

  StrCpy $ArchToInstall ${ARCH_AMD64}

  downloadUrlCheck:
  ; If we've selected an architecture that doesn't have a download URL in the
  ; partner.ini, but there is a URL there for 32-bit x86, then fall back to
  ; 32-bit x86 on the theory that we should never use a non-partner build if
  ; we are configured as a partner installer, even if the only build that's
  ; provided is suboptimal for the machine. If there isn't even an x86 URL,
  ; then we won't force x86 and GetDownloadURL will stick with the built-in URL.
  ClearErrors
  ReadINIStr $1 "${PARTNER_INI}" "DownloadURL" "X86"
  ${IfNot} ${Errors}
    ${If} $ArchToInstall == ${ARCH_AMD64}
      ReadINIStr $1 "${PARTNER_INI}" "DownloadURL" "AMD64"
      ${If} ${Errors}
        StrCpy $ArchToInstall ${ARCH_X86}
      ${EndIf}
    ${ElseIf} $ArchToInstall == ${ARCH_AARCH64}
      ReadINIStr $1 "${PARTNER_INI}" "DownloadURL" "AArch64"
      ${If} ${Errors}
        StrCpy $ArchToInstall ${ARCH_X86}
      ${EndIf}
    ${EndIf}
  ${EndIf}
FunctionEnd

Function GetDownloadURL
  Push $0
  Push $1

  ; Start with the appropriate URL from our built-in branding info.
  ${If} $ArchToInstall == ${ARCH_AMD64}
    StrCpy $0 "${URLStubDownloadAMD64}${URLStubDownloadAppend}"
  ${ElseIf} $ArchToInstall == ${ARCH_AARCH64}
    StrCpy $0 "${URLStubDownloadAArch64}${URLStubDownloadAppend}"
  ${Else}
    StrCpy $0 "${URLStubDownloadX86}${URLStubDownloadAppend}"
  ${EndIf}

  ; If we have a partner.ini file then use the URL from there instead.
  ClearErrors
  ${If} $ArchToInstall == ${ARCH_AMD64}
    ReadINIStr $1 "${PARTNER_INI}" "DownloadURL" "AMD64"
  ${ElseIf} $ArchToInstall == ${ARCH_AARCH64}
    ReadINIStr $1 "${PARTNER_INI}" "DownloadURL" "AArch64"
  ${Else}
    ReadINIStr $1 "${PARTNER_INI}" "DownloadURL" "X86"
  ${EndIf}
  ${IfNot} ${Errors}
    StrCpy $0 "$1"
  ${EndIf}

  Pop $1
  Exch $0
FunctionEnd


Function CommonOnInit
  ; Remove the current exe directory from the search order.
  ; This only effects LoadLibrary calls and not implicitly loaded DLLs.
  System::Call 'kernel32::SetDllDirectoryW(w "")'
  StrCpy $AbortInstallation "false"
  StrCpy $DownloadRequestsBlockedByServer 0
  ; Initialize PostSigningData to detect case of not being set at all
  StrCpy $PostSigningData "stub_installer:unset"
  StrCpy $NotificationHelperStopExitCode "unset"
  StrCpy $LANGUAGE 0
  ; This macro is used to set the brand name variables but the ini file method
  ; isn't supported for the stub installer.
  ${SetBrandNameVars} "$PLUGINSDIR\ignored.ini"

  StrCpy $CpuSupportsSSE "0"
  Call CheckCpuSupportsSSE

  ; Windows 8.1/Server 2012 R2 and lower are not supported.
  ${Unless} ${AtLeastWin10}
    StrCpy $ExitCode "${ERR_PREINSTALL_SYS_OS_REQ}"
    ${If} "$CpuSupportsSSE" == "0"
      strCpy $R7 "$(WARN_MIN_SUPPORTED_OSVER_CPU_MSG2)"
    ${Else}
      strCpy $R7 "$(WARN_MIN_SUPPORTED_OSVER_MSG2)"
    ${EndIf}
    MessageBox MB_OKCANCEL|MB_ICONSTOP "$R7" /SD IDCANCEL IDCANCEL +2
    ExecShell "open" "${URLWinPre10NeedsEsr115}"
    StrCpy $AbortInstallation "true"
    Return
  ${EndUnless}

  ; SSE2 CPU support
  ${If} "$CpuSupportsSSE" == "0"
    StrCpy $ExitCode "${ERR_PREINSTALL_SYS_HW_REQ}"
    MessageBox MB_OKCANCEL|MB_ICONSTOP "$(WARN_MIN_SUPPORTED_CPU_MSG2)" /SD IDCANCEL IDCANCEL +2
    ExecShell "open" "${URLSystemRequirements}"
    StrCpy $AbortInstallation "true"
    Return
  ${EndIf}

  Call GetArchToInstall
  ${If} $ArchToInstall == ${ARCH_AARCH64}
  ${OrIf} $ArchToInstall == ${ARCH_AMD64}
    StrCpy $INSTDIR "${DefaultInstDir64bit}"
  ${Else}
    StrCpy $INSTDIR "${DefaultInstDir32bit}"
  ${EndIf}

  !insertmacro IsTestBreakpointSet ${TestBreakpointArchToInstall}

  ; Require elevation if the user can elevate
  ${ElevateUAC}

  ; If we have any existing installation, use its location as the default
  ; path for this install, even if it's not the same architecture.
  SetRegView 32
  SetShellVarContext all ; Set SHCTX to HKLM
  ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $R9

  ${If} "$R9" == "false"
    ${If} ${IsNativeAMD64}
    ${OrIf} ${IsNativeARM64}
      SetRegView 64
      ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $R9
    ${EndIf}
  ${EndIf}

  ${If} "$R9" == "false"
    SetShellVarContext current ; Set SHCTX to HKCU
    ${GetSingleInstallPath} "Software\Mozilla\${BrandFullNameInternal}" $R9
  ${EndIf}

  StrCpy $PreviousInstallDir ""
  ${If} "$R9" != "false"
    StrCpy $PreviousInstallDir "$R9"
    StrCpy $INSTDIR "$PreviousInstallDir"
  ${EndIf}

  ; Used to determine if the default installation directory was used.
  StrCpy $InitialInstallDir "$INSTDIR"


  ; Initialize the majority of variables except those that need to be reset
  ; when a page is displayed.
  StrCpy $ExitCode "${ERR_UNKNOWN}"
  StrCpy $IntroPhaseSeconds "0"
  StrCpy $OptionsPhaseSeconds "0"
  StrCpy $EndPreInstallPhaseTickCount "0"
  StrCpy $EndInstallPhaseTickCount "0"
  StrCpy $StartDownloadPhaseTickCount "0"
  StrCpy $EndDownloadPhaseTickCount "0"
  StrCpy $InitialInstallRequirementsCode ""
  StrCpy $IsDownloadFinished ""
  StrCpy $FirefoxLaunchCode "0"
  StrCpy $CheckboxShortcuts "1"
  StrCpy $CheckboxCleanupProfile "0"
  StrCpy $ProgressCompleted "0"
!ifdef MOZ_MAINTENANCE_SERVICE
  ; We can only install the maintenance service if the user is an admin.
  Call IsUserAdmin
  Pop $0
  ${If} "$0" == "true"
    StrCpy $CheckboxInstallMaintSvc "1"
  ${Else}
    StrCpy $CheckboxInstallMaintSvc "0"
  ${EndIf}
!else
  StrCpy $CheckboxInstallMaintSvc "0"
!endif

!insertmacro IsTestBreakpointSet ${TestBreakpointMaintService}

  StrCpy $FontFamilyName ""
!ifdef FONT_FILE1
  ${If} ${FileExists} "$FONTS\${FONT_FILE1}"
    StrCpy $FontFamilyName "${FONT_NAME1}"
  ${EndIf}
!endif

!ifdef FONT_FILE2
  ${If} $FontFamilyName == ""
  ${AndIf} ${FileExists} "$FONTS\${FONT_FILE2}"
    StrCpy $FontFamilyName "${FONT_NAME2}"
  ${EndIf}
!endif

  ${If} $FontFamilyName == ""
    StrCpy $FontFamilyName "$(^Font)"
  ${EndIf}

  InitPluginsDir
  File /oname=$PLUGINSDIR\bgstub.jpg "bgstub.jpg"

  ; Detect whether the machine is running with a high contrast theme.
  ; We'll hide our background images in that case, both because they don't
  ; always render properly and also to improve the contrast.
  System::Call '*(i 12, i 0, p 0) p . r0'
  ; 0x42 == SPI_GETHIGHCONTRAST
  System::Call 'user32::SystemParametersInfoW(i 0x42, i 0, p r0, i 0)'
  System::Call '*$0(i, i . r1, p)'
  System::Free $0
  IntOp $UsingHighContrastMode $1 & 1

  SetShellVarContext all ; Set SHCTX to All Users
  ; If the user doesn't have write access to the installation directory set
  ; the installation directory to a subdirectory of the user's local
  ; application directory (e.g. non-roaming).
  Call CanWrite
  ${If} "$CanWriteToInstallDir" == "false"
    ${GetLocalAppDataFolder} $0
    StrCpy $INSTDIR "$0\${BrandFullName}\"
    Call CanWrite
  ${EndIf}


  Call CheckSpace

  ${If} ${FileExists} "$INSTDIR"
    ; Always display the long path if the path exists.
    ${GetLongPath} "$INSTDIR" $INSTDIR
  ${EndIf}

  ; Check whether the install requirements are satisfied using the default
  ; values for metrics.
  ${If} "$InitialInstallRequirementsCode" == ""
    ${If} "$CanWriteToInstallDir" != "true"
    ${AndIf} "$HasRequiredSpaceAvailable" != "true"
      StrCpy $InitialInstallRequirementsCode "1"
    ${ElseIf} "$CanWriteToInstallDir" != "true"
      StrCpy $InitialInstallRequirementsCode "2"
    ${ElseIf} "$HasRequiredSpaceAvailable" != "true"
      StrCpy $InitialInstallRequirementsCode "3"
    ${Else}
      StrCpy $InitialInstallRequirementsCode "0"
    ${EndIf}
  ${EndIf}

  Call CanWrite
  ${If} "$CanWriteToInstallDir" == "false"
    StrCpy $ExitCode "${ERR_PREINSTALL_NOT_WRITABLE}"
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(WARN_WRITE_ACCESS_QUIT)$\n$\n$INSTDIR" /SD IDOK
    StrCpy $AbortInstallation "true"
    Return
  ${EndIf}

  !insertmacro IsTestBreakpointSet ${TestBreakpointCanWriteToDir}

  Call CheckSpace
  ${If} "$HasRequiredSpaceAvailable" == "false"
      StrCpy $ExitCode "${ERR_PREINSTALL_SPACE}"
    MessageBox MB_OK|MB_ICONEXCLAMATION "$(WARN_DISK_SPACE_QUIT)"
    StrCpy $AbortInstallation "true"
    Return
  ${EndIf}

  !insertmacro IsTestBreakpointSet ${TestBreakpointCheckSpace}

  ${InitHashAppModelId} "$INSTDIR" "Software\Mozilla\${AppName}\TaskBarIDs"

  File /oname=$PLUGINSDIR\stub_common.css "stub_common.css"
  File /oname=$PLUGINSDIR\stub_common.js "stub_common.js"
FunctionEnd

; .onGUIInit isn't needed except for RTL locales
!ifdef ${AB_CD}_rtl
Function .onGUIInit
  ${MakeWindowRTL} $HWNDPARENT
FunctionEnd
!endif

Function .onGUIEnd
  Delete "$PLUGINSDIR\_temp"
  Delete "$PLUGINSDIR\download.exe"
  Delete "$PLUGINSDIR\${CONFIG_INI}"

  ${UnloadUAC}
FunctionEnd
