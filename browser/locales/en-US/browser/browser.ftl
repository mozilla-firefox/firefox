# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## The main browser window's title

# These are the default window titles everywhere except macOS.
# .data-title-default and .data-title-private are used when the web content
# opened has no title:
#
# default - "Mozilla Firefox"
# private - "Mozilla Firefox (Private Browsing)"
#
# .data-content-title-default and .data-content-title-private are for use when
# there *is* a content title.
#
# .data-title-default-with-profile, .data-title-private-with-profile,
# .data-content-title-default-with-profile,
# .data-content-title-private-with-profile are used when there a
# SelectableProfileService.current profile exists.
#
# Variables:
#  $content-title (String): the title of the web content.
#  $profile-name (String): the name of the current profile.
browser-main-window-titles =
  .data-title-default = { -brand-full-name }
  .data-title-private = { -brand-full-name } Private Browsing
  .data-title-default-with-profile = { $profile-name } — { -brand-full-name }
  .data-title-private-with-profile = { $profile-name } — { -brand-full-name } Private Browsing
  .data-content-title-default = { $content-title } — { -brand-full-name }
  .data-content-title-private = { $content-title } — { -brand-full-name } Private Browsing
  .data-content-title-default-with-profile = { $content-title } — { $profile-name } — { -brand-full-name }
  .data-content-title-private-with-profile = { $content-title } — { $profile-name } — { -brand-full-name } Private Browsing

# These are the default window titles on macOS.
# .data-title-default and .data-title-private are used when the web content
# opened has no title:
#
#
# "default" - "Mozilla Firefox"
# "private" - "Mozilla Firefox — (Private Browsing)"
#
# .data-content-title-default and .data-content-title-private are for use when
# there *is* a content title.
# Do not use the brand name in these, as we do on non-macOS.
#
# .data-title-default-with-profile, .data-title-private-with-profile,
# .data-content-title-default-with-profile,
# .data-content-title-private-with-profile are used when there a
# SelectableProfileService.current profile exists.
#
# Also note the other subtle difference here: we use a `-` to separate the
# brand name from `(Private Browsing)`, which does not happen on other OSes.
#
# Variables:
#  $content-title (String): the title of the web content.
#  $profile-name (String): the name of the current profile.
browser-main-window-titles-mac =
  .data-title-default = { -brand-full-name }
  .data-title-private = { -brand-full-name } — Private Browsing
  .data-title-default-with-profile = { $profile-name } — { -brand-full-name }
  .data-title-private-with-profile = { $profile-name } — { -brand-full-name } Private Browsing
  .data-content-title-default = { $content-title }
  .data-content-title-private = { $content-title } — Private Browsing
  .data-content-title-default-with-profile = { $content-title } — { $profile-name }
  .data-content-title-private-with-profile = { $content-title } — { $profile-name } — Private Browsing

# This gets set as the initial title, and is overridden as soon as we start
# updating the titlebar based on loaded tabs or private browsing state.
# This should match the `data-title-default` attribute in both
# `browser-main-window` and `browser-main-window-mac`.
browser-main-window-default-title = { -brand-full-name }

# The non-variable portion of this MUST match the translation of
# "PRIVATE_BROWSING_SHORTCUT_TITLE" in custom.properties
private-browsing-shortcut-text-2 = { -brand-shortcut-name } Private Browsing

##

urlbar-identity-button =
    .aria-label = View site information

## Tooltips for images appearing in the address bar

urlbar-services-notification-anchor =
    .tooltiptext = Open install message panel
urlbar-web-notification-anchor =
    .tooltiptext = Change whether you can receive notifications from the site
urlbar-midi-notification-anchor =
    .tooltiptext = Open MIDI panel
urlbar-eme-notification-anchor =
    .tooltiptext = Manage use of DRM software
urlbar-web-authn-anchor =
    .tooltiptext = Open Web Authentication panel
urlbar-canvas-notification-anchor =
    .tooltiptext = Manage canvas extraction permission
urlbar-web-rtc-share-microphone-notification-anchor =
    .tooltiptext = Manage sharing your microphone with the site
urlbar-default-notification-anchor =
    .tooltiptext = Open message panel
urlbar-geolocation-notification-anchor =
    .tooltiptext = Open location request panel
urlbar-xr-notification-anchor =
    .tooltiptext = Open virtual reality permission panel
urlbar-storage-access-anchor =
    .tooltiptext = Open browsing activity permission panel
urlbar-web-rtc-share-screen-notification-anchor =
    .tooltiptext = Manage sharing your windows or screen with the site
urlbar-indexed-db-notification-anchor =
    .tooltiptext = Open offline storage message panel
urlbar-password-notification-anchor =
    .tooltiptext = Open save password message panel
urlbar-web-rtc-share-devices-notification-anchor =
    .tooltiptext = Manage sharing your camera and/or microphone with the site
# "Speakers" is used in a general sense that might include headphones or
# another audio output connection.
urlbar-web-rtc-share-speaker-notification-anchor =
    .tooltiptext = Manage sharing other speakers with the site
urlbar-autoplay-notification-anchor =
    .tooltiptext = Open autoplay panel
urlbar-persistent-storage-notification-anchor =
    .tooltiptext = Store data in Persistent Storage
urlbar-addons-notification-anchor =
    .tooltiptext = Open add-on installation message panel
urlbar-search-tips-confirm = Okay, Got It
urlbar-search-tips-confirm-short = Got it

urlbar-result-menu-button =
    .title = Open menu
urlbar-result-menu-button-feedback = Feedback
    .title = Open menu
urlbar-result-menu-learn-more =
    .label = Learn more
    .accesskey = L
urlbar-result-menu-remove-from-history =
    .label = Remove from history
    .accesskey = R
