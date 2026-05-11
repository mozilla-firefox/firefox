# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

do-not-track-removal2 =
    .label = We no longer support the “Do Not Track” signal

global-privacy-control-description =
    .label = Tell websites not to sell or share my data
    .accesskey = s

non-technical-privacy-group =
    .label = Website Privacy Preferences

do-not-track-removal3 =
    .message = We no longer support the “Do Not Track” feature.

non-technical-privacy-heading =
    .label = Additional protections

preferences-privacy-relay-available =
    .label = Suggest { -relay-brand-name } email masks
    .description = Hides your real email address to protect your inbox from spam.


# Do not translate.
# "Global Privacy Control" or "GPC" are a web platform feature name and abbreviation
# included to facilitate power-user search of the about:preferences page.
global-privacy-control-search = Global Privacy Control (GPC)

settings-page-title = Settings

category-nav-heading =
    .heading = Settings

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

managed-notice-nav =
    .label = Your browser is being managed by your organization.

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

pane-privacy-title2 = Privacy and security
pane-privacy-section =
    .heading = Privacy and security

pane-sync-title3 = Sync
category-sync3 =
    .tooltiptext = { pane-sync-title3 }

pane-ai-controls-title = AI controls
category-ai-controls =
    .tooltiptext = { pane-ai-controls-title }

pane-about-firefox-title = About { -brand-short-name }
  .title = { pane-about-firefox-title }

pane-appearance-title = Appearance
  .title = { pane-appearance-title }

pane-downloads-title = Downloads
pane-downloads =
    .heading = { pane-downloads-title }

pane-accessibility-title = Accessibility
  .title = { pane-accessibility-title }

pane-languages-title = Languages
category-languages =
    .tooltiptext = { pane-languages-title }

preferences-languages-header =
    .heading = { pane-languages-title }

settings-pane-labs-title = { -firefoxlabs-brand-name }
settings-pane-labs-header =
    .heading = { -firefoxlabs-brand-name }
settings-category-labs =
    .tooltiptext = { -firefoxlabs-brand-name }
pane-experimental-description4 = Give our experimental features a try! They’re in development and evolving, which could impact how { -brand-short-name } works. We only receive data about your use of these features if you have <a data-l10n-name="data-collection">technical and interaction data</a> turned on.

pane-experimental-reset =
  .label = Restore Defaults
  .accesskey = R

help-button-label = { -brand-short-name } support
addons-button-label = Extensions and themes

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

extension-controlled-enable-2 = To re-enable this extension visit <a data-l10n-name="addons-link">Extensions and themes</a>.
# This string is shown to notify the user that their home page or new tab preferences
# are being controlled by an extension.
extension-controlling-homepage = { $name } controls some of your homepage settings.

## Preferences UI Search Results

search-results-header = Search Results

# `<span data-l10n-name="query"></span>` will be replaced by the search term.
search-results-empty-message2 =
        Sorry! There are no results in Settings for “<span data-l10n-name="query"></span>”.

search-results-help-link = Need help? Visit <a data-l10n-name="url">{ -brand-short-name } Support</a>

## General Section

startup-group =
    .label = Startup

always-check-default =
    .label = Always check if { -brand-short-name } is your default browser
    .accesskey = y

startup-restore-windows-and-tabs =
    .label = Open previous windows and tabs
    .accesskey = s
startup-windows-launch-on-login-profile-disabled =
    .message = Enable this preference by checking “{ profile-manager-use-selected.label }” in the “Choose User Profile” window.

windows-launch-on-login =
    .label = Open { -brand-short-name } automatically when your computer starts up
    .accesskey = O
windows-launch-on-login-disabled = This preference has been disabled in Windows. To change, visit <a data-l10n-name="startup-link">Startup Apps</a> in System settings.

disable-extension =
    .label = Disable Extension

preferences-data-migration-group =
    .label = Import browser data
    .description = Bring your bookmarks, passwords, history, extensions, and autofill data from another browser.
preferences-data-migration-button =
    .label = Import data
    .accesskey = m

preferences-profiles-group-header =
    .heading = Profiles
preferences-profiles-subpane-description =
    .description = Each profile has separate browsing data and settings, including history, passwords, and more.
preferences-profiles-section-header =
    .label = Profiles
    .description = Each profile has separate browsing data and settings, including history, passwords, and more.
preferences-manage-profiles-button =
  .label = Manage Profiles
preferences-profiles-settings-button =
  .label = Settings
# This string labels the entire copy profile section in the profiles sub-pane.
preferences-copy-profile-header =
    .label = Copy an existing profile
    .description = The new profile will copy your settings, add-ons, history, and saved data like bookmarks and passwords — but not your account or sync info.
# This string sits next to the copy controls, both the copy-profile-select
# drop-down and the copy-profile-button, so that the user understands they
# need to first pick a profile to copy, and then click the copy button.
preferences-profile-to-copy =
  .label = Profile to copy
# This string is a placeholder that will be shown in a drop-down list of
# profiles. The user will select a profile, then click the copy button
# to make a copy of that profile.
preferences-copy-profile-select = Select profile
preferences-copy-profile-button = Copy

tabs-browsing-section =
  .heading = Tabs and browsing

pane-tabs-browsing-title = Tabs and browsing
  .title = { pane-tabs-browsing-title }

tabs-group-header2 =
  .label = Tabs

tabs-opening-heading =
  .label = Opening

tabs-interaction-heading =
  .label = Interaction

tabs-containers-heading =
  .label = Containers

tabs-closing-heading =
  .label = Closing

ctrl-tab-recently-used-order =
    .label = Ctrl+Tab cycles through tabs in recently used order
    .accesskey = T

open-new-link-as-tabs =
    .label = Open links in tabs instead of new windows
    .accesskey = w

open-external-link-next-to-active-tab =
    .label = Open links from apps next to your active tab

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

switch-to-new-tabs-2 =
    .label = When opening links or media in a new tab, switch to it immediately
    .accesskey = h

show-tabs-in-taskbar =
    .label = Show tab previews in the Windows taskbar
    .accesskey = k

browser-containers-enabled-2 =
    .label = Use Container Tabs
    .accesskey = n

browser-containers-learn-more = Learn more

browser-containers-settings-2 =
    .label = Manage settings
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

settings-tabs-drag-to-create-tab-groups =
    .label = Drag tabs together to create tab groups

browser-layout-header2 =
    .label = Browser Layout

browser-layout-horizontal-tabs2 =
    .label = Horizontal tabs
    .title = Tabs at the top
    .description = Tabs at the top

browser-layout-vertical-tabs2 =
    .label = Vertical tabs
    .title = Tabs on the side, in the sidebar
    .description = Tabs on the side, in the sidebar

browser-layout-show-sidebar2 =
    .label = Show sidebar
    .description = Quickly access bookmarks, tabs from your phone, AI chatbots, and more without leaving your main view.

page-navigation-group =
    .label = Page navigation

## General Section - Language & Appearance

language-and-appearance-header = Language and Appearance

appearance-group2 =
  .label = Website appearance
  .description = Some websites change their colors to match your preferences. Choose your color scheme.

preferences-web-appearance-choice-auto3 =
  .label = System
  .title = Automatically change website backgrounds and content based on your system settings and { -brand-short-name } theme.
preferences-web-appearance-choice-light2 =
  .label = Light
  .title = Use a light appearance for website backgrounds and content.
preferences-web-appearance-choice-dark2 =
  .label = Dark
  .title = Use a dark appearance for website backgrounds and content.

web-appearance-group =
  .aria-label = Website appearance

# This can appear when using windows HCM or "Override colors: always" without
# system colors.
preferences-web-appearance-override-warning3 =
    .message = Your contrast control settings are overriding website appearance.

preferences-web-appearance-link =
    .label = Manage { -brand-short-name } themes in Extensions & Themes

