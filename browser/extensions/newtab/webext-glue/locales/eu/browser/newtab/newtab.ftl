# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Fitxa berria
newtab-settings-button =
    .title = Pertsonalizatu fitxa berriaren orria
newtab-personalize-settings-icon-label =
    .title = Pertsonalizatu fitxa berria
    .aria-label = Ezarpenak
newtab-settings-dialog-label =
    .aria-label = Ezarpenak
newtab-personalize-icon-label =
    .title = Pertsonalizatu fitxa berria
    .aria-label = Pertsonalizatu fitxa berria
newtab-personalize-dialog-label =
    .aria-label = Pertsonalizatu
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Bilatu
    .aria-label = Bilatu
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Bilatu { $engine } erabiliz edo idatzi helbidea
newtab-search-box-handoff-text-no-engine = Bilatu edo idatzi helbidea
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Bilatu { $engine } erabiliz edo idatzi helbidea
    .title = Bilatu { $engine } erabiliz edo idatzi helbidea
    .aria-label = Bilatu { $engine } erabiliz edo idatzi helbidea
newtab-search-box-handoff-input-no-engine =
    .placeholder = Bilatu edo idatzi helbidea
    .title = Bilatu edo idatzi helbidea
    .aria-label = Bilatu edo idatzi helbidea
newtab-search-box-text = Bilatu webean
newtab-search-box-input =
    .placeholder = Bilatu webean
    .aria-label = Bilatu webean

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Gehitu bilaketa-motorra
newtab-topsites-add-shortcut-header = Lasterbide berria
newtab-topsites-edit-topsites-header = Editatu maiz erabilitako gunea
newtab-topsites-edit-shortcut-header = Editatu lasterbidea
newtab-topsites-add-shortcut-label = Gehitu lasterbidea
newtab-topsites-title-label = Izenburua
newtab-topsites-title-input =
    .placeholder = Idatzi izenburua
newtab-topsites-url-label = URLa
newtab-topsites-url-input =
    .placeholder = Idatzi edo itsatsi URLa
newtab-topsites-url-validation = Baliozko URLa behar da
newtab-topsites-image-url-label = Irudi pertsonalizatuaren URLa
newtab-topsites-use-image-link = Erabili irudi pertsonalizatua…
newtab-topsites-image-validation = Ezin da irudia kargatu. Saiatu beste URL batekin.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Utzi
newtab-topsites-delete-history-button = Ezabatu historiatik
newtab-topsites-save-button = Gorde
newtab-topsites-preview-button = Aurrebista
newtab-topsites-add-button = Gehitu

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Ziur zaude orri honen agerpen guztiak ezabatu nahi dituzula historiatik?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ekintza hau ezin da desegin.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Babesleak hornituta

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Ireki menua
    .aria-label = Ireki menua
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Kendu
    .aria-label = Kendu
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Ireki menua
    .aria-label = Ikusi { $title } gunerako testuinguru-menua
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editatu gune hau
    .aria-label = Editatu gune hau

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editatu
newtab-menu-open-new-window = Ireki leiho berri batean
newtab-menu-open-new-private-window = Ireki leiho pribatu berrian
newtab-menu-dismiss = Baztertu
newtab-menu-pin = Ainguratu
newtab-menu-unpin = Desainguratu
newtab-menu-delete-history = Ezabatu historiatik
newtab-menu-save-to-pocket = Gorde { -pocket-brand-name }-en
newtab-menu-delete-pocket = Ezabatu { -pocket-brand-name }-etik
newtab-menu-archive-pocket = Artxibatu { -pocket-brand-name }-en
newtab-menu-show-privacy-info = Gure babesleak eta zure pribatutasuna
newtab-menu-about-fakespot = { -fakespot-brand-name }(r)i buruz
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokeatu
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Ez jarraitu gaia

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Eginda
newtab-privacy-modal-button-manage = Kudeatu babesleek ordaindutako edukien ezarpenak
newtab-privacy-modal-header = Zure pribatutasuna garrantzitsua da.
newtab-privacy-modal-paragraph-2 =
    Harrapatuko zaituzten istorioak biltzeaz gain, hautatutako babesleek
    hornitutako eta aurrez ikuskatutako eduki esanguratsua ere erakusten dizugu.
    Zaude lasai, <strong>zure nabigatze-datuak inoiz ez dira zure { -brand-product-name }(e)tik irtengo</strong> — ez guk ez eta gure babesleek ez dute halakorik ikusten.
