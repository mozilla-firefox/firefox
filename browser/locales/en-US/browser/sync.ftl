# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

fxa-toolbar-sync-syncing2 = Syncing…

sync-disconnect-dialog-title2 = Disconnect?
sync-disconnect-dialog-body = { -brand-product-name } will stop syncing your account but won’t delete any of your browsing data on this device.
sync-disconnect-dialog-button = Disconnect

fxa-signout-dialog-title2 = Sign out of your account?
fxa-signout-dialog-body = Synced data will remain in your account.
fxa-signout-dialog2-button = Sign out
fxa-signout-dialog2-checkbox = Delete data from this device (passwords, history, bookmarks, etc.)

fxa-menu-sync-settings =
    .label = Sync settings
fxa-menu-turn-on-sync =
    .value = Turn on sync
fxa-menu-turn-on-sync-default = Turn on sync

fxa-menu-connect-another-device =
    .label = Connect another device…
# Variables:
#   $tabCount (Number): The number of tabs sent to the device.
# The following string intentionally omits the word "tab" from the singular and includes it in the plural.
fxa-menu-send-to-device =
    .label =
        { $tabCount ->
            [1] Send to device
           *[other] Send { $tabCount } tabs to device
        }
# Variables:
#   $tabCount (Number): The number of tabs sent to the device.
# The following string intentionally omits the word "tab" from the singular and includes it in the plural.
fxa-menu-send-to-mobile =
    .label =
        { $tabCount ->
            [1] Send to mobile
           *[other] Send { $tabCount } tabs to mobile
        }

## The following strings are for displaying elements in the FxA send tab submenu to prompt users to sign in, enable sync, pair a device, troubleshoot device issues, or verify account.

fxa-menu-send-to-mobile-sign-in = Sign in to send tabs
fxa-menu-send-to-mobile-turn-on-sync = Turn on sync to send tabs
fxa-menu-send-to-mobile-connect-device = Connect a device to send tabs
fxa-menu-send-to-mobile-device-missing2 = Don’t see your device?
fxviewtabrow-send-to-mobile-not-verified = Account not verified
fxviewtabrow-send-to-mobile-verify-account = Verify your account

##

# This is shown dynamically within "Send tab to device" in fxa menu.
fxa-menu-send-tab-to-device-syncnotready =
    .label = Syncing Devices…

# This is shown within "Send tab to device" in fxa menu if account is not configured.
fxa-menu-send-tab-to-device-description = Send a tab instantly to any device you’re signed in on.

fxa-menu-sign-out =
    .label = Sign out…


fxa-menu-sync-description = Access your web anywhere

# Subtitle shown under the account email on the signed-in account button in the
# account menu, indicating that activating it opens account management.
fxa-menu-manage-account-subtitle = Manage Account

# Promo shown in the account menu when the user is signed out and no previously
# signed-in account is remembered, prompting them to sign in and sync.
fxa-menu-sign-in-promo-heading = Sign in to FunComputer
fxa-menu-sign-in-promo-message = Get your data everywhere
fxa-menu-sign-in-promo-button =
    .label = Sign in

# Card shown in the account menu when a previously signed-in account is
# remembered but the user needs to sign in again. Shows the remembered email,
# a reason, and a button to sign back in.
fxa-menu-signed-out-sign-in-button =
    .label = Sign in
# Reason shown when the session expired or credentials are no longer valid.
fxa-menu-signed-out-message-login-failed = You’re signed out
# Reason shown when the remembered account still needs to verify their email.
fxa-menu-signed-out-message-unverified = Finish setup

fxa-avatar-sign-in = Sign in
fxa-avatar-sign-up = Sign up
fxa-avatar-tooltip =
    .tooltiptext = Sign in to your FunComputer account

## Dialog strings that we show the user when signing into Mozilla account/setting up sync

sync-setup-verify-continue = Continue
sync-setup-verify-title = Merge Warning
sync-setup-verify-heading = Are you sure you want to sign in to sync?

##

# The user was previously signed into sync. This dialog confirms to the user
# that they will be merging the data from the previously signed in into the newly signed in one
# Variables:
#   $email - Email address of a user previously signed into sync.
sync-setup-verify-description = A different user was previously signed in to sync on this computer. Signing in will merge this browser’s bookmarks, passwords and other settings with { $email }

## Sync warning strings that support the browser profiles feature, these will be shown when the user might be merging data

# Dialog 1 - different account signing in without option to merge
sync-profile-different-account-title = Account limit reached for this profile
sync-profile-different-account-header = This profile was previously synced to a different account

# Variables:
#   $acctEmail (String) - Email of the account signing into sync.
sync-profile-different-account-description = To keep your data organized and secure, each { -brand-product-name } profile can only be synced to one account. To sign in using { $acctEmail }, create a new profile.

# Dialog 1 - different account signing in with merge option
sync-profile-different-account-title-merge = Profile synced to different account

# Variables:
#   $acctEmail (String) - Email of the account signing into sync.
#   $profileName (String) - Name of the current profile
sync-profile-different-account-description-merge = To keep your data organized and secure, we recommend creating a new profile to sign in using { $acctEmail }. If you choose to continue to sync on this profile, data from both accounts will be permanently merged on “{ $profileName }”.

# Dialog 2 - account signed in on another profile without option to merge
sync-account-in-use-header = Account already in use

# Variables:
#   $acctEmail (String) - Email of the account signing into sync.
#   $otherProfile (String) - Name of the other profile that is associated with the account
sync-account-in-use-header-merge = { $acctEmail } is already signed in to the “{ $otherProfile }” profile
sync-account-in-use-description = You can only associate this account with one profile on this computer.

# Dialog 2 - account signed in on another profile with merge option
sync-account-already-signed-in-header = This account is signed in to another profile. Sync both profiles?

# Variables:
#   $acctEmail (String) - Email of the account signing into sync.
#   $currentProfile (String): Name of the current profile signing in
#   $otherProfile (String): Name of the profile that is already signed in
sync-account-in-use-description-merge = { $acctEmail } is signed in to the “{ $otherProfile }” profile on this computer. Syncing the “{ $currentProfile }” profile will permanently combine data from both profiles, such as passwords and bookmarks.

# Variables:
#   $profileName (String) - Name of the profile to switch to
sync-button-switch-profile = Switch to “{ $profileName }”
sync-button-create-profile = Create a new profile
sync-button-sync-and-merge = Sync and merge data
# Variables:
#   $profileName (String) - Name of the profile to switch to
sync-button-sync-profile = Sync “{ $profileName }”