preferences-contrast-control-group =
    .label = Website contrast
    .description = Websites use a variety of foreground and background colors. For consistent contrast, you can use the same colors across websites.
preferences-contrast-control-radio-group =
    .label = Override colors

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

preferences-fonts-header2 =
  .label = Fonts

preferences-default-zoom-label =
    .label = Default zoom
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

website-language-heading =
  .label = Website language
  .description = Some web pages are displayed in multiple languages. Choose languages in your preferred order.

website-preferred-language =
  .label = Preferred languages

website-add-language =
  .label = Add language

website-add-language-button =
  .aria-label = Add selected language
  .title = Add selected language

# The pattern used to generate strings presented to the user in the
# website languages selection list.
#
# Example:
#   Icelandic
#   Spanish (Chile)
#
# Variables:
#   $locale (String) - A name of the locale (for example: "Icelandic", "Spanish (Chile)")
website-remove-language-button =
  .aria-label = Remove { $locale }
  .title = Remove { $locale }

choose-button =
    .label = Choose…
    .accesskey = o

choose-browser-language-description = Choose the languages used to display menus, messages, and notifications from { -brand-short-name }.
manage-browser-languages-button =
  .label = Set Alternatives…
  .accesskey = l
confirm-browser-language-change-description = Restart { -brand-short-name } to apply these changes
confirm-browser-language-change-button = Apply and Restart

browser-language-heading =
  .label = Browser language
  .description = Choose the language used to display menus, messages, and notifications from { -brand-short-name }.
browser-language-preferred-label =
  .label = Preferred language
browser-language-fallback-label =
  .label = Fallback language
  .description = Used when preferred language localization is incomplete.
browser-language-install-error =
  .message = { -brand-short-name } can’t update your languages right now. Check that you are connected to the internet or try again.

fx-translate-web-pages = { -translations-brand-name }

translate-exceptions =
    .label = Exceptions…
    .accesskey = x

settings-translations-header =
    .label = Translations
    .aria-label = Translations
    .description = Translate pages or selected text. To protect your privacy, translations stay on your device.

settings-translations-offer-to-translate-label =
    .label = Offer full page translation

settings-translations-more-settings-button =
    .label = More translation settings
    .description = Set preferences for languages, websites, and offline translation.

settings-translations-subpage-header =
    .heading = More translation settings

settings-translations-subpage-speed-up-translation-header =
    .label = Speed up translation
    .description = Download complete languages for faster translations and to translate offline.

settings-translations-subpage-automatic-translation-header =
    .label = Automatic translation

settings-translations-subpage-always-translate-header =
    .label = Always translate these languages

settings-translations-subpage-never-translate-header =
    .label = Never translate these languages

settings-translations-subpage-never-translate-sites-header =
    .label = Never translate these sites

# The icon placeholders show the translations button and the settings gear in the urlbar panel.
settings-translations-subpage-never-translate-sites-description =
    To add a site, open the <img data-l10n-name="translations-icon"/> translation panel, select <img data-l10n-name="settings-icon"/> translation settings, then choose “Never translate this site”

settings-translations-subpage-language-select-option =
    .label = Add language

settings-translations-subpage-language-add-button =
    .aria-label = Add language
    .title = Add language

settings-translations-subpage-download-languages-header =
    .label = Download languages

settings-translations-subpage-download-languages-select-option =
    .label = Select language

settings-translations-subpage-download-languages-button =
    .aria-label = Download language
    .title = Download language

# Variables:
#   $language (string) - Localized name of the language to download.
#   $size (string) - Download size in megabytes, formatted for the locale.
settings-translations-subpage-download-language-option = { $language } ({ $size }MB)
    .label = { $language } ({ $size }MB)

settings-translations-subpage-no-languages-downloaded =
    .label = No languages downloaded

settings-translations-subpage-no-languages-added =
    .label = No languages added

settings-translations-subpage-download-progress = Download in progress…

# Variables:
#   $language (string) - The localized display name of the language.
#   $size (string) - The download size of the language in megabytes.
settings-translations-subpage-download-error = Couldn’t download { $language } ({ $size }MB)

settings-translations-subpage-download-retry-button =
    .label = Try again

# Variables:
#   $language (string) - The localized display name of the language.
#   $size (string) - The download size of the language in megabytes.
settings-translations-subpage-download-delete-confirm = Delete { $language } ({ $size }MB)?

settings-translations-subpage-download-delete-button =
    .label = Delete

settings-translations-subpage-download-cancel-button =
    .label = Cancel

settings-translations-subpage-no-sites-added =
    .label = No sites added

# Variables:
#    $localeName (string) - Localized name of the locale to be used.
use-system-locale =
   .label = Use your operating system settings for “{ $localeName }” to format dates, times, numbers, and measurements.

settings-spellcheck-header =
    .label = Spell check

check-user-spelling =
    .label = Check your spelling as you type
    .accesskey = t

spellcheck-download-dictionaries =
    .label = Download dictionaries

spellcheck-promo =
    .heading = How to use spell checking
    .message = Right-click a text field to turn spell check on or off or to change the language. Not all fields support spell check.

## General Section - Files and Applications

files-and-applications-title = Files and Applications

download-save-files-header =
    .label = Save files to

download-save-where-3 =
    .aria-label = Save files to

download-always-ask-where2 =
    .label = Ask where to save files before downloading
    .accesskey = A
download-private-browsing-delete2 =
    .label = Delete private window downloads on close
    .accesskey = D

applications-header = Applications

applications-description = Choose how { -brand-short-name } handles the files you download from the web or the applications you use while browsing.

applications-setting2 =
    .label = Files and applications
    .description = Choose how { -brand-short-name } handles downloaded files and content.

applications-filter =
    .placeholder = Search file types or applications

applications-type-column =
    .label = Content Type
    .accesskey = T

applications-type-heading = Content Type

applications-action-column =
    .label = Action
    .accesskey = A

applications-action-heading = Action

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

applications-setting-new-file-types =
     .label = What should { -brand-short-name } do with other files?

applications-save-for-new-types =
    .label = Save files
    .accesskey = S
applications-save-for-new-types2 =
    .label = Automatically save files
    .accesskey = S

applications-ask-before-handling =
    .label = Ask whether to open or save files
    .accesskey = A
applications-ask-before-handling2 =
    .label = Ask to open or save files
    .accesskey = A

drm-group =
  .label = Digital Rights Management (DRM) Content

play-drm-content =
    .label = Play DRM-controlled content
    .accesskey = P

play-drm-content-learn-more = Learn more

## About Firefox

about-firefox-header =
    .heading = About { -brand-short-name }

## Firefox updates

update-application-heading =
    .label = { -brand-short-name } updates
    .description = Updates improve { -brand-short-name }’s speed, stability, and security.

update-application-suppress-prompts-heading =
    .label = Notifications

update-application-updates-managed-by-os =
    .message = Updates are managed by your operating system

# Variables:
# $version (string) - Firefox version
update-application-version = Version { $version } <a data-l10n-name="learn-more">What’s new</a>

update-history-2 =
    .label = Show update history
    .accesskey = p

update-application-installation =
    .label = Installation

update-application-auto =
    .label = Automatically install updates (recommended)
    .accesskey = A

update-application-check-choose-2 =
    .label = Check for updates, but choose when to install
    .accesskey = C

update-application-background-enabled =
    .label = When { -brand-short-name } is not running
    .accesskey = W

update-application-warning-cross-user-setting-2 =
    .message = This setting will apply to all Windows accounts and { -brand-short-name } profiles using this installation of { -brand-short-name }.

update-application-suppress-prompts-2 =
    .label = Show fewer update reminders
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

## Firefox support

support-application-heading =
    .label = { -brand-short-name } support
    .description = Troubleshoot issues or share ideas with the community.

support-get-help =
    .label = Get help

support-share-ideas =
    .label = Share ideas and feedback

## General Section - Performance

performance-group =
  .label = Performance

