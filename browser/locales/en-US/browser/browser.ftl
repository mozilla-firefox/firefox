# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# This is the initial default title for the browser window.
# It gets updated based on loaded tabs or private browsing state.
browser-main-window-default-title = { -brand-full-name }

# Note: only on macOS do we use a `-` separator between the brand name and the
# "Private Browsing" suffix.
browser-main-private-window-title = { PLATFORM() ->
      [macos] { -brand-full-name } — Private Browsing
     *[other] { -brand-full-name } Private Browsing
  }

# This is only used on macOS; on other OSes we use the full private window
# title (so including the brand name) as a suffix
browser-main-private-suffix-for-content = Private Browsing

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
urlbar-serial-notification-anchor =
    .tooltiptext = Open Serial panel
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
urlbar-localhost-notification-anchor =
    .tooltiptext = Manage local device access for this site
urlbar-local-network-notification-anchor =
    .tooltiptext = Manage sharing your local network access with this site
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
urlbar-result-menu-manage-firefox-suggest =
    .label = Manage { -firefox-suggest-brand-name }
    .accesskey = M
# Some urlbar suggestions show the user's approximate location as automatically
# detected by Firefox (e.g., weather suggestions), and this menu item lets the
# user tell Firefox that the location is not accurate. Typically the location
# will be a city name, or a city name combined with the name of its parent
# administrative division (e.g., a province, prefecture, or state).
urlbar-result-menu-report-inaccurate-location =
    .label = Report inaccurate location
urlbar-result-menu-show-less-frequently =
    .label = Show less frequently
urlbar-result-menu-dont-show-weather-suggestions =
    .label = Don’t show weather suggestions

# Used for Split Button.
urlbar-splitbutton-dropmarker =
    .title = Open menu

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
urlbar-localhost-blocked =
    .tooltiptext = You have blocked local device connections for this website.
urlbar-local-network-blocked =
    .tooltiptext = You have blocked local network connections for this website.
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
urlbar-popup-blocked2 =
    .tooltiptext = You have blocked pop-ups and third-party redirects for this website.
urlbar-autoplay-media-blocked =
    .tooltiptext = You have blocked autoplay media with sound for this website.
urlbar-canvas-blocked =
    .tooltiptext = You have blocked canvas data extraction for this website.
urlbar-midi-blocked =
    .tooltiptext = You have blocked MIDI access for this website.
urlbar-serial-blocked =
    .tooltiptext = You have blocked serial port access for this website.
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

urlbar-split-view-button =
    .tooltiptext = Split view
    .aria-label = Split view

## Searchbar context menu

clear-search-history =
    .label = Clear Search History
    .accesskey = H

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
## the action. English commas should be used, i.e. ,

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
quickactions-cmd-extensions2 = extensions, addons, add-ons

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

# Opens Firefox Library
quickactions-cmd-library = library
quickactions-library = Open Library

# Opens about:logins
quickactions-logins2 = Manage passwords
quickactions-cmd-logins = logins, passwords

# Mutes all tabs playing audio
quickactions-mute = Mute tabs playing audio
# List of words that would trigger the "mute tabs" action from the address bar.
# Replace with idiomatic expressions in your language to silence something or
# someone.
quickactions-cmd-mute = mute, shush, sssssh

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

# Opens about:translations
quickactions-translate = Translate
quickactions-cmd-translate = translate

# Opens about:preferences
quickactions-settings2 = Manage settings
# "manage" should match the corresponding command, which is “Manage settings” in English.
quickactions-cmd-settings2 = settings, preferences, options, manage

# Opens about:addons page in the themes section
quickactions-themes = Manage themes
# In English we provide multiple spellings for "add-ons". If that's not
# applicable to your language, only use the correct spelling (don't repeat the
# same word).
quickactions-cmd-themes2 = themes, add-ons, addons

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
identity-verifier-label = Verified by:
# "qualified" here refers to the qualified website authentication certificate presented by the site.
identity-etsi = Qualified as specified in Regulation (EU) 2024/1183.
identity-description-custom-root2 = Mozilla does not recognize this certificate issuer. It may have been added from your operating system or by an administrator.
identity-cert-exception-overridden = You have added a security exception for this site.
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
# Clicking this button closes the window and returns to the tab where it was opened from
browser-window-return-to-opener =
    .tooltiptext = Return

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

## WebSerial "select a port" popup

webserial-select-port-label = Select a serial port:
webserial-no-ports-available = No serial ports available

