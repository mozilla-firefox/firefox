# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nova kartica
newtab-settings-button =
    .title = Prilagodi stranicu za nove kartice
newtab-personalize-settings-icon-label =
    .title = Personaliziraj novu karticu
    .aria-label = Postavke
newtab-settings-dialog-label =
    .aria-label = Postavke
newtab-personalize-icon-label =
    .title = Personaliziraj novu karticu
    .aria-label = Personaliziraj novu karticu
newtab-personalize-dialog-label =
    .aria-label = Personaliziraj
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Traži
    .aria-label = Traži
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Traži pomoću { $engine } ili upiši adresu
newtab-search-box-handoff-text-no-engine = Traži ili upiši adresu
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Traži pomoću { $engine } ili upiši adresu
    .title = Traži pomoću { $engine } ili upiši adresu
    .aria-label = Traži pomoću { $engine } ili upiši adresu
newtab-search-box-handoff-input-no-engine =
    .placeholder = Traži ili upiši adresu
    .title = Traži ili upiši adresu
    .aria-label = Traži ili upiši adresu
newtab-search-box-text = Pretraži web
newtab-search-box-input =
    .placeholder = Pretraži web
    .aria-label = Pretraži web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Dodaj tražilicu
newtab-topsites-add-shortcut-header = Novi prečac
newtab-topsites-edit-topsites-header = Uredi najbolju stranicu
newtab-topsites-edit-shortcut-header = Uredi prečac
newtab-topsites-add-shortcut-label = Dodaj prečac
newtab-topsites-title-label = Naslov
newtab-topsites-title-input =
    .placeholder = Upiši naslov
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Utipkaj ili umetni URL
newtab-topsites-url-validation = Potrebno je unijeti ispravan URL
newtab-topsites-image-url-label = Prilagođeni URL slike
newtab-topsites-use-image-link = Koristi prilagođenu sliku…
newtab-topsites-image-validation = Neuspjelo učitavanje slike. Pokušaj jedan drugi URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Odustani
newtab-topsites-delete-history-button = Izbriši iz povijesti
newtab-topsites-save-button = Spremi
newtab-topsites-preview-button = Pregled
newtab-topsites-add-button = Dodaj

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Stvarno želiš izbrisati sve primjere ove stranice iz povijesti?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ova se radnja ne može poništiti.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponzorirano

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Otvori izbornik
    .aria-label = Otvori izbornik
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Ukloni
    .aria-label = Ukloni
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Otvori izbornik
    .aria-label = Otvorite kontekstni izbornik za { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Uredi ovu stranicu
    .aria-label = Uredi ovu stranicu

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Uredi
newtab-menu-open-new-window = Otvori u novom prozoru
newtab-menu-open-new-private-window = Otvori u novom privatnom prozoru
newtab-menu-dismiss = Odbaci
newtab-menu-pin = Zakači
newtab-menu-unpin = Otkači
newtab-menu-delete-history = Izbriši iz povijesti
newtab-menu-save-to-pocket = Spremi u { -pocket-brand-name }
newtab-menu-delete-pocket = Izbriši iz { -pocket-brand-name(case: "gen") }
newtab-menu-archive-pocket = Arhiviraj u { -pocket-brand-name }
newtab-menu-show-privacy-info = Naši sponzori i tvoja privatnost
newtab-menu-about-fakespot = O proširenju { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokiraj
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Prestani pratiti temu

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gotovo
newtab-privacy-modal-button-manage = Upravljaj postavkama sponzoriranog sadržaja
newtab-privacy-modal-header = Tvoja privatnost je važna.
newtab-privacy-modal-paragraph-2 =
    Osim što ti donosimo očaravajuće priče, također ti prikazujemo
    visoko provjeren sadržaj odabranih sponzora. <strong>Tvoji podaci nikada
    ne napuštaju tvoj lokalni { -brand-product-name }</strong> — mi ih ne vidimo,
    naši sponzori ih također ne vide.
newtab-privacy-modal-link = Saznaj kako privatnost funkcionira na novoj kartici

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Ukloni zabilješku
# Bookmark is a verb here.
newtab-menu-bookmark = Zabilježi stranicu

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopiraj poveznicu preuzimanja
newtab-menu-go-to-download-page = Idi na stranicu preuzimanja
newtab-menu-remove-download = Ukloni iz povijesti

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Prikaži u Finder-u
       *[other] Otvori sadržajnu mapu
    }
newtab-menu-open-file = Otvori datoteku

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Posjećeno
newtab-label-bookmarked = Zabilježeno
newtab-label-removed-bookmark = Zabilješka uklonjena
newtab-label-recommended = Popularno
newtab-label-saved = Spremljeno u { -pocket-brand-name }
newtab-label-download = Preuzeto
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponzorirano
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponzor { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Ukloni odjeljak
newtab-section-menu-collapse-section = Sklopi odjeljak
newtab-section-menu-expand-section = Rasklopi odjeljak
newtab-section-menu-manage-section = Upravljaj odjeljkom
newtab-section-menu-manage-webext = Upravljaj proširenjem
newtab-section-menu-add-topsite = Dodaj najbolju stranicu
newtab-section-menu-add-search-engine = Dodaj tražilicu
newtab-section-menu-move-up = Pomakni gore
newtab-section-menu-move-down = Pomakni dolje
newtab-section-menu-privacy-notice = Politika privatnosti

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Sklopi odjeljak
newtab-section-expand-section-label =
    .aria-label = Rasklopi odjeljak

## Section Headers.

newtab-section-header-topsites = Najbolje stranice
newtab-section-header-recent-activity = Nedavna aktivnost
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Preporučeno od { $provider }
newtab-section-header-stories = Priče koje potiču na razmišljanje
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Današnji preporučeni članci za tebe

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Započni pregledavanje i pokazat ćemo ti neke od izvrsnih članaka, videa i drugih web stranica prema tvojim nedavno posjećenim stranicama ili zabilješkama.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Provjeri kasnije daljnje najpopularnije priče od { $provider }. Ne možeš dočekati? Odaberi popularnu temu za pronalaženje daljnjih kvalitetnih priča s cijelog weba.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Navrati kasnije za više priča. Ne možeš dočekati? Odaberi popularne teme i pronađi još više kvalitetnih priča na internetu.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = U toku ste sa svime!
newtab-discovery-empty-section-topstories-content = Kasnije potraži daljnje priče.
newtab-discovery-empty-section-topstories-try-again-button = Pokušaj ponovo
newtab-discovery-empty-section-topstories-loading = Učitavanje…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Uh! Nismo potpuno učitali ovaj odjeljak.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Popularne teme:
newtab-pocket-new-topics-title = Želiš još više priča? Pogledaj ove popularne teme na servisu { -pocket-brand-name }
newtab-pocket-more-recommendations = Više preporuka
newtab-pocket-learn-more = Saznaj više
newtab-pocket-cta-button = Nabavi { -pocket-brand-name }
newtab-pocket-cta-text = Spremi priče koje ti se sviđaju u { -pocket-brand-name } i napuni si mozak vrhunskim štivom.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } je dio { -brand-product-name } obitelji
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Spremi
newtab-pocket-saved = Spremljeno

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Više kao ova
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nije za mene
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Hvala. Vaše povratne informacije pomoći će nam da poboljšamo predlaganje tema.
newtab-toast-dismiss-button =
    .title = Odbaci
    .aria-label = Odbaci

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Otkrij najbolje na webu
newtab-pocket-onboarding-cta = { -pocket-brand-name } istražuje raznolik raspon publikacija kako bi donio najinformativniji, inspirativniji i najvjerodostojniji sadržaj izravno u vaš { -brand-product-name } preglednik.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Došlo je do greške prilikom učitavanja ovog sadržaja.
newtab-error-fallback-refresh-link = Osvježi stranicu za ponovni pokušaj.

## Customization Menu

newtab-custom-shortcuts-title = Prečaci
newtab-custom-shortcuts-subtitle = Stranice koje spremiš ili posjetiš
newtab-custom-shortcuts-toggle =
    .label = Prečaci
    .description = Stranice koje spremiš ili posjetiš
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } redak
        [few] { $num } retka
       *[other] { $num } redaka
    }