performance-use-recommended-settings-checkbox-2 =
    .label = Use recommended performance settings
    .description = These settings are tailored to your hardware and operating system.
    .accesskey = U

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

browsing-group =
  .label = Browsing

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

settings-media-group =
    .label = Media

browsing-picture-in-picture-toggle-enabled-2 =
    .label = Use Picture-in-Picture
    .accesskey = e

browsing-picture-in-picture-enable-when-switching-tabs =
    .label = Keep playing videos in Picture-in-Picture when switching tabs
    .accesskey = s

browsing-media-control =
    .label = Control media via keyboard, headset, or virtual interface
    .accesskey = v

recommendations-group =
    .label = Recommendations

browsing-cfr-recommendations =
    .label = Recommend extensions as you browse
    .accesskey = R
browsing-cfr-features =
    .label = Recommend features as you browse
    .accesskey = f

## General Section - Proxy

network-proxy-group2 =
  .label = Proxy settings
  .description = Configure how { -brand-short-name } connects to the internet.

network-proxy-connection-settings2 =
    .label = Configure proxy
    .description = Changing these settings may cause connections issues
    .accesskey = p

## Home Section

home-section =
    .heading = Home and startup

home-new-windows-tabs-header = New Windows and Tabs

home-new-windows-tabs-description2 = Choose what you see when you open your homepage, new windows, and new tabs.

## Home Section - Default Browser

home-default-browser-title =
    .label = Default browser

is-default-browser-2 =
    .message = { -brand-short-name } is your default browser. Good choice.

is-not-default-browser-2 =
    .message = Psst, { -brand-short-name } isn’t your default.

set-as-my-default-browser-2 =
    .label = Make default
    .accesskey = D

## Home Section - Home Page Customization

home-homepage-title =
    .label = Homepage

home-homepage-mode-label = Homepage and new windows

home-homepage-new-windows =
    .label = New windows

home-newtabs-mode-label = New tabs

home-homepage-new-tabs =
    .label = New tabs

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

# This button is shown when the homepage is managed by an extension and is placed below extension-controlling-homepage.
home-homepage-manage-extension-button =
    .label = Manage extension

# This option leads to the "Custom Homepage" subpage
home-homepage-custom-homepage-button =
    .label = Choose a specific site

## Custom Homepage subpage

home-custom-homepage-header = Custom Homepage

home-custom-homepage-subpage =
    .heading = Custom Homepage

# Subheader on the Custom Homepage subpage. Followed by a form to enter URLs and a list of URLs already saved, if any.
home-custom-homepage-card-header =
    .label = Website address(es)

home-custom-homepage-address =
    .placeholder = Enter address
home-custom-homepage-address-button =
    .label = Add address

# Shown when no custom websites/URLs to use as a homepage have been added yet
home-custom-homepage-no-results =
    .label = No websites added yet.

home-custom-homepage-delete-address-button =
    .aria-label = Delete address
    .title = Delete address

# Further options to use when setting the home page. Two action buttons are placed in line with this prompt
# to replace the current home page with a currently open page or bookmark.
home-custom-homepage-replace-with-prompt =
    .label = Replace with

# Button that appears in-line after text "Replace with" (home-custom-homepage-replace-with-prompt)
home-custom-homepage-current-pages-button =
    .label = Current opened pages

# Button that appears in-line after text "Replace with" (home-custom-homepage-replace-with-prompt)
home-custom-homepage-bookmarks-button =
    .label = Bookmarks…

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

home-prefs-content-header =
    .label = { -firefox-home-brand-name }
home-prefs-content-header2 = { -firefox-home-brand-name } Content
home-prefs-content-description2 = Choose what content you want on your { -firefox-home-brand-name } screen.

home-prefs-search-header =
    .label = Web Search
home-prefs-search-header2 =
    .label = Search
home-prefs-shortcuts-header =
    .label = Shortcuts
home-prefs-shortcuts-description = Sites you save or visit
home-prefs-shortcuts-by-option-sponsored =
    .label = Sponsored shortcuts

home-prefs-recommended-by-header-generic =
    .label = Recommended stories
home-prefs-recommended-by-description-generic = Exceptional content curated by the { -brand-product-name } family

home-prefs-stories-header =
    .label = Stories
home-prefs-stories-description = Personalized stories based on your activity

home-prefs-stories-header2 =
    .label = Stories
    .description = Exceptional content curated by the { -brand-product-name } family

##

home-prefs-recommended-by-learn-more = How it works
home-prefs-recommended-by-option-sponsored-stories =
    .label = Sponsored stories

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

home-prefs-widgets-header =
    .label = Widgets

# Lists is a widget on New Tab, similar to a to-do widget
home-prefs-lists-header =
    .label = Lists

# Timer is a widget on New Tab, similar to the Pomodoro timer.
home-prefs-timer-header =
    .label = Timer

# Sports is a widget on New Tab showing sports scores and schedules.
home-prefs-sports-widget-header =
    .label = Sports

# Clock is a widget on New Tab that displays time zones around the world.
home-prefs-clocks-header =
    .label = Clock

# "Support" here means to help sustain or contribute to something, especially through funding or sponsorship.
home-prefs-support-firefox-header =
    .label = Support { -brand-product-name }

home-prefs-mission-message = Our sponsors support our mission to build a better web
home-prefs-mission-message-learn-more-link = Find out how

home-prefs-mission-message2 =
    .message = Our sponsors support our mission to build a better web.

home-prefs-manage-topics-link = Manage topics

home-prefs-manage-topics-link2 =
    .label = Manage topics

home-prefs-choose-wallpaper-link = Choose a wallpaper

home-prefs-choose-wallpaper-link2 =
    .label = Choose a wallpaper

# Informational message bar that appears in the Firefox Home section when the options are disabled.
# The user must select Firefox Home as their homepage for either new tabs or new windows to enable
# the features in settings.
home-prefs-firefox-home-disabled-notice =
    .message = To use these features, set new tabs or new windows to { -firefox-home-brand-name }.

# Variables:
#   $num (number) - Number of rows displayed
home-prefs-sections-rows-option =
    .label =
        { $num ->
            [one] { $num } row
           *[other] { $num } rows
        }

## Search Section

# Header for the search section ("search" is a noun).
search-section =
    .heading = Search

search-engine-group =
    .label = Default search engine
search-default-engine =
    .aria-label = Default search engine

# With this option enabled, while on a SERP, the URL normally displayed in the
# address bar will be replaced with the search term used to generate that SERP.
search-show-search-term-option-2 =
    .label = Show search terms in the address bar on results pages

search-separate-default-engine-2 =
    .label = Use a different default search engine in private windows
    .accesskey = U

search-separate-default-engine-dropdown =
    .aria-label = Default search engine in private windows

search-suggestions-header-2 =
    .label = Search engine suggestions

search-show-suggestions-option =
    .label = Show search suggestions
    .accesskey = S

search-show-suggestions-url-bar-option =
    .label = Show search suggestions in address bar results
    .accesskey = l

# This string describes what the user will observe when the system
# prioritizes search suggestions over browsing history in the results
# that extend down from the address bar. In the original English string,
# "before" refers to location (appearing most proximate to), not time
# (appearing before).
search-show-suggestions-above-history-option-2 =
    .label = Show search suggestions before browsing history in address bar results

search-show-suggestions-private-windows-2 =
    .label = Search suggestions in private windows

search-suggestions-cant-show-2 =
    .message = Search suggestions will not be shown in location bar results because you have configured { -brand-short-name } to never remember history.

addressbar-header-1 =
    .label = Address Bar
    .description = Choose which suggestions display in your address bar

# When Firefox Suggest is enabled, this replaces `addressbar-header-1`.
addressbar-header-firefox-suggest-2 =
    .label = { -firefox-suggest-brand-name }
    .description = Suggestions from { -brand-short-name } and our partners in your address bar.

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
addressbar-locbar-showrecentsearches-option-2 =
    .label = Recent searches
    .accesskey = r
