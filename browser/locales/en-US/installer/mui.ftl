# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### Strings used by the Windows installer. They are converted to NSIS
### language files at build time, so only plain text and the variables
### documented below are supported.
### Advanced Fluent features such as plurals, select expressions, and
### term/message references are not supported.

## Variables:
##   $BrandFullName (String) - The full brand name (e.g. Mozilla Firefox).
##   $BrandFullNameDA (String) - The full brand name, with any ampersand doubled so that it isn’t used as an accesskey.
##   $ClickInstructions (String) - The instructions to proceed to the next step of the wizard (e.g. “Click Next to continue.”).

mui-text-welcome-info-title = Welcome to the { $BrandFullNameDA } Setup Wizard
mui-text-welcome-info-text =
    This wizard will guide you through the installation of { $BrandFullNameDA }.

    It is recommended that you close all other applications before starting Setup. This will make it possible to update relevant system files without having to reboot your computer.

    { $ClickInstructions }
mui-text-components-title = Choose Components
mui-text-components-subtitle = Choose which features of { $BrandFullNameDA } you want to install.
mui-innertext-components-description-title = Description
mui-innertext-components-description-info = Position your mouse over a component to see its description.
mui-text-directory-title = Choose Install Location
mui-text-directory-subtitle = Choose the folder in which to install { $BrandFullNameDA }.
mui-text-installing-title = Installing
mui-text-installing-subtitle = Please wait while { $BrandFullNameDA } is being installed.
mui-text-finish-title = Installation Complete
mui-text-finish-subtitle = Setup was completed successfully.
mui-text-abort-title = Installation Aborted
mui-text-abort-subtitle = Setup was not completed successfully.
mui-buttontext-finish = Finish
    .accesskey = F
mui-text-finish-info-title = Completing the { $BrandFullNameDA } Setup Wizard
mui-text-finish-info-text =
    { $BrandFullNameDA } has been installed on your computer.

    Click Finish to close this wizard.
mui-text-finish-info-reboot = Your computer must be restarted in order to complete the installation of { $BrandFullNameDA }. Do you want to reboot now?
mui-text-finish-rebootnow = Reboot now
mui-text-finish-rebootlater = I want to manually reboot later
mui-text-startmenu-title = Choose Start Menu Folder
mui-text-startmenu-subtitle = Choose a Start Menu folder for the { $BrandFullNameDA } shortcuts.
mui-innertext-startmenu-top = Select the Start Menu folder in which you would like to create the program’s shortcuts. You can also enter a name to create a new folder.
mui-text-abortwarning = Are you sure you want to quit { $BrandFullName } Setup?
mui-untext-welcome-info-title = Welcome to the { $BrandFullNameDA } Uninstall Wizard
mui-untext-welcome-info-text =
    This wizard will guide you through the uninstallation of { $BrandFullNameDA }.

    Before starting the uninstallation, make sure { $BrandFullNameDA } is not running.

    { $ClickInstructions }
mui-untext-confirm-title = Uninstall { $BrandFullNameDA }
mui-untext-confirm-subtitle = Remove { $BrandFullNameDA } from your computer.
mui-untext-uninstalling-title = Uninstalling
mui-untext-uninstalling-subtitle = Please wait while { $BrandFullNameDA } is being uninstalled.
mui-untext-finish-title = Uninstallation Complete
mui-untext-finish-subtitle = Uninstall was completed successfully.
mui-untext-abort-title = Uninstallation Aborted
mui-untext-abort-subtitle = Uninstall was not completed successfully.
mui-untext-finish-info-title = Completing the { $BrandFullNameDA } Uninstall Wizard
mui-untext-finish-info-text =
    { $BrandFullNameDA } has been uninstalled from your computer.

    Click Finish to close this wizard.
mui-untext-finish-info-reboot = Your computer must be restarted in order to complete the uninstallation of { $BrandFullNameDA }. Do you want to reboot now?
mui-untext-abortwarning = Are you sure you want to quit { $BrandFullName } Uninstall?
