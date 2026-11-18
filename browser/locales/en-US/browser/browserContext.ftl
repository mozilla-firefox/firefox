# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

navbar-tooltip-instruction =
    .value = { PLATFORM() ->
        [macos] Pull down to show history
       *[other] Right-click or pull down to show history
    }

## Back

# Variables
#   $shortcut (String) - A keyboard shortcut for the Go Back command.
main-context-menu-back-3 =
    .tooltiptext = Go back one page ({ $shortcut })
    .aria-label = Back

# This menuitem is only visible on macOS
main-context-menu-back-mac =
    .label = Back
    .accesskey = B

# Variables
#   $shortcut (String) - A keyboard shortcut for the Go Back command.
navbar-tooltip-back-3 =
    .value = Go back one page ({ $shortcut })

toolbar-button-back-3 =
    .label = Back

## Forward

# Variables
#   $shortcut (String) - A keyboard shortcut for the Go Forward command.
main-context-menu-forward-3 =
    .tooltiptext = Go forward one page ({ $shortcut })
    .aria-label = Forward

# This menuitem is only visible on macOS
main-context-menu-forward-mac =
    .label = Forward
    .accesskey = F

# Variables
#   $shortcut (String) - A keyboard shortcut for the Go Forward command.
navbar-tooltip-forward-3 =
    .value = Go forward one page ({ $shortcut })

toolbar-button-forward-3 =
    .label = Forward

## Reload

main-context-menu-reload-2 =
    .aria-label = Reload

# This menuitem is only visible on macOS
main-context-menu-reload-mac =
    .label = Reload
    .accesskey = R

toolbar-button-reload-2 =
    .label = Reload

## Stop

main-context-menu-stop-2 =
    .aria-label = Stop

# This menuitem is only visible on macOS
main-context-menu-stop-mac =
    .label = Stop
    .accesskey = S

toolbar-button-stop-2 =
    .label = Stop

## Stop-Reload Button

toolbar-button-stop-reload-2 =
    .title = Reload

## Account toolbar Button

toolbar-button-account =
    .label = Account
    .tooltiptext = Account

## Save Page

main-context-menu-page-save =
    .label = Save Page As…
    .accesskey = P

## Simple menu items

main-context-menu-bookmark-page-2 =
    .aria-label = Bookmark Page…
    .tooltiptext = Bookmark page

# This menuitem is only visible on macOS
# Cannot be shown at the same time as main-context-menu-edit-bookmark-mac,
# so should probably have the same access key if possible.
main-context-menu-bookmark-page-mac =
    .label = Bookmark Page…
    .accesskey = m

# This menuitem is only visible on macOS
# Cannot be shown at the same time as main-context-menu-bookmark-page-mac,
# so should probably have the same access key if possible.
main-context-menu-edit-bookmark-mac =
    .label = Edit Bookmark…
    .accesskey = m

# Variables
#   $shortcut (String) - A keyboard shortcut for the add bookmark command.
main-context-menu-bookmark-page-with-shortcut-2 =
    .aria-label = Bookmark Page…
    .tooltiptext = Bookmark page ({ $shortcut })

main-context-menu-edit-bookmark-2 =
    .aria-label = Edit Bookmark…
    .tooltiptext = Edit bookmark

# Variables
#   $shortcut (String) - A keyboard shortcut for the edit bookmark command.
main-context-menu-edit-bookmark-with-shortcut-2 =
    .aria-label = Edit Bookmark…
    .tooltiptext = Edit bookmark ({ $shortcut })

main-context-menu-open-link =
    .label = Open Link
    .accesskey = O

main-context-menu-open-link-new-tab =
    .label = Open Link in New Tab
    .accesskey = T

main-context-menu-open-link-split-view =
    .label = Open Link in Split View
    .accesskey = M

main-context-menu-open-link-container-tab =
    .label = Open Link in New Container Tab
    .accesskey = w

main-context-menu-open-link-new-window =
    .label = Open Link in New Window
    .accesskey = d

main-context-menu-open-link-new-private-window =
    .label = Open Link in New Private Window
    .accesskey = P

main-context-menu-bookmark-link-2 =
    .label = Bookmark Link…
    .accesskey = B

main-context-menu-save-link =
    .label = Save Link As…
    .accesskey = k

## The access keys for "Copy Link" and "Copy Email Address"
## should be the same if possible; the two context menu items
## are mutually exclusive.

main-context-menu-copy-email =
    .label = Copy Email Address
    .accesskey = l