addressbar-locbar-engines-option-1 =
    .label = Suggest search engines to use
    .accesskey = a
addressbar-locbar-quickactions-option =
    .label = Quick actions
    .accesskey = Q
addressbar-locbar-showtrendingsuggestions-option-2 =
    .label = Trending search suggestions
    .accesskey = t

# Toggles whether suggestions are obtained from Firefox Suggest or not (local or online).
addressbar-locbar-suggest-all-option-2 =
    .label = Suggestions from { -brand-short-name }
    .description = Get suggestions from the web related to your search.

addressbar-locbar-suggest-sponsored-option-2 =
    .label = Suggestions from sponsors
    .description = Support { -brand-short-name } with occasional sponsored suggestions.

# This string is used for a checkbox in the settings UI that opts the
# user into "online" Firefox Suggest, allowing them to receive suggestions from
# Mozilla's Merino server.
# "Mozilla" is intentionally hardcoded to prevent forks from replacing it
# with their own vendor name, since the online suggest is created and maintained
# by Mozilla.
addressbar-firefox-suggest-online =
    .label = Retrieve suggestions from Mozilla as you type

addressbar-dismissed-suggestions-label-2 =
    .label = Dismissed suggestions
    .description = Restore dismissed suggestions from sponsors and { -brand-short-name }.
addressbar-restore-dismissed-suggestions-button-2 =
    .label = Restore suggestions

search-one-click-header2 = Search Shortcuts
search-one-click-desc = Choose the alternative search engines that appear below the address bar and search bar when you start to enter a keyword.
search-one-click-header-3 =
    .label = Additional search engines
    .description = Choose which search engines and shortcuts appear in your address bar.

update-search-engine-success =
    .message = Search engine successfully updated

search-edit-engine-2 =
    .title = Edit search engine
search-delete-engine =
    .title = Delete search engine
search-enable-engine =
    .title = Enable search engine
search-outlink-to-extensions-page =
    .title = Manage in extensions and themes

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
search-add-engine-2 =
    .label = Add search engine
    .accesskey = A
search-edit-engine =
    .label = Edit
    .accesskey = E

search-find-more-link = Find more search engines

search-filtering-for-add-engine = Add Engine

# This warning is displayed when the chosen keyword is already in use
# ('Duplicate' is an adjective)
search-keyword-warning-title = Duplicate Keyword
# Variables:
#   $name (string) - Name of a search engine.
search-keyword-warning-engine = You have chosen a keyword that is currently in use by “{ $name }”. Please select another.
search-keyword-warning-bookmark = You have chosen a keyword that is currently in use by a bookmark. Please select another.

# This warning is displayed when the chosen name is already in use.
# Variables:
#   $name (string) - Name of a search engine.
edit-engine-name-warning-duplicate = There already is a search engine with the name “{ $name }”. Please choose another name.

remove-engine-confirmation = Are you sure you want to remove this search engine?
remove-engine-remove = Remove
remove-addon-engine-alert = To remove this search engine, remove the associated add-on.

## Containers Section

containers-back-button2 =
    .aria-label = Back to Settings
containers-header = Container Tabs
containers-section-header =
    .heading = Container Tabs
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

## Account and sync

account-sync-section =
    .heading = Account and sync

pane-account-sync-title = Account and sync

sync-group-label =
    .label = Sync

account-group-label2 =
    .label = Account

account-placeholder2 =
    .label = You’re not signed in
    .description = Sign in and keep your data private, encrypted, and instantly accessible everywhere you use { -brand-short-name }.

## Firefox account - Signed out. Note that "Sync" and "Firefox account" are now
## more discrete ("signed in" no longer means "and sync is connected").

sync-signedout-caption = Take Your Web With You
sync-signedout-description2 = Synchronize your bookmarks, history, tabs, passwords, add-ons, and settings across all your devices.

sync-signedout-account-signin3 =
    .label = Sign in to sync…
    .accesskey = i

sync-signedout-account-signin-4 =
    .label = Sign in to your account to start syncing
    .accesskey = i

sync-signedout-account-short =
    .label = Sign in
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

sync-sign-out2 =
    .label = Sign out
    .accesskey = g

sync-manage-account = Manage account
    .accesskey = o

sync-manage-account2 =
    .label = Manage account
    .accesskey = o

## Variables
## $email (string) - Email used for Firefox account
## $name (string) - Name used for Firefox account

sync-account-signed-in =
    .label = { $email }

sync-account-signed-in-display-name =
    .label = { $name }
    .description = { $email }

sync-signedin-unverified = { $email } is not verified.

sync-signedin-unverified2 =
    .label = { $email } isn’t confirmed yet
    .description = Check your inbox to confirm your account and make it official.

sync-signedin-login-failure = Please sign in to reconnect { $email }

sync-signedin-login-failure2 =
    .label = You’re signed out of { $email }
    .description = Sign back in to reconnect and start syncing your data.

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

prefs-syncing-on-2 =
    .label = Syncing is ON

prefs-syncing-off = Syncing: OFF

prefs-syncing-off-2 =
    .label = Syncing is OFF
    .description = Turn on sync to get your bookmarks, passwords, history, and more on any device.

prefs-sync-turn-on-syncing =
    .label = Turn on syncing…
    .accesskey = s

prefs-sync-turn-on-syncing-2 =
    .label = Turn on syncing
    .accesskey = s

prefs-sync-offer-setup-label2 = Synchronize your bookmarks, history, tabs, passwords, add-ons, and settings across all your devices.

prefs-sync-now-button =
    .label = Sync Now
    .accesskey = N

prefs-sync-now-button-2 =
    .label = Sync now
    .accesskey = N

prefs-syncing-button =
    .label = Syncing…

prefs-syncing-button-2 =
    .label = Syncing…
    .title = Sync now

## The list of things currently syncing.

sync-syncing-across-devices-heading = You are syncing these items across all your connected devices:

sync-syncing-across-devices-heading-2 = Data synced across devices

sync-syncing-across-devices-empty-state2 =
    .label = Manage synced data
    .description = You aren’t syncing anything… yet. Start syncing to get all of your data on all your devices.

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

sync-manage-options-2 =
    .label = Manage synced data
    .accesskey = M

settings-sync-disconnect-button =
    .label = Disconnect

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

sync-device-name-header-2 =
    .label = Device name

# Variables:
#   $placeholder (string) - The placeholder text of the input
sync-device-name-input =
    .aria-label = Device Name
    .placeholder = { $placeholder }

sync-device-name-change-2 =
    .label = Change Device Name
    .accesskey = h

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

sync-connect-another-device-2 =
    .label = Connect another device

## Privacy Section

privacy-header = Browser Privacy

## Privacy Section - Passwords

# "Logins" is the former term for "Passwords". Users should find password settings
# by searching for the former term "logins". It's not displayed in the UI.
pane-privacy-passwords-header = Passwords
    .searchkeywords = logins

forms-passwords-header =
    .label = Passwords
    .aria-label = Passwords

# Checkbox to control whether UI is shown to users to save or fill logins/passwords.
forms-ask-to-save-passwords =
    .label = Ask to save passwords
    .accesskey = A
forms-manage-password-exceptions =
    .label = Manage password exceptions
    .accesskey = M
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

## Privacy Panel Settings

privacy-panel-settings-header =
    .label = Privacy panel settings
    .description = Get help protecting your info online in { -brand-short-name }.
# By “breach message” we mean the UI messaging shown when Firefox detects that a
# site the user has interacted with has been involved in a known data breach
privacy-panel-breach-alerts =
    .label = Show breach messages
    .accesskey = s
preferences-relay-integration-checkbox2 =
    .label = Suggest { -relay-brand-name } email masks to protect your email address
    .accesskey = r
relay-integration-learn-more-link = Learn more

