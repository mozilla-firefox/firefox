# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nowy rejtarik
newtab-settings-button =
    .title = Bok wašogo nowego rejtarika pśiměriś
newtab-personalize-settings-icon-label =
    .title = Nowy rejtarik personalizěrowaś
    .aria-label = Nastajenja
newtab-settings-dialog-label =
    .aria-label = Nastajenja
newtab-personalize-icon-label =
    .title = Nowy rejtarik personalizěrowaś
    .aria-label = Nowy rejtarik personalizěrowaś
newtab-personalize-dialog-label =
    .aria-label = Personalizěrowaś
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Pytaś
    .aria-label = Pytaś
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Pytajśo z { $engine } abo zapódajśo adresu
newtab-search-box-handoff-text-no-engine = Pytaś abo adresu zapódaś
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Pytajśo z { $engine } abo zapódajśo adresu
    .title = Pytajśo z { $engine } abo zapódajśo adresu
    .aria-label = Pytajśo z { $engine } abo zapódajśo adresu
newtab-search-box-handoff-input-no-engine =
    .placeholder = Pytaś abo adresu zapódaś
    .title = Pytaś abo adresu zapódaś
    .aria-label = Pytaś abo adresu zapódaś
newtab-search-box-text = Web pśepytaś
newtab-search-box-input =
    .placeholder = Web pśepytaś
    .aria-label = Web pśepytaś

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Pytnicu pśidaś
newtab-topsites-add-shortcut-header = Nowe zwězanje
newtab-topsites-edit-topsites-header = Nejcesćej woglědane sedło wobźěłaś
newtab-topsites-edit-shortcut-header = Zwězanje wobźěłaś
newtab-topsites-add-shortcut-label = Skrotconku pśidaś
newtab-topsites-title-label = Titel
newtab-topsites-title-input =
    .placeholder = Titel zapódaś
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL zapódaś abo zasajźiś
newtab-topsites-url-validation = Płaśiwy URL trjebny
newtab-topsites-image-url-label = URL swójskego wobraza
newtab-topsites-use-image-link = Swójski wobraz wužywaś…
newtab-topsites-image-validation = Wobraz njedajo se zacytaś. Wopytajśo drugi URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Pśetergnuś
newtab-topsites-delete-history-button = Z historije lašowaś
newtab-topsites-save-button = Składowaś
newtab-topsites-preview-button = Pśeglěd
newtab-topsites-add-button = Pśidaś

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Cośo napšawdu kuždu instancu toś togo boka ze swójeje historije lašowaś?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Toś ta akcija njedajo se anulěrowaś.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponserowany

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Meni wócyniś
    .aria-label = Meni wócyniś
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Wótwónoźeś
    .aria-label = Wótwónoźeś
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Meni wócyniś
    .aria-label = Kontekstowy meni za { $title } wócyniś
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Toś to sedło wobźěłaś
    .aria-label = Toś to sedło wobźěłaś

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Wobźěłaś
newtab-menu-open-new-window = W nowem woknje wócyniś
newtab-menu-open-new-private-window = W nowem priwatnem woknje wócyniś
newtab-menu-dismiss = Zachyśiś
newtab-menu-pin = Pśipěś
newtab-menu-unpin = Wótpěś
newtab-menu-delete-history = Z historije lašowaś
newtab-menu-save-to-pocket = Pla { -pocket-brand-name } składowaś
newtab-menu-delete-pocket = Z { -pocket-brand-name } wulašowaś
newtab-menu-archive-pocket = W { -pocket-brand-name } archiwěrowaś
newtab-menu-show-privacy-info = Naše sponsory a waša priwatnosć
newtab-menu-about-fakespot = Wó { -fakespot-brand-name }
newtab-menu-report-content = Toś to wopśimjeśe k wěsći daś
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokěrowaś
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Temje wěcej njeslědowaś

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Sponsorowane wopśimjeśe zastojaś
newtab-menu-our-sponsors-and-your-privacy = Naše sponsory a waša priwatnosć
newtab-menu-report-this-ad = Toś to wabjenje k wěsći daś

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Dokóńcone
newtab-privacy-modal-button-manage = Nastajenja sponserowanego wopśimjeśa zastojaś
newtab-privacy-modal-header = Waša priwatnosć jo wažna
newtab-privacy-modal-paragraph-2 =
    Pśidatnje k našwicanjeju pśejmajucych tšojenjow, pokazujomy wam teke relewantny, 
    wjelgin pśeglědane wopśimjeśe wót wubranych sponsorow. Buźćo wěsty, <strong>waše pśeglědowańske 
    daty wašu wósobinsku wersiju { -brand-product-name } nigda njespušća</strong> ­­- njewiźimy je, a naše 
    sponsory teke nic.
