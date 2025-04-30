# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

do-not-track-removal = We no longer support the “Do Not Track” signal

global-privacy-control-description =
    .label = Tell websites not to sell or share my data
    .accesskey = s

non-technical-privacy-header = Website Privacy Preferences

# Do not translate.
# "Global Privacy Control" or "GPC" are a web platform feature name and abbreviation
# included to facilitate power-user search of the about:preferences page.
global-privacy-control-search = Global Privacy Control (GPC)

settings-page-title = Settings

# This is used to determine the width of the search field in about:preferences,
# in order to make the entire placeholder string visible
#
# Please keep the placeholder string short to avoid truncation.
#
# Notice: The value of the `.style` attribute is a CSS string, and the `width`
# is the name of the CSS property. It is intended only to adjust the element's width.
# Do not translate.
search-input-box2 =
    .style = width: 15.4em
    .placeholder = Find in Settings

managed-notice = Your browser is being managed by your organization.
managed-notice-info-icon =
    .alt = Information

category-list =
    .aria-label = Categories

pane-general-title = General
category-general =
    .tooltiptext = { pane-general-title }

pane-home-title = Home
category-home =
    .tooltiptext = { pane-home-title }

pane-search-title = Search
category-search =
    .tooltiptext = { pane-search-title }

pane-privacy-title = Privacy & Security
category-privacy =
    .tooltiptext = { pane-privacy-title }

pane-sync-title3 = Sync
category-sync3 =
    .tooltiptext = { pane-sync-title3 }

settings-pane-labs-title = { -firefoxlabs-brand-name }
settings-category-labs =
    .tooltiptext = { -firefoxlabs-brand-name }
pane-experimental-description3 = Give our experimental features a try! They’re in development and evolving, which could impact how { -brand-short-name } works.

pane-experimental-reset =
  .label = Restore Defaults
  .accesskey = R

help-button-label = { -brand-short-name } Support
addons-button-label = Extensions & Themes

focus-search =
    .key = f

close-button =
    .aria-label = Close

## Browser Restart Dialog

feature-enable-requires-restart = { -brand-short-name } must restart to enable this feature.
feature-disable-requires-restart = { -brand-short-name } must restart to disable this feature.
should-restart-title = Restart { -brand-short-name }
should-restart-ok = Restart { -brand-short-name } now
cancel-no-restart-button = Cancel
restart-later = Restart Later

## Extension Control Notifications
##
## These strings are used to inform the user
## about changes made by extensions to browser settings.
##
## <img data-l10n-name="icon"/> is going to be replaced by the extension icon.
##
## Variables:
##   $name (string) - Name of the extension

# This string is shown to notify the user that the password manager setting
# is being controlled by an extension
extension-controlling-password-saving = <img data-l10n-name="icon"/> <strong>{ $name }</strong> controls this setting.

# This string is shown to notify the user that their notifications permission
# is being controlled by an extension.
extension-controlling-web-notifications = <img data-l10n-name="icon"/> <strong>{ $name }</strong> controls this setting.

# This string is shown to notify the user that Container Tabs
# are being enabled by an extension.
extension-controlling-privacy-containers = <img data-l10n-name="icon"/> <strong>{ $name }</strong> requires Container Tabs.

# This string is shown to notify the user that their content blocking "All Detected Trackers"
# preferences are being controlled by an extension.
extension-controlling-websites-content-blocking-all-trackers = <img data-l10n-name="icon"/> <strong>{ $name }</strong> controls this setting.

# This string is shown to notify the user that their proxy configuration preferences
# are being controlled by an extension.
extension-controlling-proxy-config = <img data-l10n-name ="icon"/> <strong>{ $name }</strong> controls how { -brand-short-name } connects to the internet.

# This string is shown after the user disables an extension to notify the user
# how to enable an extension that they disabled.
#
# <img data-l10n-name="addons-icon"/> will be replaced with Add-ons icon
# <img data-l10n-name="menu-icon"/> will be replaced with Menu icon
extension-controlled-enable = To enable the extension go to <img data-l10n-name="addons-icon"/> Add-ons in the <img data-l10n-name="menu-icon"/> menu.

## Preferences UI Search Results

search-results-header = Search Results

# `<span data-l10n-name="query"></span>` will be replaced by the search term.
search-results-empty-message2 =
        Sorry! There are no results in Settings for “<span data-l10n-name="query"></span>”.

search-results-help-link = Need help? Visit <a data-l10n-name="url">{ -brand-short-name } Support</a>

## General Section

startup-header = Startup

always-check-default =
    .label = Always check if { -brand-short-name } is your default browser
    .accesskey = y

is-default = { -brand-short-name } is currently your default browser
is-not-default = { -brand-short-name } is not your default browser

set-as-my-default-browser =
    .label = Make Default…
    .accesskey = D

startup-restore-windows-and-tabs =
    .label = Open previous windows and tabs
    .accesskey = s

windows-launch-on-login =
    .label = Open { -brand-short-name } automatically when your computer starts up
    .accesskey = O
windows-launch-on-login-disabled = This preference has been disabled in Windows. To change, visit <a data-l10n-name="startup-link">Startup Apps</a> in System settings.
windows-launch-on-login-profile-disabled = Enable this preference by checking “{ profile-manager-use-selected.label }” in the “Choose User Profile” window.

disable-extension =
    .label = Disable Extension

preferences-data-migration-header = Import Browser Data
preferences-data-migration-description = Import bookmarks, passwords, history, and autofill data into { -brand-short-name }.
preferences-data-migration-button =
    .label = Import Data
    .accesskey = m

preferences-profiles-header = Profiles
preferences-manage-profiles-description = Each profile has separate browsing data and settings, including history, passwords, and more.
preferences-manage-profiles-learn-more = Learn more
preferences-manage-profiles-button =
  .label = Manage Profiles

tabs-group-header = Tabs

ctrl-tab-recently-used-order =
    .label = Ctrl+Tab cycles through tabs in recently used order
    .accesskey = T

open-new-link-as-tabs =
    .label = Open links in tabs instead of new windows
    .accesskey = w

ask-on-close-multiple-tabs =
    .label = Ask before closing multiple tabs
    .accesskey = m

# This string is used for the confirm before quitting preference.
# Variables:
#   $quitKey (string) - the quit keyboard shortcut, and formatted
#                       in the same manner as it would appear,
#                       for example, in the File menu.
ask-on-quit-with-key =
    .label = Ask before quitting with { $quitKey }
    .accesskey = b

warn-on-open-many-tabs =
    .label = Warn you when opening multiple tabs might slow down { -brand-short-name }
    .accesskey = d