urlbar-result-menu-tip-get-help =
    .label = Get help
    .accesskey = h
urlbar-result-menu-dismiss-suggestion =
    .label = Dismiss this suggestion
    .accesskey = D
urlbar-result-menu-learn-more-about-firefox-suggest =
    .label = Learn more about { -firefox-suggest-brand-name }
    .accesskey = L
urlbar-result-menu-manage-firefox-suggest =
    .label = Manage { -firefox-suggest-brand-name }
    .accesskey = M
# Some urlbar suggestions show the user's approximate location as automatically
# detected by Firefox (e.g., weather suggestions), and this menu item lets the
# user tell Firefox that the location is not accurate. Typically the location
# will be a city name, or a city name combined with the name of its parent
# administrative region (e.g., a province, prefecture, or state).
urlbar-result-menu-report-inaccurate-location =
    .label = Report inaccurate location
urlbar-result-menu-show-less-frequently =
    .label = Show less frequently
urlbar-result-menu-dont-show-weather-suggestions =
    .label = Don’t show weather suggestions

# A message shown in the urlbar when the user submits feedback on a suggestion
# (e.g., it shows an inaccurate location, it's shown too often, etc.).
urlbar-feedback-acknowledgment = Thanks for your feedback

# A message shown in the urlbar when the user dismisses weather suggestions.
# Weather suggestions won't be shown at all anymore.
urlbar-dismissal-acknowledgment-weather = Thanks for your feedback. You won’t see weather suggestions anymore.

## Prompts users to use the Urlbar when they open a new tab or visit the
## homepage of their default search engine.
## Variables:
##  $engineName (String): The name of the user's default search engine. e.g. "Google" or "DuckDuckGo".

urlbar-search-tips-onboard = Type less, find more: Search { $engineName } right from your address bar.
urlbar-search-tips-redirect-2 = Start your search in the address bar to see suggestions from { $engineName } and your browsing history.

# Prompts users to use the Urlbar when they are typing in the domain of a
# search engine, e.g. google.com or amazon.com.
urlbar-tabtosearch-onboard = Select this shortcut to find what you need faster.

## Local search mode indicator labels in the urlbar

urlbar-search-mode-bookmarks = Bookmarks
urlbar-search-mode-tabs = Tabs
urlbar-search-mode-history = History
urlbar-search-mode-actions = Actions

##

urlbar-geolocation-blocked =
    .tooltiptext = You have blocked location information for this website.
urlbar-xr-blocked =
    .tooltiptext = You have blocked virtual reality device access for this website.
urlbar-web-notifications-blocked =
    .tooltiptext = You have blocked notifications for this website.
urlbar-camera-blocked =
    .tooltiptext = You have blocked your camera for this website.
urlbar-microphone-blocked =
    .tooltiptext = You have blocked your microphone for this website.
urlbar-screen-blocked =
    .tooltiptext = You have blocked this website from sharing your screen.
urlbar-persistent-storage-blocked =
    .tooltiptext = You have blocked persistent storage for this website.
urlbar-popup-blocked =
    .tooltiptext = You have blocked pop-ups for this website.
urlbar-autoplay-media-blocked =
    .tooltiptext = You have blocked autoplay media with sound for this website.
urlbar-canvas-blocked =
    .tooltiptext = You have blocked canvas data extraction for this website.
urlbar-midi-blocked =
    .tooltiptext = You have blocked MIDI access for this website.
urlbar-install-blocked =
    .tooltiptext = You have blocked add-on installation for this website.

# Variables
#   $shortcut (String) - A keyboard shortcut for the edit bookmark command.
urlbar-star-edit-bookmark =
    .tooltiptext = Edit this bookmark ({ $shortcut })

# Variables
#   $shortcut (String) - A keyboard shortcut for the add bookmark command.
urlbar-star-add-bookmark =
    .tooltiptext = Bookmark this page ({ $shortcut })

## Page Action Context Menu

page-action-manage-extension2 =
    .label = Manage Extension…
    .accesskey = E
page-action-remove-extension2 =
    .label = Remove Extension
    .accesskey = v

## Auto-hide Context Menu

full-screen-autohide =
    .label = Hide Toolbars
    .accesskey = H
full-screen-exit =
    .label = Exit Full Screen Mode
    .accesskey = F

## Search Engine selection buttons (one-offs)

# This string prompts the user to use the list of search shortcuts in
# the Urlbar and searchbar.
search-one-offs-with-title = This time, search with:

search-one-offs-change-settings-compact-button =
    .tooltiptext = Change search settings

search-one-offs-context-open-new-tab =
    .label = Search in New Tab
    .accesskey = T
search-one-offs-context-set-as-default =
    .label = Set as Default Search Engine
    .accesskey = D
search-one-offs-context-set-as-default-private =
    .label = Set as Default Search Engine for Private Windows
    .accesskey = P

# Search engine one-off buttons with an @alias shortcut/keyword.
# Variables:
#  $engineName (String): The name of the engine.
#  $alias (String): The @alias shortcut/keyword.
search-one-offs-engine-with-alias =
    .tooltiptext = { $engineName } ({ $alias })

# Shown when adding new engines from the address bar shortcut buttons or context
# menu, or from the search bar shortcut buttons.
# Variables:
#  $engineName (String): The name of the engine.
search-one-offs-add-engine =
    .label = Add “{ $engineName }”
    .tooltiptext = Add search engine “{ $engineName }”
    .aria-label = Add search engine “{ $engineName }”
# When more than 5 engines are offered by a web page, they are grouped in a
# submenu using this as its label.
search-one-offs-add-engine-menu =
    .label = Add search engine

## Local search mode one-off buttons
## Variables:
##  $restrict (String): The restriction token corresponding to the search mode.
##    Restriction tokens are special characters users can type in the urlbar to
##    restrict their searches to certain sources (e.g., "*" to search only
##    bookmarks).

