# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nová karta
newtab-settings-button =
    .title = Prispôsobte si svoju stránku Nová karta
newtab-personalize-settings-icon-label =
    .title = Prispôsobte si Novú kartu
    .aria-label = Nastavenia
newtab-settings-dialog-label =
    .aria-label = Nastavenia
newtab-personalize-icon-label =
    .title = Prispôsobiť stránku novej karty
    .aria-label = Prispôsobiť stránku novej karty
newtab-personalize-dialog-label =
    .aria-label = Prispôsobiť
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Hľadať
    .aria-label = Hľadať
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Vyhľadávajte cez { $engine } alebo zadajte webovú adresu
newtab-search-box-handoff-text-no-engine = Zadajte adresu alebo výraz vyhľadávania
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Vyhľadávajte cez { $engine } alebo zadajte webovú adresu
    .title = Vyhľadávajte cez { $engine } alebo zadajte webovú adresu
    .aria-label = Vyhľadávajte cez { $engine } alebo zadajte webovú adresu
newtab-search-box-handoff-input-no-engine =
    .placeholder = Zadajte adresu alebo výraz vyhľadávania
    .title = Zadajte adresu alebo výraz vyhľadávania
    .aria-label = Zadajte adresu alebo výraz vyhľadávania
newtab-search-box-text = Vyhľadávanie na webe
newtab-search-box-input =
    .placeholder = Vyhľadávanie na webe
    .aria-label = Vyhľadávanie na webe

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Pridať vyhľadávací modul
newtab-topsites-add-shortcut-header = Nová skratka
newtab-topsites-edit-topsites-header = Upraviť top stránku
newtab-topsites-edit-shortcut-header = Upraviť skratku
newtab-topsites-add-shortcut-label = Pridať skratku
newtab-topsites-title-label = Názov
newtab-topsites-title-input =
    .placeholder = Zadajte názov
newtab-topsites-url-label = Adresa URL
newtab-topsites-url-input =
    .placeholder = Zadajte alebo prilepte adresu URL
newtab-topsites-url-validation = Vyžaduje sa platná adresa URL
newtab-topsites-image-url-label = Adresa URL vlastného obrázka
newtab-topsites-use-image-link = Použiť vlastný obrázok…
newtab-topsites-image-validation = Obrázok sa nepodarilo načítať. Skúste inú adresu URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Zrušiť
newtab-topsites-delete-history-button = Odstrániť z histórie
newtab-topsites-save-button = Uložiť
newtab-topsites-preview-button = Ukážka
newtab-topsites-add-button = Pridať

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Ste si istí, že chcete odstrániť všetky výskyty tejto stránky zo svojej histórie prehliadania?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Túto akciu nie je možné vrátiť späť.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponzorované

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Otvorí ponuku
    .aria-label = Otvorí ponuku
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Odstrániť
    .aria-label = Odstrániť
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Otvorí ponuku
    .aria-label = Otvorí kontextovú ponuku pre { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Upraviť túto stránku
    .aria-label = Upraviť túto stránku

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Upraviť
newtab-menu-open-new-window = Otvoriť v novom okne
newtab-menu-open-new-private-window = Otvoriť v novom súkromnom okne
newtab-menu-dismiss = Skryť
newtab-menu-pin = Pripnúť
newtab-menu-unpin = Odopnúť
newtab-menu-delete-history = Odstrániť z histórie
newtab-menu-save-to-pocket = Uložiť do { -pocket-brand-name(case: "gen") }
newtab-menu-delete-pocket = Odstrániť z { -pocket-brand-name(case: "gen") }
newtab-menu-archive-pocket = Archivovať v { -pocket-brand-name(case: "loc") }
newtab-menu-show-privacy-info = Naši sponzori a vaše súkromie
newtab-menu-about-fakespot = Čo je { -fakespot-brand-name }
newtab-menu-report-content = Nahlásiť tento obsah
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokovať
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Prestať sledovať tému

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Spravovať sponzorovaný obsah
newtab-menu-our-sponsors-and-your-privacy = Naši sponzori a vaše súkromie
newtab-menu-report-this-ad = Nahlásiť túto reklamu

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Hotovo
newtab-privacy-modal-button-manage = Nastavenie sponzorovaného obsahu
newtab-privacy-modal-header = Na vašom súkromí záleží.
newtab-privacy-modal-paragraph-2 = Okrem zaujímavých článkov vám taktiež zobrazujeme relevantný a preverený obsah od vybraných sponzorov. Nemusíte sa báť, <strong>vaše údaje nikdy neopustia { -brand-product-name }</strong> - neodosielajú sa nám ani našim sponzorom.
newtab-privacy-modal-link = Ďalšie informácie o tom, ako funguje súkromie na stránke novej karty

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Odstrániť záložku
# Bookmark is a verb here.
newtab-menu-bookmark = Pridať medzi záložky

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopírovať adresu súboru
newtab-menu-go-to-download-page = Prejsť na stránku so súborom
newtab-menu-remove-download = Odstrániť z histórie

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Zobraziť vo Finderi
       *[other] Otvoriť priečinok so súborom
    }