switch-to-new-tabs =
    .label = When you open a link, image or media in a new tab, switch to it immediately
    .accesskey = h

show-tabs-in-taskbar =
    .label = Show tab previews in the Windows taskbar
    .accesskey = k

browser-containers-enabled =
    .label = Enable Container Tabs
    .accesskey = n

browser-containers-learn-more = Learn more

browser-containers-settings =
    .label = Settings…
    .accesskey = i

containers-disable-alert-title = Close All Container Tabs?

## Variables:
##   $tabCount (number) - Number of tabs

containers-disable-alert-desc =
    { $tabCount ->
        [one] If you disable Container Tabs now, { $tabCount } container tab will be closed. Are you sure you want to disable Container Tabs?
       *[other] If you disable Container Tabs now, { $tabCount } container tabs will be closed. Are you sure you want to disable Container Tabs?
    }

containers-disable-alert-ok-button =
    { $tabCount ->
        [one] Close { $tabCount } Container Tab
       *[other] Close { $tabCount } Container Tabs
    }

##

containers-disable-alert-cancel-button = Keep enabled

containers-remove-alert-title = Remove This Container?

# Variables:
#   $count (number) - Number of tabs that will be closed.
containers-remove-alert-msg =
    { $count ->
        [one] If you remove this Container now, { $count } container tab will be closed. Are you sure you want to remove this Container?
       *[other] If you remove this Container now, { $count } container tabs will be closed. Are you sure you want to remove this Container?
    }

containers-remove-ok-button = Remove this Container
containers-remove-cancel-button = Don’t remove this Container

settings-tabs-show-image-in-preview =
    .label = Show an image preview when you hover on a tab
    .accessKey = h


browser-layout-header = Browser Layout

browser-layout-horizontal-tabs =
    .label = Horizontal tabs
browser-layout-horizontal-tabs-desc = Display at top of browser

browser-layout-vertical-tabs =
    .label = Vertical tabs
browser-layout-vertical-tabs-desc = Display on the side, in the sidebar

browser-layout-show-sidebar =
    .label = Show sidebar
browser-layout-show-sidebar-desc = Quickly access bookmarks, tabs from your phone, AI chatbots, and more without leaving your main view.

## General Section - Language & Appearance

language-and-appearance-header = Language and Appearance

preferences-web-appearance-header = Website appearance

preferences-web-appearance-description = Some websites adapt their color scheme based on your preferences. Choose which color scheme you’d like to use for those sites.

preferences-web-appearance-choice-auto = Automatic
preferences-web-appearance-choice-light = Light
preferences-web-appearance-choice-dark = Dark

preferences-web-appearance-choice-tooltip-auto =
  .title = Automatically change website backgrounds and content based on your system settings and { -brand-short-name } theme.
preferences-web-appearance-choice-tooltip-light =
  .title = Use a light appearance for website backgrounds and content.
preferences-web-appearance-choice-tooltip-dark =
  .title = Use a dark appearance for website backgrounds and content.

preferences-web-appearance-choice-input-auto =
  .aria-description = { preferences-web-appearance-choice-tooltip-auto.title }

preferences-web-appearance-choice-input-light =
  .aria-description = { preferences-web-appearance-choice-tooltip-light.title }

preferences-web-appearance-choice-input-dark =
  .aria-description = { preferences-web-appearance-choice-tooltip-dark.title }

# This can appear when using windows HCM or "Override colors: always" without
# system colors.
preferences-web-appearance-override-warning3 =
    .message = Your contrast control settings are overriding website appearance.

# This message contains one link. It can be moved within the sentence as needed
# to adapt to your language, but should not be changed.
preferences-web-appearance-footer = Manage { -brand-short-name } themes in <a data-l10n-name="themes-link">Extensions & Themes</a>

preferences-contrast-control-header = Contrast Control

preferences-contrast-control-description = Websites have a variety of foreground and background colors. Configure { -brand-short-name } to use the same colors across websites for improved readability.

preferences-contrast-control-use-platform-settings =
    .label = Automatic (use system settings)
    .accesskey = A

preferences-contrast-control-off =
    .label = Off
    .accesskey = O

preferences-contrast-control-custom =
    .label = Custom
    .accesskey = C

preferences-colors-manage-button =
    .label = Manage Colors…
    .accesskey = C

preferences-fonts-header = Fonts

default-font = Default font
    .accesskey = D
default-font-size = Size
    .accesskey = S

advanced-fonts =
    .label = Advanced…
    .accesskey = A

# Zoom is a noun, and the message is used as header for a group of options
preferences-zoom-header = Zoom

preferences-default-zoom = Default zoom
    .accesskey = z

# Variables:
#   $percentage (number) - Zoom percentage value
preferences-default-zoom-value =
    .label = { $percentage }%

preferences-zoom-text-only =
    .label = Zoom text only
    .accesskey = t

preferences-text-zoom-override-warning =
    .message = Warning: If you select “Zoom text only” and your default zoom is not set to 100%, it may cause some sites or content to break.

language-header = Language

choose-language-description = Choose your preferred language for displaying pages

choose-button =
    .label = Choose…
    .accesskey = o

choose-browser-language-description = Choose the languages used to display menus, messages, and notifications from { -brand-short-name }.
manage-browser-languages-button =
  .label = Set Alternatives…
  .accesskey = l
confirm-browser-language-change-description = Restart { -brand-short-name } to apply these changes
confirm-browser-language-change-button = Apply and Restart

fx-translate-web-pages = { -translations-brand-name }

translate-exceptions =
    .label = Exceptions…
    .accesskey = x

# Variables:
#    $localeName (string) - Localized name of the locale to be used.
use-system-locale =
   .label = Use your operating system settings for “{ $localeName }” to format dates, times, numbers, and measurements.

check-user-spelling =
    .label = Check your spelling as you type
    .accesskey = t

## General Section - Files and Applications

files-and-applications-title = Files and Applications

download-header = Downloads

download-save-where = Save files to
    .accesskey = v

download-choose-folder =
    .label =
        { PLATFORM() ->
            [macos] Choose…
           *[other] Browse…
        }
    .accesskey =
        { PLATFORM() ->
            [macos] e
           *[other] o
        }

download-always-ask-where =
    .label = Always ask you where to save files
    .accesskey = A

applications-header = Applications

applications-description = Choose how { -brand-short-name } handles the files you download from the web or the applications you use while browsing.

applications-filter =
    .placeholder = Search file types or applications

applications-type-column =
    .label = Content Type
    .accesskey = T

applications-action-column =
    .label = Action
    .accesskey = A

# Variables:
#   $extension (String) - file extension (e.g .TXT)
applications-file-ending = { $extension } file
applications-action-save =
    .label = Save File

