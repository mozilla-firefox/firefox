# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

toolbar-button-firefox-view-3 =
  .label = { -firefoxview-brand-name }
  .tooltiptext = Tabs and browsing history across devices

menu-tools-firefox-view =
  .label = { -firefoxview-brand-name }
  .accesskey = F

firefoxview-page-title = { -firefoxview-brand-name }

firefoxview-page-heading =
  .heading = { -firefoxview-brand-name }

firefoxview-page-label =
  .label = { -firefoxview-brand-name }

# Used instead of the localized relative time when a timestamp is within a minute or so of now
firefoxview-just-now-timestamp = Just now

# The verb “Go” is implied but omitted from the headline: “[Go] from laptop to phone, seamlessly”
firefoxview-syncedtabs-signin-header-3 = From laptop to phone, seamlessly
firefoxview-syncedtabs-signin-description-3 = Keep your browsing connected across devices — tabs, passwords, and history, all in sync.
firefoxview-syncedtabs-signin-primarybutton-2 = Sign in

firefoxview-syncedtabs-adddevice-primarybutton = Try { -brand-product-name } for mobile

# "Your tabs called" - this is playful messaging indicating that a user can find their synced tabs on their phone.
firefoxview-syncedtabs-adddevice-header-3 = Your tabs called. They’re on your phone.
firefoxview-syncedtabs-adddevice-description-3 = Scan the QR code to get { -brand-product-name } for mobile and start syncing your open tabs and more. Learn how to <a data-l10n-name="url">connect additional devices</a>.

firefoxview-tabpickup-synctabs-primarybutton-2 = Turn on tab syncing

firefoxview-syncedtabs-synctabs-header-2 = Tab syncing is off
firefoxview-syncedtabs-synctabs-description-2 = Turn syncing back on to grab all your tabs from other devices.

firefoxview-syncedtabs-loading-header-2 = Fetching your tabs…
firefoxview-syncedtabs-loading-description-2 = Sync’s in progress. Tabs will be here soon.

firefoxview-tabpickup-fxa-admin-disabled-header-2 = Tab syncing is off
firefoxview-tabpickup-fxa-disabled-by-policy-description-2 = Your organization blocked this feature.

firefoxview-tabpickup-network-offline-primarybutton = Try again
firefoxview-tabpickup-network-offline-header-2 = { -brand-short-name } can’t connect right now
firefoxview-tabpickup-network-offline-description-2 = You might be offline, or something may be blocking the connection.

# “Sync hit a snag” means that the sync process is having a problem and can’t connect.
firefoxview-tabpickup-sync-error-header-2 = Sync hit a snag
firefoxview-tabpickup-generic-sync-error-description-2 = { -brand-short-name } couldn’t connect. Give it a moment, then try again
firefoxview-tabpickup-sync-error-primarybutton = Try again

firefoxview-tabpickup-password-locked-link = Learn more
firefoxview-tabpickup-password-locked-primarybutton = Enter primary password
firefoxview-syncedtab-password-locked-link = <a data-l10n-name="syncedtab-password-locked-link">Learn more</a>
firefoxview-tabpickup-password-locked-header-2 = Unlock tabs with your primary password
firefoxview-tabpickup-password-locked-description-2 = For your privacy, synced tabs are protected. Enter your { -brand-short-name } primary password to see tabs from your other devices.

firefoxview-tabpickup-signed-out-primarybutton = Sign in
firefoxview-tabpickup-signed-out-header-2 = Sign in to see your tabs
firefoxview-tabpickup-signed-out-description-2 = Reconnect to view tabs from other devices.

# Variables:
#   $tabTitle (string) - Title of tab being dismissed
firefoxview-closed-tabs-dismiss-tab =
  .title = Dismiss { $tabTitle }

# Variables:
#   $targetURI (string) - URL that will be opened in the new tab
firefoxview-tabs-list-tab-button =
  .title = Open { $targetURI } in a new tab

firefoxview-collapse-button-show =
  .title = Show list

firefoxview-collapse-button-hide =
  .title = Hide list

firefoxview-overview-nav = Recent browsing
  .title = Recent browsing
firefoxview-overview-header = Recent browsing
  .title = Recent browsing

## History in this context refers to browser history

firefoxview-history-nav = History
  .title = History
firefoxview-history-header = History
firefoxview-history-context-delete = Delete from History
    .accesskey = D
firefoxview-history-context-forget-site = Forget About This Site…
    .accesskey = F

## Open Tabs in this context refers to all open tabs in the browser

firefoxview-opentabs-nav = Open tabs
  .title = Open tabs
firefoxview-opentabs-header = Open tabs

## Recently closed tabs in this context refers to recently closed tabs from all windows

firefoxview-recently-closed-nav = Recently closed tabs
  .title = Recently closed tabs
firefoxview-recently-closed-header = Recently closed tabs

## Tabs from other devices refers in this context refers to synced tabs from other devices

firefoxview-synced-tabs-nav = Tabs from other devices
  .title = Tabs from other devices
firefoxview-synced-tabs-header = Tabs from other devices

##

# Used for a link in collapsible cards, in the ’Recent browsing’ page of Firefox View
firefoxview-view-all-link = View all

# Variables:
#   $winID (Number) - The index of the owner window for this set of tabs
firefoxview-opentabs-window-header =
  .title = Window { $winID }

# Variables:
#   $winID (Number) - The index of the owner window (which is currently focused) for this set of tabs
firefoxview-opentabs-current-window-header =
  .title = Window { $winID } (Current)