# Checkbox which controls filling saved logins into fields automatically when they appear, in some cases without user interaction.
forms-fill-usernames-and-passwords =
    .label = Fill usernames and passwords automatically
    .accesskey = F
forms-fill-usernames-and-passwords-2 =
    .label = Save and autofill usernames and passwords
    .accesskey = f
forms-saved-passwords =
    .label = Saved passwords
    .accesskey = d
forms-saved-passwords-2 =
    .label = Manage saved passwords
    .accesskey = d
forms-saved-passwords-searchkeywords = Logins for the following sites are stored on your computer

# Header for additional protections when managing password settings.
forms-additional-protections-header =
    .label = Additional protections
forms-primary-pw-use =
    .label = Use a primary password
    .accesskey = U
forms-primary-pw-use-2 =
    .label = Use a primary password
    .description = Adds an extra layer of security to protect your saved passwords.
    .accesskey = U
forms-primary-pw-set =
    .label = Set primary password
forms-primary-pw-on =
    .label = Primary password is ON
forms-primary-pw-change-2 =
    .label = Change primary password
# Label for button to disable primary password.
forms-primary-pw-turn-off =
    .label = Turn it off
# This operation requires the user to authenticate with the operating system (device sign-in)
forms-os-reauth =
    .label = Require device sign in to fill and manage passwords
forms-os-reauth-2 =
    .label = Require device sign in to manage passwords
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

payments-group =
    .label = Payment methods

autofill-payment-methods-header =
    .aria-label = Payment methods
autofill-payment-methods-checkbox-message-2 =
    .label = Save and autofill payment info
    .accesskey = p
autofill-payment-methods-manage-payments-title =
    .heading = Manage payment methods
autofill-payment-methods-manage-payments-button =
    .label = Manage payment methods
    .accesskey = m
# This operation requires the user to authenticate with the operating system (device sign-in)
autofill-reauth-payment-methods-checkbox-2 =
    .label = Require device sign in to autofill and manage payment methods
    .accesskey = o

autofill-payment-methods-add-button = Add new payment method
payments-list-header =
  .label = Payment methods
payments-delete-payment-prompt-title = Delete this payment method?
payments-delete-payment-prompt-confirm-button = Delete
payments-delete-payment-prompt-cancel-button = Cancel
payments-delete-payment-button-label =
    .aria-label = Delete
payments-edit-payment-button-label =
    .aria-label = Edit

# This message is displayed when no payment methods such as credit card are stored in Firefox
payments-no-payments-stored-message =
    .label = No payment methods added

# These values are displayed for each credit card record listed on the Manage Payment methods
# settings page.
# Variables:
#   $cardNumber (string) - The obscured credit card number (for example: ********* 2423)
#   $expDate (string) - The obscured expiry date of the credit card (for example: XX/2027)
payment-moz-box-item =
  .label = { $cardNumber }
  .description = { $expDate }

addresses-group =
    .label = Addresses and more
autofill-addresses-checkbox-message =
    .label = Save and autofill addresses
    .accesskey = S
autofill-addresses-manage-addresses-button =
    .label = Manage addresses and more
    .accesskey = M
addresses-list-header =
    .label = Addresses
addreses-delete-address-button-label =
    .aria-label = Delete
addreses-edit-address-button-label =
    .aria-label = Edit
addresses-delete-address-prompt-title = Delete this address?
addresses-delete-address-prompt-confirm-button = Delete
addresses-delete-address-prompt-cancel-button = Cancel
autofill-addresses-add-button = Add new address
autofill-addresses-manage-addresses-title =
    .heading = Manage addresses and more

# This message is displayed when no addresses are stored in Firefox
addresses-no-addresses-stored-message =
    .label = No addresses added

# These values are displayed for each address record listed on the "Manage addresses and more" subpage.
# Variables:
#   $name (string) - The name associated with the address
#   $address (string) - The address
address-moz-box-item =
  .label = { $name }
  .description = { $address }

category-passwords-autofill =
    .tooltiptext = Passwords and autofill settings
pane-passwords-autofill-title = Passwords and autofill
preferences-passwords-autofill-header =
   .heading = Passwords and autofill

## Privacy Section - History

history-group =
    .label = History

history-remember-option-all2 =
    .label = Remember history
history-remember-option-never2 =
    .label = Never remember history
    .description = Every window acts like a private window. When on, extensions need to be allowed.
history-remember-option-custom2 =
    .label = Customize history

history-remember-description4 =
    .aria-label = { history-group.label }
    .description = { -brand-short-name } will remember your browsing, download, form, and search history.

history-dontremember-description4 =
    .aria-label = { history-group.label }
    .description = { -brand-short-name } will use the same settings as private browsing, and will not remember any history as you browse the Web.

history-custom-description4 =
    .aria-label = { history-group.label }
    .description = { -brand-short-name } will use custom settings for your browsing, download, form and search history.

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

history-header2 =
    .heading = History

history-section-header =
    .label = History
    .description = Choose what you want { -brand-short-name } to remember when you close the browser.

history-custom-section-header =
    .label = Advanced settings
    .description = Customize what you want { -brand-short-name } to remember when you close the browser.

history-custom-button =
    .label = Choose what you want { -brand-short-name } to remember

## Privacy Section - Site Data

cookies-site-data-group =
    .label = Cookies and Site Data

sitedata-total-size-calculating = Calculating site data and cache size…

# Variables:
#   $value (number) - Value of the unit (for example: 4.6, 500)
#   $unit (string) - Name of the unit (for example: "bytes", "KB")
sitedata-total-size3 = Websites are currently using <strong>{ $value } { $unit }</strong> of disk space.

sitedata-learn-more = Learn more

sitedata-delete-on-close2 =
    .label = Clear cookies and site data every time you close { -brand-short-name }
    .accesskey = c

sitedata-delete-on-close-private-browsing3 =
    .message = Based on your history settings, { -brand-short-name } deletes cookies and site data from your session when you close the browser.

sitedata-delete-on-close-private-browsing4 =
    .heading = History won’t be saved.
    .message = { -brand-short-name } clears cookies and site data from your session when you close the browser.

sitedata-option-block-cross-site-trackers =
    .label = Cross-site trackers
sitedata-option-block-cross-site-tracking-cookies =
    .label = Cross-site tracking cookies
sitedata-option-block-cross-site-cookies2 =
    .label = Isolate cross-site cookies
sitedata-option-block-unvisited =
    .label = Cookies from unvisited websites
sitedata-option-block-all-cross-site-cookies =
    .label = All cross-site cookies (may cause websites to break)
sitedata-option-block-all =
    .label = All cookies (will cause websites to break)

sitedata-clear2 =
    .label = Clear browsing data
    .accesskey = l

sitedata-settings2 =
    .label = Manage browsing data
    .accesskey = M

sitedata-cookies-exceptions =
    .label = Manage Exceptions…
    .accesskey = x

sitedata-cookies-exceptions2 =
    .label = Manage exceptions
    .accesskey = x
    .description = You can specify which websites are always or never allowed to use cookies and site data.

sitedata-heading =
    .label = Browsing data
    .description = Manage your cookies, history, cache, website data, and more.

sitedata-settings3 =
    .label = Clear data for specific sites
    .accesskey = s

sitedata-cookies-exceptions3 =
    .label = Manage exceptions
    .accesskey = x
    .description = Choose how specific sites handle cookies and site data.

## Privacy Section - Cookie Banner Blocking

cookie-banner-blocker-header = Cookie Banner Blocker
cookie-banner-blocker-description = When a site asks if they can use cookies in private browsing mode, { -brand-short-name } automatically refuses for you. Only on supported sites.
cookie-banner-learn-more = Learn more
cookie-banner-blocker-checkbox-label =
    .label = Automatically refuse cookie banners

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
content-blocking-isolate-cross-site-cookies = Isolate cross-site cookies
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

