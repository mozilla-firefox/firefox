# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nov zavihek
newtab-settings-button =
    .title = Prilagodite stran novega zavihka
newtab-personalize-settings-icon-label =
    .title = Prilagodite stran novega zavihka
    .aria-label = Nastavitve
newtab-settings-dialog-label =
    .aria-label = Nastavitve
newtab-personalize-icon-label =
    .title = Prilagodite nov zavihek
    .aria-label = Prilagodite nov zacihek
newtab-personalize-dialog-label =
    .aria-label = Prilagodi
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Iskanje
    .aria-label = Iskanje
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Iščite z iskalnikom { $engine } ali vnesite naslov
newtab-search-box-handoff-text-no-engine = Iskanje ali naslov strani
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Iščite z iskalnikom { $engine } ali vnesite naslov
    .title = Iščite z iskalnikom { $engine } ali vnesite naslov
    .aria-label = Iščite z iskalnikom { $engine } ali vnesite naslov
newtab-search-box-handoff-input-no-engine =
    .placeholder = Iskanje ali naslov strani
    .title = Iskanje ali naslov strani
    .aria-label = Iskanje ali naslov strani
newtab-search-box-text = Išči po spletu
newtab-search-box-input =
    .placeholder = Iskanje po spletu
    .aria-label = Iskanje po spletu

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Dodaj iskalnik
newtab-topsites-add-shortcut-header = Nova bližnjica
newtab-topsites-edit-topsites-header = Uredi glavno stran
newtab-topsites-edit-shortcut-header = Uredi bližnjico
newtab-topsites-add-shortcut-label = Dodaj bližnjico
newtab-topsites-title-label = Naslov
newtab-topsites-title-input =
    .placeholder = Vnesite ime
newtab-topsites-url-label = Spletni naslov
newtab-topsites-url-input =
    .placeholder = Vnesite ali prilepite spletni naslov
newtab-topsites-url-validation = Vnesite veljaven spletni naslov
newtab-topsites-image-url-label = Spletni naslov slike po meri
newtab-topsites-use-image-link = Uporabi sliko po meri …
newtab-topsites-image-validation = Slike ni bilo mogoče naložiti. Poskusite drug spletni naslov.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Prekliči
newtab-topsites-delete-history-button = Izbriši iz zgodovine
newtab-topsites-save-button = Shrani
newtab-topsites-preview-button = Predogled
newtab-topsites-add-button = Dodaj

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Ali ste prepričani, da želite izbrisati vse primerke te strani iz zgodovine?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tega dejanja ni mogoče razveljaviti.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponzorirano

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Odpri meni
    .aria-label = Odpri meni
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Odstrani
    .aria-label = Odstrani
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Odpri meni
    .aria-label = Odpri priročni meni za { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Uredi to stran
    .aria-label = Uredi to stran

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Uredi
newtab-menu-open-new-window = Odpri v novem oknu
newtab-menu-open-new-private-window = Odpri v novem zasebnem oknu
newtab-menu-dismiss = Opusti
newtab-menu-pin = Pripni
newtab-menu-unpin = Odpni
newtab-menu-delete-history = Izbriši iz zgodovine
newtab-menu-save-to-pocket = Shrani v { -pocket-brand-name }
newtab-menu-delete-pocket = Izbriši iz { -pocket-brand-name }a
newtab-menu-archive-pocket = Arhiviraj v { -pocket-brand-name }
newtab-menu-show-privacy-info = Naši pokrovitelji in vaša zasebnost
newtab-menu-about-fakespot = O { -fakespot-brand-name(sklon: "mestnik") }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Prepovej
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Nehaj slediti temi

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Upravljanje sponzorirane vsebine
newtab-menu-our-sponsors-and-your-privacy = Naši pokrovitelji in vaša zasebnost

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Končaj
newtab-privacy-modal-button-manage = Upravljaj nastavitve sponzorirane vsebine
newtab-privacy-modal-header = Vaša zasebnost je pomembna.
newtab-privacy-modal-paragraph-2 =
    Poleg zanimivih zgodb vam pokažemo tudi ustrezne, skrbno izbrane vsebine
    izbranih pokroviteljev. Zagotavljamo vam, da <strong>vaši podatki o brskanju nikoli
    ne zapustijo vašega { -brand-product-name }a</strong>. Ne vidimo jih niti mi niti naši pokrovitelji.
newtab-privacy-modal-link = Spoznajte, kako deluje zasebnost v novem zavihku

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Odstrani zaznamek
# Bookmark is a verb here.
newtab-menu-bookmark = Dodaj med zaznamke

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopiraj povezavo za prenos
newtab-menu-go-to-download-page = Pojdi na stran za prenos
newtab-menu-remove-download = Odstrani iz zgodovine

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Prikaži v Finderju
       *[other] Odpri vsebujočo mapo
    }