search-one-offs-bookmarks =
    .tooltiptext = Bookmarks ({ $restrict })
search-one-offs-tabs =
    .tooltiptext = Tabs ({ $restrict })
search-one-offs-history =
    .tooltiptext = History ({ $restrict })
search-one-offs-actions =
    .tooltiptext = Actions ({ $restrict })

## QuickActions are shown in the urlbar as the user types a matching string
## The -cmd- strings are comma separated list of keywords that will match
## the action.

# Opens the about:addons page in the home / recommendations section
quickactions-addons = View add-ons
# In English we provide multiple spellings for "add-ons". If that's not
# applicable to your language, only use the correct spelling (don't repeat the
# same word).
quickactions-cmd-addons3 = extensions, themes, addons, add-ons

# Opens the bookmarks library window
quickactions-bookmarks2 = Manage bookmarks
quickactions-cmd-bookmarks = bookmarks

# Opens a SUMO article explaining how to clear history
quickactions-clearrecenthistory = Clear recent history
quickactions-cmd-clearrecenthistory = clear recent history, history

# Opens about:downloads page
quickactions-downloads2 = View downloads
quickactions-cmd-downloads = downloads

# Opens about:addons page in the extensions section
quickactions-extensions = Manage extensions
quickactions-cmd-extensions = extensions

# Opens Firefox View
quickactions-firefoxview = Open { -firefoxview-brand-name }
# English is using "view" and "open view", since the feature name is
# "Firefox View". If you have translated the name in your language, you
# should use a word related to the existing translation.
quickactions-cmd-firefoxview = open { -firefoxview-brand-name }, { -firefoxview-brand-name }, open view, view

# Opens SUMO home page
quickactions-help = { -brand-product-name } help
quickactions-cmd-help = help, support

# Opens the devtools web inspector
quickactions-inspector2 = Open Developer Tools
quickactions-cmd-inspector2 = inspector, devtools, dev tools

# Opens about:logins
quickactions-logins2 = Manage passwords
quickactions-cmd-logins = logins, passwords

# Opens the print dialog
quickactions-print2 = Print page
quickactions-cmd-print = print

# Opens the print dialog at the save to PDF option
quickactions-savepdf = Save page as PDF
quickactions-cmd-savepdf2 = pdf, save page

# Opens a new private browsing window
quickactions-private2 = Open private window
quickactions-cmd-private = private browsing

# Opens a SUMO article explaining how to refresh
quickactions-refresh = Refresh { -brand-short-name }
quickactions-cmd-refresh = refresh

# Restarts the browser
quickactions-restart = Restart { -brand-short-name }
quickactions-cmd-restart = restart

# Opens the screenshot tool
quickactions-screenshot3 = Take a screenshot
quickactions-cmd-screenshot2 = screenshot, take a screenshot

# Opens about:preferences
quickactions-settings2 = Manage settings
# "manage" should match the corresponding command, which is “Manage settings” in English.
quickactions-cmd-settings2 = settings, preferences, options, manage

# Opens about:addons page in the themes section
quickactions-themes = Manage themes
quickactions-cmd-themes = themes

# Opens a SUMO article explaining how to update the browser
quickactions-update = Update { -brand-short-name }
quickactions-cmd-update = update

# Opens the view-source UI with current pages source
quickactions-viewsource2 = View page source
quickactions-cmd-viewsource2 = view source, source, page source

# Tooltip text for the help button shown in the result.
quickactions-learn-more =
    .title = Learn more about Quick actions

# Will be shown to users the first configurable number of times
# they experience actions giving them instructions on how to
# select the action shown by pressing the tab key.
press-tab-label = Press tab to select:

## Bookmark Panel

bookmarks-add-bookmark = Add bookmark
bookmarks-edit-bookmark = Edit bookmark
bookmark-panel-cancel =
    .label = Cancel
    .accesskey = C
# Variables:
#  $count (number): number of bookmarks that will be removed
bookmark-panel-remove =
    .label =
        { $count ->
            [1] Remove bookmark
           *[other] Remove { $count } bookmarks
        }
    .accesskey = R
bookmark-panel-show-editor-checkbox =
    .label = Show editor when saving
    .accesskey = S
bookmark-panel-save-button =
    .label = Save

# Width of the bookmark panel.
# Should be large enough to fully display the Done and
# Cancel/Remove Bookmark buttons.
bookmark-panel =
    .style = min-width: 23em

## Identity Panel

# Variables
#  $host (String): the hostname of the site that is being displayed.
identity-site-information = Site information for { $host }
# Variables
#  $host (String): the hostname of the site that is being displayed.
identity-header-security-with-host =
    .title = Connection security for { $host }
identity-connection-not-secure = Connection not secure
identity-connection-secure = Connection secure
identity-connection-failure = Connection failure
identity-connection-internal = This is a secure { -brand-short-name } page.
identity-connection-file = This page is stored on your computer.
identity-connection-associated = This page is loaded from another page.
identity-extension-page = This page is loaded from an extension.
identity-active-blocked = { -brand-short-name } has blocked parts of this page that are not secure.
identity-custom-root = Connection verified by a certificate issuer that is not recognized by Mozilla.
identity-passive-loaded = Parts of this page are not secure (such as images).
identity-active-loaded = You have disabled protection on this page.
identity-weak-encryption = This page uses weak encryption.

identity-https-only-connection-upgraded = (upgraded to HTTPS)
identity-https-only-label2 = Automatically upgrade this site to a secure connection
identity-https-only-dropdown-on =
    .label = On
identity-https-only-dropdown-off =
    .label = Off
identity-https-only-dropdown-off-temporarily =
    .label = Off temporarily
