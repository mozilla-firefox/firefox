# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nowo karta
newtab-settings-button =
    .title = Napasuj strōna nowyj karty
newtab-personalize-icon-label =
    .title = Napasuj nowo karta
    .aria-label = Napasuj nowo karta
newtab-personalize-dialog-label =
    .aria-label = Napasuj

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Szukej
    .aria-label = Szukej
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Szukej ze { $engine } abo wkludź adresa
newtab-search-box-handoff-text-no-engine = Szukej abo wkludź adresa
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Szukej ze { $engine } abo wkludź adresa
    .title = Szukej ze { $engine } abo wkludź adresa
    .aria-label = Szukej ze { $engine } abo wkludź adresa
newtab-search-box-handoff-input-no-engine =
    .placeholder = Szukej abo wkludź adresa
    .title = Szukej abo wkludź adresa
    .aria-label = Szukej abo wkludź adresa
newtab-search-box-text = Szukej w internecie
newtab-search-box-input =
    .placeholder = Szukej w internecie
    .aria-label = Szukej w internecie

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Przidej wyszukowarka
newtab-topsites-add-shortcut-header = Nowy skrōt
newtab-topsites-edit-topsites-header = Edytuj topowo strōna
newtab-topsites-edit-shortcut-header = Edytuj skrōt
newtab-topsites-title-label = Tytuł
newtab-topsites-title-input =
    .placeholder = Wkludź tytuł
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Wpisz abo wraź adresa URL
newtab-topsites-url-validation = Potrzebno je dobro adresa URL
newtab-topsites-image-url-label = Adresa URL ôd włosnego ôbrozka
newtab-topsites-use-image-link = Użyj włosnego ôbrozka…
newtab-topsites-image-validation = Niy podarziło sie zaladować ôbrozka. Sprōbuj inkszyj adresy URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Pociep
newtab-topsites-delete-history-button = Skasuj z historyje
newtab-topsites-save-button = Spamiyntej
newtab-topsites-preview-button = Podglōnd
newtab-topsites-add-button = Przidej

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Na zicher skasować wszyskie wystōmpiynia tyj strōny z twojij historyje?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tyj akcyje niy idzie cofnōńć.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Spōnsorowane

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Ôdewrzij myni
    .aria-label = Ôdewrzij myni
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Skasuj
    .aria-label = Skasuj
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Ôdewrzij myni
    .aria-label = Ôdewrzij kōntekstowe myni do { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Edytuj ta strōna
    .aria-label = Edytuj ta strōna

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edytuj
newtab-menu-open-new-window = Ôdewrzij w nowym ôknie
newtab-menu-open-new-private-window = Ôdewrzij w nowym prywatnym ôknie
newtab-menu-dismiss = Ôdkoż
newtab-menu-pin = Przipnij
newtab-menu-unpin = Ôdepnij
newtab-menu-delete-history = Skasuj z historyje
newtab-menu-save-to-pocket = Spamiyntej do { -pocket-brand-name(case: "gen") }
newtab-menu-delete-pocket = Skasuj ze { -pocket-brand-name(case: "gen") }
newtab-menu-archive-pocket = Archiwizuj we { -pocket-brand-name(case: "loc") }
newtab-menu-show-privacy-info = Nasze spōnsory a twoja prywatność

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Fertich
newtab-privacy-modal-button-manage = Regiyruj sztalōnkami spōnsorowanyj zawartości
newtab-privacy-modal-header = Twoja prywatność je ważno.
newtab-privacy-modal-paragraph-2 = Krōm ciekawych ôzprowek pokazujymy ci tyż napasowane, sprawdzōne treści ôd ôbranych spōnsorōw. Możesz wierzić, iże <strong>dane twojigo przeglōndanio nigdy niy ôpuszczajōm twojij włosnyj kopije aplikacyje { -brand-product-name }</strong> — ani my, ani nasze spōnsory ich niy widzōm.
newtab-privacy-modal-link = Przewiydz sie wiyncyj ô prywatności na strōnie nowyj karty

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Wyciep zokłodka
# Bookmark is a verb here.
newtab-menu-bookmark = Zokłodka

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopiuj link pobiyranio
newtab-menu-go-to-download-page = Idź do strōny pobiyranio
newtab-menu-remove-download = Wyciep z historyje

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Pokoż we Finderze
       *[other] Ôdewrzij katalog
    }
