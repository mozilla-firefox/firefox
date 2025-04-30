# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = ಹೊಸ ಹಾಳೆ
newtab-settings-button =
    .title = ಹೊಸ ಹಾಳೆಯ ಪುಟವನ್ನು ಅಗತ್ಯಾನುಗುಣಗೊಳಿಸಿ

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = ಹುಡುಕು
    .aria-label = ಹುಡುಕು

## Top Sites - General form dialog.

newtab-topsites-edit-topsites-header = ಅಗ್ರ ತಾಣಗಳನ್ನು ಸಂಪಾದಿಸಿ
newtab-topsites-title-label = ಶೀರ್ಷಿಕೆ
newtab-topsites-title-input =
    .placeholder = ಶೀರ್ಷಿಕೆಯನ್ನು ನಮೂದಿಸಿ
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = ಒಂದು URL ಅನ್ನು ಟೈಪಿಸಿ ಅಥವಾ ನಕಲಿಸಿ
newtab-topsites-url-validation = ಸರಿಯಾದ URL ಬೇಕಾಗಿದೆ

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = ರದ್ದು ಮಾಡು
newtab-topsites-delete-history-button = ಇತಿಹಾಸದಿಂದ ಅಳಿಸು‍
newtab-topsites-save-button = ಉಳಿಸು
newtab-topsites-preview-button = ಮುನ್ನೋಟ
newtab-topsites-add-button = ಸೇರಿಸು

## Top Sites - Delete history confirmation dialog.

# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = ಈ ಕಾರ್ಯವನ್ನು ರದ್ದುಗೊಳಿಸಲು ಸಾಧ್ಯವಿರುವುದಿಲ್ಲ.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = ಮೆನು ತೆರೆ
    .aria-label = ಮೆನು ತೆರೆ
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = ಈ ತಾಣವನ್ನು ಸಂಪಾದಿಸು
    .aria-label = ಈ ತಾಣವನ್ನು ಸಂಪಾದಿಸು

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = ‍ತಿದ್ದು
newtab-menu-open-new-window = ಹೊಸ ಕಿಟಕಿಯಲ್ಲಿ ತೆರೆ
newtab-menu-open-new-private-window = ಹೊಸ ಖಾಸಗಿ ಕಿಟಕಿಯಲ್ಲಿ ತೆರೆ
newtab-menu-dismiss = ವಜಾಗೊಳಿಸು‍
newtab-menu-pin = ಪಿನ್
newtab-menu-unpin = ಅನ್‌ಪಿನ್
newtab-menu-delete-history = ಇತಿಹಾಸದಿಂದ ಅಳಿಸು‍
newtab-menu-save-to-pocket = { -pocket-brand-name } ಗೆ ಉಳಿಸಿ‍
newtab-menu-delete-pocket = { -pocket-brand-name } ನಿಂದ ಅಳಿಸಿ
newtab-menu-archive-pocket = { -pocket-brand-name } ಆರ್ಕೈವ್ ಮಾಡಿ

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = ಪುಟ ಗುರುತು ತೆಗೆ
# Bookmark is a verb here.
newtab-menu-bookmark = ಪುಟ ಗುರುತು

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ಡೌನ್ಲೋಡ್ ಕೊಂಡಿಯನ್ನು ಪ್ರತಿ ಮಾಡು
newtab-menu-go-to-download-page = ಡೌನ್ಲೋಡ್ ಪುಟಕ್ಕೆ ತೆರಳು
newtab-menu-remove-download = ಇತಿಹಾಸದಿಂದ ತೆಗೆದುಹಾಕು

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] ಶೋಧಕದಲ್ಲಿ ತೋರಿಸು
       *[other] ಹೊಂದಿರುವ ಕಡತಕೋಶವನ್ನು ತೆರೆ
    }
newtab-menu-open-file = ಕಡತವನ್ನು ತೆರೆ

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = ಭೇಟಿ ನೀಡಲಾದ‍
newtab-label-bookmarked = ಪುಟಗುರುತು ಮಾಡಲಾದ
newtab-label-recommended = ಪ್ರಚಲಿತ
newtab-label-saved = { -pocket-brand-name } ನಲ್ಲಿ ಉಳಿಸಲಾಗಿದೆ
newtab-label-download = ಡೌನ್ಲೋಡ್ ಮಾಡಲಾಗಿದೆ

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = ವಿಭಾಗವನ್ನು ತೆಗೆದುಹಾಕಿ
newtab-section-menu-collapse-section = ವಿಭಾಗವನ್ನು ಸಂಕುಚಿಸಿ
newtab-section-menu-expand-section = ವಿಭಾಗ ವಿಸ್ತರಿಸಿ
newtab-section-menu-manage-section = ವಿಭಾಗವನ್ನು ನಿರ್ವಹಿಸಿ
newtab-section-menu-manage-webext = ವಿಸ್ತರಣೆಯನ್ನು ನಿರ್ವಹಿಸಿ
newtab-section-menu-move-up = ಮೇಲೆ ಜರುಗಿಸು
newtab-section-menu-move-down = ಕೆಳಗೆ ಜರುಗಿಸು

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = ಪ್ರಮುಖ ತಾಣಗಳು
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } ರಿಂದ ಶಿಫಾರಸುಮಾಡುಲಾಗಿದೆ

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ವೀಕ್ಷಣೆ ಮಾಡಲು ಶುರುಮಾಡಿ, ಮತ್ತು ನಾವು ಇತ್ತೀಚೆಗೆ ಭೇಟಿ ನೀಡಿದ ಅಥವಾ ಬುಕ್‌ಮಾರ್ಕ್ ಮಾಡಲಾದ ಕೆಲವು ಶ್ರೇಷ್ಠ ಲೇಖನಗಳು, ವೀಡಿಯೊಗಳು ಮತ್ತು ಇತರ ಪುಟಗಳನ್ನು ನಾವು ತೋರಿಸುತ್ತೇವೆ.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.


## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = ಜನಪ್ರಿಯವಾದ ವಿಷಯಗಳು:
newtab-pocket-cta-button = { -pocket-brand-name } ಪಡೆಯಿರಿ

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.


## Customization Menu


## New Tab Wallpapers


## Solid Colors


## Abstract


## Celestial


## Celestial


## New Tab Weather


## Topic Labels


## Topic Selection Modal


## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

