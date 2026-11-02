# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

tabbrowser-empty-tab-title = New Tab
tabbrowser-empty-private-tab-title = New Private Tab

# Displayed within the tooltip on tabs inside of a tab group.
# Variables:
#   $tabGroupName (String): the user-defined name of the current tab group.
tabbrowser-tab-tooltip-tab-group = { $tabGroupName }

# Displayed within the tooltip on tabs in a container.
# Variables:
#   $containerName (String): the name of the current container.
tabbrowser-tab-tooltip-container = { $containerName }

# Displayed within the tooltip on tabs inside of a tab group if the tab is also in a container.
# Variables:
#   $tabGroupName (String): the user-defined name of the current tab group.
#   $containerName (String): the name of the current container.
tabbrowser-tab-tooltip-tab-group-container = { $tabGroupName } — { $containerName }

# This text serves as an on-screen tooltip as well as an accessible name for
# the "X" button that is shown on the active tab or, when multiple tabs are
# selected, to all their "X" buttons.
# Variables:
#   $tabCount (Number): The number of tabs that will be closed.
tabbrowser-close-tabs-button =
    .tooltiptext =
        { $tabCount ->
            [one] Close tab
           *[other] Close { $tabCount } tabs
        }

## Tooltips for tab audio control
## Variables:
##   $tabCount (Number): The number of tabs that will be affected.

# Variables:
#   $shortcut (String): The keyboard shortcut for "Mute tab".
tabbrowser-mute-tab-audio-tooltip =
    .label =
        { $tabCount ->
            [one] Mute tab ({ $shortcut })
           *[other] Mute { $tabCount } tabs ({ $shortcut })
        }
# Variables:
#   $shortcut (String): The keyboard shortcut for "Unmute tab".
tabbrowser-unmute-tab-audio-tooltip =
    .label =
        { $tabCount ->
            [one] Unmute tab ({ $shortcut })
           *[other] Unmute { $tabCount } tabs ({ $shortcut })
        }
tabbrowser-mute-tab-audio-background-tooltip =
    .label =
        { $tabCount ->
            [one] Mute tab
           *[other] Mute { $tabCount } tabs
        }
tabbrowser-unmute-tab-audio-background-tooltip =
    .label =
        { $tabCount ->
            [one] Unmute tab
           *[other] Unmute { $tabCount } tabs
        }
tabbrowser-unblock-tab-audio-tooltip =
    .label =
        { $tabCount ->
            [one] Play tab
           *[other] Play { $tabCount } tabs
        }

## Tooltips for tab audio control

tabbrowser-unmute-tab-audio-aria-label =
    .aria-label = Unmute tab
tabbrowser-mute-tab-audio-aria-label =
    .aria-label = Mute tab
# Used to unblock a tab with audio from autoplaying
tabbrowser-unblock-tab-audio-aria-label =
    .aria-label = Play tab

## Confirmation dialog when closing a window with more than one tab open,
## or when quitting when only one window is open.

# The singular form is not considered since this string is used only for multiple tabs.
# Variables:
#   $tabCount (Number): The number of tabs that will be closed.
tabbrowser-confirm-close-tabs-title =
    { $tabCount ->
       *[other] Close { $tabCount } tabs?
    }
tabbrowser-confirm-close-tabs-button = Close tabs
tabbrowser-ask-close-tabs-checkbox = Ask before closing multiple tabs

## Confirmation dialog when quitting using the menu and multiple windows are open.

# The forms for 0 or 1 items are not considered since this string is used only for
# multiple windows.
# Variables:
#   $windowCount (Number): The number of windows that will be closed.
tabbrowser-confirm-close-windows-title =
    { $windowCount ->
       *[other] Close { $windowCount } windows?
    }
tabbrowser-confirm-close-windows-button =
    { PLATFORM() ->
        [windows] Close and exit
       *[other] Close and quit
    }

## Confirmation dialog when quitting using the keyboard shortcut (Ctrl/Cmd+Q)
## Windows does not show a prompt on quit when using the keyboard shortcut by default.

tabbrowser-confirm-close-tabs-with-key-title = Close window and quit { -brand-short-name }?
tabbrowser-confirm-close-tabs-with-key-button = Quit { -brand-short-name }
# Variables:
#   $quitKey (String): the text of the keyboard shortcut for quitting.
tabbrowser-ask-close-tabs-with-key-checkbox = Ask before quitting with { $quitKey }

## Confirmation dialog when quitting using the keyboard shortcut (Ctrl/Cmd+Q)
## and browser.warnOnQuitShortcut is true.

tabbrowser-confirm-close-warn-shortcut-title = Quit { -brand-short-name } or close current tab?
tabbrowser-confirm-close-windows-warn-shortcut-button =
    { PLATFORM() ->
        [windows] Exit { -brand-short-name }
       *[other] Quit { -brand-short-name }
    }