newtab-privacy-modal-link = Ikasi pribatutasuna nola dabilen fitxa berrian

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Kendu laster-marka
# Bookmark is a verb here.
newtab-menu-bookmark = Egin laster-marka

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopiatu deskargaren lotura
newtab-menu-go-to-download-page = Joan deskargaren orrira
newtab-menu-remove-download = Kendu historiatik

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Erakutsi Finder-en
       *[other] Ireki dagoen karpeta
    }
newtab-menu-open-file = Ireki fitxategia

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Bisitatuta
newtab-label-bookmarked = Laster-marka eginda
newtab-label-removed-bookmark = Laster-marka kenduta
newtab-label-recommended = Joerak
newtab-label-saved = { -pocket-brand-name }-en gordeta
newtab-label-download = Deskargatuta
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource }(e)k lagundua
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Babeslea: { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Kendu atala
newtab-section-menu-collapse-section = Tolestu atala
newtab-section-menu-expand-section = Zabaldu atala
newtab-section-menu-manage-section = Kudeatu atala
newtab-section-menu-manage-webext = Kudeatu hedapena
newtab-section-menu-add-topsite = Gehitu maiz erabilitako gunea
newtab-section-menu-add-search-engine = Gehitu bilaketa-motorra
newtab-section-menu-move-up = Eraman gora
newtab-section-menu-move-down = Eraman behera
newtab-section-menu-privacy-notice = Pribatutasun-oharra

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Tolestu atala
newtab-section-expand-section-label =
    .aria-label = Zabaldu atala

## Section Headers.

newtab-section-header-topsites = Gune erabilienak
newtab-section-header-recent-activity = Azken jarduera
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } hornitzaileak gomendatuta
newtab-section-header-stories = Hausnartzeko moduko istorioak

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Hasi nabigatzen eta azkenaldian bisitatutako edo laster-marka egindako aparteko artikulu, bideo eta orriak erakutsiko ditugu.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Egunean zaude jada. Etorri berriro geroago { $provider } hornitzailearen istorio ezagun gehiagorako. Ezin duzu itxaron? Hautatu gai ezagun bat webeko istorio gehiago aurkitzeko.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Egunean zaude!
newtab-discovery-empty-section-topstories-content = Itzuli geroago istorio gehiago aurkitzeko.
newtab-discovery-empty-section-topstories-try-again-button = Saiatu berriro
newtab-discovery-empty-section-topstories-loading = Kargatzen…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Kontxo! Atal hau ia-ia kargatu dugu baina ez erabat.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Gai ezagunak:
newtab-pocket-new-topics-title = Istorio gehiago nahi dituzu? Ikusi { -pocket-brand-name }(e)ko gai ezagun hauek
newtab-pocket-more-recommendations = Gomendio gehiago
newtab-pocket-learn-more = Argibide gehiago
newtab-pocket-cta-button = Eskuratu { -pocket-brand-name }
newtab-pocket-cta-text = Gorde gogoko dituzun istorioak { -pocket-brand-name }-en eta piztu zure gogoa irakurgai erakargarriekin.
newtab-pocket-pocket-firefox-family = { -brand-product-name }(r)en familiakoa da { -pocket-brand-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Gorde
newtab-pocket-saved = Gordeta

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Horrelako gehiago
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ez zait interesatzen
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Eskerrik asko. Zure iritziak jarioa hobetzen lagunduko digu.
newtab-toast-dismiss-button =
    .title = Baztertu
    .aria-label = Baztertu

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Aurkitu webeko onena

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Kontxo, zerbait gaizki joan da edukia kargatzerakoan.
newtab-error-fallback-refresh-link = Berritu orria berriro saiatzeko.

## Customization Menu

newtab-custom-shortcuts-title = Lasterbideak
newtab-custom-shortcuts-subtitle = Gordetzen edo bisitatzen dituzun guneak
newtab-custom-shortcuts-toggle =
    .label = Lasterbideak
    .description = Gordetzen edo bisitatzen dituzun guneak
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] Errenkada bat
       *[other] { $num } errenkada
    }