# Variables:
#   $app-name (String) - Name of an application (e.g Adobe Acrobat)
applications-use-app =
    .label = Use { $app-name }

# Variables:
#   $app-name (String) - Name of an application (e.g Adobe Acrobat)
applications-use-app-default =
    .label = Use { $app-name } (default)

applications-use-os-default =
    .label =
        { PLATFORM() ->
            [macos] Use macOS default application
            [windows] Use Windows default application
           *[other] Use system default application
        }

applications-use-other =
    .label = Use other…
applications-select-helper = Select Helper Application

applications-manage-app =
    .label = Application Details…
applications-always-ask =
    .label = Always ask

# Variables:
#   $type-description (string) - Description of the type (e.g "Portable Document Format")
#   $type (string) - The MIME type (e.g application/binary)
applications-type-description-with-type = { $type-description } ({ $type })

# Variables:
#   $extension (string) - File extension (e.g .TXT)
#   $type (string) - The MIME type (e.g application/binary)
applications-file-ending-with-type = { applications-file-ending } ({ $type })

applications-open-inapp =
    .label = Open in { -brand-short-name }

## The strings in this group are used to populate
## selected label element based on the string from
## the selected menu item.

applications-action-save-label =
    .value = { applications-action-save.label }

applications-use-app-label =
    .value = { applications-use-app.label }

applications-open-inapp-label =
    .value = { applications-open-inapp.label }

applications-always-ask-label =
    .value = { applications-always-ask.label }

applications-use-app-default-label =
    .value = { applications-use-app-default.label }

applications-use-other-label =
    .value = { applications-use-other.label }

applications-use-os-default-label =
    .value = { applications-use-os-default.label }

##

applications-handle-new-file-types-description = What should { -brand-short-name } do with other files?

applications-save-for-new-types =
    .label = Save files
    .accesskey = S

applications-ask-before-handling =
    .label = Ask whether to open or save files
    .accesskey = A

drm-content-header = Digital Rights Management (DRM) Content

play-drm-content =
    .label = Play DRM-controlled content
    .accesskey = P

play-drm-content-learn-more = Learn more

update-application-title = { -brand-short-name } Updates

update-application-description = Keep { -brand-short-name } up to date for the best performance, stability, and security.

# Variables:
# $version (string) - Firefox version
update-application-version = Version { $version } <a data-l10n-name="learn-more">What’s new</a>

update-history =
    .label = Show Update History…
    .accesskey = p

update-application-allow-description = Allow { -brand-short-name } to

update-application-auto =
    .label = Automatically install updates (recommended)
    .accesskey = A

update-application-check-choose =
    .label = Check for updates but let you choose to install them
    .accesskey = C

update-application-background-enabled =
    .label = When { -brand-short-name } is not running
    .accesskey = W

update-application-warning-cross-user-setting = This setting will apply to all Windows accounts and { -brand-short-name } profiles using this installation of { -brand-short-name }.

update-application-suppress-prompts =
    .label = Show fewer update notification prompts
    .accesskey = n

update-setting-write-failure-title2 = Error saving Update settings

# Variables:
#   $path (string) - Path to the configuration file
# The newlines between the main text and the line containing the path is
# intentional so the path is easier to identify.
update-setting-write-failure-message2 =
    { -brand-short-name } encountered an error and didn’t save this change. Note that changing this update setting requires permission to write to the file below. You or a system administrator may be able resolve the error by granting the Users group full control to this file.

    Could not write to file: { $path }

update-in-progress-title = Update In Progress

update-in-progress-message = Do you want { -brand-short-name } to continue with this update?

update-in-progress-ok-button = &Discard
# Continue is the cancel button so pressing escape or using a platform standard
# method of closing the UI will not discard the update.
update-in-progress-cancel-button = &Continue

## General Section - Performance

performance-title = Performance

performance-use-recommended-settings-checkbox =
    .label = Use recommended performance settings
    .accesskey = U

performance-use-recommended-settings-desc = These settings are tailored to your computer’s hardware and operating system.

performance-settings-learn-more = Learn more

performance-allow-hw-accel =
    .label = Use hardware acceleration when available
    .accesskey = r

performance-limit-content-process-option = Content process limit
    .accesskey = l

performance-limit-content-process-enabled-desc = Additional content processes can improve performance when using multiple tabs, but will also use more memory.
performance-limit-content-process-blocked-desc = Modifying the number of content processes is only possible with multiprocess { -brand-short-name }. <a data-l10n-name="learn-more">Learn how to check if multiprocess is enabled</a>

# Variables:
#   $num (number) - Default value of the `dom.ipc.processCount` pref.
performance-default-content-process-count =
    .label = { $num } (default)

## General Section - Browsing

browsing-title = Browsing

browsing-use-autoscroll =
    .label = Use autoscrolling
    .accesskey = a

browsing-use-smooth-scrolling =
    .label = Use smooth scrolling
    .accesskey = m

browsing-gtk-use-non-overlay-scrollbars =
    .label = Always show scrollbars
    .accesskey = o

browsing-always-underline-links=
    .label = Always underline links
    .accesskey = u

browsing-use-onscreen-keyboard =
    .label = Show a touch keyboard when necessary
    .accesskey = c

browsing-use-cursor-navigation =
    .label = Always use the cursor keys to navigate within pages
    .accesskey = k

browsing-use-full-keyboard-navigation =
    .label = Use the tab key to move focus between form controls and links
    .accesskey = t

browsing-search-on-start-typing =
    .label = Search for text when you start typing
    .accesskey = x

browsing-picture-in-picture-toggle-enabled =
    .label = Enable Picture-in-Picture video controls
    .accesskey = E

browsing-media-control =
    .label = Control media via keyboard, headset, or virtual interface
    .accesskey = v

browsing-cfr-recommendations =
    .label = Recommend extensions as you browse
    .accesskey = R
browsing-cfr-features =
    .label = Recommend features as you browse
    .accesskey = f

## General Section - Proxy

network-settings-title = Network Settings

network-proxy-connection-description = Configure how { -brand-short-name } connects to the internet.

network-proxy-connection-learn-more = Learn more

network-proxy-connection-settings =
    .label = Settings…
    .accesskey = e

## Home Section

home-new-windows-tabs-header = New Windows and Tabs

home-new-windows-tabs-description2 = Choose what you see when you open your homepage, new windows, and new tabs.

## Home Section - Home Page Customization

home-homepage-mode-label = Homepage and new windows

home-newtabs-mode-label = New tabs

home-restore-defaults =
    .label = Restore Defaults
    .accesskey = R

home-mode-choice-default-fx =
    .label = { -firefox-home-brand-name } (Default)

home-mode-choice-custom =
    .label = Custom URLs…

