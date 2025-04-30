# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nova langeto
newtab-settings-button =
    .title = Personecigi la paĝon por novaj langetoj
newtab-personalize-settings-icon-label =
    .title = Personecigi la paĝon por novaj langetoj
    .aria-label = Agordoj
newtab-settings-dialog-label =
    .aria-label = Agordoj
newtab-personalize-icon-label =
    .title = Personecigi novan langeton
    .aria-label = Personecigi novan langeton
newtab-personalize-dialog-label =
    .aria-label = Personcecigi
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Serĉi
    .aria-label = Serĉi
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Serĉi per { $engine } aŭ tajpi adreson
newtab-search-box-handoff-text-no-engine = Serĉi aŭ tajpi adreson
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Serĉi per { $engine } aŭ tajpi adreson
    .title = Serĉi per { $engine } aŭ tajpi adreson
    .aria-label = Serĉi per { $engine } aŭ tajpi adreson
newtab-search-box-handoff-input-no-engine =
    .placeholder = Serĉi aŭ tajpi adreson
    .title = Serĉi aŭ tajpi adreson
    .aria-label = Serĉi aŭ tajpi adreson
newtab-search-box-text = Serĉi en la reto
newtab-search-box-input =
    .placeholder = Serĉi en la reto
    .aria-label = Serĉi en la reto

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Aldoni serĉilon
newtab-topsites-add-shortcut-header = Nova ŝparvojo
newtab-topsites-edit-topsites-header = Redakti oftan retejon
newtab-topsites-edit-shortcut-header = Redakti ŝparvojon
newtab-topsites-add-shortcut-label = Aldoni ŝparvojon
newtab-topsites-title-label = Titolo
newtab-topsites-title-input =
    .placeholder = Tajpu titolon
newtab-topsites-url-label = Retadreso
newtab-topsites-url-input =
    .placeholder = Tajpu aŭ alguu retadreson