identity-https-only-info-turn-on3 = Turn on HTTPS upgrades for this site if you want { -brand-short-name } to upgrade the connection when possible.
identity-https-only-info-turn-off3 = If the page seems broken, you may want to turn off HTTPS upgrades for this site to reload using insecure HTTP.
identity-https-only-info-no-upgrade = Unable to upgrade connection from HTTP.

identity-permissions-storage-access-header = Cross-site cookies
identity-permissions-storage-access-hint = These parties can use cross-site cookies and site data while you are on this site.
identity-permissions-storage-access-learn-more = Learn more

identity-permissions-reload-hint = You may need to reload the page for changes to apply.
identity-clear-site-data =
    .label = Clear cookies and site data…
identity-connection-not-secure-security-view = You are not securely connected to this site.
identity-connection-verified = You are securely connected to this site.
identity-ev-owner-label = Certificate issued to:
identity-description-custom-root2 = Mozilla does not recognize this certificate issuer. It may have been added from your operating system or by an administrator.
identity-remove-cert-exception =
    .label = Remove Exception
    .accesskey = R
identity-description-insecure = Your connection to this site is not private. Information you submit could be viewed by others (like passwords, messages, credit cards, etc.).
identity-description-weak-cipher-intro = Your connection to this website uses weak encryption and is not private.
identity-description-weak-cipher-risk = Other people can view your information or modify the website’s behavior.
identity-description-active-blocked2 = { -brand-short-name } has blocked parts of this page that are not secure.
identity-description-passive-loaded = Your connection is not private and information you share with the site could be viewed by others.
identity-description-passive-loaded-insecure2 = This website contains content that is not secure (such as images).
identity-description-passive-loaded-mixed2 = Although { -brand-short-name } has blocked some content, there is still content on the page that is not secure (such as images).
identity-description-active-loaded = This website contains content that is not secure (such as scripts) and your connection to it is not private.
identity-description-active-loaded-insecure = Information you share with this site could be viewed by others (like passwords, messages, credit cards, etc.).
identity-more-info-link-text =
    .label = More information

## Window controls

browser-window-minimize-button =
    .tooltiptext = Minimize
browser-window-maximize-button =
    .tooltiptext = Maximize
browser-window-restore-down-button =
    .tooltiptext = Restore Down
browser-window-close-button =
    .tooltiptext = Close

## Tab actions

# This label should be written in all capital letters if your locale supports them.
browser-tab-audio-pip = PICTURE-IN-PICTURE

## Bookmarks toolbar items

browser-import-button2 =
    .label = Import bookmarks…
    .tooltiptext = Import bookmarks from another browser to { -brand-short-name }.

bookmarks-toolbar-empty-message = For quick access, place your bookmarks here on the bookmarks toolbar. <a data-l10n-name="manage-bookmarks">Manage bookmarks…</a>

## WebRTC Pop-up notifications

popup-select-camera-device =
    .value = Camera:
    .accesskey = C
popup-select-camera-icon =
    .tooltiptext = Camera
popup-select-microphone-device =
    .value = Microphone:
    .accesskey = M
popup-select-microphone-icon =
    .tooltiptext = Microphone
popup-select-speaker-icon =
    .tooltiptext = Speakers
popup-select-window-or-screen =
    .label = Window or screen:
    .accesskey = W
popup-all-windows-shared = All visible windows on your screen will be shared.

## WebRTC window or screen share tab switch warning

sharing-warning-window = You are sharing { -brand-short-name }. Other people can see when you switch to a new tab.
sharing-warning-screen = You are sharing your entire screen. Other people can see when you switch to a new tab.
sharing-warning-proceed-to-tab =
  .label = Proceed to Tab
sharing-warning-disable-for-session =
  .label = Disable sharing protection for this session

## URL Bar

# This string is used as an accessible name to the "X" button that cancels a custom search mode (i.e. exits the Amazon.com search mode).
urlbar-search-mode-indicator-close =
  .aria-label = Close

# This placeholder is used when not in search mode and the user's default search
# engine is unknown.
urlbar-placeholder =
  .placeholder = Search or enter address

# This placeholder is used in search mode with search engines that search the
# entire web.
# Variables
#  $name (String): the name of a search engine that searches the entire Web
#  (e.g. Google).
urlbar-placeholder-search-mode-web-2 =
  .placeholder = Search the Web
  .aria-label = Search with { $name }

# This placeholder is used in search mode with search engines that search a
# specific site (e.g., Amazon).
# Variables
#  $name (String): the name of a search engine that searches a specific site
#  (e.g. Amazon).
urlbar-placeholder-search-mode-other-engine =
  .placeholder = Enter search terms
  .aria-label = Search { $name }

# This placeholder is used when searching bookmarks.
urlbar-placeholder-search-mode-other-bookmarks =
  .placeholder = Enter search terms
  .aria-label = Search bookmarks

# This placeholder is used when searching history.
urlbar-placeholder-search-mode-other-history =
  .placeholder = Enter search terms
  .aria-label = Search history

# This placeholder is used when searching open tabs.
urlbar-placeholder-search-mode-other-tabs =
  .placeholder = Enter search terms
  .aria-label = Search tabs

# This placeholder is used when searching quick actions.
urlbar-placeholder-search-mode-other-actions =
  .placeholder = Enter search terms
  .aria-label = Search actions

# Variables
#  $name (String): the name of the user's default search engine
urlbar-placeholder-with-name =
  .placeholder = Search with { $name } or enter address

# Variables
#  $component (String): the name of the component which forces remote control.
#    Example: "DevTools", "Marionette", "RemoteAgent".
urlbar-remote-control-notification-anchor2 =
  .tooltiptext = Browser is under remote control (reason: { $component })
urlbar-permissions-granted =
  .tooltiptext = You have granted this website additional permissions.