content-blocking-warning-title-2 = Some sites may break with strict tracking protection
content-blocking-warning-title-custom = Some sites may break with custom tracking protection
# “Fix site issues” references the string content-blocking-exceptions-subheader
content-blocking-and-isolating-etp-warning-description-4 = { -brand-short-name } recommends using the “Fix site issues” settings to reduce broken site features and content. If a site seems broken, try turning off tracking protection for that site to load all content.
content-blocking-warning-learn-how = Learn how

content-blocking-baseline-exceptions-3 =
    .label = Fix major site issues (recommended)
    .description = Helps load sites and features by unblocking only essential elements that may contain trackers. Covers most common problems.

# This option to fix minor site issues must be used with the option to fix major site issues (string content-blocking-baseline-exceptions-3)
content-blocking-convenience-exceptions-3 =
    .label = Fix minor site issues
    .description = Restores things like videos in an article or comment sections by unblocking elements that may contain trackers. This can reduce site issues but offers less protection. Must be used with fixes for major issues.

content-blocking-baseline-uncheck-warning-dialog-title = Are you sure you want to turn off fixes?
content-blocking-baseline-uncheck-warning-dialog-body = This setting helps fix the most common site problems. If you turn it off, some sites may not work, and { -brand-short-name } won’t be able to help troubleshoot those issues.
content-blocking-baseline-uncheck-warning-dialog-ok-button = Turn fixes off
content-blocking-baseline-uncheck-warning-dialog-cancel-button = Keep fixes on

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

permissions-data-section =
    .heading = Permissions and data

pane-permissions-data-title = Permissions and data

permissions-header3 =
    .label = Permissions
    .description = Manage what websites can access, control, or trigger.

permissions-location2 =
    .label = Location

permissions-localhost2 =
    .label = Device apps and services

permissions-local-network2 =
    .label = Local network devices

permissions-xr2 =
    .label = Virtual reality

permissions-camera2 =
    .label = Camera

permissions-microphone2 =
    .label = Microphone

# Privacy permission for sound output devices.
permissions-speaker2 =
    .label = Speaker

permissions-notification2 =
    .label = Notifications

permissions-notification-pause =
    .label = Pause notifications until { -brand-short-name } restarts
    .accesskey = n

permissions-autoplay2 =
    .label = Autoplay

permissions-block-popups2 =
    .label = Block pop-ups and third-party redirects
    .accesskey = B

# "popup" is a misspelling that is more popular than the correct spelling of
# "pop-up" so it's included as a search keyword, not displayed in the UI.
permissions-block-popups-exceptions-button4 =
    .label = Manage exceptions
    .description = Add websites that can open pop-ups and use third-party redirects.
    .accesskey = E
    .searchkeywords = popups

permissions-addon-install-warning3 =
    .label = Show warning when websites try to install extensions
    .accesskey = W

permissions-addon-exceptions2 =
    .label = Choose which websites can install extensions
    .accesskey = E

## Privacy Section - Data Collection

# The search keyword isn't shown to users but is used to find relevant settings in about:preferences.
data-collection =
    .label = { -brand-short-name } data collection and use
    .description = We strive to provide you with choices and collect only the minimal data necessary to improve { -brand-product-name } for everyone.
    .searchkeywords = telemetry
data-collection-link = View Privacy Notice
data-collection-preferences-across-profiles =
    .message = These settings apply to every { -brand-product-name } profile on this device.
data-collection-profiles-link = View all profiles
data-collection-health-report-telemetry-disabled =
    .message = You’re no longer allowing { -vendor-short-name } to capture technical and interaction data. All past data will be deleted within 30 days.
data-collection-health-report =
    .label = Send technical and interaction data to { -vendor-short-name }
    .accesskey = r
    .description = This helps us improve { -brand-product-name } features, performance, and stability.
data-collection-health-report-disabled =
    .label = Send technical and interaction data to { -vendor-short-name }
    .accesskey = r
    .description = Data reporting is disabled for this build configuration.
data-collection-run-studies =
    .label = Allow { -brand-short-name } to run feature studies
    .description = { -brand-short-name } randomly selects users to test features, which helps improve quality for everyone.
data-collection-studies-link =
    .label = View { -brand-short-name } studies
data-collection-backlogged-crash-reports =
    .label = Automatically send crash reports
    .description = This helps { -vendor-short-name } diagnose and fix issues with the browser. Reports may include personal or sensitive data.
    .accesskey = c
data-collection-usage-ping =
    .label = Send daily usage ping to { -vendor-short-name }
    .description = This helps { -vendor-short-name } to estimate active users.
    .accesskey = u

backup-multi-profile-warning-message =
    .message = To make sure this change is included in your backups, open each profile and choose “Backup now” in Settings.

nimbus-rollouts =
    .label = Allow { -brand-short-name } to improve features, performance, and stability between updates
    .description = Changes will be rolled out remotely.
addon-recommendations3 =
    .label = Allow personalized extension recommendations
    .description = Get extension recommendations to improve your browsing experience.

# This message is displayed above disabled data sharing options in developer builds
# or builds with no Telemetry support available.
collection-health-report-disabled2 = Data reporting is disabled for this build configuration.

collection-backlogged-crash-reports2 =
    .label = Automatically send crash reports
    .accesskey = c
collection-backlogged-crash-reports-description = This helps { -vendor-short-name } diagnose and fix issues with the browser. Reports may include personal or sensitive data.

# Promotional message displayed in the Settings panes to inform users of the new redesign
settings-redesign-promo =
    .heading = Same settings, new look!
    .message = We reorganized this page so it’s easier to scan and explore. Your personal settings haven’t changed, and everything’s still here. Tip: use search to jump straight to what you need.
settings-redesign-promo-dismiss-button =
    .label = Got it

privacy-segmentation-section-header = New features that enhance your browsing

privacy-segmentation-section-description = When we offer features that use your data to give you a more personal experience:

privacy-segmentation-radio-off =
    .label = Use { -brand-product-name } recommendations

privacy-segmentation-radio-on =
    .label = Show detailed information

## Privacy Section - Security
##
## It is important that wording follows the guidelines outlined on this page:
## https://developers.google.com/safe-browsing/developers_guide_v2#AcceptableUsage

security-header = Security

browsing-protection-group2 =
    .label = Deceptive content and dangerous software protection
    .description = Dangerous sites and downloads can put your data and device at risk. { -brand-short-name } automatically blocks them, and warns you about risky or unwanted software.

security-enable-safe-browsing =
    .label = Block dangerous and deceptive content
    .accesskey = B
security-enable-safe-browsing-link = Learn more

security-safe-browsing-warning =
    .message = Turning this off reduces protection against scams, malicious sites, and dangerous downloads.

security-block-downloads =
    .label = Block dangerous downloads
    .accesskey = d

security-block-uncommon-software =
    .label = Warn you about unwanted and uncommon software
    .accesskey = c

## Privacy Section - Certificates

certs-description3 =
    .label = Certificates
    .description = Configure the certificates that { -brand-short-name } uses to verify secure connections.

certs-view2 =
    .label = Manage certificates
    .accesskey = C

certs-devices2 =
    .label = Manage security devices
    .accesskey = D

certs-thirdparty-toggle =
    .label = Allow { -brand-short-name } to automatically trust third-party root certificates you install
    .accesskey = t

certs-devices-enable-fips = Enable FIPS

space-alert-over-5gb-settings-button =
    .label = Open Settings
    .accesskey = O

space-alert-over-5gb-message2 = <strong>{ -brand-short-name } is running out of disk space.</strong> Website contents may not display properly. You can clear stored data in Settings > Privacy & Security > Cookies and Site Data.

space-alert-under-5gb-message2 = <strong>{ -brand-short-name } is running out of disk space.</strong> Website contents may not display properly. Visit “Learn more” to optimize your disk usage for better browsing experience.

## Privacy Section - HTTPS-Only