home-mode-choice-blank =
    .label = Blank Page

home-homepage-custom-url =
    .placeholder = Paste a URL…

# This string has a special case for '1' and [other] (default). If necessary for
# your language, you can add {$tabCount} to your translations and use the
# standard CLDR forms, or only use the form for [other] if both strings should
# be identical.
use-current-pages =
    .label =
        { $tabCount ->
            [1] Use Current Page
           *[other] Use Current Pages
        }
    .accesskey = C

choose-bookmark =
    .label = Use Bookmark…
    .accesskey = B

## Home Section - Firefox Home Content Customization

home-prefs-content-header2 = { -firefox-home-brand-name } Content
home-prefs-content-description2 = Choose what content you want on your { -firefox-home-brand-name } screen.

home-prefs-search-header =
    .label = Web Search
home-prefs-shortcuts-header =
    .label = Shortcuts
home-prefs-shortcuts-description = Sites you save or visit
home-prefs-shortcuts-by-option-sponsored =
    .label = Sponsored shortcuts

home-prefs-recommended-by-header-generic =
    .label = Recommended stories
home-prefs-recommended-by-description-generic = Exceptional content curated by the { -brand-product-name } family

##

home-prefs-recommended-by-learn-more = How it works
home-prefs-recommended-by-option-sponsored-stories =
    .label = Sponsored stories
home-prefs-recommended-by-option-recent-saves =
    .label = Show Recent Saves

home-prefs-highlights-option-visited-pages =
    .label = Visited pages
home-prefs-highlights-options-bookmarks =
    .label = Bookmarks
home-prefs-highlights-option-most-recent-download =
    .label = Most recent download

home-prefs-recent-activity-header =
    .label = Recent activity
home-prefs-recent-activity-description = A selection of recent sites and content
home-prefs-weather-header =
  .label = Weather
home-prefs-weather-description = Today’s forecast at a glance
home-prefs-weather-learn-more-link = Learn more

# Variables:
#   $num (number) - Number of rows displayed
home-prefs-sections-rows-option =
    .label =
        { $num ->
            [one] { $num } row
           *[other] { $num } rows
        }

## Search Section

search-engine-default-header = Default Search Engine
search-engine-default-desc-2 = This is your default search engine in the address bar and search bar. You can switch it at any time.
search-engine-default-private-desc-2 = Choose a different default search engine for Private Windows only
search-separate-default-engine =
    .label = Use this search engine in Private Windows
    .accesskey = U

search-suggestions-header = Search Suggestions
search-suggestions-desc = Choose how suggestions from search engines appear.

search-show-suggestions-option =
    .label = Show search suggestions
    .accesskey = S

search-show-suggestions-url-bar-option =
    .label = Show search suggestions in address bar results
    .accesskey = l


# With this option enabled, on the search results page
# the URL will be replaced by the search terms in the address bar.
search-show-search-term-option-2 =
    .label = Show search terms in the address bar on results pages


# This string describes what the user will observe when the system
# prioritizes search suggestions over browsing history in the results
# that extend down from the address bar. In the original English string,
# "ahead" refers to location (appearing most proximate to), not time
# (appearing before).
search-show-suggestions-above-history-option =
    .label = Show search suggestions ahead of browsing history in address bar results

search-show-suggestions-private-windows =
    .label = Show search suggestions in Private Windows

search-suggestions-cant-show = Search suggestions will not be shown in location bar results because you have configured { -brand-short-name } to never remember history.

search-one-click-header2 = Search Shortcuts

search-one-click-desc = Choose the alternative search engines that appear below the address bar and search bar when you start to enter a keyword.

search-choose-engine-column =
    .label = Search Engine
search-choose-keyword-column =
    .label = Keyword

search-restore-default =
    .label = Restore Default Search Engines
    .accesskey = D

search-remove-engine =
    .label = Remove
    .accesskey = R
search-add-engine =
    .label = Add
    .accesskey = A

search-find-more-link = Find more search engines

# This warning is displayed when the chosen keyword is already in use
# ('Duplicate' is an adjective)
search-keyword-warning-title = Duplicate Keyword
# Variables:
#   $name (string) - Name of a search engine.
search-keyword-warning-engine = You have chosen a keyword that is currently in use by “{ $name }”. Please select another.
search-keyword-warning-bookmark = You have chosen a keyword that is currently in use by a bookmark. Please select another.

## Containers Section

containers-back-button2 =
    .aria-label = Back to Settings
containers-header = Container Tabs
containers-add-button =
    .label = Add New Container
    .accesskey = A

containers-new-tab-check =
    .label = Select a container for each new tab
    .accesskey = S

containers-settings-button =
    .label = Settings
containers-remove-button =
    .label = Remove

## Firefox account - Signed out. Note that "Sync" and "Firefox account" are now
## more discrete ("signed in" no longer means "and sync is connected").

sync-signedout-caption = Take Your Web With You
sync-signedout-description2 = Synchronize your bookmarks, history, tabs, passwords, add-ons, and settings across all your devices.

sync-signedout-account-signin3 =
    .label = Sign in to sync…
    .accesskey = i

# This message contains two links and two icon images.
#   `<img data-l10n-name="android-icon"/>` - Android logo icon
#   `<a data-l10n-name="android-link">` - Link to Android Download
#   `<img data-l10n-name="ios-icon">` - iOS logo icon
#   `<a data-l10n-name="ios-link">` - Link to iOS Download
#
# They can be moved within the sentence as needed to adapt
# to your language, but should not be changed or translated.
sync-mobile-promo = Download Firefox for <img data-l10n-name="android-icon"/> <a data-l10n-name="android-link">Android</a> or <img data-l10n-name="ios-icon"/> <a data-l10n-name="ios-link">iOS</a> to sync with your mobile device.

## Firefox account - Signed in

sync-profile-picture-with-alt =
    .tooltiptext = Change profile picture
    .alt = Change profile picture

sync-profile-picture-account-problem =
    .alt = Account profile picture

fxa-login-rejected-warning =
    .alt = Warning

sync-sign-out =
    .label = Sign Out…
    .accesskey = g

sync-manage-account = Manage account
    .accesskey = o

## Variables
## $email (string) - Email used for Firefox account

sync-signedin-unverified = { $email } is not verified.
sync-signedin-login-failure = Please sign in to reconnect { $email }

##

sync-verify-account =
    .label = Verify Account
    .accesskey = V

sync-remove-account =
    .label = Remove Account
    .accesskey = R

sync-sign-in =
    .label = Sign in
    .accesskey = g

## Sync section - enabling or disabling sync.

prefs-syncing-on = Syncing: ON

prefs-syncing-off = Syncing: OFF

