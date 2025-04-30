# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nowa karta
newtab-settings-button =
    .title = Dostosuj stronę nowej karty
newtab-personalize-settings-icon-label =
    .title = Personalizuj nową kartę
    .aria-label = Ustawienia
newtab-settings-dialog-label =
    .aria-label = Ustawienia
newtab-personalize-icon-label =
    .title = Personalizuj nową kartę
    .aria-label = Personalizuj nową kartę
newtab-personalize-dialog-label =
    .aria-label = Personalizuj
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Szukaj
    .aria-label = Szukaj
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Wprowadź adres lub szukaj w { $engine }
newtab-search-box-handoff-text-no-engine = Wprowadź adres lub szukaj
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Wprowadź adres lub szukaj w { $engine }
    .title = Wprowadź adres lub szukaj w { $engine }
    .aria-label = Wprowadź adres lub szukaj w { $engine }
newtab-search-box-handoff-input-no-engine =
    .placeholder = Wprowadź adres lub szukaj
    .title = Wprowadź adres lub szukaj
    .aria-label = Wprowadź adres lub szukaj
newtab-search-box-text = Szukaj w Internecie
newtab-search-box-input =
    .placeholder = Szukaj w Internecie
    .aria-label = Szukaj w Internecie

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Dodaj wyszukiwarkę
newtab-topsites-add-shortcut-header = Nowy skrót
newtab-topsites-edit-topsites-header = Edycja strony z sekcji Popularne
newtab-topsites-edit-shortcut-header = Edycja skrótu
newtab-topsites-add-shortcut-label = Dodaj skrót
newtab-topsites-title-label = Tytuł
newtab-topsites-title-input =
    .placeholder = Wpisz tytuł
newtab-topsites-url-label = Adres URL
newtab-topsites-url-input =
    .placeholder = Wpisz lub wklej adres
newtab-topsites-url-validation = Wymagany jest prawidłowy adres URL
newtab-topsites-image-url-label = Własny obraz
newtab-topsites-use-image-link = Użyj własnego obrazu…
newtab-topsites-image-validation = Wczytanie obrazu się nie powiodło. Spróbuj innego adresu.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Anuluj
newtab-topsites-delete-history-button = Usuń z historii
newtab-topsites-save-button = Zachowaj
newtab-topsites-preview-button = Podgląd
newtab-topsites-add-button = Dodaj

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Czy na pewno usunąć wszystkie wizyty na tej stronie z historii?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tej czynności nie można cofnąć.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsorowane

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Otwórz menu
    .aria-label = Otwórz menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Zamknij
    .aria-label = Zamknij
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Otwórz menu
    .aria-label = Otwórz menu kontekstowe „{ $title }”
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Edytuj stronę
    .aria-label = Edytuj stronę

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edytuj
newtab-menu-open-new-window = Otwórz w nowym oknie
newtab-menu-open-new-private-window = Otwórz w nowym oknie prywatnym
newtab-menu-dismiss = Usuń z tej sekcji
newtab-menu-pin = Przypnij
newtab-menu-unpin = Odepnij
newtab-menu-delete-history = Usuń z historii
newtab-menu-save-to-pocket = Wyślij do { -pocket-brand-name }
newtab-menu-delete-pocket = Usuń z { -pocket-brand-name }
newtab-menu-archive-pocket = Archiwizuj w { -pocket-brand-name }
newtab-menu-show-privacy-info = Nasi sponsorzy i Twoja prywatność
newtab-menu-about-fakespot = Informacje o { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokuj
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Przestań obserwować temat

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Zarządzaj sponsorowanymi treściami
newtab-menu-our-sponsors-and-your-privacy = Nasi sponsorzy i Twoja prywatność

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = OK
newtab-privacy-modal-button-manage = Zarządzaj ustawieniami treści sponsorowanych
newtab-privacy-modal-header = Twoja prywatność jest ważna.
newtab-privacy-modal-paragraph-2 =
    Oprócz ciekawych artykułów pokazujemy Ci również spersonalizowane,
    zweryfikowane treści od wybranych sponsorów. Zachowaj pewność, że
    <strong>Twoja historia przeglądania nigdy nie opuszcza Twojej własnej kopii { -brand-product-name(case: "gen") }</strong> — my jej nie widzimy, i nasi sponsorzy też nie.
newtab-privacy-modal-link = Więcej informacji o prywatności na stronie nowej karty

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Usuń zakładkę
# Bookmark is a verb here.
newtab-menu-bookmark = Dodaj zakładkę

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopiuj adres, z którego pobrano plik
newtab-menu-go-to-download-page = Przejdź do strony pobierania
newtab-menu-remove-download = Usuń z historii

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Pokaż w Finderze
       *[other] Otwórz folder nadrzędny
    }
