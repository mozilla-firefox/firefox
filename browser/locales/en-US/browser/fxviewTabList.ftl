# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# Variables:
#   $date (string) - Date to be formatted based on locale
fxviewtabrow-date = { DATETIME($date, dateStyle: "short") }

# Variables:
#   $time (string) - Time to be formatted based on locale
fxviewtabrow-time = { DATETIME($time, timeStyle: "short") }

# Variables:
#   $targetURI (string) - URL of tab that will be opened in the new tab
fxviewtabrow-tabs-list-tab =
  .title = Open { $targetURI } in a new tab

# Variables:
#   $tabTitle (string) - Title of tab being closed
fxviewtabrow-close-tab-button =
  .title = Close { $tabTitle }

# Variables:
#   $tabTitle (string) - Title of tab being dismissed
fxviewtabrow-dismiss-tab-button =
  .title = Dismiss { $tabTitle }

# Used instead of the localized relative time when a timestamp is within a minute or so of now
fxviewtabrow-just-now-timestamp = Just now

# Strings below are used for context menu options within panel-list.
# For developers, this duplicates command because the label attribute is required.

fxviewtabrow-delete = Delete
    .accesskey = D
fxviewtabrow-forget-about-this-site = Forget About This Site…
    .accesskey = F
fxviewtabrow-open-in-window = Open in New Window
    .accesskey = N
fxviewtabrow-open-in-private-window = Open in New Private Window
    .accesskey = P
# “Bookmark” is a verb, as in "Bookmark this page" (add to bookmarks).
fxviewtabrow-add-bookmark = Bookmark…
    .accesskey = B
fxviewtabrow-save-to-pocket = Save to { -pocket-brand-name }
    .accesskey = o
fxviewtabrow-copy-link = Copy Link
    .accesskey = L
fxviewtabrow-close-tab = Close Tab
    .accesskey = C
fxviewtabrow-move-tab = Move Tab
    .accesskey = v
fxviewtabrow-move-tab-start = Move to Start
    .accesskey = S
fxviewtabrow-move-tab-end = Move to End
    .accesskey = E
fxviewtabrow-move-tab-window = Move to New Window
    .accesskey = W
# Shown instead of fxviewtabrow-move-tab-group when there is no group to join.
# Accesskey avoids the Open tabs menu (v, P/p, M/m, L, n).
fxviewtabrow-move-tab-new-group = Add Tab to New Group
    .accesskey = G
fxviewtabrow-move-tab-group = Add Tab to Group
    .accesskey = G
# Accesskey is not G: that letter belongs to the parent item, and n/N belongs to Send.
fxviewtabrow-move-tab-group-new = New Group
    .accesskey = R
fxviewtabrow-move-tab-group-closed = Closed Groups
    .accesskey = O
fxviewtabrow-unnamed-group = Unnamed group
fxviewtabrow-ungroup-tab = Remove from Group
    .accesskey = R
fxviewtabrow-send-to-device = Send to Device
    .accesskey = n
fxviewtabrow-send-to-mobile = Send to Mobile
    .accesskey = n
fxviewtabrow-pin-tab = Pin Tab
    .accesskey = P
fxviewtabrow-unpin-tab = Unpin Tab
    .accesskey = p
fxviewtabrow-mute-tab = Mute Tab
    .accesskey = M
fxviewtabrow-unmute-tab = Unmute Tab
    .accesskey = m

# Variables:
#   $tabTitle (string) - Title of the tab to which the context menu is associated
fxviewtabrow-options-menu-button =
  .title = Options for { $tabTitle }

## Strings below are to be used without context (tab title/URL) on mute/unmute buttons

fxviewtabrow-mute-tab-button-no-context =
  .title = Mute tab
fxviewtabrow-unmute-tab-button-no-context =
  .title = Unmute tab

## The following strings are for displaying elements in Firefox View to prompt users to sign in, enable sync, pair a device, or troubleshoot device issues.

fxviewtabrow-send-to-mobile-sign-in = Sign in to Send Tabs
fxviewtabrow-send-to-mobile-turn-on-sync = Turn on Sync to Send Tabs
fxviewtabrow-send-to-mobile-connect-device = Connect a Device to Send Tabs
fxviewtabrow-send-to-mobile-device-missing2 = Don’t See Your Device?
fxviewtabrow-send-to-mobile-not-verified = Account Not Verified
fxviewtabrow-send-to-mobile-verify-account = Verify Your Account