urlbar-switch-to-tab =
  .value = Switch to tab:

# Used to indicate that a selected autocomplete entry is provided by an extension.
urlbar-extension =
  .value = Extension:

urlbar-go-button =
  .tooltiptext = Go to the address in the Location Bar
urlbar-page-action-button =
  .tooltiptext = Page actions
urlbar-revert-button =
  .tooltiptext = Show the address in the Location Bar

## Action text shown in urlbar results, usually appended after the search
## string or the url, like "result value - action text".

# Used when the private browsing engine differs from the default engine.
# The "with" format was chosen because the search engine name can end with
# "Search", and we would like to avoid strings like "Search MSN Search".
# Variables
#  $engine (String): the name of a search engine
urlbar-result-action-search-in-private-w-engine = Search with { $engine } in a Private Window
# Used when the private browsing engine is the same as the default engine.
urlbar-result-action-search-in-private = Search in a Private Window
# The "with" format was chosen because the search engine name can end with
# "Search", and we would like to avoid strings like "Search MSN Search".
# Variables
#  $engine (String): the name of a search engine
urlbar-result-action-search-w-engine = Search with { $engine }
urlbar-result-action-sponsored = Sponsored
urlbar-result-action-switch-tab = Switch to Tab
urlbar-result-action-visit = Visit
# "Switch to tab with container" is used when the target tab is located in a
# different container.
# Variables
# $container (String): the name of the target container
urlbar-result-action-switch-tab-with-container = Switch to Tab · <span>{ $container }</span>
# Allows the user to visit a URL that was previously copied to the clipboard.
urlbar-result-action-visit-from-clipboard = Visit from clipboard
# Directs a user to press the Tab key to perform a search with the specified
# engine.
# Variables
#  $engine (String): the name of a search engine that searches the entire Web
#  (e.g. Google).
urlbar-result-action-before-tabtosearch-web = Press Tab to search with { $engine }
# Directs a user to press the Tab key to perform a search with the specified
# engine.
# Variables
#  $engine (String): the name of a search engine that searches a specific site
#  (e.g. Amazon).
urlbar-result-action-before-tabtosearch-other = Press Tab to search { $engine }
# Variables
#  $engine (String): the name of a search engine that searches the entire Web
#  (e.g. Google).
urlbar-result-action-tabtosearch-web = Search with { $engine } directly from the address bar
# Variables
#  $engine (String): the name of a search engine that searches a specific site
#  (e.g. Amazon).
urlbar-result-action-tabtosearch-other-engine = Search { $engine } directly from the address bar
# Action text for copying to clipboard.
urlbar-result-action-copy-to-clipboard = Copy
# Shows the result of a formula expression being calculated, the last = sign will be shown
# as part of the result (e.g. "= 2").
# Variables
#  $result (String): the string representation for a formula result
urlbar-result-action-calculator-result = = { $result }
# The string returned for an undefined calculator result such as when dividing by 0
urlbar-result-action-undefined-calculator-result = undefined
# Shows the result of a formula expression being calculated, in scientific notation.
# The last = sign will be shown as part of the result (e.g. "= 1.0e17").
# Variables
#  $result (String): the string representation for a result in scientific notation
#  (e.g. "1.0e17").
urlbar-result-action-calculator-result-scientific-notation = = { $result }
# Shows the result of a formula expression being calculated, this is used for numbers >= 1.
# The last = sign will be shown as part of the result (e.g. "= 2").
# Variables
#  $result (String): the string representation for a formula result
urlbar-result-action-calculator-result-3 = = { NUMBER($result, useGrouping: "false", maximumFractionDigits: 8)}
# Shows the result of a formula expression being calculated, to a maximum of 9 significant
# digits. This is used for numbers < 1.
# The last = sign will be shown as part of the result (e.g. "= 0.333333333").
# Variables
#  $result (String): the string representation for a formula result
urlbar-result-action-calculator-result-decimal = = { NUMBER($result, maximumSignificantDigits: 9)}

## Strings used for buttons in the urlbar

# Searchmode Switcher button
# Variables:
#   $engine (String): the current default search engine.
urlbar-searchmode-button2 =
    .label = { $engine }, pick a search engine
    .tooltiptext = { $engine }, pick a search engine
urlbar-searchmode-button-no-engine =
    .label = No shortcut selected, pick a shortcut
    .tooltiptext = No shortcut selected, pick a shortcut
urlbar-searchmode-dropmarker =
    .tooltiptext = Pick a Search Engine
urlbar-searchmode-bookmarks =
    .label = Bookmarks
urlbar-searchmode-tabs =
    .label = Tabs
urlbar-searchmode-history =
    .label = History
urlbar-searchmode-actions =
    .label = Actions
urlbar-searchmode-exit-button =
    .tooltiptext = Close

# Label shown on the top of Searchmode Switcher popup. After this label, the
# available search engines will be listed.
urlbar-searchmode-popup-description = This time search with:
urlbar-searchmode-popup-search-settings-menuitem =
    .label = Search Settings

# Label prompting user to search with a particular search engine.
#  $engine (String): the name of a search engine that searches a specific site
urlbar-result-search-with = Search with { $engine }

# Label for the urlbar result row, prompting the user to use a local keyword to enter search mode.
#  $keywords (String): the restrict keyword to enter search mode.
#  $localSearchMode (String): the local search mode (history, tabs, bookmarks,
#  or actions) to search with.
urlbar-result-search-with-local-search-mode = { $keywords } - Search { $localSearchMode }

# Label for the urlbar result row, prompting the user to use engine keywords to enter search mode.
#  $keywords (String): the default keyword and user's set keyword if available
#  $engine (String): the name of a search engine
urlbar-result-search-with-engine-keywords = {$keywords} - Search with { $engine }

