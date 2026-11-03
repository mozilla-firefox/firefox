# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# In left-to-right languages this should use "Right" and in right-to-left languages this should use "Left" to indicate the direction a new tab will open.
tab-context-new-tab-open =
    .label = New Tab to Right
    .accesskey = w
tab-context-new-tab-open-vertical =
    .label = New Tab Below
    .accesskey = w
tab-context-new-group =
    .label = New Group
    .accesskey = G

tab-context-add-tab-to-group-new-group =
    .label = New Group
tab-context-add-tab-to-group-closed-groups =
    .label = Closed Groups

reload-tab =
    .label = Reload Tab
    .accesskey = R
reload-tab2 =
    .label = Reload
    .accesskey = R
select-all-tabs =
    .label = Select All Tabs
    .accesskey = S
tab-context-play-tab =
    .label = Play Tab
    .accesskey = l
tab-context-play-tabs =
    .label = Play Tabs
    .accesskey = y
duplicate-tab =
    .label = Duplicate Tab
    .accesskey = D
duplicate-tab2 =
    .label = Duplicate
    .accesskey = D
duplicate-tabs =
    .label = Duplicate Tabs
    .accesskey = D
duplicate-tabs2 =
    .label = Duplicate
    .accesskey = D
# The following string is displayed on a menuitem that will close the tabs from the start of the tabstrip to the currently targeted tab (excluding the currently targeted and any other selected tabs).
# In left-to-right languages this should use "Left" and in right-to-left languages this should use "Right".
close-tabs-to-the-start =
    .label = Close Tabs to Left
    .accesskey = l
# The following string is displayed on a menuitem ("Close Multiple") that closes the tabs from the start of the tabstrip to the currently targeted tab (excluding the currently targeted and any other selected tabs).
# In left-to-right languages this should use "Left" and in right-to-left languages this should use "Right".
close-tabs-to-the-start-alt =
    .label = Tabs to Left
    .accesskey = l
close-tabs-to-the-start-vertical =
    .label = Close Tabs Above
    .accesskey = l
# The following string is displayed on a menuitem ("Close Multiple") in vertical layout that closes the tabs from the currently targeted tab to the top of the tabstrip.
close-tabs-to-the-start-vertical-alt =
    .label = Tabs Above
    .accesskey = l
# The following string is displayed on a menuitem that will close the tabs from the end of the tabstrip to the currently targeted tab (excluding the currently targeted and any other selected tabs).
# In left-to-right languages this should use "Right" and in right-to-left languages this should use "Left".
close-tabs-to-the-end =
    .label = Close Tabs to Right
    .accesskey = i
# The following string is displayed on a menuitem ("Close Multiple") that closes the tabs from the end of the tabstrip to the currently targeted tab (excluding the currently targeted and any other selected tabs).
# In left-to-right languages this should use "Right" and in right-to-left languages this should use "Left".
close-tabs-to-the-end-alt =
    .label = Tabs to Right
    .accesskey = i
close-tabs-to-the-end-vertical =
    .label = Close Tabs Below
    .accesskey = i
# The following string is displayed on a menuitem ("Close Multiple") in vertical layout that closes the tabs from the currently targeted tab to the bottom of the tabstrip.
close-tabs-to-the-end-vertical-alt =
    .label = Tabs Below
    .accesskey = i
close-other-tabs =
    .label = Close Other Tabs
    .accesskey = o
# The following string is displayed on a menuitem ("Close Multiple"), this closes all tabs in the tabstrip except for the selected tab.
close-other-tabs-alt =
    .label = All Others
    .accesskey = o

## Variables:
##  $tabCount (Number): the number of tabs that are affected by the action.

tab-context-unload-n-tabs =
    .label =
        { $tabCount ->
            [1] Unload Tab
           *[other] Unload { $tabCount } Tabs
        }
    .accesskey = U

# Context menu option, alternate label for unloading the content of 1 or more tabs to reduce memory usage
tab-context-unload-tabs =
    .label = Unload
    .accesskey = U
reload-tabs =
    .label = Reload Tabs
    .accesskey = R
pin-tab =
    .label = Pin Tab
    .accesskey = P
tab-context-pin-tab2 =
    .label = Pin
    .accesskey = P
unpin-tab =
    .label = Unpin Tab
    .accesskey = p
tab-context-unpin-tab2 =
    .label = Unpin
    .accesskey = p
