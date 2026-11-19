# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Lock screen covering Private Windows after the computer locks, goes to
## sleep or starts the screen saver, or after the user presses the shortcut to
## lock windows.

window-lock-private-title = Private Windows are locked
window-lock-private-description-macos = { -brand-short-name } hides your private pages when your computer locks, goes to sleep, or starts the screen saver.
window-lock-private-description-windows = { -brand-short-name } hides your private pages when your computer locks.
window-lock-unlock-button =
    .label = Unlock
# Shown on the lock screen when the operating system authentication prompt failed with an error.
window-lock-auth-error =
    .message = Couldn’t verify your identity. Try again.

# Title of the Windows authentication prompt when unlocking browser windows.
window-lock-os-auth-dialog-caption = { -brand-full-name }

# This message is shown in the Windows authentication prompt when unlocking Private Windows.
window-lock-private-os-auth-dialog-message-win = To unlock your Private Windows, enter your Windows sign-in details to confirm it’s you.

# This message is shown in the macOS authentication prompt when unlocking Private Windows.
# On macOS, only provide the reason that account verification is needed. Do not put a complete sentence here
# (the system adds “Firefox is trying to…” before it).
window-lock-private-os-auth-dialog-message-macosx = unlock your Private Windows