newtab-menu-open-file = Odpri datoteko

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Obiskano
newtab-label-bookmarked = Med zaznamki
newtab-label-removed-bookmark = Zaznamek odstranjen
newtab-label-recommended = Najbolj priljubljeno
newtab-label-saved = Shranjeno v { -pocket-brand-name }
newtab-label-download = Preneseno
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Oglas
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Pokrovitelj: { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Odstrani odsek
newtab-section-menu-collapse-section = Strni odsek
newtab-section-menu-expand-section = Razširi odsek
newtab-section-menu-manage-section = Upravljanje odseka
newtab-section-menu-manage-webext = Upravljaj razširitev
newtab-section-menu-add-topsite = Dodaj glavno stran
newtab-section-menu-add-search-engine = Dodaj iskalnik
newtab-section-menu-move-up = Premakni gor
newtab-section-menu-move-down = Premakni dol
newtab-section-menu-privacy-notice = Obvestilo o zasebnosti

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Strni odsek
newtab-section-expand-section-label =
    .aria-label = Razširi odsek

## Section Headers.

newtab-section-header-topsites = Glavne strani
newtab-section-header-recent-activity = Nedavna dejavnost
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Priporoča { $provider }
newtab-section-header-stories = Zgodbe, ki spodbujajo k razmisleku
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Današnji izbor za vas

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Začnite z brskanjem, mi pa vam bomo tu prikazovali odlične članke, videoposnetke ter druge strani, ki ste jih nedavno obiskali ali shranili med zaznamke.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Zdaj ste seznanjeni z novicami. Vrnite se pozneje in si oglejte nove prispevke iz { $provider }. Komaj čakate? Izberite priljubljeno temo in odkrijte več velikih zgodb na spletu.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Zdaj ste seznanjeni z novicami. Vrnite se pozneje in si oglejte nove prispevke. Komaj čakate? Izberite priljubljeno temo in odkrijte več velikih zgodb na spletu.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Ste na tekočem!
newtab-discovery-empty-section-topstories-content = Preverite pozneje za več zgodb.
newtab-discovery-empty-section-topstories-try-again-button = Poskusi znova
newtab-discovery-empty-section-topstories-loading = Nalaganje …
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ojoj! Nekaj se je zalomilo.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Priljubljene teme:
newtab-pocket-new-topics-title = Želite še več zgodb? Oglejte si najbolj priljubljene teme iz storitve { -pocket-brand-name }
newtab-pocket-more-recommendations = Več priporočil
newtab-pocket-learn-more = Več o tem
newtab-pocket-cta-button = Prenesi { -pocket-brand-name }
newtab-pocket-cta-text = Shranite zgodbe, ki jih imate radi, v { -pocket-brand-name }, in napolnite svoje misli z navdušujočim branjem.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } je del družine { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Shrani
newtab-pocket-saved = Shranjeno

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Več takšnih
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ni zame
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Hvala. Povratne informacije nam bodo pomagale izboljšati vaš vir.
newtab-toast-dismiss-button =
    .title = Skrij
    .aria-label = Skrij

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Odkrijte najboljše, kar ponuja splet
newtab-pocket-onboarding-cta = { -pocket-brand-name } vam iz raznolike palete publikacij prinaša informativno, navdihujočo in zanesljivo vsebino naravnost v brskalnik { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ojoj, pri nalaganju te vsebine je šlo nekaj narobe.
newtab-error-fallback-refresh-link = Osvežite stran za ponoven poskus.

## Customization Menu

newtab-custom-shortcuts-title = Bližnjice
newtab-custom-shortcuts-subtitle = Strani, ki jih shranite ali obiščete
newtab-custom-shortcuts-toggle =
    .label = Bližnjice
    .description = Strani, ki jih shranite ali obiščete
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } vrstica
        [two] { $num } vrstici
        [few] { $num } vrstice
       *[other] { $num } vrstic
    }
