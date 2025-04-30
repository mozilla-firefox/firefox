# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Rakïj ñanj nakàa
newtab-settings-button =
    .title = Naduna dàj garan' ruhuât riña ñanj nakàa

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Nana'uì'
    .aria-label = Nana'uì'

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Nutò' a'ngô sa ruguñu'unj ñù' nana'uì'
newtab-topsites-edit-topsites-header = Nagi'io' sitio yitïnj in
newtab-topsites-title-label = Rà ñanj
newtab-topsites-title-input =
    .placeholder = Gachrun' rà ñanj
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Gachrun' 'ngo URL
newtab-topsites-url-validation = 'Ngo URL ni'ñanj an
newtab-topsites-image-url-label = Si URL ña du'ua ma
newtab-topsites-use-image-link = Garasun' sa nagi'iaj mu'un'...
newtab-topsites-image-validation = Nu nadusij ma. Garahue 'ngà a'ngo URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Duyichin'
newtab-topsites-delete-history-button = Dure' riña gaché nu'
newtab-topsites-save-button = Na'nïnj sà'
newtab-topsites-preview-button = Daj gá ma
newtab-topsites-add-button = Nutà'

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Yitinj àni ruat dure' daran' riña gaché nut riña pagina na anj?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Si ga'ue dure' sa 'ngà gahuin na

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Na'nïn' menû
    .aria-label = Na'nïn' menû
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Na'nïn' menû
    .aria-label = Na'ni' menu guenda { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Nagi'io' sitio na
    .aria-label = Nagi'io' sitio na

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Nagi'iô'
newtab-menu-open-new-window = Na'nïn' riña a'ngô rakïj ñaj nakàa
newtab-menu-open-new-private-window = Na'nïn' riña a'ngô rakïj ñaj huìi
newtab-menu-dismiss = Si gui'iaj guendo'
newtab-menu-pin = Gachrun'
newtab-menu-unpin = Si gachrun'
newtab-menu-delete-history = Dure' riña gaché nu'
newtab-menu-save-to-pocket = Nanín sa'aj riña { -pocket-brand-name }
newtab-menu-delete-pocket = Dure' riña { -pocket-brand-name }
newtab-menu-archive-pocket = Nagi'iaj chre' riña { -pocket-brand-name }
newtab-menu-show-privacy-info = Nej duguî' rugûñu'unj ni sa tna'uej rayi'ît

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gà' huaj
newtab-privacy-modal-header = Ûta ña'an hua sa gaché nu huìt
newtab-privacy-modal-link = Gini'in dàj 'iaj sun sa dugumîn sò' riña rakïj ñanj nakà nan

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Durë' sa arajsun nichrò' doj
# Bookmark is a verb here.
newtab-menu-bookmark = Sa arajsun nichrò' doj

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Garasun' da'nga' da' naduni'
newtab-menu-go-to-download-page = Gun' riña pagina naduninj
newtab-menu-remove-download = Dure' riña gaché nun'

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Nadigun' ma riña Finder
       *[other] Nà'nin' riña nu ma
    }
newtab-menu-open-file = Na'nïn' chrû ñanj

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Ga'anj ni'io'
newtab-label-bookmarked = Sa arajsun nichrò' doj
newtab-label-removed-bookmark = Nare' markadô
newtab-label-recommended = Chrej nìkoj hua
newtab-label-saved = Nanín sa'aj riña { -pocket-brand-name }
newtab-label-download = Ngà nadunin'

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Dure' seksion
newtab-section-menu-collapse-section = Guxun' seksion
newtab-section-menu-expand-section = Nagi'iaj yachi' seksion
newtab-section-menu-manage-section = Dugumin' seksion
newtab-section-menu-manage-webext = Dugumin' ra'a ma
newtab-section-menu-add-topsite = Nuto' sitio yitïnj doj
newtab-section-menu-add-search-engine = Nutò' a'ngô sa ruguñu'unj ñù' nana'uì'
newtab-section-menu-move-up = Dusiki' gan'an ne' yatá'a
newtab-section-menu-move-down = Dusiki' gan'an ne' riki
newtab-section-menu-privacy-notice = Notisia huìi

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Guxun' seksion
newtab-section-expand-section-label =
    .aria-label = Nagi'iaj yachi' seksion

## Section Headers.

newtab-section-header-topsites = Hiuj ni'iaj yitïnj rè'
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Sa hua hue'e taj { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Gayi’ì gachē nunt nī nadigân ñûnj nej sa huā hue’ê doj, gini’iājt nī a’ngô nej pajinâ ni’iāj nakàt doj hiūj nan.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Hua nakà ma. 'Ngà nanikaj ñunt ni nana'uit sa gahuin { $provider }. Si ga'ue gana'uij 'ngà a'. Ganahui 'ngo sa yitïnj doj da' nahuin hue'e si web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ¡Nitaj danè' gan'anjt!
newtab-discovery-empty-section-topstories-content = SI ruhuât gunïnt doj sa hua ni gatu ñû nana doj.
newtab-discovery-empty-section-topstories-try-again-button = A'ngô ñû
newtab-discovery-empty-section-topstories-loading = Hìaj ayi'ij...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ¡'Ò'! Ngà doj gachìn nayi'nïn hiuj nan, sani gàchin doj.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Sa yitïnj doj:
newtab-pocket-more-recommendations = A'ngô ne nuguan ni'ñanj huaa
newtab-pocket-cta-button = Girì' { -pocket-brand-name }
newtab-pocket-cta-text = Na'nïnj sà' nej nuguan' 'ï ruhuât riña { -pocket-brand-name } ni gataj ni'ñanj rát ngà nej sa gahiat.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Hua 'ngo sa nu gahui hue'e 'nga gayi'ij na'nïnj ma
newtab-error-fallback-refresh-link = Nagi'iaj nakà pagina nī garahue ñut

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