firefoxview-show-more = Show more
firefoxview-show-less = Show less
firefoxview-show-all = Show all

firefoxview-search-text-box-clear-button =
  .title = Clear

# Placeholder for the input field to search in recent browsing ("search" is a verb).
firefoxview-search-text-box-recentbrowsing =
  .placeholder = Search

# Placeholder for the input field to search in history ("search" is a verb).
firefoxview-search-text-box-history =
  .placeholder = Search history

# Placeholder for the input field to search in bookmarks ("search" is a verb).
firefoxview-search-text-box-bookmarks =
  .placeholder = Search bookmarks

# Placeholder for the input field to search in recently closed tabs ("search" is a verb).
firefoxview-search-text-box-recentlyclosed =
  .placeholder = Search recently closed tabs

# Placeholder for the input field to search in tabs from other devices ("search" is a verb).
firefoxview-search-text-box-tabs =
  .placeholder = Search tabs

# Placeholder for the input field to search in open tabs ("search" is a verb).
firefoxview-search-text-box-opentabs =
  .placeholder = Search open tabs

# "Search" is a noun (as in "Results of the search for")
# Variables:
#   $query (String) - The search query used for searching through browser history.
firefoxview-search-results-header = Search results for “{ $query }”

# Variables:
#   $count (Number) - The number of visits matching the search query.
firefoxview-search-results-count = { $count ->
  [one] { $count } site
 *[other] { $count } sites
}

# Message displayed when a search is performed and no matching results were found.
# Variables:
#   $query (String) - The search query.
firefoxview-search-results-empty = No results for “{ $query }”

firefoxview-sort-history-by-date-label = Sort by date
firefoxview-sort-history-by-site-label = Sort by site
firefoxview-sort-open-tabs-by-recency-label = Sort by recent activity
firefoxview-sort-open-tabs-by-order-label = Sort by tab order

## Variables:
##   $date (string) - Date to be formatted based on locale

firefoxview-history-date-today = Today - { DATETIME($date, dateStyle: "full") }
firefoxview-history-date-yesterday = Yesterday - { DATETIME($date, dateStyle: "full") }
firefoxview-history-date-this-month = { DATETIME($date, dateStyle: "full") }
firefoxview-history-date-prev-month = { DATETIME($date, month: "long", year: "numeric") }

# When history is sorted by site, this heading is used in place of a domain, in
# order to group sites that do not come from an outside host.
# For example, this would be the heading for all file:/// URLs in history.
firefoxview-history-site-localhost = (local files)

##

firefoxview-show-all-history = Show all history

## Message displayed in Firefox View when the user has no history data

# This appears when a user does not have browsing history yet.
# This is indicating that a users browsers history will be found here when they start browsing.
firefoxview-history-empty-header-2 = Your browsing trail starts here
firefoxview-history-empty-description-2 = As you visit pages, your history will appear here. Control what’s remembered in <a data-l10n-name="history-settings-url">settings</a>.

##

# Button text for choosing a browser within the ’Import history from another browser’ banner
firefoxview-choose-browser-button = Choose browser
  .title = Choose browser

## Message displayed in Firefox View when the user has chosen to never remember History

# “Off the record” is an English phrase journalists use when a conversation will not be recorded or reported on. It’s meant to convey that users’ history activity will not be recorded or shown.
firefoxview-dont-remember-history-empty-header-3 = You’re off the record
firefoxview-dont-remember-history-empty-description-2 = { -brand-short-name } isn’t saving your history right now. Change that any time in <a data-l10n-name="history-settings-url-two">settings</a>.

##

# This label is read by screen readers when focusing the close button for the "Import history from another browser" banner in Firefox View
firefoxview-import-history-close-button =
  .aria-label = Close
  .title = Close

## Text displayed in a dismissable banner to import bookmarks/history from another browser

firefoxview-import-history-header = Import history from another browser
firefoxview-import-history-description = Make { -brand-short-name } your go-to browser. Import browsing history, bookmarks, and more.

## Message displayed in Firefox View when the user has no recently closed tabs data

firefoxview-recentlyclosed-empty-header = Closed a tab too soon?
firefoxview-recentlyclosed-empty-description = Here you’ll find the tabs you recently closed, so you can reopen any of them quickly.
firefoxview-recentlyclosed-empty-description-two = To find tabs from longer ago, view your <a data-l10n-name="history-url">browsing history</a>.

## This message is displayed below the name of another connected device when it doesn't have any open tabs.

firefoxview-syncedtabs-device-notabs = No tabs open on this device

firefoxview-syncedtabs-connect-another-device = Connect another device

firefoxview-pinned-tabs =
  .title = Pinned Tabs

firefoxview-tabs =
  .title = Tabs

## These tooltips will be displayed when hovering over a pinned tab on the Open Tabs page
## Variables:
##  $tabTitle (string) - Title of pinned tab that will be opened when selected

firefoxview-opentabs-pinned-tab =
  .title = Switch to { $tabTitle }

# This tooltip will be shown for a pinned tab whose URL is currently bookmarked.
firefoxview-opentabs-bookmarked-pinned-tab =
  .title = Switch to (Bookmarked) { $tabTitle }

## These tooltips will be displayed when hovering over an unpinned Open Tab
## Variables:
##   $url (string) - URL of tab that will be opened when selected

# This tooltip will be shown for an unpinned tab whose URL is currently bookmarked.
firefoxview-opentabs-bookmarked-tab =
  .title = (Bookmarked) { $url }
