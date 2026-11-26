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
##   $MinSupportedVer (String) - The minimum supported version of Windows (e.g. Microsoft Windows 10).
##   $MinSupportedCPU (String) - The CPU feature required to run the application (e.g. SSE2).

installer-win-caption = { $BrandShortName } Installer

stub-cleanup-paveover-header = You already installed { $BrandShortName }. Let’s power up with the latest version.
stub-cleanup-reinstall-header = You already installed { $BrandShortName }. Let’s get you a fresh copy.
stub-cleanup-paveover-button = Update
stub-cleanup-reinstall-button = Re-install
stub-cleanup-checkbox-label = Clean up and speed up. Go back to default settings and delete old add-ons.

stub-installing-label = Now installing…
stub-blurb-first2 = Built to protect you, not track you
stub-blurb-second = Speedy. Safe. Blocks trackers automatically.
stub-blurb-third = Thanks for building a better web with us
stub-blurb-first-devedition = Get ready to build for the open web
stub-blurb-second-devedition = Powerful. Private. Made for developers.
stub-blurb-third-devedition = Thank you for being part of the { $BrandShortName } community

warn-min-supported-osver-msg2 = It looks like { $BrandShortName } can’t be installed. This version of { $BrandShortName } requires { $MinSupportedVer } or newer. Please click the OK button for additional information.
warn-min-supported-cpu-msg2 = It looks like { $BrandShortName } can’t be installed. This version of { $BrandShortName } requires a processor with { $MinSupportedCPU } support. Please click the OK button for additional information.
warn-min-supported-osver-cpu-msg2 = It looks like { $BrandShortName } can’t be installed. This version of { $BrandShortName } requires { $MinSupportedVer } or newer and a processor with { $MinSupportedCPU } support. Please click the OK button for additional information.
warn-write-access-quit = It looks like you’re not allowed to install to that file location.
warn-disk-space-quit = It looks like you don’t have enough storage on your device to install { $BrandShortName }.

error-download-cont = It looks like { $BrandShortName } couldn’t be installed. Choose OK to start over.

stub-cancel-prompt-heading = Do you want to install { $BrandShortName }?
stub-cancel-prompt-message = If you cancel, { $BrandShortName } will not be installed.
stub-cancel-prompt-button-continue = Install { $BrandShortName }
stub-cancel-prompt-button-exit = Cancel
