# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## App Menu

appmenuitem-banner-update-downloading =
    .label = Downloading { -brand-shorter-name } update

appmenuitem-banner-update-available =
    .label = Update available — download now

appmenuitem-banner-update-manual =
    .label = Update available — download now

appmenuitem-banner-update-unsupported =
    .label = Unable to update — system incompatible

appmenuitem-banner-update-restart =
    .label = Update available — restart now

appmenu-nova-update-title = Restart to update { -brand-short-name }
appmenu-nova-update-description = Your tabs will reopen.

appmenu-nova-fxa-sign-in = Sign in

appmenu-nova-switch-device-promo =
    .message = Getting a new device soon? Take { -brand-short-name } with you!

appmenu-nova-switch-device-link = How to migrate your data

appmenuitem-new-tab =
    .label = New Tab
appmenuitem-new-window =
    .label = New Window
appmenuitem-new-private-window =
    .label = New Private Window
appmenuitem-history =
  .label = History
appmenuitem-tab-groups =
  .label = Tab groups
appmenuitem-downloads =
  .label = Downloads
appmenuitem-passwords =
    .label = Passwords
appmenuitem-extensions-and-themes =
    .label = Extensions and Themes
appmenuitem-extensions =
    .label = Extensions
appmenuitem-print =
  .label = Print…
appmenuitem-find-in-page =
    .label = Find in Page…
appmenuitem-translate =
    .label = Translate Page…
appmenuitem-zoom =
    .value = Zoom

# This menu item opens the referral page, where the user can invite others to
# use the browser. "Share" here means recommending or referring the browser
appmenuitem-referrals =
    .label = Share { -brand-product-name }
appmenuitem-more-tools =
    .label = More Tools
# Menu combining the previous "Help" menu and report broken site
appmenuitem-help-and-report =
    .label = Help and Report
appmenuitem-exit2 =
    .label =
        { PLATFORM() ->
            [linux] Quit
           *[other] Exit
        }
appmenu-menu-button-closed2 =
    .tooltiptext = Open application menu
    .label = { -brand-short-name }
appmenu-menu-button-opened2 =
    .tooltiptext = Close application menu
    .label = { -brand-short-name }

# Settings is now used to access the browser settings across all platforms,
# instead of Options or Preferences.
appmenuitem-settings =
    .label = Settings

## Zoom and Fullscreen Controls

appmenuitem-zoom-enlarge =
  .label = Zoom In
appmenuitem-zoom-reduce =
  .label = Zoom Out
appmenuitem-fullscreen =
  .label = Full screen

## Firefox Account toolbar button and Sync panel in App menu.

appmenu-remote-tabs-sign-into-sync =
  .label = Sign in to sync…
appmenu-remote-tabs-turn-on-sync =
  .label = Turn on sync…

# This is shown after the tabs list if we can display more tabs by clicking on the button
appmenu-remote-tabs-showmore =
  .label = Show more tabs
  .tooltiptext = Show more tabs from this device

# This is shown as the label for an element to show inactive tabs from this device.
appmenu-remote-tabs-show-inactive-tabs =
  .label = Inactive tabs
  .tooltiptext = See inactive tabs on this device

# This is shown beneath the name of a device when that device has no open tabs
appmenu-remote-tabs-notabs = No open tabs

# This is shown when Sync is configured but syncing tabs is disabled.
appmenu-remote-tabs-tabsnotsyncing = Turn on tab syncing to view a list of tabs from your other devices.

appmenu-remote-tabs-opensettings =
  .label = Settings

# This is shown when Sync is configured but this appears to be the only device attached to
# the account. We also show links to download Firefox for android/ios.
appmenu-remote-tabs-noclients = Want to see your tabs from other devices here?

appmenu-remote-tabs-connectdevice =
  .label = Connect Another Device
appmenu-remote-tabs-welcome = View a list of tabs from your other devices.
appmenu-remote-tabs-unverified = Your account needs to be verified.

appmenuitem-fxa-toolbar-sync-now2 = Sync now
appmenuitem-fxa-sign-in = Sign in to { -brand-product-name }
appmenuitem-fxa-manage-account = Manage account

fxa-menu-sync-status-on = Sync is On

fxa-menu-sync-status-off = Sync is Off