newtab-custom-sponsored-sites = Babesleen lasterbideak
newtab-custom-pocket-title = { -pocket-brand-name }-ek gomendatua
newtab-custom-pocket-subtitle = { -brand-product-name } familiakide den { -pocket-brand-name }-eko taldeak hautatutako aparteko edukia.
newtab-custom-stories-toggle =
    .label = Gomendatutako istorioak
    .description = { -brand-product-name } familiak aukeratutako eduki aparta
newtab-custom-pocket-sponsored = Babesleen istorioak
newtab-custom-pocket-show-recent-saves = Erakutsi gordetako azkenak
newtab-custom-recent-title = Azken jarduera
newtab-custom-recent-subtitle = Azken gune eta edukien hautapena
newtab-custom-recent-toggle =
    .label = Azken jarduera
    .description = Azken gune eta edukien hautapena
newtab-custom-weather-toggle =
    .label = Eguraldia
    .description = Gaurko eguraldi-iragarpena
newtab-custom-close-button = Itxi
newtab-custom-settings = Kudeatu ezarpen gehiago

## New Tab Wallpapers

newtab-wallpaper-title = Horma-paperak
newtab-wallpaper-reset = Berrezarri lehenespenera
newtab-wallpaper-upload-image = Igo irudia
newtab-wallpaper-custom-color = Aukeratu kolorea
newtab-wallpaper-light-red-panda = Panda gorria
newtab-wallpaper-light-mountain = Mendi zuria
newtab-wallpaper-light-sky = Hodei more eta arrosadun zerua
newtab-wallpaper-light-color = Forma urdin, arrosa eta horiak
newtab-wallpaper-light-landscape = Lanbro urdindun mendiko paisaia
newtab-wallpaper-light-beach = Palmondoa duen hondartza
newtab-wallpaper-dark-aurora = Aurora boreala
newtab-wallpaper-dark-color = Forma gorri eta urdinak
newtab-wallpaper-dark-panda = Basoan ezkutatutako panda gorria
newtab-wallpaper-dark-sky = Gaueko zerudun hiriko paisaia
newtab-wallpaper-dark-mountain = Paisaia mendia
newtab-wallpaper-dark-city = Hiriko paisaia morea
newtab-wallpaper-dark-fox-anniversary = Azeria espaloian baso batetik gertu
newtab-wallpaper-light-fox-anniversary = Azeria belardi batean mendiko paisaia lanbrotsuarekin

## Solid Colors

newtab-wallpaper-category-title-colors = Kolore solidoak
newtab-wallpaper-blue = Urdina
newtab-wallpaper-light-blue = Urdin argia
newtab-wallpaper-light-purple = More argia
newtab-wallpaper-light-green = Berde argia
newtab-wallpaper-green = Berdea
newtab-wallpaper-beige = Beixa
newtab-wallpaper-yellow = Horia
newtab-wallpaper-orange = Laranja
newtab-wallpaper-pink = Arrosa
newtab-wallpaper-light-pink = Arrosa argia
newtab-wallpaper-red = Gorria
newtab-wallpaper-dark-blue = Urdin iluna
newtab-wallpaper-dark-purple = More iluna
newtab-wallpaper-dark-green = Berde iluna
newtab-wallpaper-brown = Marroia

## Abstract

newtab-wallpaper-category-title-abstract = Abstraktua
newtab-wallpaper-abstract-green = Forma berdeak
newtab-wallpaper-abstract-blue = Forma urdinak
newtab-wallpaper-abstract-purple = Forma moreak
newtab-wallpaper-abstract-orange = Forma laranjak
newtab-wallpaper-gradient-orange = Gradiente laranja eta arrosa
newtab-wallpaper-abstract-blue-purple = Forma urdin eta moreak