## URL Bar

# This string is used as an accessible name to the "X" button that cancels a custom search mode (i.e. exits the Amazon.com search mode).
urlbar-search-mode-indicator-close =
  .aria-label = Close

# This placeholder is used when not in search mode and the user's default search
# engine is unknown.
urlbar-placeholder =
  .placeholder = Search or enter address

# This placeholder is used when not in search mode and searching in the urlbar
# is disabled via the keyword.enabled pref.
urlbar-placeholder-keyword-disabled =
  .placeholder = Enter address

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

urlbar-go-button2 =
  .title = Go to the address in the Location Bar
urlbar-page-action-button =
  .tooltiptext = Page actions
urlbar-revert-button =
  .tooltiptext = Show the address in the Location Bar

## Action text shown in urlbar results, usually appended after the search
## string or the url, like "result value - action text".

# Used for asking AI assistant chat.
urlbar-result-action-ai-chat = Ask
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
urlbar-result-action-move-tab-to-split-view = Move Tab to Split View
urlbar-result-action-visit = Visit
# "Switch to tab with container" is used when the target tab is located in a
# different container.
# Variables
# $container (String): the name of the target container
urlbar-result-action-switch-tab-with-container = Switch to Tab · <span>{ $container }</span>
# Used when the target tab is in a tab group that doesn't have a label.
urlbar-result-action-tab-group-unnamed = Unnamed group
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
# The string returned for an undefined calculator result such as when dividing by 0
urlbar-result-action-undefined-calculator-result = undefined

# The sub title of an add-on suggestion in the urlbar.
urlbar-result-addons-subtitle = { -brand-product-name } extension

# The sub title of a mdn suggestion in the urlbar.
urlbar-result-mdn-subtitle = { -mdn-brand-name }

# The sub title of a Yelp suggestion in the urlbar.
urlbar-result-yelp-subtitle = { -yelp-brand-name }

# This string explaining that the suggestion is a recommendation.
urlbar-result-suggestion-recommended = Recommended

# The title of a weather suggestion in the urlbar. The temperature and unit
# substring should be inside a <strong> tag. If the temperature and unit are not
# adjacent in the localization, it's OK to include only the temperature in the
# tag.
# Variables:
#   $temperature (number) - The temperature value
#   $unit (String) - The unit for the temperature, either "C" or "F"
#   $city (String) - The name of the city the weather data is for
#   $region (String) - The name of the city's region or country. Depending on
#       the user's location in relation to the city, this may be the name or
#       abbreviation of one of the city's administrative divisions like a
#       province or state, or it may be the name of the city's country.
urlbar-result-weather-title = <strong>{ $temperature }°{ $unit }</strong> in { $city }, { $region }

# The title of a weather suggestion in the urlbar including a region and
# country. The temperature and unit substring should be inside a <strong> tag.
# If the temperature and unit are not adjacent in the localization, it's OK to
# include only the temperature in the tag.
# Variables:
#   $temperature (number) - The temperature value
#   $unit (String) - The unit for the temperature, either "C" or "F"
#   $city (String) - The name of the city the weather data is for
#   $region (String) - The name or abbreviation of one of the city's
#       administrative divisions like a province or state.
#   $country (String) - The name of the city's country.
urlbar-result-weather-title-with-country = <strong>{ $temperature }°{ $unit }</strong> in { $city }, { $region }, { $country }

# The title of a weather suggestion in the urlbar only including the city. The
# temperature and unit substring should be inside a <strong> tag. If the
# temperature and unit are not adjacent in the localization, it's OK to include
# only the temperature in the tag.
# Variables:
#   $temperature (number) - The temperature value
#   $unit (String) - The unit for the temperature, either "C" or "F"
#   $city (String) - The name of the city the weather data is for
urlbar-result-weather-title-city-only = <strong>{ $temperature }°{ $unit }</strong> in { $city }

# Shows the name of the provider of weather data in a weather suggestion in the
# urlbar.
# Variables:
#   $provider (String) - The name of the weather-data provider. It will be the
#       name of a company, organization, or service.
urlbar-result-weather-provider-sponsored = { $provider } · Sponsored

## These strings are used for Realtime suggestions in the urlbar.
## Market refers to stocks, indexes, and funds.

# This string is shown as title when Market suggestion are disabled.
urlbar-result-market-opt-in-title = Get stock market data right in your search bar