main-context-menu-copy-link-simple =
    .label = Copy Link
    .accesskey = L

##

main-context-menu-copy-phone =
    .label = Copy Phone Number
    .accesskey = o

main-context-menu-preview-link =
    .label = Preview Link
    .accesskey = J

# "Copy Clean Link" means that Firefox will remove things from the link you
# copied, like items that identify you for advertising purposes, and other items
# sites add into URLs to help them deliver a certain browsing experience.
main-context-menu-copy-clean-link =
    .label = Copy Clean Link
    .accesskey = u

## Media (video/audio) controls
##
## The accesskey for "Play" and "Pause" are the
## same because the two context-menu items are
## mutually exclusive.

main-context-menu-media-play =
    .label = Play
    .accesskey = P

main-context-menu-media-pause =
    .label = Pause
    .accesskey = P

##

main-context-menu-media-mute =
    .label = Mute
    .accesskey = M

main-context-menu-media-unmute =
    .label = Unmute
    .accesskey = m

main-context-menu-media-play-speed-2 =
    .label = Speed
    .accesskey = d

main-context-menu-media-play-speed-slow-2 =
    .label = 0.5×

main-context-menu-media-play-speed-normal-2 =
    .label = 1.0×

main-context-menu-media-play-speed-fast-2 =
    .label = 1.25×

main-context-menu-media-play-speed-faster-2 =
    .label = 1.5×

main-context-menu-media-play-speed-fastest-2 =
    .label = 2×

main-context-menu-media-loop =
    .label = Loop
    .accesskey = L

## The access keys for "Show Controls" and "Hide Controls" are the same
## because the two context-menu items are mutually exclusive.

main-context-menu-media-show-controls =
    .label = Show Controls
    .accesskey = C

main-context-menu-media-hide-controls =
    .label = Hide Controls
    .accesskey = C

##

main-context-menu-media-video-fullscreen =
    .label = Full Screen
    .accesskey = F

main-context-menu-media-video-leave-fullscreen =
    .label = Exit Full Screen
    .accesskey = u

# This is used when right-clicking on a video in the
# content area when the Picture-in-Picture feature is enabled.
main-context-menu-media-watch-pip =
    .label = Watch in Picture-in-Picture
    .accesskey = u

main-context-menu-image-reload =
    .label = Reload Image
    .accesskey = R

main-context-menu-image-view-new-tab =
    .label = Open Image in New Tab
    .accesskey = I

main-context-menu-video-view-new-tab =
    .label = Open Video in New Tab
    .accesskey = i

main-context-menu-image-copy =
    .label = Copy Image
    .accesskey = y

main-context-menu-image-copy-link =
    .label = Copy Image Link
    .accesskey = o

main-context-menu-video-copy-link =
    .label = Copy Video Link
    .accesskey = o

main-context-menu-audio-copy-link =
    .label = Copy Audio Link
    .accesskey = o

main-context-menu-image-save-as =
    .label = Save Image As…
    .accesskey = v

main-context-menu-image-email =
    .label = Email Image…
    .accesskey = g

main-context-menu-image-set-image-as-background =
    .label = Set Image as Desktop Background…
    .accesskey = S

main-context-menu-image-copy-text =
    .label = Copy Text From Image
    .accesskey = x

main-context-menu-image-info =
    .label = View Image Info
    .accesskey = f

main-context-menu-image-desc =
    .label = View Description
    .accesskey = D

# Variables
#   $engine (String) - Name of the search engine that will perform the search.
main-context-menu-visual-search-2 =
    .label = Search Image with { $engine }
    .accesskey = e

main-context-menu-video-save-as =
    .label = Save Video As…
    .accesskey = v

main-context-menu-audio-save-as =
    .label = Save Audio As…
    .accesskey = v

main-context-menu-video-take-snapshot =
    .label = Take Snapshot…
    .accesskey = S

main-context-menu-video-email =
    .label = Email Video…
    .accesskey = a

main-context-menu-audio-email =
    .label = Email Audio…
    .accesskey = a

main-context-menu-send-to-device-2 =
    .label = Send to Device
    .accesskey = n

main-context-menu-send-to-mobile-2 =
    .label = Send to Mobile
    .accesskey = n

## Displayed within the send tab submenu to prompt users to sign in, enable sync, pair a device, or troubleshoot device issues.