newtab-topsites-url-validation = Valida retadreso estas postulata
newtab-topsites-image-url-label = Personecitiga retadreso de bildo
newtab-topsites-use-image-link = Uzi personecigitan bildon…
newtab-topsites-image-validation = Ne eblis ŝargi la bildon. Klopodu alian retadreson.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Nuligi
newtab-topsites-delete-history-button = Forigi el historio
newtab-topsites-save-button = Konservi
newtab-topsites-preview-button = Antaŭvidi
newtab-topsites-add-button = Aldoni

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Ĉu vi certe volas forigi ĉiun aperon de tiu ĉi paĝo el via historio?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tiu ĉi ago ne estas malfarebla.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Patronita

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Malfermi menuon
    .aria-label = Malfermi menuon
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Forigi
    .aria-label = Forigi
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Malfermi menuon
    .aria-label = Malfermi kuntekstan menu por { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Redakti ĉi tiun retejon
    .aria-label = Redakti ĉi tiun retejon

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Redakti
newtab-menu-open-new-window = Malfermi en nova fenestro
newtab-menu-open-new-private-window = Malfermi en nova privata fenestro
newtab-menu-dismiss = Ignori
newtab-menu-pin = Alpingli
newtab-menu-unpin = Depingli
newtab-menu-delete-history = Forigi el historio
newtab-menu-save-to-pocket = Konservi en { -pocket-brand-name }
newtab-menu-delete-pocket = Forigi el { -pocket-brand-name }
newtab-menu-archive-pocket = Arĥivi en { -pocket-brand-name }
newtab-menu-show-privacy-info = Niaj patronoj kaj via privateco
newtab-menu-about-fakespot = Pri { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloki
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Ne plu sekvi temon

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Farita
newtab-privacy-modal-button-manage = Administri agordojn de patronita enhavo
newtab-privacy-modal-header = Via privateco gravas.
newtab-privacy-modal-paragraph-2 = Krom allogajn artikolojn ni montras al vi ankaŭ gravajn, zorge reviziitan enhavon el elektitaj patronoj. Estu certa, <strong>viaj retumaj datumoj neniam foriras el via loka instalaĵo de { -brand-product-name }</strong> — ni ne vidas ilin, kaj ankaŭ ne niaj patronoj.
newtab-privacy-modal-link = Pli da informo pri privateco en novaj folioj

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Forigi legosignon
# Bookmark is a verb here.
newtab-menu-bookmark = Aldoni legosignon

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopii elŝutan ligilon
newtab-menu-go-to-download-page = Iri al la paĝo de elŝuto
newtab-menu-remove-download = Forigi el la historio

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Montri en Finder
       *[other] Malfermi entenantan dosierujon
    }
newtab-menu-open-file = Malfermi dosieron

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Vizitita
newtab-label-bookmarked = Kun legosigno
newtab-label-removed-bookmark = Legosigno forigita
newtab-label-recommended = Tendencoj
newtab-label-saved = Konservita en { -pocket-brand-name }
newtab-label-download = Elŝutita
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Patronita
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Patronita de { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Forigi sekcion
newtab-section-menu-collapse-section = Faldi sekcion
newtab-section-menu-expand-section = Malfaldi sekcion
newtab-section-menu-manage-section = Administri sekcion
newtab-section-menu-manage-webext = Administri etendaĵon
newtab-section-menu-add-topsite = Aldoni oftan retejon
newtab-section-menu-add-search-engine = Aldoni serĉilon
newtab-section-menu-move-up = Movi supren
newtab-section-menu-move-down = Movi malsupren
newtab-section-menu-privacy-notice = Rimarko pri privateco

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Faldi sekcion
newtab-section-expand-section-label =
    .aria-label = Malfaldi sekcion

## Section Headers.

newtab-section-header-topsites = Plej vizititaj
newtab-section-header-recent-activity = Ĵusa agado
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Rekomendita de { $provider }
newtab-section-header-stories = Pensigaj artikoloj
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Niaj hodiaŭaj elektoj por vi

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Komencu retumi kaj ĉi tie ni montros al vi kelkajn el la plej bonaj artikoloj, filmetoj kaj aliaj paĝoj, kiujn vi antaŭ nelonge vizits aŭ por kiuj vi aldonis legosignon.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Vi legis ĉion. Kontrolu denove poste ĉu estas pli da novaĵoj de { $provider }. Ĉu vi ne povas atendi? Elektu popularan temon por trovi pli da interesaj artikoloj en la tuta teksaĵo.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Vi legis ĉion. Kontrolu denove poste ĉu estas pli da novaĵoj. Ĉu vi ne povas atendi? Elektu popularan temon por trovi pli da interesaj artikoloj en la tuta teksaĵo.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Estas nenio alia.
newtab-discovery-empty-section-topstories-content = Kontrolu poste por pli da artikoloj.
newtab-discovery-empty-section-topstories-try-again-button = Klopodu denove
newtab-discovery-empty-section-topstories-loading = Ŝargado…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Fuŝ! Ni preskaŭ tute ŝargis tiun ĉi sekcion, sed tamen ne.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Ĉefaj temoj:
newtab-pocket-new-topics-title = Ĉu vi volas eĉ pli da artikoloj? Vidu tiujn ĉi popularajn temojn el { -pocket-brand-name }
newtab-pocket-more-recommendations = Pli da rekomendoj
newtab-pocket-learn-more = Pli da informo
newtab-pocket-cta-button = Instali { -pocket-brand-name }
newtab-pocket-cta-text = Konservu viajn ŝatatajn artikolojn en { -pocket-brand-name }, kaj stimulu vian menson per ravaj legaĵoj.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } estas parto de la familio { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Konservi
newtab-pocket-saved = Konservitaj

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Pli da ĉi tiaj
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Tio ne interesas min
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Dankon, viaj komentoj helpos nin plibonigi vian informan fonton.
newtab-toast-dismiss-button =
    .title = Ignori
    .aria-label = I

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Malkovru la plej bonajn aferojn en la reto
newtab-pocket-onboarding-cta = { -pocket-brand-name } esploras vastan diversecon de publikigaĵoj por alporti la plej informan, inspiran kaj fidindan enhavon al via retumilo { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Fuŝ', io malbona okazis dum ŝargo de tiu ĉi enhavo.
newtab-error-fallback-refresh-link = Refreŝigi paĝon por klopodi denove.

## Customization Menu

newtab-custom-shortcuts-title = Ŝparvojoj
newtab-custom-shortcuts-subtitle = Retejoj konservitaj aŭ vizititaj de vi
newtab-custom-shortcuts-toggle =
    .label = Ŝparvojoj
    .description = Retejoj konservitaj aŭ vizititaj de vi
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] unu vico
       *[other] { $num } vicoj
    }
newtab-custom-sponsored-sites = Patronitaj ŝparvojoj
newtab-custom-pocket-title = Rekomendita de { -pocket-brand-name }
newtab-custom-pocket-subtitle = Eksterordinara  enhavo reviziita de  { -pocket-brand-name }, parto de la familio { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Rekomenditaj artikoloj
    .description = Eksterordinara enhavo elekita de la familio de { -brand-product-name }
newtab-custom-pocket-sponsored = Patronitaj artikoloj
newtab-custom-pocket-show-recent-saves = Montri ĵusajn konservojn
newtab-custom-recent-title = Ĵusa agado
newtab-custom-recent-subtitle = Elekto de ĵusaj retejoj kaj enhavoj
newtab-custom-recent-toggle =
    .label = Ĵusa agado
    .description = Elekto de ĵusaj retejoj kaj enhavoj
newtab-custom-weather-toggle =
    .label = Vetero
    .description = Rapida rigardo al la veterprognozo hodiaŭa
newtab-custom-close-button = Fermi
newtab-custom-settings = Administri aliajn agordojn

## New Tab Wallpapers

newtab-wallpaper-title = Ekranfonoj
newtab-wallpaper-reset = Reŝargi normajn valorojn
newtab-wallpaper-upload-image = Alŝuti bildon
newtab-wallpaper-custom-color = Elekti koloron
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = La grando de la bildo superas la maksimuman grandon de dosiero, kiu estas { $file_size }MO. Bonvolu provi alŝuti pli etan dosieron.
newtab-wallpaper-error-file-type = Ni ne povis alŝuti vian dosieron. Bonvolu provi denove per alia tipo de dosiero.
newtab-wallpaper-light-red-panda = Ruĝa pando
newtab-wallpaper-light-mountain = Blanka monto
newtab-wallpaper-light-sky = Ĉielo kun purpuraj kaj rozkoloraj nuboj
newtab-wallpaper-light-color = Bluaj, rozkoloraj kaj flavaj formoj
newtab-wallpaper-light-landscape = Pejzaĝo monta kun blua nebulo
newtab-wallpaper-light-beach = Strando kun palmarbo
newtab-wallpaper-dark-aurora = Polusa lumo
newtab-wallpaper-dark-color = Ruĝaj kaj bluaj formoj
newtab-wallpaper-dark-panda = Ruĝa pando kaŝita en arbaro
newtab-wallpaper-dark-sky = Pejzaĝo urba kun nokta ĉielo
newtab-wallpaper-dark-mountain = Pejzaĝo monta
newtab-wallpaper-dark-city = Purpura pejzaĝo urba
newtab-wallpaper-dark-fox-anniversary = Vulpo sur pavimo proksime de arbaro
newtab-wallpaper-light-fox-anniversary = Vulpo sur herbejo kun nebula pejzaĝo monta

## Solid Colors

newtab-wallpaper-category-title-colors = Solidaj koloroj
newtab-wallpaper-blue = Blua
newtab-wallpaper-light-blue = Helblua
newtab-wallpaper-light-purple = Helpurpura
newtab-wallpaper-light-green = Helverda
newtab-wallpaper-green = Verda
newtab-wallpaper-beige = Grizflava
newtab-wallpaper-yellow = Flava
newtab-wallpaper-orange = Oranĝa
newtab-wallpaper-pink = Roza
newtab-wallpaper-light-pink = Helroza
newtab-wallpaper-red = Ruĝa
newtab-wallpaper-dark-blue = Malhelblua
newtab-wallpaper-dark-purple = Malhelpurpura
newtab-wallpaper-dark-green = Malhelverda
newtab-wallpaper-brown = Bruna

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakta
newtab-wallpaper-abstract-green = Verdaj formoj
newtab-wallpaper-abstract-blue = Bluaj formoj
newtab-wallpaper-abstract-purple = Purpuraj formoj
newtab-wallpaper-abstract-orange = Oranĝaj formoj
newtab-wallpaper-gradient-orange = Gamo oranĝa kaj roza
newtab-wallpaper-abstract-blue-purple = Bluaj kaj purpuraj formoj
newtab-wallpaper-abstract-white-curves = Blanka kun ombritaj kurboj
newtab-wallpaper-abstract-purple-green = Gradiento luma purpura kaj verda
newtab-wallpaper-abstract-blue-purple-waves = Bluaj kaj purpuraj ondaj formoj
newtab-wallpaper-abstract-black-waves = Nigraj ondaj formoj

## Celestial

newtab-wallpaper-category-title-photographs = Fotoj
newtab-wallpaper-beach-at-sunrise = Strando dum suneliro
newtab-wallpaper-beach-at-sunset = Strando dum sunsubiro
newtab-wallpaper-storm-sky = Ŝtorma ĉielo
newtab-wallpaper-sky-with-pink-clouds = Ĉielo kun rozkoloraj nuboj
newtab-wallpaper-red-panda-yawns-in-a-tree = Ruĝa pando oscedas sur arbo
newtab-wallpaper-white-mountains = Blankaj montoj
newtab-wallpaper-hot-air-balloons = Plurkoloraj balonoj dum tago
newtab-wallpaper-starry-canyon = Blua steloplena nokto
newtab-wallpaper-suspension-bridge = Griza foto de pendponto dum tago
newtab-wallpaper-sand-dunes = Blankaj sablomontetoj
newtab-wallpaper-palm-trees = Konturo de kokosaj palmarboj dum sunsubiro
newtab-wallpaper-forest-trees = Arboj kovritaj de nebulo
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Fotita de <a data-l10n-name="name-link">{ $author_string }</a> en <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Provu koloran tuŝeton
newtab-wallpaper-feature-highlight-content = Donu al viaj langetoj novan aspekton per fonoj.
newtab-wallpaper-feature-highlight-button = Mi komprenis
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Ignori
    .aria-label = Fermi elŝprucaĵon
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Ĉiela
newtab-wallpaper-celestial-lunar-eclipse = Luneklipso
newtab-wallpaper-celestial-earth-night = Nokta foto el malalta Tera orbito
newtab-wallpaper-celestial-starry-sky = Steloplena ĉielo
newtab-wallpaper-celestial-eclipse-time-lapse = Tempopasa filmado de luneklipso
newtab-wallpaper-celestial-black-hole = Ilustraĵo de galaksio kun nigra truo
newtab-wallpaper-celestial-river = Satelita bildo de rivero

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Vidi veterprognozon en { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Patronita
newtab-weather-menu-change-location = Ŝanĝi lokon
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Serĉi lokon
    .aria-label = Serĉi lokon
newtab-weather-change-location-search-input = Serĉi lokon
newtab-weather-menu-weather-display = Montro de vetero
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simpla
newtab-weather-menu-change-weather-display-simple = Montri la simplan vidon
newtab-weather-menu-weather-display-option-detailed = Detala
newtab-weather-menu-change-weather-display-detailed = Montri la detalan vidon
newtab-weather-menu-temperature-units = Temperaturaj unuoj
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Ŝanĝi al Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Ŝanĝi al Celsius
newtab-weather-menu-hide-weather = Kaŝi veteron en nova langeto
newtab-weather-menu-learn-more = Pli da informo
# This message is shown if user is working offline
newtab-weather-error-not-available = En tiu ĉi momento ne haveblas veteraj datumoj.

## Topic Labels

newtab-topic-label-business = Negoco
newtab-topic-label-career = Kariero
newtab-topic-label-education = Eduko
newtab-topic-label-arts = Distro
newtab-topic-label-food = Manĝaĵo
newtab-topic-label-health = Sano
newtab-topic-label-hobbies = Ludo
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Financo
newtab-topic-label-society-parenting = Gepatreco
newtab-topic-label-government = Politiko
newtab-topic-label-education-science = Scienco
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Vivsimpligiloj
newtab-topic-label-sports = Sporto
newtab-topic-label-tech = Teknologio
newtab-topic-label-travel = Vojaĝo
newtab-topic-label-home = Domo kaj ĝardeno

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Elektu temojn por rafini vian informan fonton
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Elektu du aŭ pli da temoj. Niaj spertaj informzorgantoj elektos unue artikolojn, kiuj kongruas kun viaj interesoj. Vi povas ĝisdatigi tion iam ajn.
newtab-topic-selection-save-button = Konservi
newtab-topic-selection-cancel-button = Nuligi
newtab-topic-selection-button-maybe-later = Eble poste
newtab-topic-selection-privacy-link = Pli da informo pri kiel ni protektas kaj administras datumojn
newtab-topic-selection-button-update-interests = Ĝisdatigi viajn interesojn
newtab-topic-selection-button-pick-interests = Elekti viajn interesojn

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sekvi
newtab-section-following-button = Sekvata
newtab-section-unfollow-button = Ne plu sekvi

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloki
newtab-section-blocked-button = Blokita
newtab-section-unblock-button = Malbloki

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ne nun
newtab-section-confirm-block-topic-p1 = Ĉu vi certe volas bloki tiun ĉi temon?
newtab-section-confirm-block-topic-p2 = Blokitaj temoj ne plu aperos en via informa fonto
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloki { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Temoj
newtab-section-manage-topics-button-v2 =
    .label = Administri temojn
newtab-section-mangage-topics-followed-topics = Sekvataj
newtab-section-mangage-topics-followed-topics-empty-state = Vi ankoraŭ sekvas neniun temon.
newtab-section-mangage-topics-blocked-topics = Blokitaj
newtab-section-mangage-topics-blocked-topics-empty-state = Vi ankoraŭ blokas neniun temon.
