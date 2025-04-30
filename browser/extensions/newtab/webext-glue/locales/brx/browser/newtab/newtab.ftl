# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = गोदान टेब

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = नागिर
    .aria-label = नागिर

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = नागिरनाय इन्जिनखौ दाजाबदेर
newtab-topsites-edit-topsites-header = गिबि साइटखौ सुजु
newtab-topsites-title-label = बिमुं
newtab-topsites-title-input =
    .placeholder = मोनसे बिमुं हो
newtab-topsites-url-label = URL
newtab-topsites-url-validation = बाहाय जाथाव URL नांगौ

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = नेवसि
newtab-topsites-delete-history-button = जारिमिन निफ्राय खोमोर
newtab-topsites-save-button = थिना दोन
newtab-topsites-preview-button = गिबि नुथाय
newtab-topsites-add-button = दाजाबदेर

## Top Sites - Delete history confirmation dialog.

# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = बे हाबाखौ मावनो हायै खालामनो हाया।

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = मेनु खेव
    .aria-label = मेनु खेव
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = बे साइटखौ सुजु
    .aria-label = बे साइटखौ सुजु

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = सुजुनाय
newtab-menu-open-new-window = मोनसे गोदान उइन्ड'आव खेव
newtab-menu-open-new-private-window = मोनसे गोदान प्राइभेट उइन्ड'खौ खेव
newtab-menu-dismiss = फोजोब
newtab-menu-pin = पिन खालाम
newtab-menu-unpin = आनपिन
newtab-menu-delete-history = जारिमिन निफ्राय खोमोर
newtab-menu-save-to-pocket = { -pocket-brand-name } आव थिना दोन
newtab-menu-delete-pocket = { -pocket-brand-name } निफ्राय खोमोर

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = जाखांबाइ

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = बुकमाकॅखौ बोखार
# Bookmark is a verb here.
newtab-menu-bookmark = बुकमार्क

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = डाउनल'ड लिंकखौ कपि खालाम
newtab-menu-go-to-download-page = डाउनल'ड बिलाइआव थां
newtab-menu-remove-download = जारिमिन निफ्राय बोखार

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] फल्डार थानायखौ खेव
       *[other] फल्डार थानायखौ खेव
    }
newtab-menu-open-file = फाइलखौ खेव

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = थांखांनाय
newtab-label-bookmarked = बुकमाकॅ दङ
newtab-label-removed-bookmark = बुकमाॅक बोखारबाय
newtab-label-recommended = त्रेन्दिगं
newtab-label-saved = { -pocket-brand-name } आव थिना दोनबाय
newtab-label-download = डाउनल'ड खालामबाय

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = खोन्दो बोखार
newtab-section-menu-add-topsite = गिबि साइट आव दाजाब
newtab-section-menu-add-search-engine = नागिरनाय इन्जिनखौ दाजाबदेर
newtab-section-menu-move-up = गोजौआव लाबो
newtab-section-menu-move-down = गाहायाव लाबो
newtab-section-menu-privacy-notice = गुमुरथि मिथिसारहोनाय

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = गिबि साइटफोर
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } आ बसोन होनाइ

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.


## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-try-again-button = फिन नाजा
newtab-discovery-empty-section-topstories-loading = ल'ड खालाम गासिनो दं...

## Pocket Content Section.

newtab-pocket-cta-button = { -pocket-brand-name } ला

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

