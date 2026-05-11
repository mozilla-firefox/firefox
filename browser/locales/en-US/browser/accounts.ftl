# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Used as the FxA toolbar menu item value when user has not
# finished setting up an account.
account-finish-account-setup = Finish Account Setup

# Used as the FxA toolbar menu item title when the user
# needs to reconnect their account.
account-disconnected2 = Account disconnected

# Menu item that sends a tab to all synced devices.
account-send-to-all-devices = Send to all devices

# Menu item that links to the Firefox Accounts settings for connected devices.
account-manage-devices = Manage devices…

## Variables:
##   $email (String): = Email address of user's Firefox Account.

account-reconnect = Reconnect { $email }
account-verify = Verify { $email }

## Displayed in the Send Tab/Page/Link to Device context menu when right clicking a tab, a page or a link.

account-send-to-all-devices-titlecase = Send to All Devices
account-manage-devices-titlecase = Manage Devices…

## Displayed in the Send Tabs context menu when right clicking a tab, a page or a link
## and the Sync account is unverified. Redirects to the Sync preferences page.

account-send-tab-to-device-verify-status = Account Not Verified
account-send-tab-to-device-verify = Verify Your Account…

## These strings are used in a notification shown when a new device joins the Firefox account.

# The title shown in a notification when either this device or another device
# has connected to, or disconnected from, a Firefox account.
account-connection-title-2 = Account

# Variables:
#   $deviceName (String): the name of the new device
account-connection-connected-with = This computer is now connected with { $deviceName }.

# Used when the name of the new device is not known.
account-connection-connected-with-noname = This computer is now connected with a new device.

# Used in a notification shown after a Firefox account is connected to the current device.
account-connection-connected = You have signed in successfully

# Used in a notification shown after the Firefox account was disconnected remotely.
account-connection-disconnected = This computer has been disconnected.

## These strings are used in a notification shown when we're opening
## a single tab another device sent us to display.
## The body for this notification is the URL of the received tab.

account-single-tab-arriving-title = Tab Received
# Variables:
#   $deviceName (String): the device name.
account-single-tab-arriving-from-device-title = Tab from { $deviceName }

# Used when a tab from a remote device arrives but the URL must be truncated.
# Should display the URL with an indication that it's been truncated.
# Variables:
#   $url (String): the portion of the URL that remains after truncation.
account-single-tab-arriving-truncated-url = { $url }…

## These strings are used in a notification shown when we're opening
## multiple tabs another device or devices sent us to display.
## Variables:
##   $tabCount (Number): the number of tabs received

account-multiple-tabs-arriving-title = Tabs Received

# Variables:
#   $deviceName (String): the device name.
account-multiple-tabs-arriving-from-single-device =
    { $tabCount ->
        [one] { $tabCount } tab has arrived from { $deviceName }
       *[other] { $tabCount } tabs have arrived from { $deviceName }
    }
account-multiple-tabs-arriving-from-multiple-devices =
    { $tabCount ->
        [one] { $tabCount } tab has arrived from your connected devices
       *[other] { $tabCount } tabs have arrived from your connected devices
    }
# This version is used when we don't know any device names.
account-multiple-tabs-arriving-from-unknown-device =
    { $tabCount ->
        [one] { $tabCount } tab has arrived
       *[other] { $tabCount } tabs have arrived
    }

## These strings are used in notifications in which the user remotely closed
## tabs from other devices
## Variables:
##   $closedCount (Number): the number of tabs closed

account-view-recently-closed-tabs = View recently closed tabs
account-tabs-closed-remotely =
    {
        $closedCount ->
            [one] { $closedCount } { -brand-short-name } tab closed
           *[other] { $closedCount } { -brand-short-name } tabs closed
    }
