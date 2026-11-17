# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

migration-wizard-selection-header = Import browser data
migration-wizard-selection-list = Select the data you’d like to import.

# Shown in the new migration wizard's dropdown selector for choosing the browser
# to import from. This variant is shown when the selected browser doesn't support
# user profiles, and so we only show the browser name.
#
# Variables:
#  $sourceBrowser (String): the name of the browser to import from.
migration-wizard-selection-option-without-profile = { $sourceBrowser }

# Shown in the new migration wizard's dropdown selector for choosing the browser
# and user profile to import from. This variant is shown when the selected browser
# supports user profiles.
#
# Variables:
#  $sourceBrowser (String): the name of the browser to import from.
#  $profileName (String): the name of the user profile to import from.
migration-wizard-selection-option-with-profile = { $sourceBrowser } — { $profileName }

# Each migrator is expected to include a display name string, and that display
# name string should have a key with "migration-wizard-migrator-display-name-"
# as a prefix followed by the unique identification key for the migrator.

migration-wizard-migrator-display-name-arc = Arc
migration-wizard-migrator-display-name-brave = Brave
migration-wizard-migrator-display-name-canary = Chrome Canary
migration-wizard-migrator-display-name-chrome = Chrome
migration-wizard-migrator-display-name-chrome-beta = Chrome Beta
migration-wizard-migrator-display-name-chrome-dev = Chrome Dev
migration-wizard-migrator-display-name-chromium = Chromium
migration-wizard-migrator-display-name-chromium-360se = 360 Secure Browser
migration-wizard-migrator-display-name-chromium-edge = Microsoft Edge
migration-wizard-migrator-display-name-chromium-edge-beta = Microsoft Edge Beta
migration-wizard-migrator-display-name-edge-legacy = Microsoft Edge Legacy
migration-wizard-migrator-display-name-self = { -brand-product-name }
migration-wizard-migrator-display-name-file-password-csv = Passwords from CSV file
migration-wizard-migrator-display-name-file-bookmarks = Bookmarks from HTML file
migration-wizard-migrator-display-name-ie = Microsoft Internet Explorer
migration-wizard-migrator-display-name-opera = Opera
migration-wizard-migrator-display-name-opera-gx = Opera GX
migration-wizard-migrator-display-name-safari = Safari
migration-wizard-migrator-display-name-vivaldi = Vivaldi

migration-source-name-ie = Internet Explorer
migration-source-name-edge = Microsoft Edge
migration-source-name-chrome = Google Chrome

migration-imported-safari-reading-list = Reading List (From Safari)
migration-imported-edge-reading-list = Reading List (From Edge)

# Name of the bookmark folder created when importing a space from the Arc
# browser that the user did not give a name. Spaces are Arc's way of grouping
# tabs, similar to tab groups.
# Note: Arc was never localized so there are no official translastions for "Spaces"
#
# Variables:
#  $number (Number): the position of the space among the imported profile's spaces, starting at 1.
migration-imported-arc-untitled-space = Space { $number }

## These strings are shown if the selected browser data directory is unreadable.
## In practice, this tends to only occur on Linux when Firefox
## is installed as a Snap, and on macOS 27 and later.

migration-no-permissions-message = { -brand-short-name } does not have access to other browsers’ profiles installed on this device.

migration-no-permissions-instructions = To continue importing data from another browser, grant { -brand-short-name } access to its profile folder.

migration-no-permissions-instructions-step1 = Select “Continue”

# The second step in getting permissions to read data for the selected
# browser type. On macOS, the file picker already opens in the folder that
# needs to be selected.
#
# Variables:
#  $permissionsPath (String): the file system path that the user will need to grant read permission to.
migration-no-permissions-instructions-step2-2 =
  { PLATFORM() ->
      [macos] In the file picker, choose “Open”
     *[other] In the file picker, navigate to <code>{ $permissionsPath }</code> and choose “Select”
  }

## These strings will be displayed based on how many resources are selected to import

migration-all-available-data-label = Import all available data
migration-no-selected-data-label = No data selected for import
migration-selected-data-label = Import selected data

##

migration-select-all-option-label = Select all
migration-bookmarks-option-label = Bookmarks

# Favorites is used for Bookmarks when importing from Internet Explorer or
# Edge, as this is the terminology for bookmarks on those browsers.
migration-favorites-option-label = Favorites

migration-passwords-option-label = Saved passwords
migration-history-option-label = Browsing history
migration-extensions-option-label = Extensions
migration-form-autofill-option-label = Form autofill data
migration-payment-methods-option-label = Payment methods
migration-cookies-option-label = Cookies
migration-session-option-label = Windows and tabs
migration-otherdata-option-label = Other data

