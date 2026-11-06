# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Confirmation hints

# Subtle notification shown when Extensions button moves into the application menu.
confirmation-hint-extensions-button-hidden = Moved to menu
confirmation-hint-password-removed = Password removed!
confirmation-hint-page-bookmarked = Saved to bookmarks
confirmation-hint-password-created = Password saved
confirmation-hint-password-updated = Password updated
confirmation-hint-address-created = Address saved
confirmation-hint-address-updated = Address updated
confirmation-hint-payment-method-saved = Payment method saved
confirmation-hint-payment-method-updated = Payment method updated
confirmation-hint-passport-created = Passport saved
confirmation-hint-passport-updated = Passport updated
confirmation-hint-pin-tab = Pinned!
confirmation-hint-pin-tab-description = Right-click the tab to unpin it.
confirmation-hint-send-to-device = Sent!
confirmation-hint-firefox-relay-mask-created = New mask created!
confirmation-hint-firefox-relay-mask-reused = Existing mask reused!
confirmation-hint-screenshot-copied = Screenshot copied!
confirmation-hint-tab-group-saved = Tab group saved
# Shown after the tab groups button is removed from the toolbar. Points to the app menu where tab groups can still be found.
confirmation-hint-tab-group-button-removed = Find tab groups in this menu
# Variables:
#   $tabCount (Number): The number of duplicate tabs closed, at least 1.
confirmation-hint-duplicate-tabs-closed =
  { $tabCount ->
      [one] Closed { $tabCount } tab
      *[other] Closed { $tabCount } tabs
  }