main-context-menu-send-to-mobile-sign-in = Sign in to Send Tabs
main-context-menu-send-to-mobile-enable-sync2 = Enable sync to send a tab
main-context-menu-send-to-mobile-connect-phone2 = Connect your phone
main-context-menu-send-to-mobile-device-missing2 = Don’t See Your Device?
main-context-menu-send-to-mobile-sign-in-from-link = Sign in to Send a Link
main-context-menu-send-to-mobile-sign-in-from-page = Sign in to Send a Page
main-context-menu-send-to-mobile-enable-sync3 = Turn on Sync to Send Tabs
main-context-menu-send-to-mobile-enable-sync-from-link = Turn on Sync to Send a Link
main-context-menu-send-to-mobile-enable-sync-from-page = Turn on Sync to Send a Page
main-context-menu-send-to-mobile-connect-phone3 = Connect a Device to Send Tabs
main-context-menu-send-to-mobile-connect-phone-from-link = Connect a Device to Send a Link
main-context-menu-send-to-mobile-connect-phone-from-page = Connect a Device to Send a Page

##

# Displayed when there are saved passwords and the user clicks inside a username or password field
main-context-menu-use-saved-password =
    .label = Use Saved Password
    .accesskey = o

main-context-menu-use-relay-mask =
    .label = Use { -relay-brand-short-name } Email Mask
    .accesskey = E

main-context-menu-suggest-strong-password =
    .label = Suggest Strong Password…
    .accesskey = S

main-context-menu-manage-passwords =
    .label = Manage Passwords
    .accesskey = M

main-context-menu-add-engine =
    .label = Add Search Engine
    .accesskey = S

# Actions menu that appears when selecting text with actions like Ask AI, Search, or Copy.
main-context-menu-turn-on-actions-menu =
    .label = Turn On Actions Menu
    .accesskey = A

main-context-menu-link-send-to-device =
    .label = Send Link to Device
    .accesskey = n

main-context-menu-link-send-to-mobile =
    .label = Send Link to Mobile
    .accesskey = n

main-context-menu-frame =
    .label = This Frame
    .accesskey = h

main-context-menu-frame-show-this =
    .label = Show Only This Frame
    .accesskey = S

main-context-menu-frame-open-tab =
    .label = Open Frame in New Tab
    .accesskey = T

main-context-menu-frame-open-window =
    .label = Open Frame in New Window
    .accesskey = W

main-context-menu-frame-reload =
    .label = Reload Frame
    .accesskey = R

main-context-menu-frame-add-bookmark =
    .label = Bookmark Frame…
    .accesskey = m

main-context-menu-frame-save-as =
    .label = Save Frame As…
    .accesskey = F

main-context-menu-frame-print =
    .label = Print Frame…
    .accesskey = P

main-context-menu-frame-view-source =
    .label = View Frame Source
    .accesskey = V

main-context-menu-frame-view-info =
    .label = View Frame Info
    .accesskey = I

main-context-menu-print-selection-2 =
    .label = Print Selection…
    .accesskey = r

main-context-menu-view-selection-source =
    .label = View Selection Source
    .accesskey = e

main-context-menu-take-screenshot =
    .label = Take Screenshot
    .accesskey = T

main-context-menu-view-page-source =
    .label = View Page Source
    .accesskey = V

main-context-menu-bidi-switch-text =
    .label = Switch Text Direction
    .accesskey = w

main-context-menu-bidi-switch-page =
    .label = Switch Page Direction
    .accesskey = D

main-context-menu-inspect =
    .label = Inspect
    .accesskey = Q

# This item only shows for DevTools users, so its access key may be the same
# as another item's, such as "Copy Image".
main-context-menu-inspect-a11y-properties2 =
    .label = Inspect Accessibility Properties
    .accesskey = y

main-context-menu-eme-learn-more =
    .label = Learn more about DRM…
    .accesskey = D

# Variables
#   $containerName (String): The name of the current container
main-context-menu-open-link-in-container-tab =
    .label = Open Link in New { $containerName } Tab
    .accesskey = T

main-context-menu-reveal-password =
    .label = Reveal Password
    .accesskey = v

# The label of a badge shown in menu items to call out new features.
main-context-menu-new-feature-badge = New

# The label of a badge shown in menu items to call out features that are in beta.
main-context-menu-beta-feature-badge = Beta

main-context-menu-pdfjs-copy-page2 =
    .label = Copy page
    .accesskey = o

main-context-menu-pdfjs-cut-page2 =
    .label = Cut page
    .accesskey = g

main-context-menu-pdfjs-delete-page2 =
    .label = Delete page
    .accesskey = l

main-context-menu-pdfjs-save-page2 =
    .label = Save selection as…
    .accesskey = S