newtab-menu-open-file = Otvoriť súbor

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Navštívené
newtab-label-bookmarked = V záložkách
newtab-label-removed-bookmark = Záložka bola odstránená
newtab-label-recommended = Trendy
newtab-label-saved = Uložené do { -pocket-brand-name(case: "gen") }
newtab-label-download = Stiahnuté
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponzorované
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponzorované spoločnosťou { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Odstrániť sekciu
newtab-section-menu-collapse-section = Zbaliť sekciu
newtab-section-menu-expand-section = Rozbaliť sekciu
newtab-section-menu-manage-section = Spravovať sekciu
newtab-section-menu-manage-webext = Spravovať rozšírenie
newtab-section-menu-add-topsite = Pridať top stránku
newtab-section-menu-add-search-engine = Pridať vyhľadávací modul
newtab-section-menu-move-up = Posunúť vyššie
newtab-section-menu-move-down = Posunúť nižšie
newtab-section-menu-privacy-notice = Vyhlásenie o ochrane osobných údajov

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Zbaliť sekciu
newtab-section-expand-section-label =
    .aria-label = Rozbaliť sekciu

## Section Headers.

newtab-section-header-topsites = Top stránky
newtab-section-header-recent-activity = Nedávna aktivita
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Odporúča { $provider }
newtab-section-header-stories = Príbehy na zamyslenie
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dnešný výber pre vás

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Začnite s prehliadaním a my vám na tomto mieste ukážeme skvelé články, videá a ostatné stránky, ktoré ste nedávno navštívili alebo pridali medzi záložky.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Už ste prečítali všetko. Ďalšie príbehy zo služby { $provider } tu nájdete opäť neskôr. Nemôžete sa dočkať? Vyberte si populárnu tému a pozrite sa na ďalšie skvelé príbehy z celého webu.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Už ste prečítali všetko. Ďalšie príbehy tu nájdete neskôr. Neviete sa dočkať? Vyberte obľúbenú tému a nájdite ďalšie skvelé príbehy z celého webu.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Už ste prečítali všetko!
newtab-discovery-empty-section-topstories-content = Ďalšie príbehy tu nájdete opäť neskôr.
newtab-discovery-empty-section-topstories-try-again-button = Skúsiť znova
newtab-discovery-empty-section-topstories-loading = Načítava sa…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hups! Túto sekciu sa nepodarilo načítať.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populárne témy:
newtab-pocket-new-topics-title = Chcete ešte viac príbehov? Pozrite sa na tieto obľúbené témy z { -pocket-brand-name(case: "gen") }
newtab-pocket-more-recommendations = Ďalšie odporúčania
newtab-pocket-learn-more = Ďalšie informácie
newtab-pocket-cta-button = Získajte { -pocket-brand-name }
newtab-pocket-cta-text = Ukladajte si články do { -pocket-brand-name(case: "gen") } a užívajte si skvelé čítanie.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } je súčasťou rodiny { -brand-product-name(case: "gen") }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Uložiť
newtab-pocket-saved = Uložené

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Ďalšie podobné
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nie je pre mňa
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Vďaka. Vaša spätná väzba nám pomôže zlepšiť váš informačný kanál.
newtab-toast-dismiss-button =
    .title = Zavrieť
    .aria-label = Zavrieť

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Objavte to najlepšie z webu
newtab-pocket-onboarding-cta = Služba { -pocket-brand-name } skúma rozmanitú škálu rôznych príspevkov, aby vám priniesla čo najviac informatívny, inšpiratívny a dôveryhodný obsah priamo do vášho prehliadača { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Hups, pri načítavaní tohto obsahu sa niečo pokazilo.
newtab-error-fallback-refresh-link = Obnovením stránky to skúsite znova.

## Customization Menu

newtab-custom-shortcuts-title = Skratky
newtab-custom-shortcuts-subtitle = Stránky, ktoré si uložíte alebo navštívite
newtab-custom-shortcuts-toggle =
    .label = Skratky
    .description = Stránky, ktoré si uložíte alebo navštívite
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } riadok
        [few] { $num } riadky
       *[other] { $num } riadkov
    }
