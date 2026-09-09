# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# NSIS branding defines for official release builds.
# The nightly build branding.nsi is located in browser/installer/windows/nsis/
# The unofficial build branding.nsi is located in browser/branding/unofficial/

# BrandFullNameInternal is used for some registry and file system values
# instead of BrandFullName and typically should not be modified.
!define BrandFullNameInternal "FUNXPLORER"
!define BrandFullName         "FUNXPLORER"
!define CompanyName           "FUNCOMPUTER Labs"
!define URLInfoAbout          "https://funcomputer.vercel.app"
!define URLUpdateInfo         "https://www.firefox.com/firefox/${AppVersion}/releasenotes"
!define HelpLink              "https://support.mozilla.org"

; The OFFICIAL define is a workaround to support different urls for Release and
; Beta since they share the same branding when building with other branches that
; set the update channel to beta.
!define OFFICIAL
!define URLStubDownloadX86 "https://download.mozilla.org/?os=win&lang=${AB_CD}&product=firefox-latest"
!define URLStubDownloadAMD64 "https://download.mozilla.org/?os=win64&lang=${AB_CD}&product=firefox-latest"
!define URLStubDownloadAArch64 "https://download.mozilla.org/?os=win64-aarch64&lang=${AB_CD}&product=firefox-latest"
!define URLManualDownload "https://www.mozilla.org/${AB_CD}/firefox/installer-help/?channel=release&installer_lang=${AB_CD}"
!define URLSystemRequirements "https://www.mozilla.org/firefox/system-requirements/"
!define Channel "release"

# The installer's certificate name and issuer expected by the stub installer
!define CertNameDownload   "Mozilla Corporation"
!define CertIssuerDownload "DigiCert Trusted G4 Code Signing RSA4096 SHA384 2021 CA1"

# Dialog units are used so the UI displays correctly with the system's DPI
# settings. These are tweaked to look good with the en-US strings; ideally
# we would customize them for each locale but we don't really have a way to
# implement that and it would be a ton of work for the localizers.
!define PROFILE_CLEANUP_LABEL_TOP "50u"
!define PROFILE_CLEANUP_LABEL_LEFT "22u"
!define PROFILE_CLEANUP_LABEL_WIDTH "175u"
!define PROFILE_CLEANUP_LABEL_HEIGHT "100u"
!define PROFILE_CLEANUP_LABEL_ALIGN "left"
!define PROFILE_CLEANUP_CHECKBOX_LEFT "22u"
!define PROFILE_CLEANUP_CHECKBOX_WIDTH "175u"
!define PROFILE_CLEANUP_BUTTON_LEFT "22u"
!define INSTALL_HEADER_TOP "70u"
!define INSTALL_HEADER_LEFT "22u"
!define INSTALL_HEADER_WIDTH "180u"
!define INSTALL_HEADER_HEIGHT "100u"
!define INSTALL_BODY_LEFT "22u"
!define INSTALL_BODY_WIDTH "180u"
!define INSTALL_INSTALLING_TOP "115u"
!define INSTALL_INSTALLING_LEFT "270u"
!define INSTALL_INSTALLING_WIDTH "150u"
!define INSTALL_PROGRESS_BAR_TOP "100u"
!define INSTALL_PROGRESS_BAR_LEFT "270u"
!define INSTALL_PROGRESS_BAR_WIDTH "150u"
!define INSTALL_PROGRESS_BAR_HEIGHT "12u"

!define PROFILE_CLEANUP_CHECKBOX_TOP_MARGIN "12u"
!define PROFILE_CLEANUP_BUTTON_TOP_MARGIN "12u"
!define PROFILE_CLEANUP_BUTTON_X_PADDING "80u"
!define PROFILE_CLEANUP_BUTTON_Y_PADDING "8u"
!define INSTALL_BODY_TOP_MARGIN "20u"

# Font settings that can be customized for each channel
!define INSTALL_HEADER_FONT_SIZE 20
!define INSTALL_HEADER_FONT_WEIGHT 600
!define INSTALL_INSTALLING_FONT_SIZE 15
!define INSTALL_INSTALLING_FONT_WEIGHT 600

# UI Colors that can be customized for each channel
!define COMMON_TEXT_COLOR 0x000000
!define COMMON_BACKGROUND_COLOR 0xFFFFFF
!define INSTALL_INSTALLING_TEXT_COLOR 0xFFFFFF
# This color is written as 0x00BBGGRR because it's actually a COLORREF value.
!define PROGRESS_BAR_BACKGROUND_COLOR 0xFBBD02