newtab-custom-sponsored-sites = Sponzorirani prečaci
newtab-custom-pocket-title = Preporuke iz { -pocket-brand-name(case: "gen") }
newtab-custom-pocket-subtitle = Izuzetan sadržaj kojeg odabire { -pocket-brand-name }, dio obitelji { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Preporučene priče
    .description = Izvanredni sadržaj odabran od { -brand-product-name } obitelji
newtab-custom-pocket-sponsored = Sponzorirane priče
newtab-custom-pocket-show-recent-saves = Prikaži nedavna spremanja
newtab-custom-recent-title = Nedavna aktivnost
newtab-custom-recent-subtitle = Izbor nedavno posjećenih stranica i sadržaja
newtab-custom-recent-toggle =
    .label = Nedavna aktivnost
    .description = Izbor nedavno posjećenih stranica i sadržaja
newtab-custom-weather-toggle =
    .label = Vrijeme
    .description = Današnja prognoza
newtab-custom-close-button = Zatvori
newtab-custom-settings = Upravljaj dodatnim postavkama

## New Tab Wallpapers

newtab-wallpaper-title = Pozadine
newtab-wallpaper-reset = Obnovi na standardno
newtab-wallpaper-upload-image = Prenesi sliku
newtab-wallpaper-custom-color = Odaberi boju
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Slika premašuje ograničenje veličine datoteke od { $file_size } MB. Pokušaj prenijeti manju datoteku.
newtab-wallpaper-error-file-type = Nismo mogli prenijeti tvoju datoteku. Pokušaj ponovo s jednom drugom vrstom datoteke.
newtab-wallpaper-light-red-panda = Crvena panda
newtab-wallpaper-light-mountain = Bijela planina
newtab-wallpaper-light-sky = Nebo s ljubičastim i ružičastim oblacima
newtab-wallpaper-light-color = Plavi, ružičasti i žuti oblici
newtab-wallpaper-light-landscape = Planinski pejzaž plave magle
newtab-wallpaper-light-beach = Plaža s palmama
newtab-wallpaper-dark-aurora = Polarna svjetlost
newtab-wallpaper-dark-color = Crveni i plavi oblici
newtab-wallpaper-dark-panda = Crvena panda skrivena u šumi
newtab-wallpaper-dark-sky = Gradski pejzaž s noćnim nebom
newtab-wallpaper-dark-mountain = Planinski krajolik
newtab-wallpaper-dark-city = Ljubičasti gradski pejzaž
newtab-wallpaper-dark-fox-anniversary = Lisica na kolniku u blizini šume
newtab-wallpaper-light-fox-anniversary = Lisica u travnatom polju s maglovitim planinskim krajolikom

## Solid Colors

newtab-wallpaper-category-title-colors = Jednobojne
newtab-wallpaper-blue = Plava
newtab-wallpaper-light-blue = Svijetloplava
newtab-wallpaper-light-purple = Svijetloljubičasta
newtab-wallpaper-light-green = Svijetlozelena
newtab-wallpaper-green = Zelena
newtab-wallpaper-beige = Bež
newtab-wallpaper-yellow = Žuta
newtab-wallpaper-orange = Narančasta
newtab-wallpaper-pink = Ružičasta
newtab-wallpaper-light-pink = Svijetloružičasta
newtab-wallpaper-red = Crvena
newtab-wallpaper-dark-blue = Tamnoplava
newtab-wallpaper-dark-purple = Tamnoljubičasta
newtab-wallpaper-dark-green = Tamnozelena
newtab-wallpaper-brown = Smeđa

## Abstract

newtab-wallpaper-category-title-abstract = Abstraktno
newtab-wallpaper-abstract-green = Zeleni oblici
newtab-wallpaper-abstract-blue = Plavi oblici
newtab-wallpaper-abstract-purple = Ljubičasti oblici
newtab-wallpaper-abstract-orange = Narančasti oblici
newtab-wallpaper-gradient-orange = Gradijent narančaste i ružičaste
newtab-wallpaper-abstract-blue-purple = Plavi i ljubičasti oblici

## Celestial

newtab-wallpaper-category-title-photographs = Fotografije
newtab-wallpaper-beach-at-sunrise = Plaža pri izlasku sunca
newtab-wallpaper-beach-at-sunset = Plaža pri zalasku sunca
newtab-wallpaper-storm-sky = Olujno nebo
newtab-wallpaper-sky-with-pink-clouds = Nebo s ružičastim oblacima
newtab-wallpaper-red-panda-yawns-in-a-tree = Crvena panda zijeva na drvetu
newtab-wallpaper-white-mountains = Bijele planine
newtab-wallpaper-forest-trees = Drveće prekriveno maglom
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Autor fotografije <a data-l10n-name="name-link">{ $author_string }</a> na <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Pokušajte s malo boje
newtab-wallpaper-feature-highlight-content = Dajte svojoj novoj kartici svjež izgled pomoću pozadinskih slika.
newtab-wallpaper-feature-highlight-button = Razumijem
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Odbaci
    .aria-label = Zatvori skočni prozor
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

newtab-wallpaper-celestial-lunar-eclipse = Pomrčina mjeseca
newtab-wallpaper-celestial-starry-sky = Zvjezdano nebo

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Pogledajte prognozu u { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponzorirano
newtab-weather-menu-change-location = Promijeni mjesto
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Traži mjesto
    .aria-label = Traži mjesto
newtab-weather-change-location-search-input = Traži mjesto
newtab-weather-menu-weather-display = Prikaz vremena
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Jednostavno
newtab-weather-menu-change-weather-display-simple = Prebaci se na jednostavan prikaz
newtab-weather-menu-weather-display-option-detailed = Detaljano
newtab-weather-menu-change-weather-display-detailed = Prebaci na detaljni prikaz
newtab-weather-menu-temperature-units = Jedinice za temperaturu
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celzija
newtab-weather-menu-change-temperature-units-fahrenheit = Prebaci na Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Prebaci na Celzijeve stupnjeve
newtab-weather-menu-hide-weather = Sakrij vremensku prognozu na novoj kartici
newtab-weather-menu-learn-more = Saznaj više
# This message is shown if user is working offline
newtab-weather-error-not-available = Podaci o vremenu trenutačno nisu dostupni.

## Topic Labels

newtab-topic-label-business = Posao
newtab-topic-label-career = Karijera
newtab-topic-label-education = Obrazovanje
newtab-topic-label-arts = Zabava
newtab-topic-label-food = Hrana
newtab-topic-label-health = Zdravlje
newtab-topic-label-hobbies = Igranje
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Novac
newtab-topic-label-society-parenting = Odgoj
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Znanost
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Životni savjeti
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Tehnologija
newtab-topic-label-travel = Putovanja
newtab-topic-label-home = Dom i vrt

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Odaberi teme prilagođavanje tvog feeda
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Odaberi dvije ili više tema. Naši stručni suradnici prioriziraju priče prema tvojim interesima. Aktualiziraj bilo kada.
newtab-topic-selection-save-button = Spremi
newtab-topic-selection-cancel-button = Odustani
newtab-topic-selection-button-maybe-later = Možda kasnije
newtab-topic-selection-privacy-link = Saznaj kako štitimo i upravljamo podacima
newtab-topic-selection-button-update-interests = Aktualiziraj tvoje interese
newtab-topic-selection-button-pick-interests = Odaberi tvoje interese

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Prati
newtab-section-following-button = Praćenje
newtab-section-unfollow-button = Prestani pratiti

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokiraj
newtab-section-blocked-button = Blokirano
newtab-section-unblock-button = Odblokiraj

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ne sada
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokiraj temu „{ $topic }”

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Teme
newtab-section-manage-topics-button-v2 =
    .label = Upravljaj temama
newtab-section-mangage-topics-followed-topics = Praćene
newtab-section-mangage-topics-followed-topics-empty-state = Još nisi pratio/la nijednu temu.
newtab-section-mangage-topics-blocked-topics = Blokirane
newtab-section-mangage-topics-blocked-topics-empty-state = Još nisi blokirao/la nijednu temu.
