# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### Strings used by the Windows installer. They are converted to NSIS
### language files at build time, so only plain text and the variables
### documented below are supported.
### Advanced Fluent features such as plurals, select expressions, and
### term/message references are not supported.

## Variables:
##   $BrandShortName (String) - The short brand name (e.g. Firefox).
##   $BrandFullName (String) - The full brand name (e.g. Mozilla Firefox).
##   $MinSupportedVer (String) - The minimum supported version of Windows (e.g. Microsoft Windows 10).
##   $MinSupportedCPU (String) - The CPU feature required to run the application (e.g. SSE2).

reg-app-desc = { $BrandShortName } delivers safe, easy web browsing. A familiar user interface, enhanced security features including protection from online identity theft, and integrated search let you get the most out of the web.
brief-app-desc = Fast and private web browsing
# The non-variable portion of this MUST match the translation of
# "private-browsing-shortcut-text-2" in browser.ftl
private-browsing-shortcut-title = { $BrandShortName } Private Browsing
context-options = { $BrandShortName } Options
    .accesskey = O
context-safe-mode = { $BrandShortName } Safe Mode
    .accesskey = S
options-page-title = Setup Type
options-page-subtitle = Choose setup options
shortcuts-page-title = Set Up Shortcuts
shortcuts-page-subtitle = Create Program Icons
components-page-title = Set Up Optional Components
components-page-subtitle = Optional Recommended Components
optional-components-desc = The Maintenance Service will allow you to update { $BrandShortName } silently in the background.
maintenance-service-checkbox-desc = Install Maintenance Service
    .accesskey = M
summary-page-title = Summary
summary-page-subtitle = Ready to start installing { $BrandShortName }
summary-installed-to = { $BrandShortName } will be installed to the following location:
summary-reboot-required-install = A restart of your computer may be required to complete the installation.
summary-reboot-required-uninstall = A restart of your computer may be required to complete the uninstall.
summary-install-click = Click Install to continue.
summary-upgrade-click = Click Upgrade to continue.
launch-text = Launch { $BrandShortName } now
    .accesskey = L
create-icons-desc = Create icons for { $BrandShortName }:
icons-desktop = On my desktop
    .accesskey = d
icons-startmenu = In my Start menu Programs folder
    .accesskey = S
icons-taskbar = On my taskbar
    .accesskey = t
warn-manually-close-app-install =
    { $BrandShortName } must be closed to proceed with the installation.

    Please close { $BrandShortName } to continue.
warn-manually-close-app-uninstall =
    { $BrandShortName } must be closed to proceed with the uninstall.

    Please close { $BrandShortName } to continue.
warn-manually-close-app-refresh =
    { $BrandShortName } must be closed to proceed with the refresh.

    Please close { $BrandShortName } to continue.
warn-write-access =
    You don’t have access to write to the installation directory.

    Click OK to select a different directory.
warn-disk-space =
    You don’t have sufficient disk space to install to this location.

    Click OK to select a different location.
warn-min-supported-osver-msg = Sorry, { $BrandShortName } can’t be installed. This version of { $BrandShortName } requires { $MinSupportedVer } or newer. Please click the OK button for additional information.
warn-min-supported-cpu-msg = Sorry, { $BrandShortName } can’t be installed. This version of { $BrandShortName } requires a processor with { $MinSupportedCPU } support. Please click the OK button for additional information.
warn-min-supported-osver-cpu-msg = Sorry, { $BrandShortName } can’t be installed. This version of { $BrandShortName } requires { $MinSupportedVer } or newer and a processor with { $MinSupportedCPU } support. Please click the OK button for additional information.
warn-restart-required-uninstall = Your computer must be restarted to complete a previous uninstall of { $BrandShortName }. Do you want to reboot now?
warn-restart-required-upgrade = Your computer must be restarted to complete a previous upgrade of { $BrandShortName }. Do you want to reboot now?
error-create-directory-prefix = Error creating directory:
error-create-directory-suffix =
    Click Cancel to stop the installation or
    Retry to try again.

un-confirm-page-title = Uninstall { $BrandFullName }
un-confirm-uninstalled-from = { $BrandShortName } will be uninstalled from the following location:

un-refresh-page-title = Refresh { $BrandShortName } Instead?
un-refresh-page-explanation =
    If you’re having problems with { $BrandShortName }, a refresh can help.

    This will restore the default settings and remove add-ons. Start fresh for optimal performance.
un-refresh-learn-more = Learn more
    .accesskey = L
un-refresh-button = Refresh { $BrandShortName }
    .accesskey = R

banner-check-existing = Checking existing installation…

status-install-app = Installing { $BrandShortName }…
status-uninstall-main = Uninstalling { $BrandShortName }…
status-cleanup = A Little Housekeeping…

un-survey-checkbox-label = Tell Mozilla why you uninstalled { $BrandShortName }

# This string should fit in one line (approximately 65 characters).
options-summary = Choose the type of setup you prefer, then click Next.
# This string should fit in one line (approximately 65 characters).
option-standard-desc = { $BrandShortName } will be installed with the most common options.
option-standard-radio = Standard
    .accesskey = S
# This string should fit in two lines (approximately 65 characters per line).
option-custom-desc = You may choose individual options to be installed. Recommended for experienced users.
option-custom-radio = Custom
    .accesskey = C

# This text replaces the Install button text on the summary page.
upgrade-button = Upgrade
    .accesskey = U