newtab-menu-open-file = Otwórz plik

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Z odwiedzonych
newtab-label-bookmarked = Z zakładek
newtab-label-removed-bookmark = Usunięto zakładkę
newtab-label-recommended = Na czasie
newtab-label-saved = Z { -pocket-brand-name }
newtab-label-download = Z pobranych
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsorowane
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsor: { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Usuń sekcję
newtab-section-menu-collapse-section = Zwiń sekcję
newtab-section-menu-expand-section = Rozwiń sekcję
newtab-section-menu-manage-section = Zarządzaj sekcją
newtab-section-menu-manage-webext = Zarządzaj rozszerzeniem
newtab-section-menu-add-topsite = Dodaj stronę do popularnych
newtab-section-menu-add-search-engine = Dodaj wyszukiwarkę
newtab-section-menu-move-up = Przesuń w górę
newtab-section-menu-move-down = Przesuń w dół
newtab-section-menu-privacy-notice = Prywatność

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Zwiń sekcję
newtab-section-expand-section-label =
    .aria-label = Rozwiń sekcję

## Section Headers.

newtab-section-header-topsites = Popularne
newtab-section-header-recent-activity = Ostatnia aktywność
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Polecane przez { $provider }
newtab-section-header-stories = Artykuły skłaniające do myślenia
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dzisiejsze artykuły dla Ciebie

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Zacznij przeglądać Internet, a pojawią się tutaj świetne artykuły, filmy oraz inne ostatnio odwiedzane strony i dodane zakładki.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = To na razie wszystko. { $provider } później będzie mieć więcej popularnych artykułów. Nie możesz się doczekać? Wybierz popularny temat, aby znaleźć więcej artykułów z całego Internetu.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = To na razie wszystko. Później będzie tu więcej artykułów. Nie możesz się doczekać? Wybierz popularny temat, aby znaleźć więcej artykułów z całego Internetu.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Jesteś na bieżąco!
newtab-discovery-empty-section-topstories-content = Wróć później po więcej artykułów.
newtab-discovery-empty-section-topstories-try-again-button = Spróbuj ponownie
newtab-discovery-empty-section-topstories-loading = Wczytywanie…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Prawie udało się wczytać tę sekcję, ale nie do końca.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Popularne tematy:
newtab-pocket-new-topics-title = Chcesz przeczytać jeszcze więcej artykułów? Zobacz, co { -pocket-brand-name } proponuje na te popularne tematy
newtab-pocket-more-recommendations = Więcej polecanych
newtab-pocket-learn-more = Więcej informacji
newtab-pocket-cta-button = Pobierz { -pocket-brand-name }
newtab-pocket-cta-text = Zachowuj artykuły w { -pocket-brand-name }, aby wrócić później do ich lektury.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } jest częścią rodziny { -brand-product-name(case: "gen") }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Wyślij
newtab-pocket-saved = Wysłano

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Więcej takich jak to
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nie dla mnie
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Dziękujemy. Twoja opinia pomoże nam ulepszyć treści dla Ciebie.
newtab-toast-dismiss-button =
    .title = Zamknij
    .aria-label = Zamknij

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Odkrywaj to, co najlepsze w sieci
newtab-pocket-onboarding-cta = { -pocket-brand-name } przeszukuje różnorodne publikacje, aby dostarczać najbardziej bogate w informacje, inspirujące i wiarygodne treści prosto do Twojej przeglądarki { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Coś się nie powiodło podczas wczytywania tej treści
newtab-error-fallback-refresh-link = Odśwież stronę, by spróbować ponownie

## Customization Menu

newtab-custom-shortcuts-title = Skróty
newtab-custom-shortcuts-subtitle = Zachowane i odwiedzane strony.
newtab-custom-shortcuts-toggle =
    .label = Skróty
    .description = Zachowane i odwiedzane strony.
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } wiersz
        [few] { $num } wiersze
       *[many] { $num } wierszy
    }
newtab-custom-sponsored-sites = Sponsorowane skróty
newtab-custom-pocket-title = Polecane przez { -pocket-brand-name }
newtab-custom-pocket-subtitle = Wyjątkowe rzeczy wybrane przez { -pocket-brand-name }, część rodziny { -brand-product-name(case: "gen") }.
newtab-custom-stories-toggle =
    .label = Polecane artykuły
    .description = Wyjątkowe rzeczy wybrane przez rodzinę { -brand-product-name(case: "gen") }
newtab-custom-pocket-sponsored = Sponsorowane artykuły
newtab-custom-pocket-show-recent-saves = Wyświetl ostatnio zapisane
newtab-custom-recent-title = Ostatnia aktywność
newtab-custom-recent-subtitle = Wybierane z ostatnio odwiedzanych stron i treści.
newtab-custom-recent-toggle =
    .label = Ostatnia aktywność
    .description = Wybierane z ostatnio odwiedzanych stron i treści.