prefs-sync-turn-on-syncing =
    .label = Turn on syncing…
    .accesskey = s

prefs-sync-offer-setup-label2 = Synchronize your bookmarks, history, tabs, passwords, add-ons, and settings across all your devices.

prefs-sync-now-button =
    .label = Sync Now
    .accesskey = N

prefs-syncing-button =
    .label = Syncing…

## The list of things currently syncing.

sync-syncing-across-devices-heading = You are syncing these items across all your connected devices:

sync-currently-syncing-bookmarks = Bookmarks
sync-currently-syncing-history = History
sync-currently-syncing-tabs = Open tabs
sync-currently-syncing-passwords = Passwords
sync-currently-syncing-addresses = Addresses
sync-currently-syncing-payment-methods = Payment methods
sync-currently-syncing-addons = Add-ons
sync-currently-syncing-settings = Settings

sync-manage-options =
    .label = Manage sync…
    .accesskey = M

## The "Choose what to sync" dialog.

sync-choose-what-to-sync-dialog4 =
    .title = Manage what syncs on all your connected devices
    .style = min-width: 36em;
    .buttonlabelaccept = Save
    .buttonaccesskeyaccept = S
    .buttonlabelextra2 = Disconnect…
    .buttonaccesskeyextra2 = D

sync-engine-bookmarks =
    .label = Bookmarks
    .accesskey = m

sync-engine-history =
    .label = History
    .accesskey = r

sync-engine-tabs =
    .label = Open tabs
    .tooltiptext = A list of what’s open on all synced devices
    .accesskey = t

sync-engine-passwords =
    .label = Passwords
    .tooltiptext = Passwords you’ve saved
    .accesskey = P

sync-engine-addresses =
    .label = Addresses
    .tooltiptext = Postal addresses you’ve saved (desktop only)
    .accesskey = e

sync-engine-payment-methods2 =
    .label = Payment methods
    .tooltiptext = Names, card numbers, and expiry dates
    .accesskey = n

sync-engine-addons =
    .label = Add-ons
    .tooltiptext = Extensions and themes for Firefox desktop
    .accesskey = A

sync-engine-settings =
    .label = Settings
    .tooltiptext = General, Privacy, and Security settings you’ve changed
    .accesskey = s

## The device name controls.

sync-device-name-header = Device Name

sync-device-name-change =
    .label = Change Device Name…
    .accesskey = h

sync-device-name-cancel =
    .label = Cancel
    .accesskey = n

sync-device-name-save =
    .label = Save
    .accesskey = v

sync-connect-another-device = Connect another device

## Privacy Section

privacy-header = Browser Privacy

## Privacy Section - Passwords

# "Logins" is the former term for "Passwords". Users should find password settings
# by searching for the former term "logins". It's not displayed in the UI.
pane-privacy-passwords-header = Passwords
    .searchkeywords = logins

# Checkbox to control whether UI is shown to users to save or fill logins/passwords.
forms-ask-to-save-passwords =
    .label = Ask to save passwords
    .accesskey = A
forms-exceptions =
    .label = Exceptions…
    .accesskey = x
forms-suggest-passwords =
    .label = Suggest strong passwords
    .accesskey = S
forms-breach-alerts =
    .label = Show alerts about passwords for breached websites
    .accesskey = b
forms-breach-alerts-learn-more-link = Learn more
preferences-relay-integration-checkbox2 =
    .label = Suggest { -relay-brand-name } email masks to protect your email address
    .accesskey = r
relay-integration-learn-more-link = Learn more

# Checkbox which controls filling saved logins into fields automatically when they appear, in some cases without user interaction.
forms-fill-usernames-and-passwords =
    .label = Fill usernames and passwords automatically
    .accesskey = F
forms-saved-passwords =
    .label = Saved passwords
    .accesskey = d
forms-primary-pw-use =
    .label = Use a Primary Password
    .accesskey = U
# This operation requires the user to authenticate with the operating system (device sign-in)
forms-os-reauth =
    .label = Require device sign in to fill and manage passwords
forms-primary-pw-learn-more-link = Learn more
# This string uses the former name of the Primary Password feature
# ("Master Password" in English) so that the preferences can be found
# when searching for the old name. The accesskey is unused.
forms-master-pw-change =
    .label = Change Master Password…
    .accesskey = M
forms-primary-pw-change =
    .label = Change Primary Password…
    .accesskey = P
# Leave this message empty if the translation for "Primary Password" matches
# "Master Password" in your language. If you're editing the FTL file directly,
# use { "" } as the value.
forms-primary-pw-former-name = Formerly known as Master Password

forms-primary-pw-fips-title = You are currently in FIPS mode. FIPS requires a non-empty Primary Password.
forms-master-pw-fips-desc = Password Change Failed
forms-windows-sso =
    .label = Allow Windows single sign-on for Microsoft, work, and school accounts
forms-windows-sso-learn-more-link = Learn more
forms-windows-sso-desc = Manage accounts in your device settings

windows-passkey-settings-label = Manage passkeys in system settings

## OS Authentication dialog

# This message can be seen by trying to add a Primary Password.
primary-password-os-auth-dialog-message-win = To create a Primary Password, enter your Windows login credentials. This helps protect the security of your accounts.

# This message can be seen by trying to add a Primary Password.
# The macOS strings are preceded by the operating system with "Firefox is trying to "
# and includes subtitle of "Enter password for the user "xxx" to allow this." These
# notes are only valid for English. Please test in your locale.
primary-password-os-auth-dialog-message-macosx = create a Primary Password
master-password-os-auth-dialog-caption = { -brand-full-name }

# The macOS string is preceded by the operating system with "Firefox is trying to ".
autofill-creditcard-os-dialog-message = { PLATFORM () ->
    [macos] change the settings for payment methods
    *[other] { -brand-short-name } is trying to change the settings for payment methods. Use your device sign in to allow this.
}
autofill-creditcard-os-auth-dialog-caption = { -brand-full-name }

## Privacy section - Autofill

pane-privacy-autofill-header = Autofill
autofill-addresses-checkbox = Save and fill addresses
    .accesskey = a
autofill-saved-addresses-button = Saved addresses
    .accesskey = S
autofill-payment-methods-checkbox-message = Save and fill payment methods
    .accesskey = m
autofill-payment-methods-checkbox-submessage = Includes credit and debit cards
    .accesskey = I
autofill-saved-payment-methods-button = Saved payment methods
    .accesskey = v

# This operation requires the user to authenticate with the operating system (device sign-in)
autofill-reauth-payment-methods-checkbox = Require device sign in to fill and manage payment methods
    .accesskey = o

## Privacy Section - History

history-header = History

