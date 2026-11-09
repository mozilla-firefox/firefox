# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

menu-view-genai-chat =
  .label = AI Chatbot

menu-view-contextual-password-manager =
  .label = Passwords

menu-view-resource-monitor =
  .label = Resource Monitor

# Label for the Open Tabs entry in the View > Sidebars menu bar menu.
# "Open Tabs" is a noun phrase referring to the tabs currently open in
# the browser, not an instruction to open tabs.
menu-view-open-tabs =
  .label = Open Tabs

sidebar-options-menu-button =
  .title = Open menu

# Accessible label for the splitter used to resize the sidebar.
sidebar-resize-splitter =
  .aria-label = Resize sidebar

## Labels for sidebar history panel

# Variables:
#   $date (string) - Date to be formatted based on locale
sidebar-history-date-today =
  .heading = Today - { DATETIME($date, dateStyle: "full") }
sidebar-history-date-yesterday =
  .heading = Yesterday - { DATETIME($date, dateStyle: "full") }
sidebar-history-date-this-month =
  .heading = { DATETIME($date, dateStyle: "full") }
sidebar-history-date-prev-month =
  .heading = { DATETIME($date, month: "long", year: "numeric") }

# When history is sorted by site, this heading is used in place of a domain, in
# order to group sites that do not come from an outside host.
# For example, this would be the heading for all file:/// URLs in history.
sidebar-history-site-localhost =
  .heading = (local files)

sidebar-history-delete =
  .title = Delete from History

sidebar-history-clear =
  .label = Clear history

sidebar-history-sort-by-heading-menucaption =
  .label = Sort by:
sidebar-history-sort-option-date =
  .label = Date
sidebar-history-sort-option-site =
  .label = Site
sidebar-history-sort-option-date-and-site =
  .label = Date and site
sidebar-history-sort-option-last-visited =
  .label = Last visited
sidebar-history-sort-option-most-visited =
  .label = Most visited

# Promo shown in the history panel to people with a new profile, inviting them
# to import their browsing history from another browser.
sidebar-history-import-promo =
  .message = Missing something? Grab history from another browser.
sidebar-history-import-promo-button =
  .label = Import to { -brand-short-name }

## Labels for sidebar search

# "Search" is a noun (as in "Results of the search for")
# Variables:
#   $query (String) - The search query used for searching through browser history.
sidebar-search-results-header =
  .heading = Search results for “{ $query }”

## Labels for sidebar customize panel

sidebar-customize-extensions-header2 = Extensions
sidebar-customize-firefox-tools-header2 =
  .label = Tools
sidebar-customize-firefox-settings = Manage { -brand-short-name } settings
sidebar-vertical-tabs =
  .label = Vertical tabs
sidebar-settings2 =
  .label = Settings
sidebar-hide-tabs-and-sidebar =
  .label = Hide tabs and sidebar
sidebar-open-tools-from-sidebar =
  .label = Open tools from sidebar
sidebar-show-on-the-right =
  .label = Move sidebar to the right
sidebar-show-on-the-left =
  .label = Move sidebar to the left
# Option to automatically expand the collapsed sidebar when the mouse pointer
# hovers over it.
expand-sidebar-on-hover =
  .label = Expand sidebar on hover
# Option to show a preview of the most recently active tabs when the mouse
# pointer hovers over the Open Tabs button in the sidebar.
sidebar-show-preview-on-hover =
  .label = Show preview on hover
sidebar-manage-extensions2 = Manage all extensions

## Labels for sidebar context menu items

sidebar-context-menu-manage-extension =
  .label = Manage extension
sidebar-context-menu-report-extension =
  .label = Report extension
sidebar-context-menu-open-in-tab =
  .label = Open in New Tab
sidebar-context-menu-open-in-container-tab =
  .label = Open in New Container Tab
sidebar-context-menu-open-in-window =
  .label = Open in New Window
sidebar-context-menu-open-in-private-window =
  .label = Open in New Private Window
sidebar-context-menu-forget-site =
  .label = Clear All Data for Website…
sidebar-context-menu-bookmark-tab =
  .label = Bookmark Tab…
sidebar-context-menu-copy-link =
  .label = Copy Link
sidebar-context-menu-hide-sidebar =
  .label = Hide Sidebar
sidebar-context-menu-enable-vertical-tabs =
  .label = Turn on Vertical Tabs
sidebar-context-menu-customize-sidebar =
  .label = Customize Sidebar
# Variables:
#   $deviceName (String) - The name of the device the user is closing a tab for
sidebar-context-menu-close-remote-tab =
  .label = Close tab on { $deviceName }
sidebar-context-menu-remove-extension2 =
  .label = Remove from { -brand-short-name }
sidebar-context-menu-unpin-extension =
  .label = Remove from Sidebar

## Labels for sidebar history context menu items

sidebar-history-context-menu-delete-page-2 =
  .label = Delete Page from History
sidebar-history-context-menu-bookmark-page =
  .label = Bookmark Page…
sidebar-history-context-menu-delete-pages =
  .label = Delete Pages from History
# Deletes every page listed under a date or site heading in the history sidebar.
sidebar-history-context-menu-delete =
  .label = Delete

## Labels for sidebar bookmarks context menu items

sidebar-bookmarks-context-menu-edit-bookmark =
  .label = Edit Bookmark…
sidebar-bookmarks-context-menu-delete-bookmark =
  .label = Delete Bookmark
sidebar-bookmarks-context-menu-delete-separator =
  .label = Delete

## Labels for sidebar open tabs context menu items

# Label for the context menu item that closes the tab the user
# right-clicked in the Open Tabs sidebar panel.
sidebar-opentabs-context-close-tab =
  .label = Close Tab

## Labels for the open tabs panel options menu

# Heading in the Open Tabs panel options menu for the group of
# tab sorting options, which appear directly beneath this heading.
sidebar-opentabs-sort-by-heading =
  .label = Sort by

# Sort option that lists tabs in the order they appear on the tab strip.
sidebar-opentabs-sort-option-order =
  .label = Tab order

# Sort option that lists tabs by most recent activity, newest first.
sidebar-opentabs-sort-option-recency =
  .label = Recently active

## Labels for the open tabs hover preview

# Heading at the top of the preview shown when hovering the Open Tabs button
# in the sidebar. "Recent tabs" refers to the tabs that were active most
# recently, not to tabs that were recently opened.
sidebar-opentabs-preview-heading = Recent tabs

# Tooltip for the button that closes a tab from the Open Tabs hover preview.
# Variables:
#   $tabTitle (String) - Title of tab being closed
sidebar-opentabs-preview-close-tab =
  .title = Close { $tabTitle }

# Tooltip for the button that mutes a tab from the Open Tabs hover preview.
sidebar-opentabs-preview-mute-tab =
  .title = Mute tab

# Tooltip for the button that unmutes a tab from the Open Tabs hover preview.
sidebar-opentabs-preview-unmute-tab =
  .title = Unmute tab

## Labels for sidebar menu items.

sidebar-menu-genai-chat-label =
  .label = AI chatbot
sidebar-menu-history-label =
  .label = History
sidebar-menu-synced-tabs-label =
  .label = Tabs from other devices
# Label for the Open Tabs panel in the sidebar tools list and customize
# menu. "Open tabs" is a noun phrase referring to the tabs currently open
# in the browser, not an instruction to open tabs.
sidebar-menu-open-tabs-label =
  .label = Open tabs
sidebar-menu-bookmarks-label =
  .label = Bookmarks
sidebar-menu-customize-label =
  .label = Customize sidebar
sidebar-menu-contextual-password-manager-label =
  .label = Passwords
sidebar-menu-resource-monitor-label =
  .label = Resource monitor
sidebar-menu-more-tools-label =
  .label = More tools

## Tooltips for sidebar menu items.

# The tooltip to show over the history icon, when history is not currently showing.
# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-menu-open-history-tooltip = Open history ({ $shortcut })

# The tooltip to show over the history icon, when history is currently showing.
# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-menu-close-history-tooltip = Close history ({ $shortcut })

# The tooltip to show over the bookmarks icon, when bookmarks is not currently showing.
# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-menu-open-bookmarks-tooltip = Open bookmarks ({ $shortcut })

# The tooltip to show over the bookmarks icon, when bookmarks is currently showing.
# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-menu-close-bookmarks-tooltip = Close bookmarks ({ $shortcut })

## Tooltips displayed over the AI chatbot icon.
## Variables:
##   $shortcut (String) - The OS specific keyboard shortcut.
##   $provider (String) - The name of the AI chatbot provider (if available).

sidebar-menu-open-ai-chatbot-tooltip-generic = Open AI chatbot ({ $shortcut })
sidebar-menu-open-ai-chatbot-provider-tooltip = Open { $provider } ({ $shortcut })

sidebar-menu-close-ai-chatbot-tooltip-generic = Close AI chatbot ({ $shortcut })
sidebar-menu-close-ai-chatbot-provider-tooltip = Close { $provider } ({ $shortcut })

## Headings for sidebar menu panels.

sidebar-panel-header-close-button =
  .tooltiptext = Close
sidebar-menu-customize-header =
  .heading = Customize sidebar
sidebar-menu-history-header =
  .heading = History
sidebar-menu-syncedtabs-header =
  .heading = Tabs from other devices
# Heading shown at the top of the Open Tabs sidebar panel. "Open tabs"
# refers to the tabs currently open in the browser.
sidebar-menu-open-tabs-header =
  .heading = Open tabs
sidebar-menu-cpm-header =
  .heading = Passwords
sidebar-menu-bookmarks-header =
  .heading = Bookmarks

## Labels for sidebar bookmarks panel folder names.

sidebar-bookmarks-folder-menu = Bookmarks Menu
sidebar-bookmarks-folder-toolbar = Bookmarks Toolbar
sidebar-bookmarks-folder-other = Other Bookmarks
sidebar-bookmarks-folder-mobile = Mobile Bookmarks

## Titles for sidebar menu panels.

sidebar-bookmarks-title = Bookmarks
sidebar-customize-title = Customize sidebar
sidebar-history-title = History
sidebar-syncedtabs-title = Tabs from other devices
# Title of the Open Tabs sidebar panel. "Open tabs" refers to the tabs
# currently open in the browser.
sidebar-opentabs-title = Open tabs

# Title attribute for the pinned tabs section in the Open Tabs sidebar
# panel.
sidebar-opentabs-pinned-tabs =
  .title = Pinned tabs

# Heading shown above the tab list for the currently focused window
# in the Open Tabs sidebar panel.
# Variables:
#   $winID (Number) - The position of the window in the open windows list.
sidebar-opentabs-current-window-header =
  .heading = Window { $winID } (current)

# Heading shown above the tab list for a non-focused window in the
# Open Tabs sidebar panel.
# Variables:
#   $winID (Number) - The position of the window in the open windows list.
sidebar-opentabs-window-header =
  .heading = Window { $winID }

## Context for closing synced tabs when hovering over the items

# Context for hovering over the close tab button that will
# send a push to the device to close said tab
# Variables:
#   $deviceName (String) - the name of the device the user is closing a tab for
synced-tabs-context-close-tab-title =
    .title = Close tab on { $deviceName }

show-sidebars =
  .tooltiptext = Show sidebars
  .label = Sidebars

## Tooltips for the sidebar toolbar widget.

# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-widget-expand-sidebar2 =
  .tooltiptext = Expand sidebar ({ $shortcut })
  .label = Sidebars

# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-widget-collapse-sidebar2 =
  .tooltiptext = Collapse sidebar ({ $shortcut })
  .label = Sidebars

# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-widget-show-sidebar2 =
  .tooltiptext = Show sidebar ({ $shortcut })
  .label = Sidebars

# Variables:
#   $shortcut (String) - The OS specific keyboard shortcut.
sidebar-widget-hide-sidebar2 =
  .tooltiptext = Hide sidebar ({ $shortcut })
  .label = Sidebars

# Promotional message displayed in the expanded sidebar state for Vertical Tabs
# users who do not have any pinned tabs. Indicates that they can drop tabs in
# this area to pin them.
sidebar-pins-promo-text = Drag important tabs here to keep them within reach
sidebar-pins-promo-text-2 =
  .heading = Drag important tabs here to keep them within reach