pin-selected-tabs =
    .label = Pin Tabs
    .accesskey = P
unpin-selected-tabs =
    .label = Unpin Tabs
    .accesskey = p
bookmark-selected-tabs =
    .label = Bookmark Tabs…
    .accesskey = B
tab-context-bookmark-tab =
    .label = Bookmark Tab…
    .accesskey = B
# Context menu item used to bookmark 1 or more selected tabs
tab-context-bookmark-tab2 =
    .label = Bookmark
    .accesskey = B
tab-context-open-in-new-container-tab =
    .label = Open in New Container Tab
    .accesskey = e
tab-context-open-in-new-container-tab2 =
    .label = Open in a New Container Tab
    .accesskey = e
tab-context-open-in-container =
    .label = Open in Container
    .accesskey = e
move-to-start =
    .label = Move to Start
    .accesskey = S
move-to-start-alt =
    .label = Start
    .accesskey = S
move-to-end =
    .label = Move to End
    .accesskey = E
move-to-end-alt =
    .label = End
    .accesskey = E
move-to-new-window =
    .label = Move to New Window
    .accesskey = W
move-to-new-window-alt =
    .label = New Window
    .accesskey = W
# Variables
#  $profileName (string): The name of the profile to move tab to
move-to-new-profile =
    .label = Move to { $profileName }
tab-context-close-multiple-tabs =
    .label = Close Multiple Tabs
    .accesskey = M
# Sub-menu label in context menu with different options to close multiple tabs (e.g. close to right, left, etc.).
tab-context-close-multiple-tabs2 =
    .label = Close Multiple
    .accesskey = M
tab-context-close-duplicate-tabs =
    .label = Close Duplicate Tabs
    .accesskey = x
# The following string is displayed on a menuitem "Close Multiple", this closes all duplicate tabs of the selected tab in the tabstrip.
tab-context-close-duplicate-tabs-alt =
    .label = Duplicate Tabs
    .accesskey = x
tab-context-share-url =
    .label = Share
    .accesskey = h

## Variables:
##  $tabCount (Number): the number of tabs that are affected by the action.

tab-context-reopen-closed-tabs =
    .label =
        { $tabCount ->
            [1] Reopen Closed Tab
           *[other] Reopen Closed Tabs
        }
    .accesskey = o
tab-context-close-n-tabs =
    .label =
        { $tabCount ->
            [1] Close Tab
           *[other] Close { $tabCount } Tabs
        }
    .accesskey = C
tab-context-close-n-tabs2 =
    .label =
        { $tabCount ->
            [1] Close
           *[other] Close { $tabCount } Tabs
        }
    .accesskey = C
tab-context-move-tabs =
    .label =
        { $tabCount ->
            [1] Move Tab
           *[other] Move Tabs
        }
    .accesskey = v

# Context menu option, highlighting this shows a submenu of potential destinations to move one or more tabs to (tab groups, windows, profiles, start, end, etc.)
tab-context-move-tabs3 =
    .label =
        { $tabCount ->
            [1] Move to
           *[other] Move { $tabCount } Tabs to
        }
    .accesskey = v

# Context menu option, highlighting this shows a submenu of potential destinations to move two tabs of a split view together to (tab groups, windows, profiles, start, end, etc.)
tab-context-move-split-view =
    .label = Move Split View to
    .accesskey = v
# Context menu option, highlighting this shows a submenu of potential destinations to move two tabs of a split view together to (tab groups, windows, profiles, start, end, etc.)
tab-context-move-split-view-alt =
    .label = Move Tabs to
    .accesskey = v

# The following string intentionally omits the word "Tab" from the singular and includes it in the plural.
tab-context-send-to-device =
    .label =
        { $tabCount ->
            [1] Send to Device
           *[other] Send { $tabCount } Tabs to Device
        }
    .accesskey = n
tab-context-send-to-device2 =
    .label = Send to Your Devices
    .accesskey = n
# The following string intentionally omits the word "Tab" from the singular and includes it in the plural.
tab-context-send-to-mobile =
    .label =
        { $tabCount ->
            [1] Send to Mobile
           *[other] Send { $tabCount } Tabs to Mobile
        }
    .accesskey = n

# Verb: share a collection of selected tabs as a link
tab-context-share-selected-tabs =
    .label = Create Shareable Link
    .accesskey = a
