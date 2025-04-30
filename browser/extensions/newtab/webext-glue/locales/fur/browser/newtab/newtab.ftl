# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Gnove schede
newtab-settings-button =
    .title = Personalize la pagjine de tô gnove schede
newtab-personalize-settings-icon-label =
    .title = Personalize Gnove schede
    .aria-label = Impostazions
newtab-settings-dialog-label =
    .aria-label = Impostazions
newtab-personalize-icon-label =
    .title = Personalize gnove schede
    .aria-label = Personalize gnove schede
newtab-personalize-dialog-label =
    .aria-label = Personalize
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Cîr
    .aria-label = Cîr
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Cîr cun { $engine } o inserìs la direzion
newtab-search-box-handoff-text-no-engine = Cîr o inserìs la direzion
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Cîr cun { $engine } o inserìs la direzion
    .title = Cîr cun { $engine } o inserìs la direzion
    .aria-label = Cîr cun { $engine } o inserìs la direzion
newtab-search-box-handoff-input-no-engine =
    .placeholder = Cîr o inserìs la direzion
    .title = Cîr o inserìs la direzion
    .aria-label = Cîr o inserìs la direzion
newtab-search-box-text = Cîr tal web
newtab-search-box-input =
    .placeholder = Cîr tal web
    .aria-label = Cîr tal web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Zonte motôr di ricercje
newtab-topsites-add-shortcut-header = Gnove scurte
newtab-topsites-edit-topsites-header = Modifiche sît principâl
newtab-topsites-edit-shortcut-header = Modifiche scurte
newtab-topsites-add-shortcut-label = Zonte scurte
newtab-topsites-title-label = Titul
newtab-topsites-title-input =
    .placeholder = Inserìs un titul
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Scrîf o tache un URL
newtab-topsites-url-validation = URL valit necessari
newtab-topsites-image-url-label = URL di imagjin personalizade
newtab-topsites-use-image-link = Dopre une imagjin personalizade…
newtab-topsites-image-validation = No si à rivât a cjariâ la imagjin. Prove cuntun URL diferent.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Anule
newtab-topsites-delete-history-button = Elimine de cronologjie
newtab-topsites-save-button = Salve
newtab-topsites-preview-button = Anteprime
newtab-topsites-add-button = Zonte

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Eliminâ pardabon ogni istance di cheste pagjine de tô cronologjie?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = No si pues tornâ indaûr di cheste operazion.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsorizât

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Vierç menù
    .aria-label = Vierç menù
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Gjave
    .aria-label = Gjave
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Vierç menù
    .aria-label = Vierç il menù contestuâl par { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Modifiche chest sît
    .aria-label = Modifiche chest sît

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Modifiche
newtab-menu-open-new-window = Vierç intun gnûf barcon
newtab-menu-open-new-private-window = Vierç intun gnûf barcon privât
newtab-menu-dismiss = Scarte
newtab-menu-pin = Fisse
newtab-menu-unpin = Mole
newtab-menu-delete-history = Elimine de cronologjie
newtab-menu-save-to-pocket = Salve su { -pocket-brand-name }
newtab-menu-delete-pocket = Elimine di { -pocket-brand-name }
newtab-menu-archive-pocket = Archivie in { -pocket-brand-name }
newtab-menu-show-privacy-info = I nestris patrocinadôrs e la tô riservatece
newtab-menu-about-fakespot = Informazions su { -fakespot-brand-name }
newtab-menu-report-content = Segnale chest contignût
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloche
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Smet di sta daûr al argoment

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Gjestìs contignûts sponsorizâts
newtab-menu-our-sponsors-and-your-privacy = I nestris sponsors e la tô riservatece
newtab-menu-report-this-ad = Segnale chest anunci

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Fat
newtab-privacy-modal-button-manage = Gjestìs lis impostazions dai contignûts sponsorizâts
newtab-privacy-modal-header = La tô riservatece e je impuartante.
newtab-privacy-modal-paragraph-2 =
    Sore a proponiti storiis inmagantis, ti mostrin ancje contignûts,
    pertinents e curâts cun atenzion, promovûts di un grup selezionât di
    sponsors. Ti garantìn che <strong>nissun dât relatîf ae tô navigazion
    al vignarà condividût de tô copie personâl di { -brand-product-name }</strong>.
    Nô no viodìn chestis informazions e ancjemò di mancul i nestris sponsors.
newtab-privacy-modal-link = Scuvierç cemût che la riservatece e funzione te gnove schede

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Gjave segnelibri
# Bookmark is a verb here.
newtab-menu-bookmark = Segnelibri

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copie colegament dal discjariament
newtab-menu-go-to-download-page = Va te pagjine dai discjariaments
newtab-menu-remove-download = Gjave de cronologjie

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mostre in Finder
       *[other] Vierç la cartele che lu conten
    }