# This string is shown as description when Market suggestion are disabled.
urlbar-result-market-opt-in-description = Show market updates and more from our partners when you share search query data with { -vendor-short-name }. <a data-l10n-name="learn-more-link">Learn more</a>

# This string is shown as button to activate online when realtime suggestion are disabled.
urlbar-result-realtime-opt-in-allow = Show suggestions

# This string is shown in split button to dismiss activation the Realtime suggestion.
urlbar-result-realtime-opt-in-not-now = Not now
urlbar-result-realtime-opt-in-dismiss = Dismiss
urlbar-result-realtime-opt-in-dismiss-all =
    .label = Don’t show these suggestions

# This string is shown in the result menu.
urlbar-result-menu-dont-show-market =
  .label = Don’t show market suggestions

# A message that replaces a result when the user dismisses Market suggestions.
urlbar-result-dismissal-acknowledgment-market = Thanks for your feedback. You won’t see market suggestions anymore.

# This a11y label is read by screen readers when an item in the row is selected.
urlbar-result-aria-group-market =
  .aria-label = Stock market suggestions

# A message that replaces a result when the user dismisses all suggestions of a
# particular type.
urlbar-result-dismissal-acknowledgment-all = Thanks for your feedback. You won’t see these suggestions anymore.

## These strings are used for suggestions of important dates in the urlbar.

# The name of an event and the number of days until it starts separated by a
# middot.
# Variables:
#   $name (string) - The name of the event.
#   $daysUntilStart (integer) - The number of days until the event starts.
urlbar-result-dates-countdown =
    { $daysUntilStart ->
        [one] { $name } · In { $daysUntilStart } day
        *[other] { $name } · In { $daysUntilStart } days
    }

# The name of a multiple day long event and the number of days until it starts
# separated by a middot.
# Variables:
#   $name (string) - The name of the event.
#   $daysUntilStart (integer) - The number of days until the event starts.
urlbar-result-dates-countdown-range =
    { $daysUntilStart ->
        [one] { $name } · Starts in { $daysUntilStart } day
        *[other] { $name } · Starts in { $daysUntilStart } days
    }

# The name of a multiple day long event and the number of days until it ends
# separated by a middot.
# Variables:
#   $name (string) - The name of the event.
#   $daysUntilEnd (integer) - The number of days until the event ends.
urlbar-result-dates-ongoing =
    { $daysUntilEnd ->
        [one] { $name } · Ends in { $daysUntilEnd } day
        *[other] { $name } · Ends in { $daysUntilEnd } days
    }

# The name of an event and a note that it is happening today separated by a
# middot.
# Variables:
#   $name (string) - The name of the event.
urlbar-result-dates-today = { $name } · Today

# The name of multiple day long event and a note that it is ends today
# separated by a middot.
# Variables:
#   $name (string) - The name of the event.
urlbar-result-dates-ends-today = { $name } · Ends today

## Strings used for buttons in the urlbar

# Searchmode Switcher button
# Variables:
#   $engine (String): the current default search engine.
urlbar-searchmode-button3 =
    .title = { $engine }, pick a search engine
urlbar-searchmode-button-no-engine2 =
    .title = No shortcut selected, pick a shortcut

# Refers to the ability to search using keywords in the address bar
urlbar-searchmode-no-keyword2 =
    .title = Keyword search is disabled

urlbar-searchmode-dropmarker2 =
    .title = Pick a Search Engine
urlbar-searchmode-bookmarks2 = Bookmarks
urlbar-searchmode-tabs2 = Tabs
urlbar-searchmode-history2 = History
urlbar-searchmode-actions2 = Actions
urlbar-searchmode-exit-button2 =
    .title = Close
urlbar-searchmode-default2 =
    .title = Default search engine

# Shown when adding new search engines from the search mode switcher.
# Variables:
#  $engineName (String): The name of the search engine.
urlbar-searchmode-popup-add-engine = Add “{ $engineName }”
    .title = Add search engine “{ $engineName }”

# Label shown on the top of Searchmode Switcher popup. After this label, the
# available search engines will be listed.
urlbar-searchmode-popup-one-off-header = This time search with:
# Label shown on the top of Searchmode Switcher popup when the search engine won't automatically
# reset after submitting.
urlbar-searchmode-popup-header = Search with:
urlbar-searchmode-popup-search-settings-panelitem = Search Settings
urlbar-searchmode-popup-settings-panelitem = Settings

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

## Used in the context menu in urlbar view.

