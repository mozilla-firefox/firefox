# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Neuvo feuggio
newtab-settings-button =
    .title = Personalizza a teu pagina Neuvo feuggio

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Çerca
    .aria-label = Çerca

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Azonzi motô de riçerca
newtab-topsites-edit-topsites-header = Cangia scito prinçipâ
newtab-topsites-title-label = Titolo
newtab-topsites-title-input =
    .placeholder = Scrivi 'n titolo
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Scrivi ò incòlla URL
newtab-topsites-url-validation = Serve 'na URL bonn-a
newtab-topsites-image-url-label = URL da inmagine personalizâ
newtab-topsites-use-image-link = Adeuvia inagine personalizâ…
newtab-topsites-image-validation = Erô into caregamento de l'inmagine. Preuva 'n atra URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Anulla
newtab-topsites-delete-history-button = Scancella da-a stöia
newtab-topsites-save-button = Sarva
newtab-topsites-preview-button = Anteprimma
newtab-topsites-add-button = Azonzi

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Te seguo de scancelâ tutte e ripetiçioin de sta pagina da stöia?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Sta açion a no se peu anulâ.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Arvi menû
    .aria-label = Arvi menû
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Arvi menû
    .aria-label = Arvi into menû contesto pe { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Cangia sto scito
    .aria-label = Cangia sto scito

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Cangia
newtab-menu-open-new-window = Arvi in neuvo barcon
newtab-menu-open-new-private-window = Arvi in neuvo barcon privòu
newtab-menu-dismiss = Scancella
newtab-menu-pin = Azonzi a-a bacheca
newtab-menu-unpin = Leva da bacheca
newtab-menu-delete-history = Scancella da-a stöia
newtab-menu-save-to-pocket = Sarva in { -pocket-brand-name }
newtab-menu-delete-pocket = Scancella da { -pocket-brand-name }
newtab-menu-archive-pocket = Archivia in { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Scancella segnalibbro
# Bookmark is a verb here.
newtab-menu-bookmark = Azonzi a-i segnalibbri

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Còpia indirisso òrigine
newtab-menu-go-to-download-page = Vanni a-a pagina de descaregamento
newtab-menu-remove-download = Scancella da-a stöia

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Fanni vedde in Finder
       *[other] Arvi cartella
    }
newtab-menu-open-file = Arvi schedaio

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Vixitou
newtab-label-bookmarked = Azonto a-i segnalibbri
newtab-label-recommended = De tentensa
newtab-label-saved = Sarvou in { -pocket-brand-name }
newtab-label-download = Descaregou
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } men

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Scancella seçion
newtab-section-menu-collapse-section = Conprimmi seçion
newtab-section-menu-expand-section = Espandi seçion
newtab-section-menu-manage-section = Gestisci seçion
newtab-section-menu-manage-webext = Gestisci estenscioin
newtab-section-menu-add-topsite = Azonzi scito prinçipâ
newtab-section-menu-add-search-engine = Azonzi motô de riçerca
newtab-section-menu-move-up = Mescia in sciù
newtab-section-menu-move-down = Mescia in zu
newtab-section-menu-privacy-notice = Informativa in sciâ privacy

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Conprimmi seçion
newtab-section-expand-section-label =
    .aria-label = Espandi seçion

## Section Headers.

newtab-section-header-topsites = I megio sciti
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Consegiou da { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Iniçia a navegâ e, in sta seçion, saian mostræ articoli, video e atre pagine vixitæ de fresco ò azonti a-i segnalibbri.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = No gh'é atro. Contròlla ciù tardi se gh'é atre stöie da { $provider }. No t'eu aspetâ? Seleçionn-a 'n argomento tra quelli ciù popolari pe descovrî atre notiçie interesanti da-o Web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-try-again-button = Preuva torna
newtab-discovery-empty-section-topstories-loading = Carego…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ahime mi! Emmo squæxi caregou sta seçion ma no semmo ariescîi a caregâla tutta.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Argomenti popolari:
newtab-pocket-more-recommendations = Atri conseggi
newtab-pocket-learn-more = Atre informaçioin
newtab-pocket-cta-button = Piggite { -pocket-brand-name }
newtab-pocket-cta-text = Sarva e stöie che te piaxan into { -pocket-brand-name }, e carega torna a mente con letue che incantan.
newtab-pocket-save = Sarva
newtab-pocket-saved = Sarvòu

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ahime mi, gh'é quarche problema into caregamento de sto contegnuo.
newtab-error-fallback-refresh-link = Agiorna pagina pe provâ torna.

## Customization Menu

newtab-custom-shortcuts-title = Scorsaieu
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } riga
       *[other] { $num } righe
    }
newtab-custom-sponsored-sites = Scorsaieu sponsorizæ
newtab-custom-pocket-title = Consegiou da { -pocket-brand-name }

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