migration-passwords-from-file-progress-header = Import passwords file
migration-passwords-from-file-success-header = Passwords imported successfully
migration-passwords-from-file = Checking file for passwords
migration-passwords-new = New passwords
migration-passwords-updated = Existing passwords
migration-passwords-from-file-no-valid-data = The file doesn’t include any valid password data. Pick another file.

migration-passwords-from-file-picker-title = Import Passwords File
# A description for the .csv file format that may be shown as the file type
# filter by the operating system.
migration-passwords-from-file-csv-filter-title =
  { PLATFORM() ->
      [macos] CSV Document
     *[other] CSV File
  }
# A description for the .tsv file format that may be shown as the file type
# filter by the operating system. TSV is short for 'tab separated values'.
migration-passwords-from-file-tsv-filter-title =
  { PLATFORM() ->
      [macos] TSV Document
     *[other] TSV File
  }

# Shown in the migration wizard after importing passwords from a file
# has completed, if new passwords were added.
#
# Variables:
#  $newEntries (Number): the number of new successfully imported passwords
migration-wizard-progress-success-new-passwords =
    { $newEntries ->
        [one] { $newEntries } added
       *[other] { $newEntries } added
    }

# Shown in the migration wizard after importing passwords from a file
# has completed, if existing passwords were updated.
#
# Variables:
#  $updatedEntries (Number): the number of updated passwords
migration-wizard-progress-success-updated-passwords =
    { $updatedEntries ->
        [one] { $updatedEntries } updated
       *[other] { $updatedEntries } updated
    }

migration-bookmarks-from-file-picker-title = Import bookmarks file
migration-bookmarks-from-file-progress-header = Importing bookmarks
migration-bookmarks-from-file = Bookmarks
migration-bookmarks-from-file-success-header = Bookmarks imported successfully
migration-bookmarks-from-file-no-valid-data = The file doesn’t include any bookmark data. Pick another file.

# A description for the .html file format that may be shown as the file type
# filter by the operating system.
migration-bookmarks-from-file-html-filter-title =
  { PLATFORM() ->
      [macos] HTML Document
     *[other] HTML File
  }

# A description for the .json file format that may be shown as the file type
# filter by the operating system.
migration-bookmarks-from-file-json-filter-title = JSON File

# Shown in the migration wizard after importing bookmarks from a file
# has completed.
#
# Variables:
#  $newEntries (Number): the number of imported bookmarks.
migration-wizard-progress-success-new-bookmarks =
    { $newEntries ->
        [one] { $newEntries } bookmark
       *[other] { $newEntries } bookmarks
    }

migration-import-button-label = Import
migration-choose-to-import-from-file-button-label = Import from file
migration-import-from-file-button-label = Select file
migration-cancel-button-label = Cancel
migration-done-button-label = Done
migration-continue-button-label = Continue

migration-wizard-import-browser-no-browsers = { -brand-short-name } couldn’t find any programs that contain bookmark, history or password data.
migration-wizard-import-browser-no-resources = There was an error. { -brand-short-name } can’t find any data to import from that browser profile.

## These strings will be used to create a dynamic list of items that can be
## imported. The list will be created using Intl.ListFormat(), so it will
## follow each locale's rules, and the first item will be capitalized by code.
## When applicable, the resources should be in their plural form.
## For example, a possible list could be "Bookmarks, passwords and autofill data".

migration-list-bookmark-label = bookmarks

# “favorites” refers to bookmarks in Edge and Internet Explorer. Use the same terminology
# if the browser is available in your language.
migration-list-favorites-label = favorites
migration-list-password-label = passwords
migration-list-history-label = history
migration-list-extensions-label = extensions
migration-list-autofill-label = autofill data
migration-list-payment-methods-label = payment methods

##

migration-wizard-progress-header = Importing data

# This header appears in the final page of the migration wizard only if
# all resources were imported successfully.
migration-wizard-progress-done-header = Data imported successfully

# This header appears in the final page of the migration wizard if only
# some of the resources were imported successfully. This is meant to be
# distinct from migration-wizard-progress-done-header, which is only shown
# if all resources were imported successfully.
migration-wizard-progress-done-with-warnings-header = Data import complete

migration-wizard-progress-icon-in-progress =
  .aria-label = Importing…
migration-wizard-progress-icon-completed =
  .aria-label = Completed