httpsonly-group =
    .label = HTTPS-Only Mode
    .description = Only allows secure connections to websites. { -brand-short-name } will ask before connecting insecurely.

httpsonly-label2 =
    .aria-label = { httpsonly-group.label }

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
dns-over-https-group2 =
    .label = DNS over HTTPS
    .description = Domain Name System over HTTPS (DoH) encrypts site lookups so it’s harder for your internet provider or others to see what websites you’re about to visit.

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

preferences-doh-overview-default =
    .label = Default protection
    .description = Use secure DNS in regions where it’s available.

preferences-doh-overview-custom =
    .label = Custom
    .description = Always use secure DNS with control over your provider and fallback behavior.

preferences-doh-overview-off =
    .label = Off
    .description = Use your default DNS resolver.

preferences-doh-advanced-button =
    .label = Advanced settings

preferences-doh-advanced-section =
    .label = Advanced settings
    .description = Domain Name System over HTTPS (DoH) encrypts site lookups so it’s harder for your internet provider or others to see what websites you’re about to visit.

preferences-doh-manage-exceptions2 =
    .label = Manage exceptions
    .accesskey = x

preferences-doh-radio-default =
    .label = Default
    .description = Use secure DNS in regions where it’s available

preferences-doh-radio-custom =
    .label = Custom
    .description = Always use secure DNS with control over your provider and fallback behavior

preferences-doh-radio-off =
    .label = Off
    .description = Use your default DNS resolver

preferences-doh-fallback-label =
    .label = Always warn me if secure DNS isn’t available

preferences-doh-status-item-off =
    .message = DNS over HTTPS is off

# Variables:
#   $reason (string) - A string representation of the reason DoH is not active. For example NS_ERROR_UNKNOWN_HOST or TRR_RCODE_FAIL.
#   $name (string) - The name of the DNS over HTTPS resolver. If a custom resolver is used, the name will be the domain of the URL.
preferences-doh-status-item-not-active =
    .message = DNS over HTTPS is not working because we encountered an error ({ $reason }) while trying to use the provider { $name }


# Variables:
#   $reason (string) - A string representation of the reason DoH is not active. For example NS_ERROR_UNKNOWN_HOST or TRR_RCODE_FAIL.
preferences-doh-status-item-not-active-bad-url =
    .message = DNS over HTTPS is not working because we received an invalid URL ({ $reason })


# Variables:
#   $name (string) - The name of the DNS over HTTPS resolver. If a custom resolver is used, the name will be the domain of the URL.
preferences-doh-status-item-active =
    .message = DNS over HTTPS is using the provider { $name }

# Variables:
#   $reason (string) - A string representation of the reason DoH is not active. For example NS_ERROR_UNKNOWN_HOST or TRR_RCODE_FAIL.
#   $name (string) - The name of the DNS over HTTPS resolver. If a custom resolver is used, the name will be the domain of the URL.
preferences-doh-status-item-not-active-local =
    .message = DNS over HTTPS is not working because we encountered an error ({ $reason }) while trying to use the local provider { $name }

# Variables:
#   $name (string) - The name of the DNS over HTTPS resolver. If a custom resolver is used, the name will be the domain of the URL.
preferences-doh-status-item-active-local =
    .message = DNS over HTTPS is using the local provider { $name }

preferences-doh-select-resolver-label =
    .label = Choose provider:

# Variables:
#   $name (String) - Display name or URL for the DNS over HTTPS provider
connection-dns-over-https-url-item =
    .label = { $name }
    .tooltiptext = Use this provider for resolving DNS over HTTPS

preferences-doh-custom-provider-label =
    .aria-label = Enter a custom provider URL

preferences-doh-header2 =
  .heading = DNS over HTTPS

## Connection and software security section

preferences-connection-header =
    .heading = Connection and software security

preferences-connection-link-section =
    .label = Connection and software security
    .description = See how connections stay secure, harmful software is blocked, and websites are verified.

preferences-connection-link-button =
    .label = Advanced settings

## The following strings are used in the Download section of settings

desktop-folder-name = Desktop
downloads-folder-name = Downloads

## Appearance page

preferences-appearance-header =
    .heading = Appearance

browser-theme-group =
    .label = Browser theme
    .description = Style { -brand-short-name } your way. Theme colors apply to toolbars, menus, and messages.

browser-theme-manage-link =
    .label = Manage { -brand-short-name } themes

related-settings-group =
    .label = Related settings

related-settings-accessibility-link =
    .label = Customize zoom and font settings in Accessibility

related-settings-home-link =
    .label = Customize { -firefox-home-brand-name }

## Accessibility page

preferences-accessibility-header =
    .heading = Accessibility

preferences-default-zoom-select =
    .aria-label = Default zoom

preferences-fonts-family =
    .label = Font family
    .accesskey = D

preferences-fonts-size =
    .label = Font size
    .accesskey = s

preferences-fonts-advanced-settings =
    .label = Advanced settings
    .accesskey = A

browsing-use-autoscroll =
    .label = Use autoscrolling
    .accesskey = a

keyboard-and-scrolling-group =
  .label = Keyboard navigation and scrolling

motion-and-link-group =
  .label = Motion and link styling

## AI controls page

preferences-ai-controls-header =
    .heading = { pane-ai-controls-title }

preferences-ai-controls-description = You always have a choice in { -brand-short-name }, including whether to use features enhanced with AI. More controls coming soon.

preferences-ai-controls-block-ai-label = Block AI enhancements
preferences-ai-controls-block-ai =
    .label = { preferences-ai-controls-block-ai-label }
preferences-ai-controls-block-ai-description = Blocking means you won’t see new or current AI enhancements in { -brand-short-name }, or pop-ups about them. <a data-l10n-name="link">Get more details</a> about what’s included and how to control traditional machine learning features, like search suggestions and recommendations.

preferences-ai-controls-blocked-message =
    .message = New and current AI enhancements are blocked by default. To unblock a specific feature, use the controls below.

preferences-ai-controls-on-device-group =
    .label = On-device AI
    .description = These use small AI models that download to your device if you use the feature. This approach helps protect your privacy.

preferences-ai-controls-translations-control =
    .label = Translations
    .description = Seamlessly browse the web in your preferred language.
preferences-ai-controls-translations-more-link = More translations settings

preferences-ai-controls-pdfjs-control =
    .label = Image alt text in { -brand-short-name } PDF viewer
    .description = When you add images to PDFs, this adds descriptions to make them accessible.

preferences-ai-controls-tab-group-suggestions-control =
    .label = Tab group suggestions
    .description = Get suggestions to name and organize your tabs.

preferences-ai-controls-key-points-control =
    .label = Key points in link previews
    .description = See a quick summary before opening a link.

preferences-ai-controls-sidebar-chatbot-group =
    .label = AI chatbot providers in sidebar
    .description = Keep a chatbot in view as you browse. Choose from Anthropic Claude, ChatGPT, Copilot, Google Gemini, and Le Chat Mistral.

preferences-ai-controls-sidebar-chatbot-control =
    .label = Chatbot in sidebar

# This option means that a user will see the feature and can use it.
preferences-ai-controls-state-available =
    .label = Available
# This option means a user has opted in to use the feature.
preferences-ai-controls-state-enabled =
    .label = Enabled
# This option means the user won't see and can't use the feature. For on-device AI, any models already downloaded are removed.
preferences-ai-controls-state-blocked =
    .label = Blocked

preferences-ai-controls-state-description-before = What the options mean:
preferences-ai-controls-state-description-available = <strong>Available:</strong> You’ll see the feature and can use it.
preferences-ai-controls-state-description-enabled = <strong>Enabled:</strong> You’ve opted in to use the feature.
preferences-ai-controls-state-description-blocked = <strong>Blocked:</strong> You won’t see and can’t use the feature. For on-device AI, any models already downloaded are removed.