newtab-custom-weather-toggle =
    .label = Pogoda
    .description = Dzisiejsza prognoza w skrócie
newtab-custom-close-button = Zamknij
newtab-custom-settings = Więcej ustawień

## New Tab Wallpapers

newtab-wallpaper-title = Tapety
newtab-wallpaper-reset = Przywróć domyślne
newtab-wallpaper-upload-image = Dodaj obraz
newtab-wallpaper-custom-color = Wybierz kolor
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Obraz przekracza ograniczenie rozmiaru pliku wynoszące { $file_size } MB. Spróbuj dodać mniejszy plik.
newtab-wallpaper-error-file-type = Nie udało się dodać tego pliku. Spróbuj ponownie z innym typem pliku.
newtab-wallpaper-light-red-panda = Pandka ruda
newtab-wallpaper-light-mountain = Biała góra
newtab-wallpaper-light-sky = Niebo z fioletowymi i różowymi chmurami
newtab-wallpaper-light-color = Niebieskie, różowe i żółte kształty
newtab-wallpaper-light-landscape = Górski pejzaż z niebieską mgłą
newtab-wallpaper-light-beach = Plaża z palmą
newtab-wallpaper-dark-aurora = Zorza polarna
newtab-wallpaper-dark-color = Czerwone i niebieskie kształty
newtab-wallpaper-dark-panda = Pandka ruda schowana w lesie
newtab-wallpaper-dark-sky = Miejski pejzaż z nocnym niebem
newtab-wallpaper-dark-mountain = Górski pejzaż
newtab-wallpaper-dark-city = Fioletowy miejski pejzaż
newtab-wallpaper-dark-fox-anniversary = Lis na chodniku w pobliżu lasu
newtab-wallpaper-light-fox-anniversary = Lis na łące na tle mglistych gór

## Solid Colors

newtab-wallpaper-category-title-colors = Jednolite kolory
newtab-wallpaper-blue = Niebieski
newtab-wallpaper-light-blue = Jasnoniebieski
newtab-wallpaper-light-purple = Jasnofioletowy
newtab-wallpaper-light-green = Jasnozielony
newtab-wallpaper-green = Zielony
newtab-wallpaper-beige = Beżowy
newtab-wallpaper-yellow = Żółty
newtab-wallpaper-orange = Pomarańczowy
newtab-wallpaper-pink = Różowy
newtab-wallpaper-light-pink = Jasnoróżowy
newtab-wallpaper-red = Czerwony
newtab-wallpaper-dark-blue = Ciemnoniebieski
newtab-wallpaper-dark-purple = Ciemnofioletowy
newtab-wallpaper-dark-green = Ciemnoniebieski
newtab-wallpaper-brown = Brązowy

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakcyjne
newtab-wallpaper-abstract-green = Zielone kształty
newtab-wallpaper-abstract-blue = Niebieskie kształty
newtab-wallpaper-abstract-purple = Fioletowe kształty
newtab-wallpaper-abstract-orange = Pomarańczowe kształty
newtab-wallpaper-gradient-orange = Przejście między pomarańczowym a różowym
newtab-wallpaper-abstract-blue-purple = Niebieskie i fioletowe kształty
newtab-wallpaper-abstract-white-curves = Biały z cieniowanymi łukami
newtab-wallpaper-abstract-purple-green = Gradient fioletowego i zielonego światła
newtab-wallpaper-abstract-blue-purple-waves = Niebieskie i fioletowe faliste kształty
newtab-wallpaper-abstract-black-waves = Czarne faliste kształty

## Celestial