# Shown in place of "Sync is Off" on the sync status button when the user has
# never signed in. Sync is a verb, short for synchronize.
fxa-menu-sync-your-data = Sync Your Data

# Shown as a secondary label under "Sync is Off" when the user is signed in but
# sync is turned off.
fxa-menu-sync-off-data-description = Your data isn’t syncing

# Button shown next to "Sync is Off" when the user is signed in but sync is
# turned off. Turns syncing on by opening sync preferences.
fxa-menu-sync-status-turn-on-button =
    .label = Turn on

# Shown as a secondary label under "Sync is Off" when the user needs to sign in
# (again) for sync to work.
fxa-menu-sync-off-signin-description = Sign in to sync

# Sync is a verb, short for synchronize.
# Variables:
#   $deviceName (String): The name of the local device.
fxa-menu-sync-device-now = Sync { $deviceName } Now

fxa-menu-manage-sync-settings =
    .label = Manage Sync Settings

fxa-menu-add-device =
    .label = Add a Device

fxa-menu-manage-devices =
    .label = Manage Your Devices

fxa-menu-device-missing =
    .label = Don’t See Your Device?

# Mozilla account menu item when selected opens a panel with all devices synced to the user's account
fxa-menu-all-devices =
    .label = All Devices

# Mozilla account panel title which shows all devices synced to a user's account
fxa-menu-all-devices-panel =
    .title = All Devices

fxa-menu-get-firefox-mobile =
    .label = Get { -brand-product-name } for Android or iOS

fxa-menu-secure-sync-subpanel =
    .title = Secure sync
appmenu-account-header = Account
# Variables
# $time (string) - Localized relative time since last sync (e.g. 1 second ago,
# 3 hours ago, etc.)
appmenu-fxa-last-sync = Last synced { $time }
    .label = Last synced { $time }
appmenu-fxa-sync-and-save-data2 = Sync and Save Data
appmenu-fxa-signed-in-label = Sign In

# Sign-in promo shown in the app menu when signed out and no menu message or
# update banner is present, prompting the user to sign in and sync.
appmenu-fxa-sign-in-promo-heading = Sign in to sync
appmenu-fxa-sign-in-promo-message = Get your data everywhere
appmenu-fxa-sign-in-promo-button =
    .label = Sign in
appmenu-fxa-setup-sync =
    .label = Turn On Syncing…
appmenu-fxa-setup-sync-new = Turn On
appmenuitem-save-page =
    .label = Save Page As…

appmenuitem-fxa-sync-off-title = Sync is off
appmenuitem-fxa-sync-off-description = Protect and access your bookmarks, passwords, and more anywhere.

## The Firefox Profiler – The popup is the UI to turn on the profiler, and record
## performance profiles. To enable it go to profiler.firefox.com and click
## "Enable Profiler Menu Button".

profiler-button-dropmarker =
    .label = Open the profiler panel
    .tooltiptext = Open the profiler panel

profiler-popup-button-idle =
  .label = Profiler
  .tooltiptext = Record a performance profile

profiler-popup-button-recording =
  .label = Profiler
  .tooltiptext = The profiler is recording a profile

profiler-popup-button-capturing =
  .label = Profiler
  .tooltiptext = The profiler is capturing a profile

profiler-popup-header-text = { -profiler-brand-name }

profiler-popup-reveal-description-button =
  .aria-label = Reveal more information

profiler-popup-description-title =
  .value = Record, analyze, share

profiler-popup-description =
  Collaborate on performance issues by publishing profiles to share with your team.

profiler-popup-learn-more-button =
  .label = Learn more

profiler-popup-settings =
  .value = Settings

# This link takes the user to about:profiling, and is only visible with the Custom preset.
profiler-popup-edit-settings-button =
  .label = Edit Settings…

profiler-popup-recording-screen = Recording…

profiler-popup-start-recording-button =
  .label = Start Recording

profiler-popup-discard-button =
  .label = Discard

profiler-popup-capture-button =
  .label = Capture

profiler-popup-start-shortcut =
  { PLATFORM() ->
      [macos] ⌃⇧1
     *[other] Ctrl+Shift+1
  }

profiler-popup-capture-shortcut =
  { PLATFORM() ->
      [macos] ⌃⇧2
     *[other] Ctrl+Shift+2
  }

## Profiler presets
## They are shown in the popup's select box.