# This label is followed, on the same line, by a dropdown list of options
# (Remember history, etc.).
# In English it visually creates a full sentence, e.g.
# "Firefox will" + "Remember history".
#
# If this doesn't work for your language, you can translate this message:
#   - Simply as "Firefox", moving the verb into each option.
#     This will result in "Firefox" + "Will remember history", etc.
#   - As a stand-alone message, for example "Firefox history settings:".
history-remember-label = { -brand-short-name } will
    .accesskey = w

history-remember-option-all =
    .label = Remember history
history-remember-option-never =
    .label = Never remember history
history-remember-option-custom =
    .label = Use custom settings for history

history-remember-description = { -brand-short-name } will remember your browsing, download, form, and search history.
history-dontremember-description = { -brand-short-name } will use the same settings as private browsing, and will not remember any history as you browse the Web.

history-private-browsing-permanent =
    .label = Always use private browsing mode
    .accesskey = p

history-remember-browser-option =
    .label = Remember browsing and download history
    .accesskey = b

history-remember-search-option =
    .label = Remember search and form history
    .accesskey = f

history-clear-on-close-option =
    .label = Clear history when { -brand-short-name } closes
    .accesskey = r

history-clear-on-close-settings =
    .label = Settings…
    .accesskey = t

history-clear-button =
    .label = Clear History…
    .accesskey = s

## Privacy Section - Site Data

sitedata-header = Cookies and Site Data

sitedata-total-size-calculating = Calculating site data and cache size…

# Variables:
#   $value (number) - Value of the unit (for example: 4.6, 500)
#   $unit (string) - Name of the unit (for example: "bytes", "KB")
sitedata-total-size = Your stored cookies, site data, and cache are currently using { $value } { $unit } of disk space.

sitedata-learn-more = Learn more

sitedata-delete-on-close =
    .label = Delete cookies and site data when { -brand-short-name } is closed
    .accesskey = c

sitedata-delete-on-close-private-browsing2 = Based on your history settings, { -brand-short-name } deletes cookies and site data from your session when you close the browser.

sitedata-option-block-cross-site-trackers =
    .label = Cross-site trackers
sitedata-option-block-cross-site-tracking-cookies =
    .label = Cross-site tracking cookies
sitedata-option-block-cross-site-cookies =
    .label = Cross-site tracking cookies, and isolate other cross-site cookies
sitedata-option-block-unvisited =
    .label = Cookies from unvisited websites
sitedata-option-block-all-cross-site-cookies =
    .label = All cross-site cookies (may cause websites to break)
sitedata-option-block-all =
    .label = All cookies (will cause websites to break)

sitedata-clear =
    .label = Clear Data…
    .accesskey = l

sitedata-settings =
    .label = Manage Data…
    .accesskey = M

sitedata-cookies-exceptions =
    .label = Manage Exceptions…
    .accesskey = x

## Privacy Section - Cookie Banner Blocking

cookie-banner-blocker-header = Cookie Banner Blocker
cookie-banner-blocker-description = When a site asks if they can use cookies in private browsing mode, { -brand-short-name } automatically refuses for you. Only on supported sites.
cookie-banner-learn-more = Learn more
cookie-banner-blocker-checkbox-label =
    .label = Automatically refuse cookie banners

## Privacy Section - Address Bar

addressbar-header = Address Bar

addressbar-suggest = When using the address bar, suggest

# When Firefox Suggest is enabled, this replaces `addressbar-header`.
addressbar-header-firefox-suggest = Address Bar — { -firefox-suggest-brand-name }

# When Firefox Suggest is enabled, this replaces `addressbar-suggest`.
addressbar-suggest-firefox-suggest = Choose the type of suggestions that appear in the address bar.

# When Firefox Suggest is enabled, a "Learn more" link appears at the end of
# `addressbar-suggest-firefox-suggest`.
addressbar-locbar-firefox-suggest-learn-more = Learn more

addressbar-locbar-history-option =
    .label = Browsing history
    .accesskey = h
addressbar-locbar-bookmarks-option =
    .label = Bookmarks
    .accesskey = k
addressbar-locbar-clipboard-option =
    .label = Clipboard
    .accesskey = C
addressbar-locbar-openpage-option =
    .label = Open tabs
    .accesskey = O
# Shortcuts refers to the shortcut tiles on the new tab page, previously known as top sites. Translation should be consistent.
addressbar-locbar-shortcuts-option =
    .label = Shortcuts
    .accesskey = S
addressbar-locbar-topsites-option =
    .label = Top sites
    .accesskey = T
addressbar-locbar-engines-option =
    .label = Search engines
    .accesskey = a
addressbar-locbar-quickactions-option =
    .label = Quick actions
    .accesskey = Q
addressbar-locbar-showrecentsearches-option =
    .label = Show recent searches
    .accesskey = r
addressbar-locbar-showtrendingsuggestions-option =
    .label = Show trending search suggestions
    .accesskey = t

# Nonsponsored suggestions refers to Firefox Suggest suggestions like Wikipedia.
addressbar-locbar-suggest-nonsponsored-option =
  .label = Suggestions from { -brand-short-name }
addressbar-locbar-suggest-nonsponsored-desc = Get suggestions from the web related to your search.

addressbar-locbar-suggest-sponsored-option =
  .label = Suggestions from sponsors
addressbar-locbar-suggest-sponsored-desc = Support { -brand-short-name } with occasional sponsored suggestions.

addressbar-quickactions-learn-more = Learn more

addressbar-dismissed-suggestions-label = Dismissed suggestions
addressbar-restore-dismissed-suggestions-description = Restore dismissed suggestions from sponsors and { -brand-short-name }.
addressbar-restore-dismissed-suggestions-button =
  .label = Restore

## Privacy Section - Content Blocking

content-blocking-enhanced-tracking-protection = Enhanced Tracking Protection

content-blocking-section-top-level-description = Trackers follow you around online to collect information about your browsing habits and interests. { -brand-short-name } blocks many of these trackers and other malicious scripts.

content-blocking-learn-more = Learn more

content-blocking-fpi-incompatibility-warning = You are using First Party Isolation (FPI), which overrides some of { -brand-short-name }’s cookie settings.

# There is no need to translate "Resist Fingerprinting (RFP)". This is a
# feature that can only be enabled via about:config, and it's not exposed to
# standard users (e.g. via Settings).
content-blocking-rfp-incompatibility-warning = You’re using Resist Fingerprinting (RFP), which replaces some of { -brand-short-name }’s fingerprinting protection settings. This might cause some sites to break.

## These strings are used to define the different levels of
## Enhanced Tracking Protection.

