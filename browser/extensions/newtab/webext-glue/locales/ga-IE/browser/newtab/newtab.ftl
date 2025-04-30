# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Cluaisín Nua
newtab-settings-button =
    .title = Saincheap an Leathanach do Chluaisín Nua

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Cuardach
    .aria-label = Cuardach
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Cuardaigh le { $engine } nó cuir isteach seoladh
newtab-search-box-handoff-text-no-engine = Cuardaigh nó cuir isteach seoladh
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Cuardaigh le { $engine } nó cuir isteach seoladh
    .title = Cuardaigh le { $engine } nó cuir isteach seoladh
    .aria-label = Cuardaigh le { $engine } nó cuir isteach seoladh
newtab-search-box-handoff-input-no-engine =
    .placeholder = Cuardaigh nó cuir isteach seoladh
    .title = Cuardaigh nó cuir isteach seoladh
    .aria-label = Cuardaigh nó cuir isteach seoladh
newtab-search-box-text = Cuardaigh an gréasán
newtab-search-box-input =
    .placeholder = Cuardaigh an Gréasán
    .aria-label = Cuardaigh an Gréasán

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Cuir Inneall Cuardaigh Leis
newtab-topsites-edit-topsites-header = Cuir an Barrshuíomh in Eagar
newtab-topsites-title-label = Teideal
newtab-topsites-title-input =
    .placeholder = Cuir teideal isteach
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Clóscríobh nó greamaigh URL
newtab-topsites-url-validation = URL neamhbhailí

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cealaigh
newtab-topsites-delete-history-button = Scrios ón Stair
newtab-topsites-save-button = Sábháil
newtab-topsites-preview-button = Réamhamharc
newtab-topsites-add-button = Cuir leis

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = An bhfuil tú cinnte gur mhaith leat an leathanach seo a scriosadh go hiomlán ó do stair?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ní féidir an gníomh seo a chur ar ceal.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Cuir an suíomh seo in eagar
    .aria-label = Cuir an suíomh seo in eagar

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Eagar
newtab-menu-open-new-window = Oscail i bhFuinneog Nua
newtab-menu-open-new-private-window = Oscail i bhFuinneog Nua Phríobháideach
newtab-menu-dismiss = Ruaig
newtab-menu-pin = Pionnáil
newtab-menu-unpin = Díphionnáil
newtab-menu-delete-history = Scrios ón Stair
newtab-menu-save-to-pocket = Sábháil in { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Scrios an Leabharmharc
# Bookmark is a verb here.
newtab-menu-bookmark = Cruthaigh leabharmharc

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".


## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.


## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Feicthe
newtab-label-bookmarked = Leabharmharcáilte
newtab-label-recommended = Treochtáil
newtab-label-download = Íoslódáilte

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-add-search-engine = Cuir Inneall Cuardaigh Leis

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = Barrshuímh
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Molta ag { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.


## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.


## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Topaicí i mbéal an phobail:

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.


## Customization Menu

newtab-custom-settings = Bainistigh tuilleadh socruithe

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

