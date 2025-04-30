# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Xikua tsàa
newtab-settings-button =
    .title = Sa'a nixi kunu koo pagina kitsau

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Nduku
    .aria-label = Nduku

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Chika'a ñaa nduku
newtab-topsites-edit-topsites-header = Sama sitio popular
newtab-topsites-title-label = Título
newtab-topsites-title-input =
    .placeholder = Chaa título
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Chaa a chisti'in iin URL

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Kunchatu
newtab-topsites-delete-history-button = Stoò ntii ña niya'a
newtab-topsites-save-button = Chika vaà
newtab-topsites-add-button = Chikaa

## Top Sites - Delete history confirmation dialog.


## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Kuna menú
    .aria-label = Kuna menú
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Kuna menú
    .aria-label = Kuna menú contextual takua { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Sama sitio yo
    .aria-label = Sama sitio yo

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Sama
newtab-menu-open-new-window = Kunàa nu inka nu sachuun tsàa
newtab-menu-open-new-private-window = Kunàa see inka nu sachuun tsàa
newtab-menu-dismiss = Kasi
newtab-menu-pin = Chita'an
newtab-menu-unpin = Sia'a
newtab-menu-delete-history = Stoò ntii ña ntsinu
newtab-menu-save-to-pocket = Chika va'a nu { -pocket-brand-name }
newtab-menu-delete-pocket = Stoo ña inka nu{ -pocket-brand-name }
newtab-menu-archive-pocket = Chika va'a nu { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Stoo markador
# Bookmark is a verb here.
newtab-menu-bookmark = Marka

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Ndatava enlace nu snuu
newtab-menu-go-to-download-page = Kua'an nu página snuu
newtab-menu-remove-download = Stoò ntii ña ntsinu

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Sna'a nu Finder
       *[other] Kuna karpeta nu inkai
    }
newtab-menu-open-file = Kuna tutu

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Ña ntsinu
newtab-label-bookmarked = Marka
newtab-label-recommended = Tu'un kanu
newtab-label-saved = Inka vai ni { -pocket-brand-name }
newtab-label-download = Snui

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Stoo ña nikatsiku
newtab-section-menu-collapse-section = Sección de colapso
newtab-section-menu-expand-section = Saa kanu ña nikatsiku
newtab-section-menu-manage-section = Administrar sección
newtab-section-menu-manage-webext = Gestionar extensión
newtab-section-menu-add-topsite = Chika sitio popular
newtab-section-menu-add-search-engine = Chika motor de búsqueda
newtab-section-menu-move-up = Kanta kuchi
newtab-section-menu-move-down = Kanta ninu
newtab-section-menu-privacy-notice = Aviso de privacidad

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Saa luu sección
newtab-section-expand-section-label =
    .aria-label = Sa kanu sección

## Section Headers.

newtab-section-header-topsites = Sitios favoritos

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.


## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-try-again-button = kitsà tuku
newtab-discovery-empty-section-topstories-loading = Sachuin

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temas populares:
newtab-pocket-cta-button = Nduku { -pocket-brand-name }

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