# "Standard" in this case is an adjective, meaning "default" or "normal".
enhanced-tracking-protection-setting-standard =
  .label = Standard
  .accesskey = d
enhanced-tracking-protection-setting-strict =
  .label = Strict
  .accesskey = r
enhanced-tracking-protection-setting-custom =
  .label = Custom
  .accesskey = C

##

content-blocking-etp-standard-desc = Balanced for protection and performance. Pages will load normally.
content-blocking-etp-strict-desc = Stronger protection, but may cause some sites or content to break.
content-blocking-etp-custom-desc = Choose which trackers and scripts to block.
content-blocking-etp-blocking-desc = { -brand-short-name } blocks the following:

content-blocking-private-windows = Tracking content in Private Windows
content-blocking-cross-site-cookies-in-all-windows2 = Cross-site cookies in all windows
content-blocking-cross-site-tracking-cookies = Cross-site tracking cookies
content-blocking-all-cross-site-cookies-private-windows = Cross-site cookies in Private Windows
content-blocking-cross-site-tracking-cookies-plus-isolate = Cross-site tracking cookies, and isolate remaining cookies
content-blocking-social-media-trackers = Social media trackers
content-blocking-all-cookies = All cookies
content-blocking-unvisited-cookies = Cookies from unvisited sites
content-blocking-all-windows-tracking-content = Tracking content in all windows
content-blocking-all-cross-site-cookies = All cross-site cookies
content-blocking-cryptominers = Cryptominers
content-blocking-fingerprinters = Fingerprinters
# The known fingerprinters are those that are known for collecting browser fingerprints from user devices. And
# the suspected fingerprinters are those that we are uncertain about browser fingerprinting activities. But they could
# possibly acquire browser fingerprints because of the behavior on accessing APIs that expose browser fingerprints.
content-blocking-known-and-suspected-fingerprinters = Known and suspected fingerprinters

# The tcp-rollout strings are no longer used for the rollout but for tcp-by-default in the standard section

# "Contains" here means "isolates", "limits".
content-blocking-etp-standard-tcp-rollout-description = Total Cookie Protection contains cookies to the site you’re on, so trackers can’t use them to follow you between sites.
content-blocking-etp-standard-tcp-rollout-learn-more = Learn more

content-blocking-etp-standard-tcp-title = Includes Total Cookie Protection, our most powerful privacy feature ever

content-blocking-warning-title = Heads up!
content-blocking-and-isolating-etp-warning-description-2 = This setting may cause some websites to not display content or work correctly. If a site seems broken, you may want to turn off tracking protection for that site to load all content.
content-blocking-warning-learn-how = Learn how

content-blocking-reload-description = You will need to reload your tabs to apply these changes.
content-blocking-reload-tabs-button =
  .label = Reload All Tabs
  .accesskey = R

content-blocking-tracking-content-label =
  .label = Tracking content
  .accesskey = T
content-blocking-tracking-protection-option-all-windows =
  .label = In all windows
  .accesskey = A
content-blocking-option-private =
  .label = Only in private windows
  .accesskey = p
content-blocking-tracking-protection-change-block-list = Change block list

content-blocking-cookies-label =
  .label = Cookies
  .accesskey = C

content-blocking-expand-section =
  .tooltiptext = More information

# Cryptomining refers to using scripts on websites that can use a computer’s resources to mine cryptocurrency without a user’s knowledge.
content-blocking-cryptominers-label =
  .label = Cryptominers
  .accesskey = y

# Browser fingerprinting is a method of tracking users by the configuration and settings information (their "digital fingerprint")
# that is visible to websites they browse, rather than traditional tracking methods such as IP addresses and unique cookies.
#
# The known fingerprinters are those that are known for collecting browser fingerprints from user devices.
content-blocking-known-fingerprinters-label =
  .label = Known fingerprinters
  .accesskey = K

# The suspected fingerprinters are those that we are uncertain about browser fingerprinting activities. But they could
# possibly acquire browser fingerprints because of the behavior on accessing APIs that expose browser fingerprints.
content-blocking-suspected-fingerprinters-label =
  .label = Suspected fingerprinters
  .accesskey = S

## Privacy Section - Tracking

tracking-manage-exceptions =
    .label = Manage Exceptions…
    .accesskey = x

## Privacy Section - Permissions

permissions-header = Permissions

permissions-location = Location
permissions-location-settings =
    .label = Settings…
    .accesskey = t

permissions-xr = Virtual Reality
permissions-xr-settings =
    .label = Settings…
    .accesskey = t

permissions-camera = Camera
permissions-camera-settings =
    .label = Settings…
    .accesskey = t

permissions-microphone = Microphone
permissions-microphone-settings =
    .label = Settings…
    .accesskey = t

# Short form for "the act of choosing sound output devices and redirecting audio to the chosen devices".
permissions-speaker = Speaker Selection
permissions-speaker-settings =
    .label = Settings…
    .accesskey = t

permissions-notification = Notifications
permissions-notification-settings =
    .label = Settings…
    .accesskey = t
permissions-notification-link = Learn more

permissions-notification-pause =
    .label = Pause notifications until { -brand-short-name } restarts
    .accesskey = n

permissions-autoplay = Autoplay

permissions-autoplay-settings =
    .label = Settings…
    .accesskey = t

permissions-block-popups =
    .label = Block pop-up windows
    .accesskey = B

# "popup" is a misspelling that is more popular than the correct spelling of
# "pop-up" so it's included as a search keyword, not displayed in the UI.
permissions-block-popups-exceptions-button =
    .label = Exceptions…
    .accesskey = E
    .searchkeywords = popups

permissions-addon-install-warning =
    .label = Warn you when websites try to install add-ons
    .accesskey = W

permissions-addon-exceptions =
    .label = Exceptions…
    .accesskey = E

## Privacy Section - Data Collection

collection-header2 = { -brand-short-name } Data Collection and Use
    .searchkeywords = telemetry

preferences-collection-description = We strive to provide you with choices and collect only the minimal data necessary to improve { -brand-product-name } for everyone.
preferences-collection-privacy-notice = View Privacy Notice
preferences-across-profiles = These settings apply to every { -brand-product-name } profile on this device.
preferences-view-profiles = View all profiles

collection-health-report-telemetry-disabled = You’re no longer allowing { -vendor-short-name } to capture technical and interaction data. All past data will be deleted within 30 days.
collection-health-report-telemetry-disabled-link = Learn more

collection-usage-ping =
    .label = Send daily usage ping to { -vendor-short-name }
    .accesskey = u
collection-usage-ping-description = This helps { -vendor-short-name } to estimate active users.

collection-health-report2 =
    .label = Send technical and interaction data to { -vendor-short-name }
    .accesskey = r