## Action text shown in urlbar results, usually appended after the search
## string or the url, like "result value - action text".
## In these actions "Search" is a verb, followed by where the search is performed.

urlbar-result-action-search-bookmarks = Search Bookmarks
urlbar-result-action-search-history = Search History
urlbar-result-action-search-tabs = Search Tabs
urlbar-result-action-search-actions = Search Actions

# Label for a quickaction result used to switch to an open tab group.
#  $group (String): the name of the tab group to switch to
urlbar-result-action-switch-to-tabgroup = Switch to { $group }
# Label for a quickaction result used to re-opan a saved tab group.
#  $group (String): the name of the tab group to re-open
urlbar-result-action-open-saved-tabgroup = Open { $group }

## Labels shown above groups of urlbar results

# A label shown above the "Firefox Suggest" (bookmarks/history) group in the
# urlbar results.
urlbar-group-firefox-suggest =
  .label = { -firefox-suggest-brand-name }

# A label shown above the search suggestions group in the urlbar results. It
# should use sentence case.
# Variables
#  $engine (String): the name of the search engine providing the suggestions
urlbar-group-search-suggestions =
  .label = { $engine } suggestions

# A label shown above Quick Actions in the urlbar results.
urlbar-group-quickactions =
  .label = Quick Actions

# A label shown above the recent searches group in the urlbar results.
# Variables
#  $engine (String): the name of the search engine used to search.
urlbar-group-recent-searches =
  .label = Recent Searches

# The header shown above trending results.
# Variables:
#  $engine (String): the name of the search engine providing the trending suggestions
urlbar-group-trending =
  .label = Trending on { $engine }

# The result menu labels shown next to trending results.
urlbar-result-menu-trending-dont-show =
    .label = Don’t show trending searches
    .accesskey = D
urlbar-result-menu-trending-why =
    .label = Why am I seeing this?
    .accesskey = W

# A message that replaces a result when the user dismisses all suggestions of a
# particular type.
urlbar-trending-dismissal-acknowledgment = Thanks for your feedback. You won’t see trending searches anymore.

## Reader View toolbar buttons

# This should match menu-view-enter-readerview in menubar.ftl
reader-view-enter-button =
    .aria-label = Enter Reader View
# This should match menu-view-close-readerview in menubar.ftl
reader-view-close-button =
    .aria-label = Close Reader View

## Picture-in-Picture urlbar button
## Variables:
##   $shortcut (String) - Keyboard shortcut to execute the command.

picture-in-picture-urlbar-button-open =
 .tooltiptext = Open Picture-in-Picture ({ $shortcut })

picture-in-picture-urlbar-button-close =
 .tooltiptext = Close Picture-in-Picture ({ $shortcut })

picture-in-picture-panel-header = Picture-in-Picture
picture-in-picture-panel-headline = This website does not recommend Picture-in-Picture
picture-in-picture-panel-body = Videos might not display as the developer intended while Picture-in-Picture is enabled.
picture-in-picture-enable-toggle =
  .label = Enable anyway

## Full Screen and Pointer Lock UI

# Please ensure that the domain stays in the `<span data-l10n-name="domain">` markup.
# Variables
#  $domain (String): the domain that is full screen, e.g. "mozilla.org"
fullscreen-warning-domain = <span data-l10n-name="domain">{ $domain }</span> is now full screen
fullscreen-warning-no-domain = This document is now full screen


fullscreen-exit-button = Exit Full Screen (Esc)
# "esc" is lowercase on mac keyboards, but uppercase elsewhere.
fullscreen-exit-mac-button = Exit Full Screen (esc)

# Please ensure that the domain stays in the `<span data-l10n-name="domain">` markup.
# Variables
#  $domain (String): the domain that is using pointer-lock, e.g. "mozilla.org"
pointerlock-warning-domain = <span data-l10n-name="domain">{ $domain }</span> has control of your pointer. Press Esc to take back control.
pointerlock-warning-no-domain = This document has control of your pointer. Press Esc to take back control.

## Bookmarks panels, menus and toolbar

bookmarks-manage-bookmarks =
  .label = Manage bookmarks
bookmarks-recent-bookmarks-panel-subheader = Recent bookmarks
bookmarks-toolbar-chevron =
  .tooltiptext = Show more bookmarks
bookmarks-sidebar-content =
  .aria-label = Bookmarks
bookmarks-menu-button =
  .label = Bookmarks menu
bookmarks-other-bookmarks-menu =
  .label = Other bookmarks
bookmarks-mobile-bookmarks-menu =
  .label = Mobile bookmarks

## Variables:
##   $isVisible (boolean): if the specific element (e.g. bookmarks sidebar,
##                         bookmarks toolbar, etc.) is visible or not.

bookmarks-tools-sidebar-visibility =
  .label = { $isVisible ->
     [true] Hide bookmarks sidebar
    *[other] View bookmarks sidebar
  }
bookmarks-tools-toolbar-visibility-menuitem =
  .label = { $isVisible ->
     [true] Hide Bookmarks Toolbar
    *[other] View Bookmarks Toolbar
  }
bookmarks-tools-toolbar-visibility-panel =
  .label = { $isVisible ->
     [true] Hide bookmarks toolbar
    *[other] Show bookmarks toolbar
  }

##

bookmarks-search =
  .label = Search bookmarks
bookmarks-tools =
  .label = Bookmarking Tools
bookmarks-subview-edit-bookmark =
  .label = Edit this bookmark…

# The aria-label is a spoken label that should not include the word "toolbar" or
# such, because screen readers already know that this container is a toolbar.
# This avoids double-speaking.
bookmarks-toolbar =
  .toolbarname = Bookmarks Toolbar
  .accesskey = B
  .aria-label = Bookmarks