newtab-menu-open-file = Ôdewrzij zbiōr

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Nawiydzōne
newtab-label-bookmarked = W zokłodkach
newtab-label-removed-bookmark = Zokłodka je wyciepano
newtab-label-recommended = Popularne
newtab-label-saved = Spamiyntane do { -pocket-brand-name(case: "gen") }
newtab-label-download = Pobrane
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Spōnsorowane
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Spōnsorowane ôd: { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Wyciep sekcyjo
newtab-section-menu-collapse-section = Swijej sekcyjo
newtab-section-menu-expand-section = Rozszyrzej sekcyjo
newtab-section-menu-manage-section = Regiyruj sekcyjōm
newtab-section-menu-manage-webext = Regiyruj rozszyrzyniym
newtab-section-menu-add-topsite = Przidej do topowych strōn
newtab-section-menu-add-search-engine = Przidej wyszukowarka
newtab-section-menu-move-up = Posuń na wiyrch
newtab-section-menu-move-down = Posuń na spodek
newtab-section-menu-privacy-notice = Ô prywatności

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Swijej sekcyjo
newtab-section-expand-section-label =
    .aria-label = Rozszyrzej sekcyjo

## Section Headers.

newtab-section-header-topsites = Topowe strōny
newtab-section-header-recent-activity = Niydowno aktywność
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Rekōmyndowane ôd { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Zacznij przeglōndać internet, a my pokożymy ci sam szumne artikle, filmy a inksze strōny niydowno nawiedzōne abo przidane do zokłodek.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = To tela. Wejrzij sam niyskorzij za nowymi artiklami ôd { $provider }. Niy umisz sie doczkać? Ôbier popularny tymat, coby znojś inkszo ciekawo zawartość z cołkigo neca.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Wszysko przeczytane!
newtab-discovery-empty-section-topstories-content = Wejrzij sam niyskorzij za nowościami.
newtab-discovery-empty-section-topstories-try-again-button = Sprōbuj jeszcze roz
newtab-discovery-empty-section-topstories-loading = Ladowanie…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Niy podarziło sie blank zaladować tyj sekcyji.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Popularne tymaty:
newtab-pocket-new-topics-title = Chcesz wiyncyj artykułōw? Wejrzij na te popularne tymaty: { -pocket-brand-name }
newtab-pocket-more-recommendations = Wiyncyj rekōmyndowanych
newtab-pocket-learn-more = Przewiydz sie wiyncyj
newtab-pocket-cta-button = Dostōń ze { -pocket-brand-name(case: "gen") }
newtab-pocket-cta-text = Spamiyntuj we { -pocket-brand-name(case: "loc") } teksty, co ci pasujōm, coby durch mieć co ciekawego do poczytanio.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } je tajlōm familije { -brand-product-name }

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Cosik sie niy podarziło przi wczytowaniu tyj zawartości.
newtab-error-fallback-refresh-link = Ôdświyż strōna, coby sprōbować jeszcze roz.

## Customization Menu

newtab-custom-shortcuts-title = Skrōty
newtab-custom-shortcuts-subtitle = Spamiyntane i nawiydzane strōny
newtab-custom-shortcuts-toggle =
    .label = Skrōty
    .description = Spamiyntane i nawiydzane strōny
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } raja
        [few] { $num } raje
       *[many] { $num } raji
    }
newtab-custom-sponsored-sites = Spōnsorowane skrōty
newtab-custom-pocket-title = Doradzōne ôd { -pocket-brand-name }
newtab-custom-pocket-subtitle = Ekstra zawartość ôbrano ôd { -pocket-brand-name }, co je we familiji { -brand-product-name }
newtab-custom-pocket-sponsored = Spōnsorowane nowiny
newtab-custom-recent-title = Niydowno aktywność
newtab-custom-recent-subtitle = Wybōr z niydownych strōn i zawartości
newtab-custom-recent-toggle =
    .label = Niydowno aktywność
    .description = Wybōr z niydownych strōn i zawartości
newtab-custom-close-button = Zawrzij
newtab-custom-settings = Inksze nasztalowania

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