newtab-privacy-modal-link = Zgóńśo, kak priwatnosć w nowem rejtariku funkcioněrujo

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Cytańske znamje wótpóraś
# Bookmark is a verb here.
newtab-menu-bookmark = Ako cytańske znamje składowaś

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Ześěgnjeński wótkaz kopěrowaś
newtab-menu-go-to-download-page = K ześěgnjeńskemu bokoju pśejś
newtab-menu-remove-download = Z historije wótwónoźeś

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] W Finder pokazaś
       *[other] Wopśimujucy zarědnik wócyniś
    }
newtab-menu-open-file = Dataju wócyniś

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Woglědany
newtab-label-bookmarked = Ako cytańske znamje skłaźony
newtab-label-removed-bookmark = Cytańske znamje jo wótwónoźone
newtab-label-recommended = Popularny
newtab-label-saved = Do { -pocket-brand-name } skłaźony
newtab-label-download = Ześěgnjony
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } - sponserowane
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponserowany wót { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Wótrězk wótwónoźeś
newtab-section-menu-collapse-section = Wótrězk schowaś
newtab-section-menu-expand-section = Wótrězk pokazaś
newtab-section-menu-manage-section = Wótrězk zastojaś
newtab-section-menu-manage-webext = Rozšyrjenje zastojaś
newtab-section-menu-add-topsite = Woblubowane sedło pśidaś
newtab-section-menu-add-search-engine = Pytnicu pśidaś
newtab-section-menu-move-up = Górjej
newtab-section-menu-move-down = Dołoj
newtab-section-menu-privacy-notice = Powěźeńka priwatnosći

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Wótrězk schowaś
newtab-section-expand-section-label =
    .aria-label = Wótrězk pokazaś

## Section Headers.

newtab-section-header-topsites = Nejcesćej woglědane sedła
newtab-section-header-recent-activity = Nejnowša aktiwita
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Wót { $provider } dopórucony
newtab-section-header-stories = Tšojeńka, kótarež k rozmyslowanju pógnuwaju
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Źinsajšne pśirucenja za was

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Zachopśo pśeglědowaś, a pokažomy někotare wjelicne nastawki, wideo a druge boki, kótarež sćo se njedawno woglědał abo how ako cytańske znamjenja składował.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = To jo nachylu wšykno. Wrośćo se pózdźej wjelicnych tšojeńkow dla wót { $provider }. Njamóžośo cakaś? Wubjeŕśo woblubowanu temu, aby dalšne wjelicne tšojeńka we webje namakał.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = To jo nachylu wšykno. Wrośćo se pózdźej tšojeńkow dla. Njamóžośo cakaś? Wubjeŕśo woblubowanu temu, aby dalšne wjelicne tšojeńka we webje namakał.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Sćo dogónjony!
newtab-discovery-empty-section-topstories-content = Glědajśo póozdźej za wěcej tšojenjami.
newtab-discovery-empty-section-topstories-try-again-button = Hyšći raz wopytaś
newtab-discovery-empty-section-topstories-loading = Zacytujo se…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hopla! Smy womało zacytali toś ten wótrězk, ale nic cele.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Woblubowane temy:
newtab-pocket-new-topics-title = Cośo dalšne tšojeńka? Woglědajśo se toś te woblubowane temy z { -pocket-brand-name }
newtab-pocket-more-recommendations = Dalšne pórucenja
newtab-pocket-learn-more = Dalšne informacije
newtab-pocket-cta-button = { -pocket-brand-name } wobstaraś
newtab-pocket-cta-text = Składujśo tšojeńka, kótarež se wam spódobuju, w { -pocket-brand-name } a žywśo swój duch z fasciněrujucymi cytańkami.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } jo źěl swójźby { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Składowaś
newtab-pocket-saved = Skłaźony

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Wěcej ako ta
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nic za mnjo
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Źěkujomy se. Wašo měnjenje buźo nam pomagaś, waš kanal pólěpšyś.
newtab-toast-dismiss-button =
    .title = Zachyśiś
    .aria-label = Zachyśiś

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Wuslěźćo nejlěpše interneta
newtab-pocket-onboarding-cta = { -pocket-brand-name } šyroku paletu publikacijow pśeslěźujo, aby nejwěcej informatiwne, inspirěrujuace a dowěry gódne wopśimjeśe direktnje do wašogo wobglědowaka { -brand-product-name } donjasł.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Hopla, pśi cytanju toś togo wopśimjeśa njejo se něco raźiło.
newtab-error-fallback-refresh-link = Aktualizěrujśo bok, aby hyšći raz wopytał.

## Customization Menu

newtab-custom-shortcuts-title = Zwězanja
newtab-custom-shortcuts-subtitle = Sedła, kótarež składujośo abo ku kótarymž se woglědujośo
newtab-custom-shortcuts-toggle =
    .label = Zwězanja
    .description = Sedła, kótarež składujośo abo ku kótarymž se woglědujośo
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } smužka
        [two] { $num } smužce
        [few] { $num } smužki
       *[other] { $num } smužkow
    }
