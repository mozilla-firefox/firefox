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
reload-tab =
    .label = Reload Tab
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
duplicate-tabs =
    .label = Duplicate Tabs
    .accesskey = D
# The following string is displayed on a menuitem that will close the tabs from the start of the tabstrip to the currently targeted tab (excluding the currently targeted and any other selected tabs).
# In left-to-right languages this should use "Left" and in right-to-left languages this should use "Right".
close-tabs-to-the-start =
    .label = Close Tabs to Left
    .accesskey = l
close-tabs-to-the-start-vertical =
    .label = Close Tabs Above
    .accesskey = l
# The following string is displayed on a menuitem that will close the tabs from the end of the tabstrip to the currently targeted tab (excluding the currently targeted and any other selected tabs).
# In left-to-right languages this should use "Right" and in right-to-left languages this should use "Left".
close-tabs-to-the-end =
    .label = Close Tabs to Right
    .accesskey = i
close-tabs-to-the-end-vertical =
    .label = Close Tabs Below
    .accesskey = i
close-other-tabs =
    .label = Close Other Tabs
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
reload-tabs =
    .label = Reload Tabs
    .accesskey = R
pin-tab =
    .label = Pin Tab
    .accesskey = P
unpin-tab =
    .label = Unpin Tab
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
tab-context-open-in-new-container-tab =
    .label = Open in New Container Tab
    .accesskey = e
move-to-start =
    .label = Move to Start
    .accesskey = S
move-to-end =
    .label = Move to End
    .accesskey = E
move-to-new-window =
    .label = Move to New Window
    .accesskey = W
# Variables
#  $profileName (string): The name of the profile to move tab to
move-to-new-profile =
    .label = Move to { $profileName }
tab-context-close-multiple-tabs =
    .label = Close Multiple Tabs
    .accesskey = M
tab-context-close-duplicate-tabs =
    .label = Close Duplicate Tabs
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
tab-context-move-tabs =
    .label =
        { $tabCount ->
            [1] Move Tab
           *[other] Move Tabs
        }
    .accesskey = v

# The following string intentionally omits the word "Tab" from the singular and includes it in the plural.
tab-context-send-to-device =
    .label =
        { $tabCount ->
            [1] Send to Device
           *[other] Send { $tabCount } Tabs to Device
        }
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
