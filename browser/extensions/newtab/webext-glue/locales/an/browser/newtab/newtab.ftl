# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nueva Pestanya
newtab-settings-button =
    .title = Personaliza la tuya pachina de Nueva Pestanya

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Mirar
    .aria-label = Mirar

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Anyadir motor de busqueda
newtab-topsites-edit-topsites-header = Editar lo puesto popular
newtab-topsites-title-label = Titol
newtab-topsites-title-input =
    .placeholder = Escribir un titol
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Triar u apegar una adreza web
newtab-topsites-url-validation = Fa falta una URL valida
newtab-topsites-image-url-label = URL d'imachen personalizada
newtab-topsites-use-image-link = Usar una imachen personalizada…
newtab-topsites-image-validation = Ha fallau la carga d'a imachen. Preba con una URL diferent.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancelar
newtab-topsites-delete-history-button = Eliminar de l'historial
newtab-topsites-save-button = Alzar
newtab-topsites-preview-button = Previsualizar
newtab-topsites-add-button = Anyadir

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Yes seguro que quiers borrar totas las instancias d'esta pachina en o tuyo historial?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Esta acción no se puede desfer.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Ubrir menú
    .aria-label = Ubrir menú
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Eliminar
    .aria-label = Eliminar
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Ubrir menú
    .aria-label = Ubrir menú contextual pa { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editar este puesto
    .aria-label = Editar este puesto

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editar
newtab-menu-open-new-window = Ubrir en una nueva finestra
newtab-menu-open-new-private-window = Ubrir en una nueva finestra privada
newtab-menu-dismiss = Descartar
newtab-menu-pin = Clavar
newtab-menu-unpin = Desclavar
newtab-menu-delete-history = Eliminar de l'historial
newtab-menu-save-to-pocket = Alzar en { -pocket-brand-name }
newtab-menu-delete-pocket = Borrar de { -pocket-brand-name }
newtab-menu-archive-pocket = Archivar en { -pocket-brand-name }
newtab-menu-show-privacy-info = Los nuestros patrocinadors y la tuya privacidat

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Feito
newtab-privacy-modal-button-manage = Chestionar los achustos de contenius patrocinaus
newtab-privacy-modal-header = La tuya privacidat ye important.
newtab-privacy-modal-paragraph-2 = Amás d'amostrar los articlos mas cautivadors, tamién tos amostramos conteniu relevant de patrocinadors seleccionaus. Te garantizamos que <strong>los tuyos datos de navegación no salen nunca de { -brand-product-name }</strong>: no las veyemos ni nusatros ni los nuestros patrocinadors.
newtab-privacy-modal-link = Saber mas sobre cómo funciona la privacidat en a nueva pestanya

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Sacar lo marcapachinas
# Bookmark is a verb here.
newtab-menu-bookmark = Anyadir marcapachinas

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar vinclo de descarga
newtab-menu-go-to-download-page = Ir ta la pachina de descarga
newtab-menu-remove-download = Borrar de l'historial

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Amostrar en o Finder
       *[other] Ubrir la carpeta an que se troba
    }
newtab-menu-open-file = Ubrir fichero

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Vesitau
newtab-label-bookmarked = Con marcapachinas
newtab-label-removed-bookmark = S'ha eliminau lo marcapachinas
newtab-label-recommended = Tendencia
newtab-label-saved = Alzau en { -pocket-brand-name }
newtab-label-download = Descargau
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Patrocinau
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Patrocianu per { $sponsor }

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Borrar la sección
newtab-section-menu-collapse-section = Plegar la sección
newtab-section-menu-expand-section = Desplegar la sección
newtab-section-menu-manage-section = Chestionar la sección
newtab-section-menu-manage-webext = Chestionar la extensión
newtab-section-menu-add-topsite = Anyadir un puesto popular
newtab-section-menu-add-search-engine = Anyadir motor de busqueda
newtab-section-menu-move-up = Puyar
newtab-section-menu-move-down = Baixar
newtab-section-menu-privacy-notice = Nota sobre privacidat

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Reducir la sección
newtab-section-expand-section-label =
    .aria-label = Ixamplar la sección

## Section Headers.

newtab-section-header-topsites = Mas freqüents
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recomendau per { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Empecipia a navegar, y t'iremos amostrando aquí grans articlos, videos y atras pachinas que has vesitau u marcau en zagueras.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ya ye tot per agora. Torna mas ta debant pa veyer mas articlos populars de { $provider }. No i puetz aguardar? Tría un tema popular pa descubrir los articlos mas interesants de tot lo web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Ya lo tiens!
newtab-discovery-empty-section-topstories-content = Torna mas tarda pa leyer mas articlos
newtab-discovery-empty-section-topstories-try-again-button = Torna-lo a intentar
newtab-discovery-empty-section-topstories-loading = Se ye cargando…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ups! Pareix que no s'ha puesto cargar de tot esta sección.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temas populars:
newtab-pocket-more-recommendations = Mas recomendacions
newtab-pocket-learn-more = Saber-ne mas
newtab-pocket-cta-button = Instala { -pocket-brand-name }
newtab-pocket-cta-text = Alza los tuyos articlos preferius en { -pocket-brand-name }, y regala-te con lecturas fascinants.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oi, ha fallau bella cosa en a carga d'este conteniu.
newtab-error-fallback-refresh-link = Refrescar la pachina pa tornar-lo a intentar.

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