migration-safari-password-import-header = Import passwords from Safari
migration-safari-password-import-post-sequoia-step1 = Open the <a data-l10n-name="macos-passwords-app">Passwords app</a> on your Mac
migration-safari-password-import-post-sequoia-step2 = From the menu bar at the top of the screen, choose File > Export All Passwords to File…
migration-safari-password-import-post-sequoia-step3 = Save the file
migration-safari-password-import-post-sequoia-step4 = Select the file you saved

## These instructions only appear and are applicable on versions of macOS before
## Sequoia (macOS 15). They can be removed once importing on pre-Sequoia macOS
## is no longer supported.

migration-safari-password-import-steps-header = To import Safari passwords:
migration-safari-password-import-step1 = In Safari, open “Safari” menu and go to Preferences > Passwords
migration-safari-password-import-step2 = Select the <img data-l10n-name="safari-icon-3dots"/> button and choose “Export All Passwords”
migration-safari-password-import-step3 = Save the passwords file
migration-safari-password-import-step4 = Use “Select file” below to choose the passwords file you saved

##

migration-chrome-windows-password-import-header = How to import passwords from Chrome
migration-chrome-windows-password-import-steps-header = In Chrome:
migration-chrome-windows-password-import-step1 = Open the main menu <img data-l10n-name="chrome-icon-3dots"/> and go to Passwords and Autofill > Google Password Manager.
migration-chrome-windows-password-import-step2 = Select “Settings” from the menu.
migration-chrome-windows-password-import-step3 = Choose “Download file” and save it to your device.
migration-chrome-windows-password-import-step4 = Return here and “Select file” to finish import.

migration-manual-password-import-skip-button = Skip
migration-manual-password-import-cancel-button = Cancel
migration-manual-password-import-select-button = Select file

# Shown in the migration wizard after importing bookmarks from another
# browser has completed.
#
# Variables:
#  $quantity (Number): the number of successfully imported bookmarks
migration-wizard-progress-success-bookmarks =
    { $quantity ->
        [one] { $quantity } bookmark
       *[other] { $quantity } bookmarks
    }

# Shown in the migration wizard after importing bookmarks from either
# Internet Explorer or Edge.
#
# Use the same terminology if the browser is available in your language.
#
# Variables:
#  $quantity (Number): the number of successfully imported bookmarks
migration-wizard-progress-success-favorites =
    { $quantity ->
        [one] { $quantity } favorite
       *[other] { $quantity } favorites
    }

## The import process identifies extensions installed in other supported
## browsers and installs the corresponding (matching) extensions compatible
## with Firefox, if available.

# Shown in the migration wizard after importing all matched extensions
# from supported browsers.
#
# Variables:
#   $quantity (Number): the number of successfully imported extensions
migration-wizard-progress-success-extensions =
    { $quantity ->
        [one] { $quantity } extension
       *[other] { $quantity } extensions
    }

# Shown in the migration wizard after importing a partial amount of
# matched extensions from supported browsers.
#
# Variables:
#   $matched (Number): the number of matched imported extensions
#   $quantity (Number): the number of total extensions found during import
migration-wizard-progress-partial-success-extensions = { $matched } of { $quantity } extensions

migration-wizard-progress-extensions-support-link = Learn how { -brand-product-name } matches extensions
# Shown in the migration wizard if there are no matched extensions
# on import from supported browsers.
migration-wizard-progress-no-matched-extensions = No matching extensions

migration-wizard-progress-extensions-addons-link = Browse extensions for { -brand-short-name }

##

# Shown in the migration wizard after importing passwords from another
# browser has completed.
#
# Variables:
#  $quantity (Number): the number of successfully imported passwords
migration-wizard-progress-success-passwords =
    { $quantity ->
        [one] { $quantity } password
       *[other] { $quantity } passwords
    }

# Shown in the migration wizard after importing history from another
# browser has completed.
#
# Variables:
#  $maxAgeInDays (Number): the maximum number of days of history that might be imported.
migration-wizard-progress-success-history =
    { $maxAgeInDays ->
        [one] From the last day
       *[other] From the last { $maxAgeInDays } days
    }

migration-wizard-progress-success-formdata = Form history

# Shown in the migration wizard after importing payment methods from another
# browser has completed.
#
# Variables:
#  $quantity (Number): the number of successfully imported payment methods
migration-wizard-progress-success-payment-methods =
    { $quantity ->
        [one] { $quantity } payment method
       *[other] { $quantity } payment methods
    }

migration-wizard-safari-permissions-sub-header = To import Safari bookmarks and browsing history:
migration-wizard-safari-instructions-continue = Select “Continue”
migration-wizard-safari-instructions-folder = Select Safari folder in the list and choose “Open”
