# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Llingüeta nueva
newtab-settings-button =
    .title = Personalizar la páxina «Llingüeta nueva»
newtab-personalize-icon-label =
    .title = Personaliza «Llingüeta nueva»
    .aria-label = Personalizar «Llingüeta nueva»

## Search box component.

# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Busca con { $engine } o introduz una direición

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Amiestu d'un motor de busca
newtab-topsites-add-shortcut-header = Atayu nuevu
newtab-topsites-edit-shortcut-header = Edición d'un atayu
newtab-topsites-title-label = Títulu
newtab-topsites-title-input =
    .placeholder = Introduz un títulu
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Teclexa o apiega una URL
newtab-topsites-url-validation = Ríquese una URL válida
newtab-topsites-image-url-label = URL d'imaxe personalizada
newtab-topsites-use-image-link = Usar una imaxe personalizada…

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Encaboxar
newtab-topsites-delete-history-button = Desaniciar del historial
newtab-topsites-save-button = Guardar
newtab-topsites-add-button = Amestar

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ¿De xuru que quies desaniciar del to historial cada instancia d'esta páxina?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Esta aición nun pue desfacese.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Abrir el menú
    .aria-label = Abrir el menú
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Quitar
    .aria-label = Quitar
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editar esti sitiu
    .aria-label = Editar esti sitiu

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editar
newtab-menu-open-new-window = Abrir nuna ventana nueva
newtab-menu-open-new-private-window = Abrir nuna ventana privada nueva
newtab-menu-dismiss = Escartar
newtab-menu-pin = Fixar
newtab-menu-unpin = Lliberar
newtab-menu-delete-history = Desaniciar del historial
newtab-menu-save-to-pocket = Guardar en { -pocket-brand-name }
newtab-menu-delete-pocket = Desaniciar de { -pocket-brand-name }
newtab-menu-archive-pocket = Archivar en { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Fecho
newtab-privacy-modal-button-manage = Xestionar los axustes del conteníu patrocináu
newtab-privacy-modal-header = La privacidá importa.

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Quitar el marcador
# Bookmark is a verb here.
newtab-menu-bookmark = Amestar a Marcadores

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar l'enllaz de descarga
newtab-menu-go-to-download-page = Dir a la páxina de descarga
newtab-menu-remove-download = Quitar del historial

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Amosar en Finder
       *[other] Abrir la carpeta contenedora
    }
newtab-menu-open-file = Abrir el ficheru

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitóse
newtab-label-bookmarked = En Marcadores
newtab-label-recommended = Tendencia

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Quitar la seición
newtab-section-menu-collapse-section = Recoyer la seición
newtab-section-menu-expand-section = Espander la seición
newtab-section-menu-manage-section = Xestionar la seición
newtab-section-menu-manage-webext = Xestionar la estensión
newtab-section-menu-add-topsite = Amestar un sitiu principal
newtab-section-menu-add-search-engine = Amestar un motor de busca
newtab-section-menu-move-up = Xubir
newtab-section-menu-move-down = Baxar
newtab-section-menu-privacy-notice = Avisu de privacidá

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = Sitios principales
newtab-section-header-recent-activity = Actividá recién

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Comienza a restolar y equí vamos amosate dalgunos de los meyores artículos, vídeos y otres páxines que visitesti o amestesti apocayá a Marcadores.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-loading = Cargando...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ¡Meca! Paez qu'esta seición nun cargó del too.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temes populares:
newtab-pocket-more-recommendations = Más recomendaciones
newtab-pocket-learn-more = Lleer más

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Meca, asocedió daqué malo al cargar esti conteníu.

## Customization Menu

newtab-custom-shortcuts-title = Atayos
newtab-custom-shortcuts-subtitle = Sitios que guardes o visites
newtab-custom-shortcuts-toggle =
    .label = Atayos
    .description = Sitios que guardes o visites
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } filera
       *[other] { $num } fileres
    }
newtab-custom-sponsored-sites = Atayos patrocinaos
newtab-custom-recent-title = Actividá recién
newtab-custom-recent-subtitle = Una esbilla de los sitios y del conteníu recién
newtab-custom-recent-toggle =
    .label = Actividá recién
    .description = Una esbilla de los sitios y del conteníu recién
newtab-custom-close-button = Zarrar
newtab-custom-settings = Xestionar más axustes

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