tabbrowser-confirm-close-tab-only-button = Close current tab

## Confirmation dialog when opening multiple tabs simultaneously

tabbrowser-confirm-open-multiple-tabs-title = Confirm open
# Variables:
#   $tabCount (Number): The number of tabs that will be opened.
tabbrowser-confirm-open-multiple-tabs-message =
    { $tabCount ->
       *[other] You are about to open { $tabCount } tabs. This might slow down { -brand-short-name } while the pages are loading. Are you sure you want to continue?
    }
tabbrowser-confirm-open-multiple-tabs-button = Open tabs
tabbrowser-confirm-open-multiple-tabs-checkbox = Warn me when opening multiple tabs might slow down { -brand-short-name }

## Confirmation dialog for enabling caret browsing

tabbrowser-confirm-caretbrowsing-title = Caret Browsing
tabbrowser-confirm-caretbrowsing-message = Pressing F7 turns Caret Browsing on or off. This feature places a moveable cursor in web pages, allowing you to select text with the keyboard. Do you want to turn Caret Browsing on?
tabbrowser-confirm-caretbrowsing-checkbox = Do not show me this dialog box again.

## Confirmation dialog for closing all duplicate tabs

tabbrowser-confirm-close-all-duplicate-tabs-title = Close duplicate tabs?
tabbrowser-confirm-close-all-duplicate-tabs-text = We’ll close duplicate tabs in this window. The last active
 tab will stay open.
tabbrowser-confirm-close-all-duplicate-tabs-button-closetabs = Close tabs

##

# Variables:
#   $domain (String): URL of the page that is trying to steal focus.
tabbrowser-allow-dialogs-to-get-focus =
    .label = Allow notifications like this from { $domain } to take you to their tab

tabbrowser-customizemode-tab-title = Customize { -brand-short-name }

## Context menu buttons, of which only one will be visible at a time

tabbrowser-context-mute-tab =
    .label = Mute Tab
    .accesskey = M
tabbrowser-context-mute-tab2 =
    .label = Mute
    .accesskey = M
tabbrowser-context-unmute-tab =
    .label = Unmute Tab
    .accesskey = m
tabbrowser-context-unmute-tab2 =
    .label = Unmute
    .accesskey = m
# The accesskey should match the accesskey for tabbrowser-context-mute-tab
tabbrowser-context-mute-selected-tabs =
    .label = Mute Tabs
    .accesskey = M
# The accesskey should match the accesskey for tabbrowser-context-unmute-tab
tabbrowser-context-unmute-selected-tabs =
    .label = Unmute Tabs
    .accesskey = m

# This string is used as an additional tooltip and accessibility description for tabs playing audio
tabbrowser-tab-audio-playing-description = Playing audio

## Ctrl-Tab dialog

# Variables:
#   $tabCount (Number): The number of tabs in the current browser window. It will always be 2 at least.
tabbrowser-ctrl-tab-list-all-tabs =
    .label =
        { $tabCount ->
           *[other] List All { $tabCount } Tabs
        }

## Tab manager menu buttons
## Variables:
##  $tabGroupName (String): The name of the tab group. See also tab-group-name-default, which will be
##                          used when the group's name is empty.

tabbrowser-manager-mute-tab =
  .tooltiptext = Mute tab
tabbrowser-manager-unmute-tab =
  .tooltiptext = Unmute tab
tabbrowser-manager-close-tab =
  .tooltiptext = Close tab
# This is for tab groups that have been "saved and closed" (see tab-group-editor-action-save). It does
# not include "deleted" tab groups (see tab-group-editor-action-delete).
tabbrowser-manager-closed-tab-group =
  .label = { $tabGroupName }
  .tooltiptext = { $tabGroupName } — Closed
tabbrowser-manager-current-window-tab-group =
  .label = { $tabGroupName }
  .tooltiptext = { $tabGroupName } — Current window

## Tab Groups
## Variables:
##  $tabGroupName (String): The name of the tab group. See also tab-group-name-default, which will be
##                          used when the group's name is empty.

# Title placed over a list of all of the user's tab groups
tab-groups-list-title = Tab groups

tab-group-name-default = Unnamed Group
tab-group-editor-title-create = Create tab group
tab-group-editor-title-edit = Manage tab group
tab-group-editor-name-label = Name
tab-group-editor-name-field =
  .placeholder = Example: Shopping
tab-group-editor-cancel =
  .label = Cancel
  .accesskey = C

tab-group-editor-color-selector =
  .aria-label = Tab group color
tab-group-editor-color-selector2-blue = Blue
  .title = Blue
tab-group-editor-color-selector2-purple = Purple
  .title = Purple
tab-group-editor-color-selector2-cyan = Cyan
  .title = Cyan