newtab-custom-sponsored-sites = Sponzorované skratky
newtab-custom-pocket-title = Odporúčané službou { -pocket-brand-name }
newtab-custom-pocket-subtitle = Výnimočný obsah vybraný službou { -pocket-brand-name }, ktorá je súčasťou rodiny { -brand-product-name(case: "gen") }
newtab-custom-stories-toggle =
    .label = Odporúčané príbehy
    .description = Výnimočný obsah spravovaný rodinou { -brand-product-name }
newtab-custom-pocket-sponsored = Sponzorované príbehy
newtab-custom-pocket-show-recent-saves = Zobraziť nedávno uložené položky
newtab-custom-recent-title = Nedávna aktivita
newtab-custom-recent-subtitle = Výber z nedávno navštívených stránok a obsahu
newtab-custom-recent-toggle =
    .label = Nedávna aktivita
    .description = Výber z nedávno navštívených stránok a obsahu
newtab-custom-weather-toggle =
    .label = Počasie
    .description = Dnešná predpoveď v skratke
newtab-custom-close-button = Zavrieť
newtab-custom-settings = Ďalšie nastavenia

## New Tab Wallpapers

newtab-wallpaper-title = Tapety
newtab-wallpaper-reset = Obnoviť predvolenú tapetu
newtab-wallpaper-upload-image = Nahrať obrázok
newtab-wallpaper-custom-color = Zvoľte farbu
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Obrázok prekročil limit veľkosti súboru { $file_size } MB. Skúste nahrať menší súbor.
newtab-wallpaper-error-file-type = Nepodarilo sa nám nahrať váš súbor. Skúste to znova s iným typom súboru.
newtab-wallpaper-light-red-panda = Červená panda
newtab-wallpaper-light-mountain = Biela hora
newtab-wallpaper-light-sky = Obloha s fialovými a ružovými oblakmi
newtab-wallpaper-light-color = Modré, ružové a žlté tvary
newtab-wallpaper-light-landscape = Scenéria zahmlenej hory
newtab-wallpaper-light-beach = Pláž s palmou
newtab-wallpaper-dark-aurora = Polárna žiara
newtab-wallpaper-dark-color = Červené a modré tvary
newtab-wallpaper-dark-panda = Panda červená ukrytá v lese
newtab-wallpaper-dark-sky = Mestská scenéria s nočnou oblohou
newtab-wallpaper-dark-mountain = Horská scenéria
newtab-wallpaper-dark-city = Fialová mestská scenéria
newtab-wallpaper-dark-fox-anniversary = Líška na chodníku pri lese
newtab-wallpaper-light-fox-anniversary = Líška na trávnatom poli so zahmlenou horskou krajinou