## Celestial

newtab-wallpaper-category-title-photographs = Argazkiak
newtab-wallpaper-beach-at-sunrise = Hondartza egunsentian
newtab-wallpaper-beach-at-sunset = Hondartza ilunabarrean
newtab-wallpaper-storm-sky = Ekaitz zerua
newtab-wallpaper-sky-with-pink-clouds = Hodei arrosadun zerua
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda gorria zuhaitzean aharrausika
newtab-wallpaper-white-mountains = Mendi zuriak
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Argazkiaren egilea: <a data-l10n-name="name-link">{ $author_string }</a>, <a data-l10n-name="webpage-link">{ $webpage_string }</a> webgunean
newtab-wallpaper-feature-highlight-header = Probatu kolore ukitu bat
newtab-wallpaper-feature-highlight-content = Emaiozu itxura berria zure fitxa berriari horma-paperekin.
newtab-wallpaper-feature-highlight-button = Ulertuta
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Utzi
    .aria-label = Itxi laster-lehioa
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

newtab-weather-menu-change-location = Aldatu kokapena
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Bilatu kokapena
    .aria-label = Bilatu kokapena
newtab-weather-change-location-search-input = Bilatu kokapena
newtab-weather-menu-weather-display = Eguraldia bistaratzea
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Sinplea
newtab-weather-menu-change-weather-display-simple = Aldatu ikuspegi sinplera
newtab-weather-menu-weather-display-option-detailed = Xehatua
newtab-weather-menu-change-weather-display-detailed = Aldatu ikuspegi xehatura
newtab-weather-menu-temperature-units = Tenperatura-unitateak
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Aldatu Fahrenheit-era
newtab-weather-menu-change-temperature-units-celsius = Aldatu Celsius-era
newtab-weather-menu-hide-weather = Ezkutatu eguraldia fitxa berrian
newtab-weather-menu-learn-more = Argibide gehiago
# This message is shown if user is working offline
newtab-weather-error-not-available = Eguraldiari buruzko daturik ez dago erabilgarri orain.

## Topic Labels

newtab-topic-label-business = Negozioak
newtab-topic-label-career = Lan-eskaintzak
newtab-topic-label-education = Hezkuntza
newtab-topic-label-arts = Entretenimendua
newtab-topic-label-food = Janaria
newtab-topic-label-health = Osasuna
newtab-topic-label-hobbies = Jokoak
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Finantzak
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Zientzia
newtab-topic-label-sports = Kirolak
newtab-topic-label-tech = Teknologia
newtab-topic-label-travel = Bidaiak

## Topic Selection Modal

newtab-topic-selection-save-button = Gorde
newtab-topic-selection-cancel-button = Utzi
newtab-topic-selection-button-maybe-later = Geroago agian
newtab-topic-selection-button-update-interests = Eguneratu zure interesak
newtab-topic-selection-button-pick-interests = Hautatu zure interesak

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Jarraitu
newtab-section-following-button = Jarraitzen
newtab-section-unfollow-button = Utzi jarraitzeari

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokeatu

## Confirmation modal for blocking a section

newtab-section-cancel-button = Une honetan ez
newtab-section-confirm-block-topic-p1 = Ziur zaude gai hau blokeatu nahi duzula?
newtab-section-confirm-block-topic-p2 = Blokeatutako gaiak ez dira gehiago azalduko zure jarioan.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokeatu { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-manage-topics-button-v2 =
    .label = Kudeatu gaiak
newtab-section-mangage-topics-followed-topics = Jarraituta
newtab-section-mangage-topics-followed-topics-empty-state = Ez duzu inongo gairik jarraitu oraindik.
newtab-section-mangage-topics-blocked-topics = Blokeatuta
newtab-section-mangage-topics-blocked-topics-empty-state = Ez duzu inongo gairik blokeatu oraindik.