urlbar-view-context-menu-open-in-tab =
  .label = Open in New Tab
  .accesskey = w
urlbar-view-context-menu-open-in-container-tab =
  .label = Open in New Container Tab
  .accesskey = i
urlbar-view-context-menu-open-in-window =
  .label = Open in New Window
  .accesskey = N
urlbar-view-context-menu-open-in-private-window =
  .label = Open in New Private Window
  .accesskey = P

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

fullscreen-keyboardlock-exit-button = Exit Full Screen (Press and hold Esc)
# "esc" is lowercase on mac keyboards, but uppercase elsewhere.
fullscreen-keyboardlock-exit-mac-button = Exit Full Screen (Press and hold esc)

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

qrcode-panel-error =
    .message = Failed to generate QR code. Please try again.

qrcode-copy-button =
    .label = Copy
qrcode-copy-success =
    .message = QR code copied to clipboard.
qrcode-copy-error =
    .message = Failed to copy QR code.

qrcode-save-button =
    .label = Save
qrcode-save-success =
    .message = QR code saved.
qrcode-save-error =
    .message = Failed to save QR code.
qrcode-save-title = Save QR Code
qrcode-save-filter-png = PNG Image
qrcode-save-filename = qrcode.png
qrcode-window-title = QR Code
qrcode-dialog-title = QR Code
qrcode-image =
    .aria-label = QR code
qrcode-close-button =
    .aria-label = Close

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

toolbar-button-send-tab =
  .label = Send tab
  .tooltiptext = Send current tab to another device

# Variables
# $shortcut (string) - Keyboard shortcut to open a new private browsing window
toolbar-button-new-private-window =
  .label = New private window
  .tooltiptext = Open a new private browsing window ({ $shortcut })

toolbar-button-share-tab =
  .label = Share
  .tooltiptext = Share this page

toolbar-button-tab-groups =
  .label = Tab groups
  .tooltiptext = Show your tab groups

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
menu-share-windows =
    .label = More Options
# Variables:
#   $count (Number) - The number of links that will be copied.
menu-share-copy-links =
    .label =
        { $count ->
            [one] Copy Link
           *[other] Copy { $count } Links
        }
    .accesskey = L
ui-tour-info-panel-close =
    .tooltiptext = Close

## Variables:
##  $uriHost (String): URI host for which the popup was allowed or blocked.

popups-infobar-allow2 =
    .label = Allow pop-ups and third-party redirects for { $uriHost }
    .accesskey = p

##

popups-infobar-dont-show-message2 =
    .label = Don’t show this message when pop-ups or third-party redirects are blocked
    .accesskey = D

edit-popup-settings2 =
    .label = Manage pop-up and third-party redirect settings…
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

navbar-overflow-2 =
    .tooltiptext = More tools

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

## Drop indicator text for pinned tabs when no tabs are pinned.

pinned-tabs-drop-indicator = Drop tab here to pin

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

reset-pbm-toolbar-button2 =
    .label = Clear Private Session
    .tooltiptext = Clear Private Session
reset-pbm-panel-heading2 = Clear data and start a fresh private session?
reset-pbm-panel-description2 = This deletes history, cookies, and all other site data without closing your Private Window.
reset-pbm-panel-always-ask-checkbox =
     .label = Always ask me
     .accesskey = A
reset-pbm-panel-cancel-button =
    .label = Cancel
    .accesskey = C
reset-pbm-panel-confirm-button2 =
    .label = Clear private session
    .accesskey = l
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
firefox-relay-offer-legal-notice-1 = By signing up and creating an email mask, you agree to the <label data-l10n-name="tos-url">Terms of Service</label> and <label data-l10n-name="privacy-url">Privacy Notice</label>.

## Add-on Pop-up Notifications

popup-notification-addon-install-unsigned =
    .value = (Unverified)
popup-notification-xpinstall-prompt-learn-more = Learn more about installing add-ons safely

popup-notification-xpinstall-prompt-block-url = See details

# Note: Access key is set to p to match "private" in the corresponding localized label.
popup-notification-addon-privatebrowsing-checkbox2 =
    .label = Allow extension to run in private windows
    .accesskey = p

# This string is similar to `webext-perms-description-data-long-technicalAndInteraction`
# but it is used in the install prompt, and it needs an access key.
popup-notification-addon-technical-and-interaction-checkbox =
    .label = Share technical and interaction data with extension developer
    .accesskey = S

## Pop-up warning