newtab-wallpaper-category-title-photographs = Zdjęcia
newtab-wallpaper-beach-at-sunrise = Plaża o wschodzie słońca
newtab-wallpaper-beach-at-sunset = Plaża o zachodzie słońca
newtab-wallpaper-storm-sky = Burzowe niebo
newtab-wallpaper-sky-with-pink-clouds = Niebo z różowymi chmurami
newtab-wallpaper-red-panda-yawns-in-a-tree = Pandka ruda ziewa na drzewie
newtab-wallpaper-white-mountains = Białe góry
newtab-wallpaper-hot-air-balloons = Różnorodne kolory balonów na ogrzane powietrze w ciągu dnia
newtab-wallpaper-starry-canyon = Niebieska gwiaździsta noc
newtab-wallpaper-suspension-bridge = Szary most wiszący sfotografowany w ciągu dnia
newtab-wallpaper-sand-dunes = Białe wydmy
newtab-wallpaper-palm-trees = Sylwetka palm kokosowych przed zachodem słońca
newtab-wallpaper-blue-flowers = Zbliżenie na kwitnące niebieskie kwiatki
newtab-wallpaper-forest-trees = Drzewa pokryte mgłą
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Zdjęcie: <a data-l10n-name="name-link">{ $author_string }</a> z witryny <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Wypróbuj odrobiny koloru
newtab-wallpaper-feature-highlight-content = Nadaj nowej karcie świeży wygląd dzięki tapetom.
newtab-wallpaper-feature-highlight-button = OK
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Zamknij
    .aria-label = Zamknij tę funkcję
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Astronomiczne
newtab-wallpaper-celestial-lunar-eclipse = Zaćmienie Księżyca
newtab-wallpaper-celestial-earth-night = Zdjęcie nocne z niskiej orbity okołoziemskiej
newtab-wallpaper-celestial-starry-sky = Gwiaździste niebo
newtab-wallpaper-celestial-eclipse-time-lapse = Poklatkowe zaćmienie Księżyca
newtab-wallpaper-celestial-black-hole = Ilustracja galaktyki z czarną dziurą
newtab-wallpaper-celestial-river = Zdjęcie satelitarne rzeki

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Zobacz prognozę na witrynie { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsorowane
newtab-weather-menu-change-location = Zmień położenie
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Wyszukaj położenie
    .aria-label = Wyszukaj położenie
newtab-weather-change-location-search-input = Wyszukaj położenie
newtab-weather-menu-weather-display = Wyświetlanie pogody
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Proste
newtab-weather-menu-change-weather-display-simple = Przełącz na prosty widok
newtab-weather-menu-weather-display-option-detailed = Szczegółowe
newtab-weather-menu-change-weather-display-detailed = Przełącz na szczegółowy widok
newtab-weather-menu-temperature-units = Jednostka temperatury
newtab-weather-menu-temperature-option-fahrenheit = Stopnie Fahrenheita
newtab-weather-menu-temperature-option-celsius = Stopnie Celsjusza
newtab-weather-menu-change-temperature-units-fahrenheit = Przełącz na stopnie Fahrenheita
newtab-weather-menu-change-temperature-units-celsius = Przełącz na stopnie Celsjusza
newtab-weather-menu-hide-weather = Ukryj pogodę na stronie nowej karty
newtab-weather-menu-learn-more = Więcej informacji
# This message is shown if user is working offline
newtab-weather-error-not-available = Informacje o pogodzie nie są w tej chwili dostępne.

## Topic Labels

newtab-topic-label-business = Biznes
newtab-topic-label-career = Praca
newtab-topic-label-education = Edukacja
newtab-topic-label-arts = Rozrywka
newtab-topic-label-food = Jedzenie
newtab-topic-label-health = Zdrowie
newtab-topic-label-hobbies = Gry
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Finanse
newtab-topic-label-society-parenting = Rodzicielstwo
newtab-topic-label-government = Polityka
newtab-topic-label-education-science = Nauka
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Porady
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Technologia
newtab-topic-label-travel = Podróże
newtab-topic-label-home = Dom i ogród

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Dostosuj treści dla siebie, wybierając tematy
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Wybierz minimum dwa tematy. Nasi eksperci wybierają artykuły pasujące do Twoich zainteresowań. Swój wybór możesz zmienić w dowolnej chwili.
newtab-topic-selection-save-button = Zachowaj
newtab-topic-selection-cancel-button = Anuluj
newtab-topic-selection-button-maybe-later = Może później
newtab-topic-selection-privacy-link = Dowiedz się, jak chronimy i zarządzamy danymi
newtab-topic-selection-button-update-interests = Zaktualizuj swoje zainteresowania
newtab-topic-selection-button-pick-interests = Wybierz swoje zainteresowania

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Obserwuj
newtab-section-following-button = Obserwowane
newtab-section-unfollow-button = Przestań obserwować

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Zablokuj
newtab-section-blocked-button = Zablokowano
newtab-section-unblock-button = Odblokuj

## Confirmation modal for blocking a section

newtab-section-cancel-button = Nie teraz
newtab-section-confirm-block-topic-p1 = Czy na pewno zablokować ten temat?
newtab-section-confirm-block-topic-p2 = Zablokowane tematy nie będą już wyświetlane.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Zablokuj temat „{ $topic }”

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Tematy
newtab-section-manage-topics-button-v2 =
    .label = Zarządzaj tematami
newtab-section-mangage-topics-followed-topics = Obserwowane
newtab-section-mangage-topics-followed-topics-empty-state = Żadne tematy nie są jeszcze obserwowane.
newtab-section-mangage-topics-blocked-topics = Zablokowane
newtab-section-mangage-topics-blocked-topics-empty-state = Żadne tematy nie są jeszcze zablokowane.
