# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

mini-window-close-button =
    .label = Close
    .tooltiptext = Close

# Moves the tab in the mini window back to the original window
# it came from.
mini-window-restore-button =
    .label = Put tab back
    .tooltiptext = Put tab back

## These strings are used as the label and tooltip for the button that mutes
## and unmutes the mini window's tab. The button only appears while the tab is
## playing audio, or while it is muted.

mini-window-audio-mute =
    .label = Mute tab
    .tooltiptext = Mute tab
mini-window-audio-unmute =
    .label = Unmute tab
    .tooltiptext = Unmute tab

## These strings label the buttons on the Screenshots selection overlay while
## it is being used to pick the region for a Mini Window.

# Moves the tab to a Mini Window which frames the selected area of the page.
screenshots-component-mini-window-button = Move to Mini Window
  .title = Move to Mini Window
  .aria-label = Move to Mini Window

# Dismisses the Mini Window selection overlay without moving the tab.
screenshots-component-mini-window-cancel-button = Cancel
  .title =
    { PLATFORM() ->
      [macos] Cancel (esc)
     *[other] Cancel (Esc)
    }
  .aria-label = Cancel

# Clears the current selection so the user can select a new region.
screenshots-component-reselect-button = Start over
  .title = Start over
  .aria-label = Start over

# Opens a selection tool to select a section of a page to
# open in a smaller window to appear cropped and always on top.
toolbar-button-mini-window =
  .label = Mini Window
  .tooltiptext = Use Mini Window

# Opens up the Mini Window section selector tool.
main-context-menu-use-mini-window =
    .label = Move Section to Mini Window
    .accesskey = w

# Moves full tabs to Mini Windows, one window per tab.
# Variables
#  $tabCount (Number): The number of tabs that will be moved.
move-to-mini-window =
    .label =
        { $tabCount ->
            [1] Move to Mini Window
           *[other] Move to Mini Windows
        }
    .accesskey = M

## These strings label the Screenshots overlay while it is being used to pick
## what to move into a Mini Window.

mini-window-overlay-header = Select part of the page to keep visible
mini-window-overlay-instructions = Drag or click to select an area to move to a Mini Window. It’ll stay on top while you browse.

## These strings label the panel that asks what the user wants to move into a
## Mini Window, shown alongside the Screenshots selection overlay.

mini-window-panel-header = Choose what to move to a Mini Window
# Selection for what to move to a Mini Window
mini-window-panel-part-of-page = Part of page
# Selection for what to move to a Mini Window
mini-window-panel-full-page = Full page