collection-health-report-link = Learn more
collection-health-report-description = This helps us improve { -brand-product-name } features, performance, and stability.

collection-studies2 =
    .label = Install and run studies
collection-studies-description = Try out features and ideas before they’re released to everyone.
collection-studies-link = View { -brand-short-name } studies

addon-recommendations2 =
    .label = Allow personalized extension recommendations
addon-recommendations-description = Get extension recommendations to improve your browsing experience.

# This message is displayed above disabled data sharing options in developer builds
# or builds with no Telemetry support available.
collection-health-report-disabled2 = Data reporting is disabled for this build configuration.

collection-backlogged-crash-reports2 =
    .label = Automatically send crash reports
    .accesskey = c
collection-backlogged-crash-reports-description = This helps { -vendor-short-name } diagnose and fix issues with the browser. Reports may include personal or sensitive data.

privacy-segmentation-section-header = New features that enhance your browsing

privacy-segmentation-section-description = When we offer features that use your data to give you a more personal experience:

privacy-segmentation-radio-off =
    .label = Use { -brand-product-name } recommendations

privacy-segmentation-radio-on =
    .label = Show detailed information

## Privacy Section - Website Advertising Preferences

website-advertising-header = Website Advertising Preferences

website-advertising-private-attribution =
    .label = Allow websites to perform privacy-preserving ad measurement
    .accesskey = a

website-advertising-private-attribution-description = This helps sites understand how their ads perform without collecting data about you.

## Privacy Section - Security
##
## It is important that wording follows the guidelines outlined on this page:
## https://developers.google.com/safe-browsing/developers_guide_v2#AcceptableUsage

security-header = Security

security-browsing-protection = Deceptive Content and Dangerous Software Protection

security-enable-safe-browsing =
    .label = Block dangerous and deceptive content
    .accesskey = B
security-enable-safe-browsing-link = Learn more

security-block-downloads =
    .label = Block dangerous downloads
    .accesskey = d

security-block-uncommon-software =
    .label = Warn you about unwanted and uncommon software
    .accesskey = c

## Privacy Section - Certificates

certs-header = Certificates

certs-enable-ocsp =
    .label = Query OCSP responder servers to confirm the current validity of certificates
    .accesskey = Q

certs-view =
    .label = View Certificates…
    .accesskey = C

certs-devices =
    .label = Security Devices…
    .accesskey = D

certs-thirdparty-toggle =
    .label = Allow { -brand-short-name } to automatically trust third-party root certificates you install
    .accesskey = t

space-alert-over-5gb-settings-button =
    .label = Open Settings
    .accesskey = O

space-alert-over-5gb-message2 = <strong>{ -brand-short-name } is running out of disk space.</strong> Website contents may not display properly. You can clear stored data in Settings > Privacy & Security > Cookies and Site Data.

space-alert-under-5gb-message2 = <strong>{ -brand-short-name } is running out of disk space.</strong> Website contents may not display properly. Visit “Learn more” to optimize your disk usage for better browsing experience.

## Privacy Section - HTTPS-Only

httpsonly-header = HTTPS-Only Mode

httpsonly-description3 = Only allows secure connections to websites. { -brand-short-name } will ask before connecting insecurely.

httpsonly-learn-more2 = How HTTPS-Only works

httpsonly-radio-enabled =
    .label = Enable HTTPS-Only Mode in all windows

httpsonly-radio-enabled-pbm =
    .label = Enable HTTPS-Only Mode in private windows only

httpsonly-radio-disabled3 =
    .label = Don’t enable HTTPS-Only Mode
    .description = { -brand-short-name } may still upgrade some connections

## DoH Section

preferences-doh-header = DNS over HTTPS

preferences-doh-description2 = Domain Name System (DNS) over HTTPS sends your request for a domain name through an encrypted connection, providing a secure DNS and making it harder for others to see which website you’re about to access.

# Variables:
#   $status (string) - The status of the DoH connection
preferences-doh-status = Status: { $status }
# Variables:
#   $name (string) - The name of the DNS over HTTPS resolver. If a custom resolver is used, the name will be the domain of the URL.
preferences-doh-resolver = Provider: { $name }
# This is displayed instead of $name in preferences-doh-resolver
# when the DoH URL is not a valid URL
preferences-doh-bad-url = Invalid URL
preferences-doh-steering-status = Using local provider

preferences-doh-status-active = Active
preferences-doh-status-disabled = Off
# Variables:
#   $reason (string) - A string representation of the reason DoH is not active. For example NS_ERROR_UNKNOWN_HOST or TRR_RCODE_FAIL.
preferences-doh-status-not-active = Not active ({ $reason })

preferences-doh-group-message2 = Enable DNS over HTTPS using:

preferences-doh-expand-section =
  .tooltiptext = More information

preferences-doh-setting-default =
  .label = Default Protection
  .accesskey = D
preferences-doh-default-desc = { -brand-short-name } decides when to use secure DNS to protect your privacy.
preferences-doh-default-detailed-desc-1 = Use secure DNS in regions where it’s available
preferences-doh-default-detailed-desc-2 = Use your default DNS resolver if there is a problem with the secure DNS provider
preferences-doh-default-detailed-desc-3 = Use a local provider, if possible
preferences-doh-default-detailed-desc-4 = Turn off when VPN, parental control, or enterprise policies are active
preferences-doh-default-detailed-desc-5 = Turn off when a network tells { -brand-short-name } it shouldn’t use secure DNS

preferences-doh-setting-enabled =
  .label = Increased Protection
  .accesskey = I
preferences-doh-enabled-desc = You control when to use secure DNS and choose your provider.
preferences-doh-enabled-detailed-desc-1 = Use the provider you select
preferences-doh-enabled-detailed-desc-2 = Only use your default DNS resolver if there is a problem with secure DNS

preferences-doh-setting-strict =
  .label = Max Protection
  .accesskey = M
preferences-doh-strict-desc = { -brand-short-name } will always use secure DNS. You’ll see a security risk warning before we use your system DNS.
preferences-doh-strict-detailed-desc-1 = Only use the provider you select
preferences-doh-strict-detailed-desc-2 = Always warn if secure DNS isn’t available
preferences-doh-strict-detailed-desc-3 = If secure DNS is not available sites will not load or function properly

preferences-doh-setting-off =
  .label = Off
  .accesskey = O
preferences-doh-off-desc = Use your default DNS resolver

preferences-doh-select-resolver = Choose provider:

preferences-doh-manage-exceptions =
    .label = Manage Exceptions…
    .accesskey = x

## The following strings are used in the Download section of settings

desktop-folder-name = Desktop
downloads-folder-name = Downloads
choose-download-folder-title = Choose Download Folder:
