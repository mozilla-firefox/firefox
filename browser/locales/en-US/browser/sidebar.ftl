# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

menu-view-genai-chat =
  .label = AI Chatbot

menu-view-review-checker =
  .label = Review Checker

menu-view-contextual-password-manager =
  .label = Passwords

sidebar-options-menu-button =
  .title = Open menu

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

sidebar-history-delete =
  .title = Delete from History

sidebar-history-clear =
  .label = Clear history

sidebar-history-sort-by-heading = Sort by:
sidebar-history-sort-option-date =
  .label = Date
sidebar-history-sort-option-site =
  .label = Site
sidebar-history-sort-option-date-and-site =
  .label = Date and site
sidebar-history-sort-option-last-visited =
  .label = Last visited

## Labels for sidebar search

# "Search" is a noun (as in "Results of the search for")
# Variables:
#   $query (String) - The search query used for searching through browser history.
sidebar-search-results-header =
  .heading = Search results for “{ $query }”

## Labels for sidebar customize panel

sidebar-customize-extensions-header = Sidebar extensions
sidebar-customize-firefox-tools-header =
  .label = { -brand-product-name } tools
sidebar-customize-firefox-settings = Manage { -brand-short-name } settings
sidebar-vertical-tabs =
  .label = Vertical tabs
sidebar-settings =
  .label = Sidebar settings
sidebar-hide-tabs-and-sidebar =
  .label = Hide tabs and sidebar
sidebar-show-on-the-right =
  .label = Move sidebar to the right
sidebar-show-on-the-left =
  .label = Move sidebar to the left
# Option to automatically expand the collapsed sidebar when the mouse pointer
# hovers over it.
expand-sidebar-on-hover =
  .label = Expand sidebar on hover

## Labels for sidebar context menu items

sidebar-context-menu-manage-extension =
  .label = Manage extension
sidebar-context-menu-remove-extension =
  .label = Remove extension
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

## Labels for sidebar history context menu items

sidebar-history-context-menu-delete-page-2 =
  .label = Delete Page from History
sidebar-history-context-menu-bookmark-page =
  .label = Bookmark Page…

## Labels for sidebar menu items.

sidebar-menu-genai-chat-label =
  .label = AI chatbot
sidebar-menu-history-label =
  .label = History
sidebar-menu-synced-tabs-label =
  .label = Tabs from other devices
sidebar-menu-bookmarks-label =
  .label = Bookmarks
sidebar-menu-customize-label =
  .label = Customize sidebar
sidebar-menu-review-checker-label =
  .label = Review Checker
sidebar-menu-contextual-password-manager-label =
  .label = Passwords

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
sidebar-menu-cpm-header =
  .heading = Passwords

## Titles for sidebar menu panels.

sidebar-customize-title = Customize sidebar
sidebar-history-title = History
sidebar-syncedtabs-title = Tabs from other devices

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
