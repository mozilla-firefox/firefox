# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nova scheda
newtab-settings-button =
    .title = Pirsunalizza a pàggina dâ scheda nova

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Riscedi
    .aria-label = Riscedi
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Riscedi cu { $engine } o metti nu nnirizzu
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Riscedi cu { $engine } o metti nu nnirizzu
    .title = Riscedi cu { $engine } o metti nu nnirizzu
    .aria-label = Riscedi cu { $engine } o metti nu nnirizzu

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Junci muturi di risciduta
newtab-topsites-edit-topsites-header = Cancia situ principali
newtab-topsites-title-label = Tìtulu
newtab-topsites-title-input =
    .placeholder = Metti un tìtulu
newtab-topsites-url-label = Nnirizzu
newtab-topsites-url-input =
    .placeholder = Scrivi o ncoḍḍa nu nnirizzu
newtab-topsites-url-validation = È nicissariu nu nnirizzu vàlitu
newtab-topsites-image-url-label = Nnirizzu dâ mmàggini pirsunalizzata
newtab-topsites-use-image-link = Usa na mmàggini pirsunalizzata…
newtab-topsites-image-validation = Mpussìbbili carricari a mmàggini. Prova nu nnirizzu diversu.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Sfai
newtab-topsites-delete-history-button = Scancella dâ crunuluggìa
newtab-topsites-save-button = Sarba
newtab-topsites-preview-button = Antiprima
newtab-topsites-add-button = Junci

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Sì sicuru chi voi scancillari tutti i voti chi visitasti sta pàggina dâ crunuluggìa?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = St'azziuni nun si po sfari.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Grapi u minù
    .aria-label = Grapi u minù
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Grapi u minù
    .aria-label = Grapi u minù cuntistuali pi { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Cancia stu situ
    .aria-label = Cancia stu situ

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Cancia
newtab-menu-open-new-window = Grapi nta na finestra nova
newtab-menu-open-new-private-window = Grapi nta na finestra privata nova
newtab-menu-dismiss = Leva
newtab-menu-pin = Appuntiḍḍa
newtab-menu-unpin = Spuntiḍḍa
newtab-menu-delete-history = Scancella dâ crunuluggìa
newtab-menu-save-to-pocket = Sarba nne { -pocket-brand-name }
newtab-menu-delete-pocket = Scancella di { -pocket-brand-name }
newtab-menu-archive-pocket = Archivia nne { -pocket-brand-name }
newtab-menu-show-privacy-info = I nostri patrucinatura e a to privatizza

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Bonu
newtab-privacy-modal-header = A to privatizza è mpurtanti.
newtab-privacy-modal-link = Nzìgnati comu funziona a privatizza nnâ scheda nova.

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Scancella nzingalibbru
# Bookmark is a verb here.
newtab-menu-bookmark = Junci ê nzingalibbra

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copia a lijami di scarricamentu
newtab-menu-go-to-download-page = Vai â pàggina di scarricamentu
newtab-menu-remove-download = Scancella dâ crunuluggìa

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mustra nne Finder
       *[other] Grapi a carpetta unni s'attrova
    }
newtab-menu-open-file = Grapi pricu

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitatu
newtab-label-bookmarked = Nnê nzingalibbra
newtab-label-removed-bookmark = Nzingalibbru scancillatu
newtab-label-recommended = Di tinnenza
newtab-label-saved = Sarbatu nne { -pocket-brand-name }
newtab-label-download = Scarricatu

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Scancella sizziuni
newtab-section-menu-collapse-section = Riduci sizziuni
newtab-section-menu-expand-section = Spanni sizziuni
newtab-section-menu-manage-section = Manija sizzioni
newtab-section-menu-manage-webext = Manija stinneriu
newtab-section-menu-add-topsite = Junci situ principali
newtab-section-menu-add-search-engine = Junci muturi di risciduta
newtab-section-menu-move-up = Movi supra
newtab-section-menu-move-down = Movi jusu
newtab-section-menu-privacy-notice = Abbisu di privatizza

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Riduci sizziuni
newtab-section-expand-section-label =
    .aria-label = Spanni sizziuni

## Section Headers.

newtab-section-header-topsites = Siti principali
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Cunzigghiati di { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Accumincia a navigari, e cca ti mustraremu l'artìculi, i vidiu e autri pàggini chi visitasti di picca o chi mittisti nnê nzingalibbra.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Nun cci sunnu autri cosi. Torna cchiù tardu p'aviri autri nutizzi di { $provider }. Nun po' aspittari? Scarta n'argumentu pupulari p'attruvari autri nutizzi ntirissanti dâ riti.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Nun cci sunnu autri cosi!
newtab-discovery-empty-section-topstories-content = Torna cchiù tardu pi autri nutizzi.
newtab-discovery-empty-section-topstories-try-again-button = Torna a prova
newtab-discovery-empty-section-topstories-loading = Staju carricannu…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ops! Parissi chi sta sizziuni nun si carricò tutta.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Argumenti pupulari:
newtab-pocket-more-recommendations = Cchiù suggirimenti
newtab-pocket-cta-button = Pigghia { -pocket-brand-name }
newtab-pocket-cta-text = Sarba l'artìculi chi ti piàcinu nne { -pocket-brand-name }, e stìmula a to mmagginazziuni cu litturi ntirissanti.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ops! Quarchi cosa fallìu carricannu stu cuntinutu.
newtab-error-fallback-refresh-link = Attualizza sta pàggina pi pruvari arrè.

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