newtab-menu-open-file = Vierç file

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitât
newtab-label-bookmarked = Metût tai segnelibris
newtab-label-removed-bookmark = Segnelibri gjavât
newtab-label-recommended = Di tindince
newtab-label-saved = Salvât su { -pocket-brand-name }
newtab-label-download = Discjariât
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsorizât
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Patrocinât di { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Gjave sezion
newtab-section-menu-collapse-section = Strenç la sezion
newtab-section-menu-expand-section = Slargje sezion
newtab-section-menu-manage-section = Gjestìs sezion
newtab-section-menu-manage-webext = Gjestìs estension
newtab-section-menu-add-topsite = Zonte sît principâl
newtab-section-menu-add-search-engine = Zonte motôr di ricercje
newtab-section-menu-move-up = Sposte in sù
newtab-section-menu-move-down = Sposte in jù
newtab-section-menu-privacy-notice = Informative su la riservatece

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Strenç la sezion
newtab-section-expand-section-label =
    .aria-label = Slargje sezion

## Section Headers.

newtab-section-header-topsites = Sîts principâi
newtab-section-header-recent-activity = Ativitât resinte
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Conseât di { $provider }
newtab-section-header-stories = Storiis che a fasin pensâ
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Lis sieltis di vuê par te

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Scomence a navigâ e, in cheste sezion, ti mostrarìn cualchi articul impuartant, videos e altris pagjinis che tu âs visitât di resint o tu âs metût tai segnelibris.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Tu sês rivât insom. Controle plui indenant par vê altris storiis di { $provider }. No tu rivis a spietâ? Selezione un argoment tra chei plui popolârs par cjatâ altris storiis interessantis ator pal web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Nuie altri di gnûf. Torne controle plui indevant par altris storiis. No tu rivis a spietâ? Selezione un argoment di chei plui popolârs par scuvierzi altris storiis interessantis dal web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Tu sês rivât insom!
newtab-discovery-empty-section-topstories-content = Controle plui indenant par vê plui storiis.
newtab-discovery-empty-section-topstories-try-again-button = Torne prove
newtab-discovery-empty-section-topstories-loading = Daûr a cjariâ…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Orpo! Al somee che cheste sezion no si sedi cjariade dal dut.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Argoments popolârs:
newtab-pocket-new-topics-title = Desideristu vê ancjemò plui storiis? Bute un voli a chescj argoments popolârs di { -pocket-brand-name }
newtab-pocket-more-recommendations = Altris sugjeriments
newtab-pocket-learn-more = Plui informazions
newtab-pocket-cta-button = Oten { -pocket-brand-name }
newtab-pocket-cta-text = Salve lis storiis che ti plasin in { -pocket-brand-name } e nudrìs il to cjâf cun leturis apassionantis.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } al è part de famee { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Salve
newtab-pocket-saved = Salvât

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Plui contignûts come chest
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = No mi interesse
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Graciis. La tô opinion nus judarà a miorâ il to feed.
newtab-toast-dismiss-button =
    .title = Siere
    .aria-label = Scarte

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Scuvierç il miôr dal web
newtab-pocket-onboarding-cta = { -pocket-brand-name } al esplore une grande schirie di publicazions par puartâ i contignûts plui informatîfs, stimolants e afidabii dret sul to navigadôr { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Orpo!, alc al è lât strucj tal cjariâ chest contignût.
newtab-error-fallback-refresh-link = Inzorne la pagjine par tornâ a provâ.

## Customization Menu

newtab-custom-shortcuts-title = Scurtis
newtab-custom-shortcuts-subtitle = Sîts che tu âs salvât o visitât
newtab-custom-shortcuts-toggle =
    .label = Scurtis
    .description = Sîts che tu âs salvât o visitât
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rie
       *[other] { $num } riis
    }
newtab-custom-sponsored-sites = Scurtis sponsorizadis
newtab-custom-pocket-title = Conseâts di { -pocket-brand-name }
newtab-custom-pocket-subtitle = Contignûts ecezionâi curâts di { -pocket-brand-name }, part de famee { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Storiis conseadis
    .description = Contignûts ecezionâl curât de famee di prodots { -brand-product-name }
newtab-custom-pocket-sponsored = Storiis sponsorizadis
newtab-custom-pocket-show-recent-saves = Mostre salvaments resints
newtab-custom-recent-title = Ativitât resinte
newtab-custom-recent-subtitle = Une selezion di sîts e contignûts resints
newtab-custom-recent-toggle =
    .label = Ativitât resinte
    .description = Une selezion di sîts e contignûts resints
newtab-custom-weather-toggle =
    .label = Meteo
    .description = Previsions par vuê cuntune voglade
newtab-custom-close-button = Siere
newtab-custom-settings = Gjestìs plui impostazions

## New Tab Wallpapers

newtab-wallpaper-title = Fonts
newtab-wallpaper-reset = Ripristine predefinît
newtab-wallpaper-upload-image = Cjame une imagjin
newtab-wallpaper-custom-color = Sielç un colôr
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = La imagjin e va fûr de dimension massime ametude ({ $file_size } MB). Prove a cjariâ un file plui piçul.
newtab-wallpaper-error-file-type = No rivìn a cjariâ il to file. Torne prove cuntun altri gjenar di file.
newtab-wallpaper-light-red-panda = Panda ros
newtab-wallpaper-light-mountain = Montagne blancje
newtab-wallpaper-light-sky = Cîl cun nui viole e rose
newtab-wallpaper-light-color = Formis zalis, blu e rose
newtab-wallpaper-light-landscape = Paisaç cun montagne involuçât intune fumate blu
newtab-wallpaper-light-beach = Splaze cun palmis
newtab-wallpaper-dark-aurora = Aurore boreâl
newtab-wallpaper-dark-color = Formis rossis e blu
newtab-wallpaper-dark-panda = Panda ros platât tal bosc
newtab-wallpaper-dark-sky = Paisaç di citât cun cîl di gnot
newtab-wallpaper-dark-mountain = Paisaç cun montagne
newtab-wallpaper-dark-city = Paisaç citadin cun tonalitât viole
newtab-wallpaper-dark-fox-anniversary = Une bolp sul marcjepît dongje di un bosc
newtab-wallpaper-light-fox-anniversary = Une bolp intun cjamp jerbôs cuntun paesaç di montagne infumatât

## Solid Colors

newtab-wallpaper-category-title-colors = Colôr solit
newtab-wallpaper-blue = Blu
newtab-wallpaper-light-blue = Blu clâr
newtab-wallpaper-light-purple = Viole clâr
newtab-wallpaper-light-green = Vert clâr
newtab-wallpaper-green = Vert
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Zâl
newtab-wallpaper-orange = Naranç
newtab-wallpaper-pink = Rose
newtab-wallpaper-light-pink = Rose clâr
newtab-wallpaper-red = Ros
newtab-wallpaper-dark-blue = Blu scûr
newtab-wallpaper-dark-purple = Viole scûr
newtab-wallpaper-dark-green = Vert scûr
newtab-wallpaper-brown = Maron

## Abstract

newtab-wallpaper-category-title-abstract = Astrat
newtab-wallpaper-abstract-green = Formis verdis
newtab-wallpaper-abstract-blue = Formis blu
newtab-wallpaper-abstract-purple = Formis viole
newtab-wallpaper-abstract-orange = Formis naranç
newtab-wallpaper-gradient-orange = Gradient naranç e rose
newtab-wallpaper-abstract-blue-purple = Formis blu e viole
newtab-wallpaper-abstract-white-curves = Blanc cun curvis sfumadis
newtab-wallpaper-abstract-purple-green = Sfumadure di lûs viole e verde
newtab-wallpaper-abstract-blue-purple-waves = Formis ondadis blu e viole
newtab-wallpaper-abstract-black-waves = Formis ondadis neris

## Celestial

newtab-wallpaper-category-title-photographs = Fotografiis
newtab-wallpaper-beach-at-sunrise = Splaze al cricâ dal dì
newtab-wallpaper-beach-at-sunset = Splaze al tramont
newtab-wallpaper-storm-sky = Cîl di tampieste
newtab-wallpaper-sky-with-pink-clouds = Cîl cun nui rose
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda ros che al sossede suntun arbul
newtab-wallpaper-white-mountains = Montagnis blancjis
newtab-wallpaper-hot-air-balloons = Mongolfieris cun colôrs assortîts cjapadis in plen dì
newtab-wallpaper-starry-canyon = Gnot stelade blu
newtab-wallpaper-suspension-bridge = Fotografie di un puint grîs sospindût fate vie pal dì
newtab-wallpaper-sand-dunes = Dunis di savalon blanc
newtab-wallpaper-palm-trees = Sacume di palme di coco cjapade vie pe ore di aur
newtab-wallpaper-blue-flowers = Fotografie fate di dongje di rosis cun pics blu in floridure
newtab-wallpaper-forest-trees = Arbui cuvierts de fumate
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto di <a data-l10n-name="name-link">{ $author_string }</a> su <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Prove une man di colôr
newtab-wallpaper-feature-highlight-content = Da un aspiet diviers aes tôs gnovis schedis cui fonts.
newtab-wallpaper-feature-highlight-button = Capît
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Siere
    .aria-label = Siere barcon a comparse
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Spazi celest
newtab-wallpaper-celestial-lunar-eclipse = Eclìs lunâr
newtab-wallpaper-celestial-earth-night = Foto di gnot de orbite de tiere basse
newtab-wallpaper-celestial-starry-sky = Cîl di stelis
newtab-wallpaper-celestial-eclipse-time-lapse = Dade di timp de eclìs lunâr
newtab-wallpaper-celestial-black-hole = Ilustrazion de galassie di un bûs neri
newtab-wallpaper-celestial-river = Imagjin satelitâr di un flum

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Viôt lis previsions in { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsorizât
newtab-weather-menu-change-location = Cambie localitât
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Cîr localitât
    .aria-label = Cîr localitât
newtab-weather-change-location-search-input = Cîr localitât
newtab-weather-menu-weather-display = Visualizazion meteo
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Sintetiche
newtab-weather-menu-change-weather-display-simple = Passe ae viodude sintetiche
newtab-weather-menu-weather-display-option-detailed = Detaiade
newtab-weather-menu-change-weather-display-detailed = Passe ae viodude detaiade
newtab-weather-menu-temperature-units = Unitâts di temperadure
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Passe a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Passe a Celsius
newtab-weather-menu-hide-weather = Plate il meteo ae Gnove schede
newtab-weather-menu-learn-more = Plui informazions
# This message is shown if user is working offline
newtab-weather-error-not-available = I dâts sul meteo in chest moment no son disponibii.

## Topic Labels

newtab-topic-label-business = Economie
newtab-topic-label-career = Cariere
newtab-topic-label-education = Educazion
newtab-topic-label-arts = Intratigniment
newtab-topic-label-food = Mangjative
newtab-topic-label-health = Salût
newtab-topic-label-hobbies = Zûcs
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Financis personâls
newtab-topic-label-society-parenting = Educazion dai fîs
newtab-topic-label-government = Politiche
newtab-topic-label-education-science = Sience
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Mioraments personâi
newtab-topic-label-sports = Sports
newtab-topic-label-tech = Tecnologjie
newtab-topic-label-travel = Viaçs
newtab-topic-label-home = Cjase e zardin

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Selezione i argoments par regolâ il to feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Sielç doi o plui argoments. I nestris esperts curadôrs a daran prioritât aes storiis plui adatis ai tiei interès. Tu puedis inzornâ lis tôs preferencis cuant che tu vûs.
newtab-topic-selection-save-button = Salve
newtab-topic-selection-cancel-button = Anule
newtab-topic-selection-button-maybe-later = Magari plui indevant
newtab-topic-selection-privacy-link = Scuvierç cemût che o gjestìn e o protezìn i dâts
newtab-topic-selection-button-update-interests = Inzorne i tiei interès
newtab-topic-selection-button-pick-interests = Sielç i tiei interès

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sta daûr
newtab-section-following-button = Tu stâs daûr
newtab-section-unfollow-button = Smet di sta daûr

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloche
newtab-section-blocked-button = Blocât
newtab-section-unblock-button = Sbloche

## Confirmation modal for blocking a section

newtab-section-cancel-button = No cumò
newtab-section-confirm-block-topic-p1 = Blocâ pardabon chest argoment?
newtab-section-confirm-block-topic-p2 = I argoments blocâts no vignaran plui mostrâts tal to feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloche { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Argoments
newtab-section-manage-topics-button-v2 =
    .label = Gjestìs argoments
newtab-section-mangage-topics-followed-topics = Tu vâs daûr
newtab-section-mangage-topics-followed-topics-empty-state = No tu sês ancjemò daûr a nissun argoment.
newtab-section-mangage-topics-blocked-topics = Blocât
newtab-section-mangage-topics-blocked-topics-empty-state = No tu âs ancjemò blocât nissun argoment.
newtab-custom-wallpaper-title = I fonts personalizâts a son achì
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Cjame il to font o sielç un colôr personalizât par fâ to { -brand-product-name }.
newtab-custom-wallpaper-cta = Provilu