newtab-custom-sponsored-sites = Bližnjice oglaševalcev
newtab-custom-pocket-title = Priporoča { -pocket-brand-name }
newtab-custom-pocket-subtitle = Izjemna vsebina, ki jo pripravlja { -pocket-brand-name }, del družine { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Priporočene zgodbe
    .description = Izjemna vsebina, ki jo pripravlja družina { -brand-product-name }
newtab-custom-pocket-sponsored = Zgodbe oglaševalcev
newtab-custom-pocket-show-recent-saves = Prikaži nedavno shranjene strani
newtab-custom-recent-title = Nedavna dejavnost
newtab-custom-recent-subtitle = Izbor nedavnih spletnih mest in vsebin
newtab-custom-recent-toggle =
    .label = Nedavna dejavnost
    .description = Izbor nedavnih spletnih mest in vsebin
newtab-custom-weather-toggle =
    .label = Vreme
    .description = Današnja napoved vedno na očeh
newtab-custom-close-button = Zapri
newtab-custom-settings = Več nastavitev

## New Tab Wallpapers

newtab-wallpaper-title = Ozadja
newtab-wallpaper-reset = Ponastavi privzeto
newtab-wallpaper-upload-image = Naloži sliko
newtab-wallpaper-custom-color = Izberite barvo
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Največja dovoljena velikost slike je { $file_size } MB. Poskusite naložiti manjšo datoteko.
newtab-wallpaper-error-file-type = Datoteke ni bilo mogoče naložiti. Poskusite znova z drugo vrsto datoteke.
newtab-wallpaper-light-red-panda = Mačji panda
newtab-wallpaper-light-mountain = Bela gora
newtab-wallpaper-light-sky = Nebo z vijoličastimi in rožnatimi oblaki
newtab-wallpaper-light-color = Modri, rožnati in rumeni liki
newtab-wallpaper-light-landscape = Gorska pokrajina z modrimi meglicami
newtab-wallpaper-light-beach = Plaža s palmo
newtab-wallpaper-dark-aurora = Severni sij
newtab-wallpaper-dark-color = Rdeči in modri liki
newtab-wallpaper-dark-panda = Mačji panda, skrit v gozdu
newtab-wallpaper-dark-sky = Mestna pokrajina z nočnim nebom
newtab-wallpaper-dark-mountain = Gorska pokrajina
newtab-wallpaper-dark-city = Vijolična mestna pokrajina
newtab-wallpaper-dark-fox-anniversary = Lisica na pločniku blizu gozda
newtab-wallpaper-light-fox-anniversary = Lisica na travnatem polju v megleni gorski pokrajini

## Solid Colors

newtab-wallpaper-category-title-colors = Enobarvna
newtab-wallpaper-blue = Modro
newtab-wallpaper-light-blue = Svetlo modro
newtab-wallpaper-light-purple = Svetlo vijolično
newtab-wallpaper-light-green = Svetlo zeleno
newtab-wallpaper-green = Zeleno
newtab-wallpaper-beige = Bež
newtab-wallpaper-yellow = Rumeno
newtab-wallpaper-orange = Oranžno
newtab-wallpaper-pink = Roza
newtab-wallpaper-light-pink = Svetlo roza
newtab-wallpaper-red = Rdeče
newtab-wallpaper-dark-blue = Temno modro
newtab-wallpaper-dark-purple = Temno vijolično
newtab-wallpaper-dark-green = Temno zeleno
newtab-wallpaper-brown = Rjavo

## Abstract

newtab-wallpaper-category-title-abstract = Abstraktna
newtab-wallpaper-abstract-green = Zelene oblike
newtab-wallpaper-abstract-blue = Modre oblike
newtab-wallpaper-abstract-purple = Vijoličaste oblike
newtab-wallpaper-abstract-orange = Oranžne oblike
newtab-wallpaper-gradient-orange = Preliv oranžne in roza
newtab-wallpaper-abstract-blue-purple = Modre in vijolične oblike
newtab-wallpaper-abstract-white-curves = Bela z zasenčenimi krivuljami
newtab-wallpaper-abstract-purple-green = Preliv vijolične in zelene svetlobe
newtab-wallpaper-abstract-blue-purple-waves = Modre in vijolične valovite oblike
newtab-wallpaper-abstract-black-waves = Črne valovite oblike

## Celestial

newtab-wallpaper-category-title-photographs = Fotografije
newtab-wallpaper-beach-at-sunrise = Plaža ob sončnem vzhodu
newtab-wallpaper-beach-at-sunset = Plaža ob sončnem zahodu
newtab-wallpaper-storm-sky = Nevihtno nebo
newtab-wallpaper-sky-with-pink-clouds = Nebo z rožnatimi oblaki
newtab-wallpaper-red-panda-yawns-in-a-tree = Zehajoč mačji panda na drevesu
newtab-wallpaper-white-mountains = Bele gore
newtab-wallpaper-hot-air-balloons = Različne barve toplozračnih balonov podnevi
newtab-wallpaper-starry-canyon = Modra zvezdna noč
newtab-wallpaper-suspension-bridge = Siva fotografija visečega mostu podnevi
newtab-wallpaper-sand-dunes = Bele peščene sipine
newtab-wallpaper-palm-trees = Silhueta kokosovih palm med zlato uro
newtab-wallpaper-blue-flowers = Posnetek cvetočih rož z modrimi listi od blizu
newtab-wallpaper-forest-trees = Drevesa, prekrita z meglo
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Avtor fotografije: <a data-l10n-name="name-link">{ $author_string }</a>, <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Poskusite s kančkom barve
newtab-wallpaper-feature-highlight-content = Vdahnite strani novega zavihka svež videz in ji nastavite ozadje.
newtab-wallpaper-feature-highlight-button = Razumem
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Skrij
    .aria-label = Zapri okence
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Nebeški
newtab-wallpaper-celestial-lunar-eclipse = Lunin mrk
newtab-wallpaper-celestial-earth-night = Nočna fotografija iz nizke orbite Zemlje
newtab-wallpaper-celestial-starry-sky = Zvezdnato nebo
newtab-wallpaper-celestial-eclipse-time-lapse = Upočasnjen posnetek luninega mrka
newtab-wallpaper-celestial-black-hole = Ilustracija galaksije
newtab-wallpaper-celestial-river = Satelitski posnetek reke

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Prikaži napoved v storitvi { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponzorirano
newtab-weather-menu-change-location = Spremeni lokacijo
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Išči lokacijo
    .aria-label = Išči lokacijo
newtab-weather-change-location-search-input = Išči lokacijo
newtab-weather-menu-weather-display = Prikazovalnik vremena
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Preprosto
newtab-weather-menu-change-weather-display-simple = Preklopi na preprost pogled
newtab-weather-menu-weather-display-option-detailed = Podrobno
newtab-weather-menu-change-weather-display-detailed = Preklopi na podroben pogled
newtab-weather-menu-temperature-units = Enote za temperaturo
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celzij
newtab-weather-menu-change-temperature-units-fahrenheit = Preklopi na stopinje Fahrenheita
newtab-weather-menu-change-temperature-units-celsius = Preklopi na stopinje Celzija
newtab-weather-menu-hide-weather = Skrij vreme na novem zavihku
newtab-weather-menu-learn-more = Več o tem
# This message is shown if user is working offline
newtab-weather-error-not-available = Podatki o vremenu trenutno niso na voljo.

## Topic Labels

newtab-topic-label-business = Posel
newtab-topic-label-career = Kariera
newtab-topic-label-education = Izobraževanje
newtab-topic-label-arts = Zabava
newtab-topic-label-food = Hrana
newtab-topic-label-health = Zdravje
newtab-topic-label-hobbies = Igre
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Denar
newtab-topic-label-society-parenting = Starševstvo
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Znanost
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Življenjske zvijače
newtab-topic-label-sports = Šport
newtab-topic-label-tech = Tehnologija
newtab-topic-label-travel = Potovanje
newtab-topic-label-home = Dom in vrt

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Izberite teme za fino nastavitev vira
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Izberite dve ali več tem. Naši strokovnjaki dajejo prednost zgodbam, prilagojenim vašim zanimanjem. Nastavitve lahko spremenite kadarkoli.
newtab-topic-selection-save-button = Shrani
newtab-topic-selection-cancel-button = Prekliči
newtab-topic-selection-button-maybe-later = Morda pozneje
newtab-topic-selection-privacy-link = Kako ščitimo in upravljamo podatke
newtab-topic-selection-button-update-interests = Posodobite svoja zanimanja
newtab-topic-selection-button-pick-interests = Izberite svoja zanimanja

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sledi
newtab-section-following-button = Sledite
newtab-section-unfollow-button = Nehaj slediti

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Prepovej
newtab-section-blocked-button = Prepovedano
newtab-section-unblock-button = Dovoli

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ne zdaj
newtab-section-confirm-block-topic-p1 = Ali ste prepričani, da želite blokirati to temo?
newtab-section-confirm-block-topic-p2 = Blokirane teme se ne bodo več prikazovale v vašem viru.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokiraj { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Teme
newtab-section-manage-topics-button-v2 =
    .label = Upravljanje tem
newtab-section-mangage-topics-followed-topics = Spremljate
newtab-section-mangage-topics-followed-topics-empty-state = Ne spremljate še nobene teme.
newtab-section-mangage-topics-blocked-topics = Blokirano
newtab-section-mangage-topics-blocked-topics-empty-state = Prepovedali niste še nobene teme.
newtab-custom-wallpaper-title = Ozadja po meri so tu
newtab-custom-wallpaper-cta = Preizkusite