# Presets and their l10n IDs are defined in the file
# devtools/client/performance-new/shared/background.sys.mjs
# Please take care that the same values are also defined in devtools' perftools.ftl.

profiler-popup-presets-web-developer-description = Recommended preset for most web app debugging, with low overhead.
profiler-popup-presets-web-developer-label =
  .label = Web Developer

profiler-popup-presets-firefox-description = Recommended preset for profiling { -brand-shorter-name }.
profiler-popup-presets-firefox-label =
  .label = { -brand-shorter-name }

profiler-popup-presets-graphics-description = Preset for investigating graphics bugs in { -brand-shorter-name }.
profiler-popup-presets-graphics-label =
  .label = Graphics

profiler-popup-presets-media-description2 = Preset for investigating audio and video bugs in { -brand-shorter-name }.
profiler-popup-presets-media-label =
  .label = Media

profiler-popup-presets-ml-description = Preset for investigating machine learning bugs in { -brand-shorter-name }.
profiler-popup-presets-ml-label =
  .label = Machine Learning

profiler-popup-presets-networking-description = Preset for investigating networking bugs in { -brand-shorter-name }.
profiler-popup-presets-networking-label =
  .label = Networking

profiler-popup-presets-networking-with-logs-description = Preset for investigating networking bugs in { -brand-shorter-name }, including networking logs. These logs may contain sensitive information such as the URLs you visit.
profiler-popup-presets-networking-with-logs-label =
  .label = Networking with Logs

profiler-popup-presets-power-description = Preset for investigating power use bugs in { -brand-shorter-name }, with low overhead.
# "Power" is used in the sense of energy (electricity used by the computer).
profiler-popup-presets-power-label =
  .label = Power

profiler-popup-presets-debug-description = Preset for debugging in { -brand-shorter-name }. High overhead, do not use for performance work but use for focusing on understanding browser behavior.
profiler-popup-presets-debug-label =
  .label = Debug

profiler-popup-presets-web-compat-description = Recommended preset for debugging web compatibility issues in websites, rather than tracking performance.
profiler-popup-presets-web-compat-label =
  .label = Web Compat

profiler-popup-presets-custom-label =
  .label = Custom

## History panel

appmenu-manage-history =
    .label = Manage history

## Sync promo shown at the bottom of the History and Bookmarks panels. Its
## variant depends on the user's account and Sync state. The sign-in heading and
## all three call-to-action labels are shared by both panels; the remaining
## headings are specific to the panel they appear in.

appmenu-sync-promo-signin =
    .heading = Stay in sync across devices
appmenu-sync-promo-signin-cta = Sign in
appmenu-sync-promo-turnonsync =
    .heading = Sync your tabs and history
appmenu-sync-promo-turnonsync-cta = Turn on sync
# This refers to getting tabs from your mobile device via sync
appmenu-sync-promo-connectdevice =
    .heading = Grab your mobile tabs
appmenu-sync-promo-connectdevice-cta = Connect a device
appmenu-bookmarks-sync-promo-turnonsync =
    .heading = Bring your bookmarks with you
# This refers to having your bookmarks available on your mobile device via sync
appmenu-bookmarks-sync-promo-connectdevice =
    .heading = Take your bookmarks to go

##

appmenu-restore-session =
    .label = Restore previous session
appmenu-clear-history =
    .label = Clear recent history…
appmenu-recent-history-subheader = Recent history
appmenu-recently-closed-tabs =
    .label = Recently closed tabs
appmenu-recently-closed-windows =
    .label = Recently closed windows
# This allows to search through the browser's history.
appmenu-search-history =
    .label = Search history

## Help panel

# Menu header for combination of "Help" menu and report broken site
appmenu-help-and-report-header =
    .title = Help and Report
appmenu-about =
    .label = About { -brand-shorter-name }
    .accesskey = A
# This menu item opens the referral page, where the user can invite others to
# use the browser. "Share" here means recommending or referring the browser
appmenu-referrals2 =
    .label = Share { -brand-product-name }
    .accesskey = r
appmenu-get-help =
    .label = Get help
    .accesskey = h
appmenu-help-more-troubleshooting-info =
    .label = More troubleshooting information
    .accesskey = t
appmenu-help-share-ideas =
    .label = Share ideas and feedback…
    .accesskey = S
appmenu-help-switch-device =
    .label = Switching to a new device