bookmarks-toolbar-menu =
  .label = Bookmarks toolbar
bookmarks-toolbar-placeholder =
  .title = Bookmarks toolbar items
bookmarks-toolbar-placeholder-button =
  .label = Bookmarks toolbar items

# "Bookmark" is a verb, as in "Add current tab to bookmarks".
bookmarks-subview-bookmark-tab =
  .label = Bookmark current tab…

## Library Panel items

library-bookmarks-menu =
  .label = Bookmarks

## Pocket toolbar button

save-to-pocket-button =
  .label = Save to { -pocket-brand-name }
  .tooltiptext = Save to { -pocket-brand-name }

## Repair text encoding toolbar button

repair-text-encoding-button =
  .label = Repair text encoding
  .tooltiptext = Guess correct text encoding from page content

## Customize Toolbar Buttons

# Variables:
#  $shortcut (String): keyboard shortcut to open settings (only on macOS)
toolbar-settings-button =
  .label = Settings
  .tooltiptext = { PLATFORM() ->
      [macos] Open settings ({ $shortcut })
     *[other] Open settings
  }

toolbar-overflow-customize-button =
  .label = Customize toolbar…
  .accesskey = C

toolbar-button-email-link =
  .label = Email link
  .tooltiptext = Email a link to this page

toolbar-button-logins =
  .label = Passwords
  .tooltiptext = View and manage your saved passwords

# Variables:
#  $shortcut (String): keyboard shortcut to save a copy of the page
toolbar-button-save-page =
  .label = Save page
  .tooltiptext = Save this page ({ $shortcut })

# Variables:
#  $shortcut (String): keyboard shortcut to open a local file
toolbar-button-open-file =
  .label = Open file
  .tooltiptext = Open a file ({ $shortcut })

toolbar-button-synced-tabs =
  .label = Synced tabs
  .tooltiptext = Show tabs from other devices

# Variables
# $shortcut (string) - Keyboard shortcut to open a new private browsing window
toolbar-button-new-private-window =
  .label = New private window
  .tooltiptext = Open a new private browsing window ({ $shortcut })

## EME notification panel

eme-notifications-drm-content-playing = Some audio or video on this site uses DRM software, which may limit what { -brand-short-name } can let you do with it.
eme-notifications-drm-content-playing-manage = Manage settings
eme-notifications-drm-content-playing-manage-accesskey = M
eme-notifications-drm-content-playing-dismiss = Dismiss
eme-notifications-drm-content-playing-dismiss-accesskey = D

## Password save/update panel

panel-save-update-username = Username
panel-save-update-password = Password

##

# "More" item in macOS share menu
menu-share-more =
    .label = More…
menu-share-copy-link =
    .label = Copy Link
    .accesskey = L
ui-tour-info-panel-close =
    .tooltiptext = Close

## Variables:
##  $uriHost (String): URI host for which the popup was allowed or blocked.

popups-infobar-allow =
    .label = Allow pop-ups for { $uriHost }
    .accesskey = p

popups-infobar-block =
    .label = Block pop-ups for { $uriHost }
    .accesskey = p

##

popups-infobar-dont-show-message =
    .label = Don’t show this message when pop-ups are blocked
    .accesskey = D

edit-popup-settings =
    .label = Manage pop-up settings…
    .accesskey = M

picture-in-picture-hide-toggle =
    .label = Hide Picture-in-Picture Toggle
    .accesskey = H

## Since the default position for PiP controls does not change for RTL layout,
## right-to-left languages should use "Left" and "Right" as in the English strings,

picture-in-picture-move-toggle-right =
    .label = Move Picture-in-Picture Toggle to Right Side
    .accesskey = R

picture-in-picture-move-toggle-left =
    .label = Move Picture-in-Picture Toggle to Left Side
    .accesskey = L

##

# Navigator Toolbox

# This string is a spoken label that should not include
# the word "toolbar" or such, because screen readers already know that
# this container is a toolbar. This avoids double-speaking.
navbar-accessible =
    .aria-label = Navigation

navbar-downloads =
    .label = Downloads

navbar-overflow =
    .tooltiptext = More tools…

# Variables:
#   $shortcut (String): keyboard shortcut to print the page
navbar-print =
    .label = Print
    .tooltiptext = Print this page… ({ $shortcut })

navbar-home =
    .label = Home
    .tooltiptext = { -brand-short-name } Home Page

navbar-library =
    .label = Library
    .tooltiptext = View history, saved bookmarks, and more

navbar-search =
    .title = Search

# Name for the tabs toolbar as spoken by screen readers. The word
# "toolbar" is appended automatically and should not be included in
# in the string
tabs-toolbar =
    .aria-label = Browser tabs

tabs-toolbar-new-tab =
    .label = New Tab

tabs-toolbar-list-all-tabs =
    .label = List all tabs
    .tooltiptext = List all tabs

## Infobar shown at startup to suggest session-restore

# <img data-l10n-name="icon"/> will be replaced by the application menu icon
restore-session-startup-suggestion-message = <strong>Open previous tabs?</strong> You can restore your previous session from the { -brand-short-name } application menu <img data-l10n-name="icon"/>, under History.
restore-session-startup-suggestion-button = Show me how

## Infobar shown when the user tries to open a file picker and file pickers are blocked by enterprise policy

filepicker-blocked-infobar = Your organization has blocked access to local files on this computer

## Mozilla data reporting notification (Telemetry, Firefox Health Report, etc)

data-reporting-notification-message = { -brand-short-name } automatically sends some data to { -vendor-short-name } so that we can improve your experience.
data-reporting-notification-button =
    .label = Choose What I Share
    .accesskey = C

# Label for the indicator shown in the private browsing window titlebar.
private-browsing-indicator-label = Private browsing