newtab-custom-sponsored-sites = Sponserowane zwězanja
newtab-custom-pocket-title = Wót { -pocket-brand-name } dopórucone
newtab-custom-pocket-subtitle = Wósebne wopśimjeśe, wubrane pśez { -pocket-brand-name }, źěla swójźby { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Dopórucone tšojeńka
    .description = Wuwześowe wopśimjeśe, kótarež se pśez swójźbu { -brand-product-name } wótwardujo
newtab-custom-pocket-sponsored = Sponserowane tšojeńka
newtab-custom-pocket-show-recent-saves = Nejnowše składowanja pokazaś
newtab-custom-recent-title = Nejnowša aktiwita
newtab-custom-recent-subtitle = Wuběrk nejnowšych sedłow a nejnowšego wopśimjeśa
newtab-custom-recent-toggle =
    .label = Nejnowša aktiwita
    .description = Wuběrk nejnowšych sedłow a nejnowšego wopśimjeśa
newtab-custom-weather-toggle =
    .label = Wjedro
    .description = Źinsajšna wjedrowa pśedpowěsć
newtab-custom-close-button = Zacyniś
newtab-custom-settings = Dalšne nastajenja zastojaś

## New Tab Wallpapers

newtab-wallpaper-title = Slězynowe wobraze
newtab-wallpaper-reset = Na standard slědk stajiś
newtab-wallpaper-upload-image = Wobraz nagraś
newtab-wallpaper-custom-color = Barwu wubraś
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Wobraz jo pśekšocył limit datajoweje wjelikosći { $file_size }. Nagrajśo pšosym mjeńšu dataju.
newtab-wallpaper-error-file-type = Njejsmy mógli wašu dataju nagraś. Wopytajśo pšosym z drugim datajowym typom hyšći raz.
newtab-wallpaper-light-red-panda = Cerwjeny panda
newtab-wallpaper-light-mountain = Běła góra
newtab-wallpaper-light-sky = Njebjo z wioletnymi a rožowymi mrokawami
newtab-wallpaper-light-color = Módre, rožowe a žołte formy
newtab-wallpaper-light-landscape = Módra kurjawkata górinowa krajina
newtab-wallpaper-light-beach = Pśibrjog z palmu
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Cerwjene a módre formy
newtab-wallpaper-dark-panda = Cerwjeny panda w lěsu schowany
newtab-wallpaper-dark-sky = Měsćańska krajina z nocnym njebjom
newtab-wallpaper-dark-mountain = Górinowa krajina
newtab-wallpaper-dark-city = Wioletna měsćańska krajina
newtab-wallpaper-dark-fox-anniversary = Liška na flastarju blisko lěsa
newtab-wallpaper-light-fox-anniversary = Liška w tšawowem pólu z kurjawkateju górinoweju krajinu

## Solid Colors

newtab-wallpaper-category-title-colors = Jadnotne barwy
newtab-wallpaper-blue = Módry
newtab-wallpaper-light-blue = Swětłomódry
newtab-wallpaper-light-purple = Swětłowioletny
newtab-wallpaper-light-green = Swětłozeleny
newtab-wallpaper-green = Zeleny
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Žołty
newtab-wallpaper-orange = Oranžowy
newtab-wallpaper-pink = Pink
newtab-wallpaper-light-pink = Swětłopink
newtab-wallpaper-red = Cerwjeny
newtab-wallpaper-dark-blue = Śamnomódry
newtab-wallpaper-dark-purple = Śamnowioletny
newtab-wallpaper-dark-green = Śamnozeleny
newtab-wallpaper-brown = Bruny

## Abstract

newtab-wallpaper-category-title-abstract = Abstraktne
newtab-wallpaper-abstract-green = Zelene formy
newtab-wallpaper-abstract-blue = Módre formy
newtab-wallpaper-abstract-purple = Wioletne formy
newtab-wallpaper-abstract-orange = Oranžowe formy
newtab-wallpaper-gradient-orange = Woběžk oranžowy a pink
newtab-wallpaper-abstract-blue-purple = Módre a wioletne formy
newtab-wallpaper-abstract-white-curves = Běły z wósenjonymi wukulowaśenjami
newtab-wallpaper-abstract-purple-green = Wioletny a zeleny swětłowy pśeběg
newtab-wallpaper-abstract-blue-purple-waves = Módre a wioletne žwałkate formy
newtab-wallpaper-abstract-black-waves = Carne žwałkate formy

## Celestial

newtab-wallpaper-category-title-photographs = Fota
newtab-wallpaper-beach-at-sunrise = Pśibrjog pśi zejźenju słyńca
newtab-wallpaper-beach-at-sunset = Pśibrjog pśi schowanju słyńca
newtab-wallpaper-storm-sky = Wichorowe njebjo
newtab-wallpaper-sky-with-pink-clouds = Njebjo z rožowymi mrokami
newtab-wallpaper-red-panda-yawns-in-a-tree = Cerwjeny panda w bomje zewa
newtab-wallpaper-white-mountains = Běłe góry
newtab-wallpaper-hot-air-balloons = Rozdźělna barwa górucopówětšowych balonow wódnjo
newtab-wallpaper-starry-canyon = Módra gwězdna noc
newtab-wallpaper-suspension-bridge = Šera fotografija wisatego mosta wódnjo
newtab-wallpaper-sand-dunes = Běłe změty pěska
newtab-wallpaper-palm-trees = Silueta bomow kokosowych palmow w złotej góźinje
newtab-wallpaper-blue-flowers = Fotografija kwětkow z módrymi łopjenkami w kwiśenju z bliskosći
newtab-wallpaper-forest-trees = Z kurjawu pókšyte bomy
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto wót <a data-l10n-name="name-link">{ $author_string }</a> na <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Wopytajśo chrapku barwy
newtab-wallpaper-feature-highlight-content = Dajśo swójomu rejtarikoju fryšne wuglědanje ze slězynowymi wobrazami.
newtab-wallpaper-feature-highlight-button = Som zrozměł
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Zachyśiś
    .aria-label = Wóskokujuce wokno zacyniś
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Njebjaski
newtab-wallpaper-celestial-lunar-eclipse = Zajśmiśe mjaseca
newtab-wallpaper-celestial-earth-night = Nocne foto z dolnego orbita zemje
newtab-wallpaper-celestial-starry-sky = Gwězdne njebjo
newtab-wallpaper-celestial-eclipse-time-lapse = Casowy wótběg zajśmiśa mjaseca
newtab-wallpaper-celestial-black-hole = Zwobraznjenje galaksije z carneju źěru
newtab-wallpaper-celestial-river = Satelitowy wobraz rěki

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Wjedrowu pśedpowěsć w { $provider } pokazaś
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ sponserowany
newtab-weather-menu-change-location = Městno změniś
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Městno pytaś
    .aria-label = Městno pytaś
newtab-weather-change-location-search-input = Městno pytaś
newtab-weather-menu-weather-display = Wjedrowe pokazanje
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Jadnory
newtab-weather-menu-change-weather-display-simple = Jadnory naglěd wužywaś
newtab-weather-menu-weather-display-option-detailed = Detailěrowany
newtab-weather-menu-change-weather-display-detailed = Detailěrowany naglěd wužywaś
newtab-weather-menu-temperature-units = Temperaturowe jadnotki
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Fahrenheit wužywaś
newtab-weather-menu-change-temperature-units-celsius = Celsius wužywaś
newtab-weather-menu-hide-weather = Wjedro na nowem rejtariku schowaś
newtab-weather-menu-learn-more = Dalšne informacije
# This message is shown if user is working offline
newtab-weather-error-not-available = Wjedrowe daty njejsu tuchylu k dispoziciji

## Topic Labels

newtab-topic-label-business = Pśekupniske
newtab-topic-label-career = Kariera
newtab-topic-label-education = Kubłanje
newtab-topic-label-arts = Rozdrosćenje
newtab-topic-label-food = Caroba
newtab-topic-label-health = Strowje
newtab-topic-label-hobbies = Graśe
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Pjenjeze
newtab-topic-label-society-parenting = Wótkubłanje
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Wědomnosć
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Swójske pólěpšenja
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Technologija
newtab-topic-label-travel = Drogowanje
newtab-topic-label-home = Dom a zagroda

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Wubjeŕśo temy, aby swój kanal optiměrował
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Wubjeŕśo dwě temje abo wěcej z nich. Naše nazgónjone kuratory prioritu na tšojeńka kładu, kótarež su na waše zajmy wusměrjone. Pśiměŕśo to kuždy cas.
newtab-topic-selection-save-button = Składowaś
newtab-topic-selection-cancel-button = Pśetergnuś
newtab-topic-selection-button-maybe-later = Snaź pózdźej
newtab-topic-selection-privacy-link = Zgóńśo, kak daty šćitamy a zastojmy
newtab-topic-selection-button-update-interests = Zaktualizěrujśo swóje zajmy
newtab-topic-selection-button-pick-interests = Wubjeŕśo swóje zajmy

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Slědowaś
newtab-section-following-button = Slědujucy
newtab-section-unfollow-button = Wěcej njeslědowaś

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokěrowaś
newtab-section-blocked-button = Blokěrowany
newtab-section-unblock-button = Wěcej njeblokěrowaś

## Confirmation modal for blocking a section

newtab-section-cancel-button = Nic něnto
newtab-section-confirm-block-topic-p1 = Cośo napšawdu toś tu temu blokěrowaś?
newtab-section-confirm-block-topic-p2 = Blokěrowane temy se wěcej we wašom kanalu njezjawiju.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = { $topic } blokěrowaś

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Temy
newtab-section-manage-topics-button-v2 =
    .label = Temy zastojaś
newtab-section-mangage-topics-followed-topics = Slědowany
newtab-section-mangage-topics-followed-topics-empty-state = Hyšći žednym temam njeslědujośo.
newtab-section-mangage-topics-blocked-topics = Blokěrowany
newtab-section-mangage-topics-blocked-topics-empty-state = Hyšći njejsćo blokěrował temy.
newtab-custom-wallpaper-title = How su swójske slězynowe wobraze
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Nagrajśo swójski slězynowy wobraz abo wubjeŕśo swójsku barwu, aby se { -brand-product-name } pśiswójł.
newtab-custom-wallpaper-cta = Wopytajśo jen