## appmenu-help-enter-troubleshoot-mode and appmenu-help-exit-troubleshoot-mode
## are mutually exclusive, so it's possible to use the same accesskey for both.

appmenu-help-enter-troubleshoot-mode2 =
    .label = Troubleshoot Mode…
    .accesskey = M
appmenu-help-exit-troubleshoot-mode =
    .label = Turn Troubleshoot Mode off
    .accesskey = M

## appmenu-help-report-deceptive-site and appmenu-help-not-deceptive
## are mutually exclusive, so it's possible to use the same accesskey for both.

appmenu-help-report-deceptive-site =
    .label = Report deceptive site…
    .accesskey = d
appmenu-help-not-deceptive =
    .label = This isn’t a deceptive site…
    .accesskey = d

## More Tools

appmenu-customizetoolbar =
    .label = Customize toolbar…
appmenu-abouttranslations =
    .label = Translate…
appmenu-edit-pdf =
    .label = Edit PDF…

appmenu-developer-tools-subheader = Browser tools
appmenu-developer-tools-extensions =
    .label = Extensions for developers
appmenuitem-report-broken-site =
  .label = Report Broken Site

## Panel for privacy and security products

appmenuitem-sign-in-account = Sign in to your account

appmenuitem-monitor-title2 = Stay Ahead of Identity Theft
appmenuitem-monitor-description2 = Get alerts about data breaches
# Shown in place of appmenuitem-monitor-title2 when the user has signed up for Monitor.
appmenuitem-monitor-title-signed-in = { -monitor-brand-short-name } Data Breach Alerts
appmenuitem-relay-title2 = Keep Your Email Private
appmenuitem-relay-description2 = Helps prevent spam in your inbox
# Shown in place of appmenuitem-relay-title2 when the user has signed up for Relay.
appmenuitem-relay-title-signed-in = View Email Masks
appmenuitem-vpn-title2 = Hide Your Location with { -mozilla-vpn-brand-name }
appmenuitem-vpn-description5 = Get extra protection across devices
# Shown in place of appmenuitem-vpn-title2 when the user has signed up for VPN.
appmenuitem-vpn-title-signed-in1 = Download { -mozilla-vpn-brand-name }

# "Mozilla" is intentionally hardcoded to prevent forks from replacing it
# with their own vendor name, since these tools are created and maintained by
# Mozilla.
appmenu-other-protection-header3 = Privacy tools

## Profiles panel

appmenu-profiles-header = Profiles
appmenu-all-profiles =
    .label = All Profiles
appmenu-secure-sync-header = Secure sync

# Panel shown when clicking a synced device in the Mozilla account menu.
# The .title attribute sets the panel header text.
fxa-menu-device-recent-tabs-panel =
    .title = Recent tabs

# Button at the bottom of the per-device recent tabs subpanel.
# Variables:
#   $tabCount (Number): Total number of synced tabs on the device.
fxa-menu-device-view-all-synced-tabs =
    .label = { $tabCount ->
        [one] View { $tabCount } Synced Tab
       *[other] View All { $tabCount } Synced Tabs
    }

# Button in the recent tabs sub-panel that sends the current page to the synced device.
fxa-menu-device-send-current-page =
    .label = Send Current Page to This Device

appmenu-profiles-2 =
    .label = Profiles
appmenu-other-profiles = Other profiles
appmenu-manage-profiles =
    .label = Manage Profiles
appmenu-copy-profile =
    .label = Copy This Profile
appmenu-create-profile2 =
    .label = Create a New Profile
appmenu-edit-profile =
    .aria-label = Edit profile
appmenu-edit-this-profile =
    .label = Edit This Profile
appmenu-profile-current-in-use = Current profile in use

fxa-menu-create-profile-subpanel =
    .title = Create a new profile

fxa-menu-create-profile-heading = Level up your browsing with a new profile

fxa-menu-create-profile-description = Keep your bookmarks, passwords, and history separate for work and personal browsing.

fxa-menu-create-profile-confirm =
    .label = Create new profile

fxa-menu-create-profile-learn-more =
    .label = What Are Profiles?

# Button in the account menu that links to the Referrals page
appmenuitem-share-firefox-title2 = Share { -brand-product-name }
appmenuitem-share-firefox-description = Invite someone to choose the browser that puts privacy first