## Solid Colors

newtab-wallpaper-category-title-colors = Plné farby
newtab-wallpaper-blue = Modrá
newtab-wallpaper-light-blue = Svetlomodrá
newtab-wallpaper-light-purple = Svetlofialová
newtab-wallpaper-light-green = Svetlozelená
newtab-wallpaper-green = Zelená
newtab-wallpaper-beige = Béžová
newtab-wallpaper-yellow = Žltá
newtab-wallpaper-orange = Oranžová
newtab-wallpaper-pink = Ružová
newtab-wallpaper-light-pink = Svetloružová
newtab-wallpaper-red = Červená
newtab-wallpaper-dark-blue = Tmavomodrá
newtab-wallpaper-dark-purple = Tmavofialová
newtab-wallpaper-dark-green = Tmavozelená
newtab-wallpaper-brown = Hnedá

## Abstract

newtab-wallpaper-category-title-abstract = Abstraktné
newtab-wallpaper-abstract-green = Zelené tvary
newtab-wallpaper-abstract-blue = Modré tvary
newtab-wallpaper-abstract-purple = Fialové tvary
newtab-wallpaper-abstract-orange = Oranžové tvary
newtab-wallpaper-gradient-orange = Prechod oranžový a ružový
newtab-wallpaper-abstract-blue-purple = Modré a fialové tvary
newtab-wallpaper-abstract-white-curves = Biela s tieňovanými krivkami
newtab-wallpaper-abstract-purple-green = Gradient fialového a zeleného svetla
newtab-wallpaper-abstract-blue-purple-waves = Modré a fialové vlnité tvary
newtab-wallpaper-abstract-black-waves = Čierne vlnité tvary

## Celestial

