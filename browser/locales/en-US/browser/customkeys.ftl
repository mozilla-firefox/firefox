# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

customkeys-title = Keyboard Shortcuts

## Sidebar

customkeys-sidebar =
  .aria-label = Sidebar
customkeys-title-heading =
  .heading = Keyboard Shortcuts
# Search is a verb, as in "search through shortcuts".
customkeys-search-input =
  .aria-label = Search shortcuts
  .placeholder = Search shortcuts
customkeys-description = Control how you move around and interact with { -brand-short-name }.
customkeys-support-link-text = Learn more
customkeys-reset-all-button = Restore defaults

## Shortcut actions

# Displayed below the action label (e.g. "Undo") when the shortcut cannot be changed.
customkeys-key-unchangeable = (cannot be changed)
# Displayed below the action label (e.g. "Undo") when the user has assigned the
# shortcut in the operating system's keyboard settings (System Settings on
# macOS). That shortcut is used instead of any shortcut set here, and it can't
# be changed here. Translate "keyboard settings" the way the operating system
# names these settings in your language. "set" here means "assigned".
customkeys-key-system-assigned =
    { PLATFORM() ->
        [macos] (set in macOS keyboard settings)
       *[other] (set in your system’s keyboard settings)
    }
customkeys-key-new =
  .label = Press new key:
# Displayed in the new key field when the key that was pressed isn't valid.
customkeys-key-invalid = Invalid
customkeys-shortcut-unassigned =
  .placeholder = Add shortcut
# Variables:
# $keyLabel (string) - The name of the shortcut, e.g. “New Tab”.
customkeys-shortcut-input = Shortcut for: { $keyLabel }
customkeys-key-edit =
  .aria-label = Edit
  .tooltiptext = Edit
customkeys-key-clear =
  .aria-label = Clear
  .tooltiptext = Clear
customkeys-key-reset =
  .aria-label = Restore
  .tooltiptext = Restore

## Confirmation dialogs

customkeys-conflict-confirm-title = Remove another shortcut?
# Variables
# $conflict (string) - The title of the conflicting shortcut.
customkeys-conflict-confirm-body = This key is already used by “{ $conflict }”.
customkeys-conflict-confirm-button-confirm = Use anyway
customkeys-conflict-confirm-button-cancel = Cancel

customkeys-conflict-unusable-title = Key cannot be used
# Variables
# $conflict (string) - The title of the conflicting shortcut.
customkeys-conflict-unusable-body = This key is already used by “{ $conflict }” and cannot be used.

customkeys-reset-all-confirm-title = Restore defaults?
customkeys-reset-all-confirm-body = Any custom keyboard shortcuts you’ve created will be removed.
customkeys-reset-all-confirm-button-confirm = Restore defaults
customkeys-reset-all-confirm-button-cancel = Cancel

## Added shortcuts:

customkeys-file-duplicate-tab = Duplicate Tab
customkeys-file-copy-tab-url = Copy Tab URL
customkeys-file-focus-search = Focus the Search Bar

customkeys-history-reopen-tab = Reopen Last Closed Tab
customkeys-history-reopen-window = Reopen Last Closed window
customkeys-history-reopen-tab-current-window = Reopen Last Closed Tab of the Current Window

customkeys-sidebar-toggle = Toggle Sidebar

customkeys-view-bookmarks-toolbar = Toggle Bookmarks Toolbar
customkeys-view-picture-in-picture = Picture-in-Picture
customkeys-view-add-split-view = Add Split View
# Separate is a verb
customkeys-view-separate-split-view = Separate Split View
customkeys-dev-tools = Web Developer Tools
customkeys-dev-inspector = DOM and Style Inspector
customkeys-dev-webconsole = Web Console
customkeys-dev-debugger = JavaScript Debugger
customkeys-dev-network = Network Monitor
customkeys-dev-style = Style Editor
customkeys-dev-performance = Performance
customkeys-dev-storage = Storage Inspector
customkeys-dev-dom = DOM
customkeys-dev-accessibility = Accessibility
customkeys-dev-profiler-toggle = Start/Stop the Performance Profiler
customkeys-dev-profiler-capture = Capture a Performance Profile

customkeys-edit-find-previous = Find Previous

customkeys-tools-screenshot = Take a Screenshot

customkeys-category-navigation-2 =
  .heading = Navigation
customkeys-nav-back = Back
customkeys-nav-forward = Forward
customkeys-nav-home = Home
customkeys-nav-reload = Reload
customkeys-nav-reload-skip-cache = Reload (Override Cache)
customkeys-nav-stop = Stop

customkeys-nav-select-tab-1 = Go to Tab 1
customkeys-nav-select-tab-2 = Go to Tab 2
customkeys-nav-select-tab-3 = Go to Tab 3
customkeys-nav-select-tab-4 = Go to Tab 4
customkeys-nav-select-tab-5 = Go to Tab 5
customkeys-nav-select-tab-6 = Go to Tab 6
customkeys-nav-select-tab-7 = Go to Tab 7
customkeys-nav-select-tab-8 = Go to Tab 8
customkeys-nav-select-last-tab = Go to Last Tab
customkeys-nav-toggle-mute = Mute/Unmute Audio