# Variables:
#   $popupCount (Number): the number of pop-ups blocked.
popup-warning-message =
    { $popupCount ->
        [1] { -brand-short-name } prevented this site from opening a pop-up window.
       *[other] { -brand-short-name } prevented this site from opening { $popupCount } pop-up windows.
    }

# Variables:
#   $popupCount (Number): the number of pop-ups blocked.
redirect-warning-with-popup-message =
    { $popupCount ->
        [0] { -brand-short-name } prevented this site from redirecting.
        [1] { -brand-short-name } prevented this site from opening a pop-up window and redirecting.
       *[other] { -brand-short-name } prevented this site from opening { $popupCount } pop-up windows and redirecting.
    }

# The singular form is left out for English, since the number of blocked pop-ups is always greater than 1.
# Variables:
#   $popupCount (Number): the number of pop-ups blocked.
popup-warning-exceeded-message =
    { $popupCount ->
       *[other] { -brand-short-name } prevented this site from opening more than { $popupCount } pop-up windows.
    }

# Variables:
#   $popupCount (Number): the number of pop-ups blocked.
popup-warning-exceeded-with-redirect-message =
    { $popupCount ->
       *[other] { -brand-short-name } prevented this site from opening more than { $popupCount } pop-up windows and redirecting.
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

# Variables:
#   $redirectURI (String): the URI for the redirect
popup-trigger-redirect-menuitem =
    .label = Show “{ $redirectURI }”

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

onboarding-aw-finish-setup-button =
    .label = Finish setup
    .tooltiptext = Finish setting up { -brand-short-name }

onboarding-checklist-button-label = Finish setup

## The urlbar trust icon & panel

# LOCALIZATION NOTE (trustpanel-urlbar-notsecure-label):
# Keep this string as short as possible, this is displayed in the URL bar
# use a synonym for "safe" or "private" if "secure" is too long.
urlbar-trust-icon-notsecure-label = Not Secure

trustpanel-etp-label-enabled = Enhanced Tracking Protection is on
trustpanel-etp-label-disabled = Enhanced Tracking Protection is off

# Variables
#  $host (String): the hostname of the site that is being displayed.
trustpanel-etp-toggle-on =
  .aria-label = Enhanced Tracking Protection: On for { $host }
# Variables
#  $host (String): the hostname of the site that is being displayed.
trustpanel-etp-toggle-off =
  .aria-label = Enhanced Tracking Protection: Off for { $host }

trustpanel-etp-description-enabled = If something looks broken on this site, try turning off protections.
trustpanel-etp-description-disabled = { -brand-product-name } thinks companies should follow you less. We block as many trackers as we can when you turn on protections.

trustpanel-connection-label-secure = Connection secure
trustpanel-connection-label-insecure = Connection not secure

trustpanel-header-enabled = { -brand-product-name } is on guard
trustpanel-description-enabled2 = You’re protected. If we spot something, we’ll let you know.
trustpanel-header-enabled-insecure = Be careful on this site
trustpanel-description-enabled-insecure = { -brand-product-name } noticed something suspicious.

trustpanel-header-disabled = You turned off protections
trustpanel-description-disabled = { -brand-product-name } is off-duty. We suggest turning protections back on.

trustpanel-clear-cookies-button = Clear cookies and site data
trustpanel-privacy-link = Privacy Settings

# Variables
#  $host (String): the hostname of the site that is being displayed.
trustpanel-clear-cookies-header =
    .title = Clear cookies and site data for { $host }

trustpanel-clear-cookies-description = Removing cookies and site data might log you out of websites and clear shopping carts.

trustpanel-clear-cookies-subview-button-clear = Clear
trustpanel-clear-cookies-subview-button-cancel = Cancel

# Variables
#  $host (String): the hostname of the site that is being displayed.
trustpanel-site-information-header =
    .title = Connection protections for { $host }

trustpanel-siteinformation-morelink = More site information

trustpanel-blocker-see-all = See All

# Variables
#  $host (String): the hostname of the site that is being displayed.
trustpanel-blocker-header =
    .title = Tracking protections for { $host }

## Variables
##  $count (String): the number of trackers blocked.

trustpanel-blocker-section-header2 = { $count ->
  [one] <span data-l10n-name="count">{ $count }</span> Tracker blocked on this site
  *[other] <span data-l10n-name="count">{ $count }</span> Trackers blocked on this site
}
trustpanel-blocker-description = { -brand-product-name } thinks companies should follow you less. So we block as many as we can.
trustpanel-blocked-header = { -brand-product-name } blocked these things for you:
trustpanel-tracking-header = { -brand-product-name } allowed these things so sites don’t break:
trustpanel-tracking-description = Without trackers, some buttons, forms, and login fields might not work.
trustpanel-insecure-section-header = Your connection isn’t secure
trustpanel-insecure-description = The data you’re sending to this site isn’t encrypted. It could be viewed, stolen, or altered.

trustpanel-list-label-tracking-cookies = { $count ->
  [one] { $count } Cross-site tracking cookie
  *[other] { $count } Cross-site tracking cookies
}
trustpanel-list-label-tracking-content = Tracking content
trustpanel-list-label-fingerprinter =  { $count ->
  [one] { $count } Fingerprinters
  *[other] { $count } Fingerprinters
}
trustpanel-list-label-social-tracking = { $count ->
  [one] { $count } Social media tracker
  *[other] { $count } Social media trackers
}
trustpanel-list-label-cryptominer = { $count ->
  [one] { $count } Cryptominer
  *[other] { $count } Cryptominers
}
trustpanel-social-tracking-blocking-tab-header = { $count ->
  [one] { -brand-product-name } blocked { $count } social media tracker
  *[other] { -brand-product-name } blocked { $count } social media trackers
}
trustpanel-social-tracking-not-blocking-tab-header = { $count ->
  [one] { -brand-product-name } allowed { $count } social media tracker
  *[other] { -brand-product-name } allowed { $count } social media trackers
}

trustpanel-tracking-cookies-blocking-tab-header = { $count ->
  [one] { -brand-product-name } blocked { $count } cross-site tracking cookie
  *[other] { -brand-product-name } blocked { $count } cross-site tracking cookies
}
trustpanel-tracking-cookies-not-blocking-tab-header = { $count ->
  [one] { -brand-product-name } allowed { $count } cross-site tracking cookie
  *[other] { -brand-product-name } allowed { $count } cross-site tracking cookies
}

trustpanel-tracking-content-blocking-tab-header = { $count ->
  [one] { -brand-product-name } blocked { $count } tracker
  *[other] { -brand-product-name } blocked { $count } trackers
}
trustpanel-tracking-content-not-blocking-tab-header = { $count ->
  [one] { -brand-product-name } allowed { $count } tracker
  *[other] { -brand-product-name } allowed { $count } trackers
}
trustpanel-tracking-content-tab-list-header = These sites are trying to track you:

trustpanel-fingerprinter-blocking-tab-header = { $count ->
  [one] { -brand-product-name } blocked { $count } fingerprinter
  *[other] { -brand-product-name } blocked { $count } fingerprinters
}
trustpanel-fingerprinter-not-blocking-tab-header = { $count ->
  [one] { -brand-product-name } allowed { $count } fingerprinter
  *[other] { -brand-product-name } allowed { $count } fingerprinters
}
trustpanel-fingerprinter-list-header = These sites are trying to fingerprint you:

trustpanel-cryptominer-blocking-tab-header = { $count ->
  [one] { -brand-product-name } blocked { $count } cryptominer
  *[other] { -brand-product-name } blocked { $count } cryptominers
}
trustpanel-cryptominer-not-blocking-tab-header = { $count ->
  [one] { -brand-product-name } allowed { $count } cryptominer
  *[other] { -brand-product-name } allowed { $count } cryptominers
}
trustpanel-cryptominer-tab-list-header = These sites are trying to cryptomine:
# "account on this site" refers to the (breached) site the user is currently visiting, not a Mozilla Monitor account.
trustpanel-breachalerts-anonymous-breached-header = Have an account on this site?
trustpanel-breachalerts-anonymous-breached-description = { -brand-product-name } found that this site had a data breach in the last 12 months. Find out if you were affected.
trustpanel-breachalerts-anonymous-breached-button-dismiss = Dismiss
trustpanel-breachalerts-anonymous-breached-button-check-monitor = Start free scan

## Reduced Protection Infobar ("ReducedProtectionNotification.sys.mjs")

# "temporarily lower your tracking protection" refers to temporarily decreasing the amount of tracking protection.
reduced-protection-infobar-message = <strong>Site looks broken?</strong> Reload the page to temporarily lower your tracking protection.
reduced-protection-infobar-reload-button = Reload
  .accesskey = R
reduced-protection-infobar-never-show-button = Don’t show again
  .accesskey = D
