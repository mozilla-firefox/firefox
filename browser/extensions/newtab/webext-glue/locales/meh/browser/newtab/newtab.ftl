# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Sukua jíía
newtab-settings-button =
    .title = Sá´á vii tutu ya´a sukua jíía

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Nánuku
    .aria-label = Nánuku
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Nánuku ji { $engine } a xíín chu'un iin nuu
newtab-search-box-handoff-text-no-engine = Nánuku a xíín chu´un iin nuu
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Nánuku ji { $engine } a xíín chu'un iin nuu
    .title = Nánuku ji { $engine } a xíín chu'un iin nuu
    .aria-label = Nánuku ji { $engine } a xíín chu'un iin nuu
newtab-search-box-handoff-input-no-engine =
    .placeholder = Nánuku a xíín chu´un iin nuu
    .title = Nánuku a xíín chu´un iin nuu
    .aria-label = Nánuku a xíín chu´un iin nuu
newtab-search-box-input =
    .placeholder = Nánuku nuu Web
    .aria-label = Nánuku nuu Web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Chisó ka̱a̱ nánuku
newtab-topsites-add-shortcut-header = A jíía acceso ñama
newtab-topsites-edit-topsites-header = Natee sitio xinañu´u
newtab-topsites-edit-shortcut-header = Natee acceso ñama
newtab-topsites-title-label = Título
newtab-topsites-title-input =
    .placeholder = Chu´un iin título
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Tee iin URL
newtab-topsites-url-validation = Nejika chu´unu iin URL vatu
newtab-topsites-image-url-label = URL tutu nátava personalizada
newtab-topsites-use-image-link = Ni´i iin tutu nátva personalizada…
newtab-topsites-image-validation = Ntu nkuvi síne tutu natava. Nachu´un inka URL

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Nkuvi-ka
newtab-topsites-delete-history-button = Náxitá nuu sɨ´ɨ nsá´ánu
newtab-topsites-save-button = Chuva´a
newtab-topsites-preview-button = Vista previa
newtab-topsites-add-button = Tee

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ¿A mana kuvinu xino da nchu'un nuu página ya'a?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ya´a nkuvi tanɨ.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Patrocinado

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Síne menu
    .aria-label = Síne menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Xita
    .aria-label = Xita
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Síne menu
    .aria-label = Síne menu nuu { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Natee sitio
    .aria-label = Natee sitio

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Natee
newtab-menu-open-new-window = Síne nuu iin ventana jíia
newtab-menu-open-new-private-window = Síne iin ventana yu´u
newtab-menu-dismiss = Xita
newtab-menu-pin = Anclar
newtab-menu-unpin = Desanclar
newtab-menu-delete-history = Xita nuu sɨ´ɨ nsá´ánu
newtab-menu-save-to-pocket = Chuva´a nuu { -pocket-brand-name }
newtab-menu-delete-pocket = Xita nuu { -pocket-brand-name }
newtab-menu-archive-pocket = Chuva´a nuu { -pocket-brand-name }
newtab-menu-show-privacy-info = Da patrocinadores da nuu'u ji privacidad

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Nsá´á
newtab-privacy-modal-button-manage = Tetiñu da ke'i contenido íyo patrocinado
newtab-privacy-modal-header = Privacidad jituinidani
newtab-privacy-modal-link = Kuni kua privacidad satiñu ji pestaña jíía

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Xita a ta´a ini noo´o
# Bookmark is a verb here.
newtab-menu-bookmark = A ta´a ini noo´o

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Tɨɨn link xinuun
newtab-menu-go-to-download-page = Kua'an nuu página xinuun
newtab-menu-remove-download = Xita nuu sɨ´ɨ nsá´ánu

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Kutuvi nuu Finder
       *[other] Síne carpeta contenedora
    }
newtab-menu-open-file = Sine archivo

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Nne´yanu
newtab-label-bookmarked = Nte nuu ta´a ini noo´o
newtab-label-removed-bookmark = Iin a ta´a ini noo´o nxita
newtab-label-recommended = Tendencias
newtab-label-saved = Chuva´a nuu { -pocket-brand-name }
newtab-label-download = Nxinuu
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Patrocinado
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Ya'a teku jie { $sponsor }

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Sna sección
newtab-section-menu-collapse-section = Sección de colapso
newtab-section-menu-expand-section = Sá´á ka´nu sección
newtab-section-menu-manage-section = Tetiñu sección
newtab-section-menu-manage-webext = Tetiñu da extensión
newtab-section-menu-add-topsite = Tee sitio vii
newtab-section-menu-add-search-engine = Chisó ka̱a̱ nánuku
newtab-section-menu-move-up = Scana si̱kɨ̱
newtab-section-menu-move-down = Scana chuve
newtab-section-menu-privacy-notice = Tu´un xitu a kumiji noo´o

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Sá´á luli sección
newtab-section-expand-section-label =
    .aria-label = Sá´á ka´nu sección

## Section Headers.

newtab-section-header-topsites = Da sitio vii
newtab-section-header-recent-activity = Da a skuata nsá´á
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = A kachi { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Kajie´e nánuku, je xine´e ani da tutu vii, tutu nátava káná ji da inka nuu nnánukunu.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Niko íchika je nánuku kue'ka tu'un { $provider }. ¿Nkunetunu? Kaji iin tu'un vii je nani'inu kue'eka tu'un vii nuu kuaiyo web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ¡A íyonu ki̱vɨ̱ vitá!
newtab-discovery-empty-section-topstories-content = Niko íchika saa kuninu kue´e artículos.
newtab-discovery-empty-section-topstories-try-again-button = Nasá´á tuku
newtab-discovery-empty-section-topstories-loading = Kunetu…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ¡Ups! Nkuni mati´i kuvi kivɨnu ya´a, je ntu nkuvi.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Tu'un vii:
newtab-pocket-more-recommendations = Kue´eka a kuvi ni´inu
newtab-pocket-learn-more = Ka´vi kue´eka
newtab-pocket-cta-button = Taji { -pocket-brand-name }
newtab-pocket-cta-text = Chuva'a da tu'un jiniñu'unu { -pocket-brand-name } je chu'un mikinu ji tu'un ka'vi vii.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ups, iyo iin ntu nkene vii saa kuvi síne yaa´a.
newtab-error-fallback-refresh-link = Nachu´un página je nakajie´e tuku.

## Customization Menu

newtab-custom-shortcuts-title = Da acceso ñama
newtab-custom-shortcuts-subtitle = Da nuu chuva´anu a xíín ne´yanu
newtab-custom-shortcuts-toggle =
    .label = Da acceso ñama
    .description = Da nuu chuva´anu a xíín ne´yanu
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
       *[other] { $num } filas
    }
newtab-custom-sponsored-sites = Da acceso íyo patrocinado
newtab-custom-pocket-sponsored = Da historias patrocinadas
newtab-custom-recent-title = Da a skuata nsá´á
newtab-custom-close-button = Nakasɨ
newtab-custom-settings = Tetiñu kue´e da kei´i

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