tab-group-editor-color-selector2-orange = Orange
  .title = Orange
tab-group-editor-color-selector2-yellow = Yellow
  .title = Yellow
tab-group-editor-color-selector2-pink = Pink
  .title = Pink
tab-group-editor-color-selector2-green = Green
  .title = Green
tab-group-editor-color-selector2-gray = Gray
  .title = Gray
tab-group-editor-color-selector2-red = Red
  .title = Red

tab-group-menu-closed-tab-group =
  .label = { $tabGroupName }
  .title = { $tabGroupName } — Closed

## Variables:
##  $tabGroupName (String): The name of the tab group. Defaults to the value
##                          of tab-group-name-default.

tab-group-description = { $tabGroupName } — Tab Group
tab-group-label-tooltip-collapsed = { $tabGroupName } — Collapsed
tab-group-label-tooltip-expanded = { $tabGroupName } — Expanded
tab-group-preview-name =
  .aria-label = Tabs in a collapsed group

## When collapsed, the tab group label's aria-description will indicate
## whether the hover menu is open or closed.

tab-group-preview-open-description = Tabs list open
tab-group-preview-closed-description = Tabs list closed

##

tab-context-unnamed-group =
    .label = Unnamed group

## Variables:
##  $tabCount (Number): the number of tabs that are affected by the action.
##  $splitViewCount (Number): the number of split views that are affected by the action.

# When a tab group containing the active tab is collapsed, the active tab
# remains visible. An indicator appears at the end of the group showing the
# number of remaining tabs that are hidden by the collapsed group,
# e.g. "+2" for a group with 3 total tabs.
tab-group-overflow-count = +{ $tabCount }
tab-group-overflow-count-tooltip =
    { $tabCount ->
        [one] { $tabCount } more tab
        *[other] { $tabCount } more tabs
    }
tab-context-move-tab-to-new-group =
    .label =
        { $tabCount ->
            [1] Add Tab to New Group
           *[other] Add Tabs to New Group
        }
    .accesskey = G
tab-context-add-tab-to-group =
    .label =
        { $tabCount ->
            [1] Add to Group
           *[other] Add { $tabCount } Tabs to Group
        }
    .accesskey = G
tab-context-move-tab-to-group =
    .label =
        { $tabCount ->
            [1] Add Tab to Group
           *[other] Add Tabs to Group
        }
    .accesskey = G
tab-context-move-split-view-to-new-group =
    .label =
        { $splitViewCount ->
            [1] Add Split View to New Group
           *[other] Add Split Views to New Group
        }
    .accesskey = G
tab-context-move-split-view-to-group =
    .label =
        { $splitViewCount ->
            [1] Add Split View to Group
           *[other] Add Split Views to Group
        }
    .accesskey = G

##

# Used for the "Add to Group" submenu when opened on a split view.
# The split view's two tabs are added to the group together.
tab-context-add-split-view-to-group =
    .label = Add Tabs to Group
    .accesskey = G

tab-splitview-splitter =
    .aria-label = Resize split view tabs
tab-devtools-splitter =
    .aria-label = Resize Developer Tools panel

tab-context-move-tab-to-group-saved-groups =
    .label = Closed Groups
tab-group-editor-action-new-tab =
    .label = New tab in group
tab-group-editor-action-new-window =
    .label = Move group to new window
# Variables:
#  $linkCount (Number): the number of shareable links in the group.
tab-group-editor-action-copy-links =
    .label =
        { $linkCount ->
            [1] Copy link in group
           *[other] Copy { $linkCount } links in group
        }
tab-group-editor-action-save =
    .label = Save and close group
tab-group-editor-action-ungroup =
    .label = Ungroup tabs
tab-group-editor-action-delete =
    .label = Delete group
tab-group-editor-done =
    .label = Done
    .accessKey = D
# Share is a verb here. Meaning to "Share" the "tab group"
tab-group-editor-action-share-group =
    .label = Share group

tab-context-reopen-tab-group =
    .label = Reopen tab group

# Variables:
#  $groupCount (Number): the number of tab groups that are affected by the action.
tab-context-ungroup-tab =
    .label =
        { $groupCount ->
            [1] Remove from Group
           *[other] Remove from Groups
        }
    .accesskey = R

## The tab groups list provides a list of all open tab groups and saved tab
## groups in one place. When the user has no tab groups, the list instead
## recommends that the user create a tab group.

# Text for a button that, when clicked, creates a new tab group
tab-groups-list-create-group-button = New Group

tab-groups-list-empty-header = Tidy up your tabs
tab-groups-list-empty-description = Drag one tab onto another or right-click a tab to start organizing. We’ll save your groups here so they’re easy to find later.
tab-groups-list-empty-button = Create a tab group

