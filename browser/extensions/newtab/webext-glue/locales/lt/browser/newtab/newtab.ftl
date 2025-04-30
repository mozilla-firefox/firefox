# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nauja kortelė
newtab-settings-button =
    .title = Tinkinkite savo naujos kortelės puslapį
newtab-personalize-icon-label =
    .title = Tinkinti naują kortelę
    .aria-label = Tinkinti naują kortelę
newtab-personalize-dialog-label =
    .aria-label = Tinkinti

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Ieškoti
    .aria-label = Ieškoti
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Įveskite adresą arba ieškokite per „{ $engine }“
newtab-search-box-handoff-text-no-engine = Įveskite adresą arba paieškos žodžius
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Įveskite adresą arba ieškokite per „{ $engine }“
    .title = Įveskite adresą arba ieškokite per „{ $engine }“
    .aria-label = Įveskite adresą arba ieškokite per „{ $engine }“
newtab-search-box-handoff-input-no-engine =
    .placeholder = Įveskite adresą arba paieškos žodžius
    .title = Įveskite adresą arba paieškos žodžius
    .aria-label = Įveskite adresą arba paieškos žodžius
newtab-search-box-text = Ieškokite saityne
newtab-search-box-input =
    .placeholder = Ieškokite saityne
    .aria-label = Ieškokite saityne

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Pridėti ieškyklę
newtab-topsites-add-shortcut-header = Naujas leistukas
newtab-topsites-edit-topsites-header = Redaguoti mėgstamą svetainę
newtab-topsites-edit-shortcut-header = Keisti leistuką
newtab-topsites-title-label = Pavadinimas
newtab-topsites-title-input =
    .placeholder = Įveskite pavadinimą
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Įveskite arba įklijuokite URL
newtab-topsites-url-validation = Reikalingas tinkamas URL
newtab-topsites-image-url-label = Kitoks paveikslo URL
newtab-topsites-use-image-link = Naudoti kitą paveikslą…
newtab-topsites-image-validation = Nepavyko įkelti paveikslo. Pabandykite kitokį URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Atsisakyti
newtab-topsites-delete-history-button = Pašalinti iš istorijos
newtab-topsites-save-button = Įrašyti
newtab-topsites-preview-button = Peržiūrėti
newtab-topsites-add-button = Pridėti

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Ar tikrai norite pašalinti visus šio tinklalapio įrašus iš savo naršymo žurnalo?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Atlikus šį veiksmą, jo atšaukti neįmanoma.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Remiama

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Atverti meniu
    .aria-label = Atverti meniu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Pašalinti
    .aria-label = Pašalinti
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Atverti meniu
    .aria-label = Atverti kontekstinį { $title } meniu
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Redaguoti šią svetainę
    .aria-label = Redaguoti šią svetainę

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Keisti
newtab-menu-open-new-window = Atverti naujame lange
newtab-menu-open-new-private-window = Atverti naujame privačiajame lange
newtab-menu-dismiss = Paslėpti
newtab-menu-pin = Įsegti
newtab-menu-unpin = Išsegti
newtab-menu-delete-history = Pašalinti iš istorijos
newtab-menu-save-to-pocket = Įrašyti į „{ -pocket-brand-name }“
newtab-menu-delete-pocket = Trinti iš „{ -pocket-brand-name }“
newtab-menu-archive-pocket = Archyvuoti per „{ -pocket-brand-name }“
newtab-menu-show-privacy-info = Mūsų rėmėjai ir jūsų privatumas

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gerai
newtab-privacy-modal-button-manage = Tvarkykite remiamo turinio nuostatas
newtab-privacy-modal-header = Jūsų privatumas yra svarbus.
newtab-privacy-modal-paragraph-2 =
    Mes ne tik pateikiame įtraukiančias istorijas, bet ir rodome susijusį,
    gerai vertinamą turinį iš atrinktų rėmėjų. Būkite ramūs – <strong>jūsų naršymo
    duomenys niekada neiškeliauja už jūsų asmeninės „{ -brand-product-name }“ ribų</strong> – mes jų nematome,
    kaip ir nemato mūsų rėmėjai.
newtab-privacy-modal-link = Sužinokite, kaip naujoje kortelėje veikia privatumas

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Pašalinti iš adresyno
# Bookmark is a verb here.
newtab-menu-bookmark = Įrašyti į adresyną

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopijuoti šaltinio adresą
newtab-menu-go-to-download-page = Eiti į atsisiuntimo tinklalapį
newtab-menu-remove-download = Pašalinti iš žurnalo

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Rodyti per „Finder“
       *[other] Atverti aplanką
    }