newtab-wallpaper-category-title-photographs = Fotografie
newtab-wallpaper-beach-at-sunrise = Pláž pri východe slnka
newtab-wallpaper-beach-at-sunset = Pláž pri západe slnka
newtab-wallpaper-storm-sky = Búrková obloha
newtab-wallpaper-sky-with-pink-clouds = Obloha s ružovými oblakmi
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda červená zíva na strome
newtab-wallpaper-white-mountains = Biele hory
newtab-wallpaper-hot-air-balloons = Rôzne farby teplovzdušných balónov počas dňa
newtab-wallpaper-starry-canyon = Modrá hviezdna noc
newtab-wallpaper-suspension-bridge = Sivá fotografia celoodpruženého mosta počas dňa
newtab-wallpaper-sand-dunes = Biele pieskové duny
newtab-wallpaper-palm-trees = Silueta kokosových paliem počas zlatej hodiny
newtab-wallpaper-blue-flowers = Detailná fotografia kvetov s modrými okvetnými lístkami
newtab-wallpaper-forest-trees = Stromy pokryté hmlou
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Autor fotografie: <a data-l10n-name="name-link">{ $author_string }</a>, zdroj: <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Vyskúšajte nádych farieb
newtab-wallpaper-feature-highlight-content = Dodajte svojej novej karte svieži vzhľad pomocou tapiet.
newtab-wallpaper-feature-highlight-button = Rozumiem
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Zavrieť
    .aria-label = Zavrieť vyskakovacie okno
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Nebeské
newtab-wallpaper-celestial-lunar-eclipse = Zatmenie Mesiaca
newtab-wallpaper-celestial-earth-night = Nočná fotografia z nízkej obežnej dráhy Zeme
newtab-wallpaper-celestial-starry-sky = Hviezdna obloha
newtab-wallpaper-celestial-eclipse-time-lapse = Časozberné zatmenie Mesiaca
newtab-wallpaper-celestial-black-hole = Ilustrácia galaxie čiernej diery
newtab-wallpaper-celestial-river = Satelitný obraz rieky

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Pozrite si predpoveď od { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponzorované
newtab-weather-menu-change-location = Zmeniť oblasť
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Hľadať oblasť
    .aria-label = Hľadať oblasť
newtab-weather-change-location-search-input = Hľadať oblasť
newtab-weather-menu-weather-display = Zobrazenie počasia
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Jednoduché
newtab-weather-menu-change-weather-display-simple = Prepnúť na jednoduché zobrazenie
newtab-weather-menu-weather-display-option-detailed = Podrobné
newtab-weather-menu-change-weather-display-detailed = Prepnúť na podrobné zobrazenie
newtab-weather-menu-temperature-units = Jednotky teploty
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celzius
newtab-weather-menu-change-temperature-units-fahrenheit = Prepnúť na stupne Fahrenheita
newtab-weather-menu-change-temperature-units-celsius = Prepnite na stupne Celzia
newtab-weather-menu-hide-weather = Skryť počasie na novej karte
newtab-weather-menu-learn-more = Ďalšie informácie
# This message is shown if user is working offline
newtab-weather-error-not-available = Údaje o počasí nie sú momentálne k dispozícii.

## Topic Labels

newtab-topic-label-business = Podnikanie
newtab-topic-label-career = Kariéra
newtab-topic-label-education = Vzdelávanie
newtab-topic-label-arts = Zábava
newtab-topic-label-food = Jedlo
newtab-topic-label-health = Zdravie
newtab-topic-label-hobbies = Hranie hier
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Financie
newtab-topic-label-society-parenting = Rodičovstvo
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Veda
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Zaujímavé tipy
newtab-topic-label-sports = Šport
newtab-topic-label-tech = Technológie
newtab-topic-label-travel = Cestovanie
newtab-topic-label-home = Dom a záhrada

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Vyberte témy na doladenie informačného kanála
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Vyberte si dve alebo viac tém. Naši odborní kurátori uprednostňujú príbehy prispôsobené vašim záujmom. Aktualizovať môžete kedykoľvek.
newtab-topic-selection-save-button = Uložiť
newtab-topic-selection-cancel-button = Zrušiť
newtab-topic-selection-button-maybe-later = Možno neskôr
newtab-topic-selection-privacy-link = Zistite, ako chránime a spravujeme údaje
newtab-topic-selection-button-update-interests = Aktualizujte svoje záujmy
newtab-topic-selection-button-pick-interests = Vyberte si svoje záujmy

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sledovať
newtab-section-following-button = Sledované
newtab-section-unfollow-button = Prestať sledovať

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Zablokovať
newtab-section-blocked-button = Zablokované
newtab-section-unblock-button = Odblokovať

## Confirmation modal for blocking a section

newtab-section-cancel-button = Teraz nie
newtab-section-confirm-block-topic-p1 = Naozaj chcete zablokovať túto tému?
newtab-section-confirm-block-topic-p2 = Zablokované témy sa už nebudú zobrazovať vo vašom informačnom kanáli.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokovať { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Témy
newtab-section-manage-topics-button-v2 =
    .label = Spravovať témy
newtab-section-mangage-topics-followed-topics = Sledované
newtab-section-mangage-topics-followed-topics-empty-state = Zatiaľ nesledujete žiadne témy.
newtab-section-mangage-topics-blocked-topics = Zablokované
newtab-section-mangage-topics-blocked-topics-empty-state = Zatiaľ ste nezablokovali žiadne témy.
newtab-custom-wallpaper-title = Vlastné tapety sú tu
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Nahrajte svoju vlastnú tapetu alebo si vyberte vlastnú farbu a prispôsobte si svoj { -brand-product-name }.
newtab-custom-wallpaper-cta = Vyskúšajte to