preferences-ai-controls-block-confirmation-heading = Block AI enhancements?
preferences-ai-controls-block-confirmation-description = You won’t see new or current AI enhancements in { -brand-short-name }, or pop-ups about them. Afterwards, you can unblock anything you want to keep using.

preferences-ai-controls-block-confirmation-features-start = What will be blocked:
preferences-ai-controls-block-confirmation-translations = Translations
preferences-ai-controls-block-confirmation-pdfjs = Image alt text in { -brand-short-name } PDF viewer
preferences-ai-controls-block-confirmation-tab-group-suggestions = Tab group suggestions
preferences-ai-controls-block-confirmation-key-points = Key points in link previews
preferences-ai-controls-block-confirmation-sidebar-chatbot = Chatbot providers in sidebar
preferences-ai-controls-block-confirmation-features-after = Blocking also affects extensions that use AI provided by { -brand-short-name }.

preferences-ai-controls-block-confirmation-cancel =
    .label = Cancel
preferences-ai-controls-block-confirmation-confirm =
    .label = Block

## Privacy and security status card

security-privacy-status-ok-header = { -brand-short-name } is on guard

# This is the header above a section telling the user about problems in their settings
security-privacy-status-problem-header = { -brand-short-name } recommends some security improvements
security-privacy-status-ok-label = Enhanced Tracking Protection is on
security-privacy-status-problem-label = We found settings affecting your protection
security-privacy-status-problem-helper-label = View issues
security-privacy-status-pending-trackers-label = Looking up how many trackers { -brand-short-name } blocked over the last month

# This label tells the user how many trackers we have blocked for them.
# Variables:
#   $trackerCount (Number) - Number of trackers we have blocked in the last month
security-privacy-status-trackers-label = { $trackerCount ->
      [one] { $trackerCount } tracker blocked over the last month
      *[other] { $trackerCount } trackers blocked over the last month
  }
# This string appears under "Enhanced Tracking Protection is on" when a user has enabled "Strict" in Enhanced Tracking Protection advanced settings
security-privacy-status-strict-enabled-label = You have <a data-l10n-name="strict-tracking-protection">strict protection</a>
# This string appears under "Enhanced Tracking Protection is on" when a user has enabled "Custom" in Enhanced Tracking Protection advanced settings
security-privacy-status-custom-enabled-label = You have <a data-l10n-name="custom-tracking-protection">custom protection</a>
security-privacy-status-up-to-date-label = You’ve got the latest, safest version of { -brand-short-name }
security-privacy-status-update-needed-label = A new version of { -brand-short-name } is available.
security-privacy-status-update-error-label = { -brand-short-name } is having trouble updating itself
security-privacy-status-update-checking-label = { -brand-short-name } is checking for updates
security-privacy-status-update-needed-description = Update for the latest speed, stability, and security updates.
security-privacy-status-update-button-label =
  .label = Update { -brand-short-name }

security-privacy-image-warning =
  .alt = A shield with an exclamation mark, expressing concern over your security warnings
security-privacy-image-ok =
  .alt = A shield with a check mark, showing that you have no outstanding security issues

security-privacy-issue-card =
  .heading = Security warnings
issue-card-reset-button =
  .label = Reset
issue-card-dismiss-button =
  .tooltiptext = Dismiss
  .aria-label = Dismiss

## Enhanced Tracking Protection (ETP) status section

preferences-etp-status-header =
    .label = Enhanced Tracking Protection
    .description = Sites use trackers to follow you online and show creepy ads. { -brand-short-name } shields you as you browse, blocking trackers automatically so you’re in control of your digital trail.

preferences-etp-level-standard =
    .label = Standard (default)
    .description = Strong, reliable protections that work smoothly with most websites.

preferences-etp-level-strict =
    .label = Strict
    .description = Stronger protections that block more trackers, but may cause some sites to break.

preferences-etp-level-custom =
    .label = Custom
    .description = Choose which protections to turn on or off.

preferences-etp-status-advanced-button =
    .label = Advanced settings

preferences-etp-status-protections-dashboard-link =
        .label = View your personalized protections dashboard
        .description = See how many sneaky trackers { -brand-short-name } has blocked for you, including social media trackers, fingerprinters, and cryptominers.

preferences-etp-header =
    .heading = Enhanced Tracking Protection

preferences-etp-advanced-settings-group =
    .label = Advanced settings
    .description = Sites use trackers to follow you online and show creepy ads. { -brand-short-name } shields you as you browse, blocking most trackers automatically so you’re in control of your digital trail.

preferences-etp-customize-button =
    .label = Customize tracking protection

preferences-etp-reload-tabs-hint =
    .message = Reload your tabs to apply these changes.
preferences-etp-reload-tabs-hint-button =
  .label = Reload all tabs

preferences-etp-rfp-warning-message =
    .message = You’re using Resist Fingerprinting (RFP), which replaces some of { -brand-short-name }’s fingerprinting protection settings. This might cause some sites to break.

preferences-etp-level-warning-message =
    .heading = Heads up! Some sites may not work as expected.
    .message = Some sites build trackers into their features or content. When { -brand-short-name } blocks them, the site looks broken. Try using “Fix site issue” or turning off tracking protection on that site.

preferences-etp-manage-exceptions-button =
    .label = Manage exceptions
    .description = Manage websites where Enhanced Tracking Protection is disabled.

preferences-etp-customize-header =
    .heading = Customize tracking protection

preferences-etp-reset =
    .label = Reset customizations
    .description = Restore settings to a preset protection level.

preferences-etp-reset-standard-button =
    .label = Reset to standard

preferences-etp-reset-strict-button =
    .label = Reset to strict

preferences-etp-custom-control-group =
    .label = Tracking protection
    .description = Choose which protections to turn on or off.

preferences-etp-custom-cookies-enabled =
    .label = Cookies

preferences-etp-custom-cookie-behavior =
    .aria-label = Cookies

preferences-etpc-custom-cookie-behavior-accept-all =
    .label = Allow all cookies

preferences-etp-custom-tracking-protection-enabled =
    .label = Tracking content

preferences-etp-custom-tracking-protection-enabled-context =
    .aria-label = Tracking content

preferences-etp-custom-crypto-mining-protection-enabled =
    .label = Cryptominers

preferences-etp-custom-known-fingerprinting-protection-enabled =
    .label = Known fingerprinters

preferences-etp-custom-suspect-fingerprinting-protection-enabled =
    .label = Suspected fingerprinters

preferences-etp-custom-suspect-fingerprinting-protection-enabled-context =
    .aria-label = Suspected fingerprinters

## Warnings section

security-privacy-issue-warning-fingerprinters =
  .label = Known fingerprinters are not blocked
  .description = This may allow some trackers to follow you without cookies.

security-privacy-issue-warning-third-party-cookies =
  .label = Third-party cookies are enabled
  .description = Third-party cookies are used to track you across websites.

security-privacy-issue-warning-password-manager =
  .label = Password manager is disabled
  .description = Password managers help you store strong passwords for your accounts.

security-privacy-issue-warning-popup-blocker =
  .label = Popup blocker is disabled
  .description = Popups are interruptive and potentially harmful.

security-privacy-issue-warning-extension-install =
  .label = Websites can install extensions
  .description = Websites can install extensions to { -brand-short-name } without asking.

security-privacy-issue-warning-safe-browsing =
  .label = Dangerous and deceptive content is not blocked
  .description = Your exposure to scams and malware from websites is increased.

security-privacy-issue-warning-doh =
  .label = DNS over HTTPS is disabled
  .description = DNS over HTTPS hides what sites you visit from your network provider.

security-privacy-issue-warning-ech =
  .label = Encrypted Client Hello is disabled
  .description = Encrypted Client Hello hides what sites you visit from your network provider.

security-privacy-issue-warning-proxy-autodetection =
  .label = Proxy auto-configuration is enabled
  .description = Proxy auto-configuration could let untrusted networks to monitor your activity.