## Open/saved tab group context menu

# For right-click context menu use in the "all tabs"/"tab overflow menu" when
# right-clicking on a tab group that is currently open in one of the user's
# windows.

# For a tab group open in any window, clicking this will create a new
# window and move this tab group to that new window.
tab-group-context-move-to-new-window =
    .label = Move Group to New Window

# For a tab group open in a different window from the one that the
# user is using to access the tab group menu, move that tab group into the
# user's current window.
tab-group-context-move-to-this-window =
    .label = Move Group to This Window

# For a tab group that is open in any window, build a shareable link for the
# tab group and open the content sharing dialog. Share is a verb here.
# .badge labels this menu item as a newly introduced feature.
tab-group-context-share-group =
    .label = Share Group
    .badge = New

# For a tab group that is open in any window, close the tab group and
# do not save it. For a tab group that is closed but saved by the user, clicking
# this will forget the saved tab group.
tab-group-context-delete =
    .label = Delete Group

# For a saved tab group that is not open in any window, open the tab group
# in the user's current window.
tab-group-context-open-saved-group-in-this-window =
    .label = Open Group in This Window

# For a saved tab group that is not open in any window, create a new window and
# open the tab group in that window.
tab-group-context-open-saved-group-in-new-window =
    .label = Open Group in New Window

## Tab Notes

tab-context-add-note =
    .label = Add Note
    .accesskey = A
tab-context-edit-note =
    .label = Edit Note
    .accesskey = E

# TODO Bug 2023230: `tab-context-delete-note` is no longer used as of bug 2023200,
# but it seems likely to return, so this string is being left in place for now.

tab-context-delete-note =
    .label = Delete Note
    .accesskey = D
tab-note-editor-title-create = Add note
tab-note-editor-title-edit = Edit note
tab-note-editor-text-field =
    .placeholder = What do you want to remember about this tab?
tab-note-editor-button-cancel =
    .label = Cancel
    .accesskey = C
tab-note-editor-button-save =
    .label = Save
    .accesskey = S
tab-note-editor-button-delete =
    .title = Delete note
    .aria-label = Delete note
    .accesskey = D
tab-note-preview-edit-icon =
    .alt = Edit note
# Link to show the full tab note in case it was truncated.
tab-note-preview-expand = Read more
tab-note-panel-add-note-new-badge =
    .label = New

# Displayed within the tab note edit dialog box when the user has entered more
# characters than are allowed.
# Variables:
#   $totalCharacters (Number): the number of characters the user has entered.
#   $maxAllowedCharacters (Number): the maximum number of characters allowed for a tab note.
tab-note-editor-character-limit =
    { $maxAllowedCharacters ->
         *[other] { NUMBER($totalCharacters, useGrouping: "false") }/{ NUMBER($maxAllowedCharacters, useGrouping: "false") } characters
    }

## Split View

# Split view tabs display their respective contents side by side
# Displayed within the tooltip on the left tab inside of a tab split view
# "left" corresponds to the visual position. Translate literally; do not swap for RTL languages.
# Variables:
#   $label (String): the text label of the tab visible in the tab strip
tabbrowser-tab-label-tab-split-view-left = { $label }, Split view left

# Split view tabs display their respective contents side by side
# Displayed within the tooltip on the right tab inside of a tab split view
# "right" corresponds to the visual position. Translate literally; do not swap for RTL languages.
# Variables:
#   $label (String): the text label of the tab visible in the tab strip
tabbrowser-tab-label-tab-split-view-right = { $label }, Split view right

# Open a new tab next to the current tab and display their contents side by side
tab-context-add-split-view =
    .label = Add Split View
    .accesskey = t
# Display the two selected tabs' contents side by side
tab-context-open-in-split-view =
    .label = Open in Split View
    .accesskey = t
# Separate the two split view tabs and display the tabs and their contents as normal
tab-context-separate-split-view =
    .label = Separate Split View
    .accesskey = t
# Separate the two split view tabs and display the tabs and their contents as normal
tab-context-separate-split-view-alt =
    .label = Separate Tabs
    .accesskey = t
# Reverse the order of the two tabs in the split view
tab-context-reverse-split-view =
    .label = Reverse Tabs
    .accesskey = r
tab-context-badge-new = New
# The label of a badge shown in menu items to call out features that are in beta.
tab-context-badge-beta = Beta

## Manage Split View (icon in the address bar & three-dot menu in the footer)

# "Separate" is a verb, as in "separate the split view tabs and display them normally".
split-view-menuitem-separate-tabs =
    .label = Separate Tabs
# "Reverse" is a verb, as in "reverse the order of split view tabs".
split-view-menuitem-reverse-tabs =
    .label = Reverse Tabs
split-view-menuitem-close-both-tabs =
    .label = Close Both Tabs

##