# Tooltip for the indicator shown in the private browsing window titlebar.
private-browsing-indicator-tooltip =
    .tooltiptext = Private browsing

# Tooltip for the indicator shown in the window titlebar when content analysis is active.
# Variables:
#   $agentName (String): The name of the DLP agent that is connected
content-analysis-indicator-tooltip =
    .tooltiptext = Data loss prevention (DLP) by { $agentName }. Click for more info.
content-analysis-panel-title = Data protection
# Variables:
#   $agentName (String): The name of the DLP agent that is connected
content-analysis-panel-text-styled = Your organization uses <b>{ $agentName }</b> to protect against data loss. <a data-l10n-name="info">Learn more</a>

## Unified extensions (toolbar) button

unified-extensions-button =
    .label = Extensions
    .tooltiptext = Extensions

## Unified extensions button when permission(s) are needed.
## Note that the new line is intentionally part of the tooltip.

unified-extensions-button-permissions-needed =
    .label = Extensions
    .tooltiptext =
        Extensions
        Permissions needed

## Unified extensions button when some extensions are quarantined.
## Note that the new line is intentionally part of the tooltip.

unified-extensions-button-quarantined =
    .label = Extensions
    .tooltiptext =
        Extensions
        Some extensions are not allowed

## Unified extensions button when some extensions are disabled (e.g. through add-ons blocklist).
## Note that the new line is intentionally part of the tooltip.

unified-extensions-button-blocklisted =
    .label = Extensions
    .tooltiptext =
        Extensions
        Some extensions are disabled

## Private browsing reset button

reset-pbm-toolbar-button =
    .label = End Private Session
    .tooltiptext = End Private Session
reset-pbm-panel-heading = End your private session?
reset-pbm-panel-description = Close all private tabs and delete history, cookies, and all other site data.
reset-pbm-panel-always-ask-checkbox =
     .label = Always ask me
     .accesskey = A
reset-pbm-panel-cancel-button =
    .label = Cancel
    .accesskey = C
reset-pbm-panel-confirm-button =
    .label = Delete session data
    .accesskey = D
reset-pbm-panel-complete = Private session data deleted

## Autorefresh blocker

refresh-blocked-refresh-label = { -brand-short-name } prevented this page from automatically reloading.
refresh-blocked-redirect-label = { -brand-short-name } prevented this page from automatically redirecting to another page.

refresh-blocked-allow =
    .label = Allow
    .accesskey = A

## Firefox Relay integration

firefox-relay-offer-why-to-use-relay = Our secure, easy-to-use masks protect your identity and prevent spam by hiding your email address.

# Variables:
#  $useremail (String): user email that will receive messages
firefox-relay-offer-what-relay-provides = All emails sent to your email masks will be forwarded to <strong>{ $useremail }</strong> (unless you decide to block them).

firefox-relay-offer-legal-notice = By clicking “Use email mask”, you agree to the <label data-l10n-name="tos-url">Terms of Service</label> and <label data-l10n-name="privacy-url">Privacy Notice</label>.

## Add-on Pop-up Notifications

popup-notification-addon-install-unsigned =
    .value = (Unverified)
popup-notification-xpinstall-prompt-learn-more = Learn more about installing add-ons safely

popup-notification-xpinstall-prompt-block-url = See details

# Note: Access key is set to p to match "private" in the corresponding localized label.
popup-notification-addon-privatebrowsing-checkbox2 =
    .label = Allow extension to run in private windows
    .accesskey = p

## Pop-up warning

# Variables:
#   $popupCount (Number): the number of pop-ups blocked.
popup-warning-message =
    { $popupCount ->
        [1] { -brand-short-name } prevented this site from opening a pop-up window.
       *[other] { -brand-short-name } prevented this site from opening { $popupCount } pop-up windows.
    }
# The singular form is left out for English, since the number of blocked pop-ups is always greater than 1.
# Variables:
#   $popupCount (Number): the number of pop-ups blocked.
popup-warning-exceeded-message =
    { $popupCount ->
       *[other] { -brand-short-name } prevented this site from opening more than { $popupCount } pop-up windows.
    }
popup-warning-button =
    .label =
        { PLATFORM() ->
            [windows] Options
           *[other] Preferences
        }
    .accesskey =
        { PLATFORM() ->
            [windows] O
           *[other] P
        }

# Variables:
#   $popupURI (String): the URI for the pop-up window
popup-show-popup-menuitem =
    .label = Show “{ $popupURI }”

## File-picker crash notification ("FilePickerCrashed.sys.mjs")

file-picker-failed-open = The Windows file-dialog could not be opened. No file or folder could be selected.
#   $path (string): The full path to which the file will be saved (e.g., 'C:\Users\Default User\Downloads\readme.txt').
file-picker-failed-save-somewhere = The Windows file-dialog could not be opened. The file will be saved to { $path }.
file-picker-failed-save-nowhere = The Windows file-dialog could not be opened. No default folder could be found; the file will not be saved.

file-picker-crashed-open = The Windows file-dialog has crashed. No file or folder could be selected.
#   $path (string): The full path to which the file will be saved (e.g., 'C:\Users\Default User\Downloads\readme.txt').
file-picker-crashed-save-somewhere = The Windows file-dialog has crashed. The file will be saved to { $path }.
file-picker-crashed-save-nowhere = The Windows file-dialog has crashed. No default folder could be found; the file will not be saved.

# Button used with file-picker-crashed-save-default. Opens the folder in Windows
# Explorer, with the saved file selected and in focus.
#
# The wording here should be consistent with the Windows variant of
# `downloads-cmd-show-menuitem-2` and similar messages.

file-picker-crashed-show-in-folder =
    .label = Show in Folder
    .accessKey = F

## Onboarding Finish Setup checklist

onboarding-checklist-button-label = Finish setup