newtab-menu-open-file = Atverti failą

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Lankytasi
newtab-label-bookmarked = Iš adresyno
newtab-label-removed-bookmark = Adresyno įrašas pašalintas
newtab-label-recommended = Populiaru
newtab-label-saved = Įrašyta į „{ -pocket-brand-name }“
newtab-label-download = Atsiųsta
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Rėmėjas
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Rėmėjas: „{ $sponsor }“
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Pašalinti skiltį
newtab-section-menu-collapse-section = Suskleisti skiltį
newtab-section-menu-expand-section = Išplėsti skiltį
newtab-section-menu-manage-section = Tvarkyti skiltį
newtab-section-menu-manage-webext = Tvarkyti priedą
newtab-section-menu-add-topsite = Pridėti lankomą svetainę
newtab-section-menu-add-search-engine = Pridėti ieškyklę
newtab-section-menu-move-up = Pakelti
newtab-section-menu-move-down = Nuleisti
newtab-section-menu-privacy-notice = Privatumo pranešimas

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Suskleisti skiltį
newtab-section-expand-section-label =
    .aria-label = Išplėsti skiltį

## Section Headers.

newtab-section-header-topsites = Lankomiausios svetainės
newtab-section-header-recent-activity = Paskiausia veikla
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Rekomenduoja „{ $provider }“

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Pradėkite naršyti, o mes čia pateiksime puikių straipsnių, vaizdo įrašų bei kitų tinklalapių, kuriuose neseniai lankėtės ar įtraukėte į adresyną.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Viską perskaitėte. Užsukite vėliau, norėdami rasti daugiau gerų straipsnių iš „{ $provider }“. Nekantraujate? Pasirinkite populiarią temą, norėdami rasti daugiau puikių straipsnių saityne.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Viską perskaitėte!
newtab-discovery-empty-section-topstories-content = Daugiau straipsnių atsiras vėliau.
newtab-discovery-empty-section-topstories-try-again-button = Bandyti dar kartą
newtab-discovery-empty-section-topstories-loading = Įkeliama…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oi! Mes beveik įkėlėme šį skyrių, tačiau ne visai.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populiarios temos:
newtab-pocket-new-topics-title = Norite dar daugiau straipsnių? Peržvelkite šias populiarias temas iš „{ -pocket-brand-name }“
newtab-pocket-more-recommendations = Daugiau rekomendacijų
newtab-pocket-learn-more = Sužinoti daugiau
newtab-pocket-cta-button = Gauti „{ -pocket-brand-name }“
newtab-pocket-cta-text = Išsaugokite patinkančius straipsnius į „{ -pocket-brand-name }“, bei sužadinkite savo mintis stulbinančiomis istorijomis.
newtab-pocket-pocket-firefox-family = „{ -pocket-brand-name }“ yra „{ -brand-product-name }“ šeimos dalis

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ups, įkeliant šį turinį įvyko klaida.
newtab-error-fallback-refresh-link = Pabandykite iš naujo įkelti tinklalapį.

## Customization Menu

newtab-custom-shortcuts-title = Leistukai
newtab-custom-shortcuts-subtitle = Jūsų įrašytos arba lankomos svetainės
newtab-custom-shortcuts-toggle =
    .label = Leistukai
    .description = Jūsų įrašytos arba lankomos svetainės
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } eilutė
        [few] { $num } eilutės
       *[other] { $num } eilučių
    }
newtab-custom-sponsored-sites = Rėmėjų leistukai
newtab-custom-pocket-title = Rekomenduoja „{ -pocket-brand-name }“
newtab-custom-pocket-subtitle = Išskirtinis turinys, kuruojamas „{ -pocket-brand-name }“, kuri yra „{ -brand-product-name }“ šeimos dalis
newtab-custom-pocket-sponsored = Rėmėjų straipsniai
newtab-custom-recent-title = Paskiausia veikla
newtab-custom-recent-subtitle = Paskiausiai lankytos svetainės ir žiūrėtas turinys
newtab-custom-recent-toggle =
    .label = Paskiausia veikla
    .description = Paskiausiai lankytos svetainės ir žiūrėtas turinys
newtab-custom-close-button = Užverti
newtab-custom-settings = Keisti daugiau nuostatų

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

