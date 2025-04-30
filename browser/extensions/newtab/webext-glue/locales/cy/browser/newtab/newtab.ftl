# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Tab Newydd
newtab-settings-button =
    .title = Cyfaddasu eich tudalen Tab Newydd
newtab-personalize-settings-icon-label =
    .title = Personoli Tab Newydd
    .aria-label = Gosodiadau
newtab-settings-dialog-label =
    .aria-label = Gosodiadau
newtab-personalize-icon-label =
    .title = Personoli tab newydd
    .aria-label = Personoli tab newydd
newtab-personalize-dialog-label =
    .aria-label = Personoli
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Chwilio
    .aria-label = Chwilio
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Chwilio gyda { $engine } neu roi cyfeiriad
newtab-search-box-handoff-text-no-engine = Chwilio neu gyfeiriad gwe
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Chwilio gyda { $engine } neu roi cyfeiriad
    .title = Chwilio gyda { $engine } neu roi cyfeiriad
    .aria-label = Chwilio gyda { $engine } neu roi cyfeiriad
newtab-search-box-handoff-input-no-engine =
    .placeholder = Chwilio neu gyfeiriad gwe
    .title = Chwilio neu gyfeiriad gwe
    .aria-label = Chwilio neu gyfeiriad gwe
newtab-search-box-text = Chwilio'r we
newtab-search-box-input =
    .placeholder = Chwilio'r we
    .aria-label = Chwilio'r we

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Ychwanegu Peiriant Chwilio
newtab-topsites-add-shortcut-header = Llwybr Byr Newydd
newtab-topsites-edit-topsites-header = Golygu'r Hoff Wefan
newtab-topsites-edit-shortcut-header = Golygu Llwybr Byr
newtab-topsites-add-shortcut-label = Ychwanegu Llwybr Byr
newtab-topsites-title-label = Teitl
newtab-topsites-title-input =
    .placeholder = Rhoi teitl
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Teipio neu ludo URL
newtab-topsites-url-validation = Mae angen URL Ddilys
newtab-topsites-image-url-label = URL Delwedd Gyfaddas
newtab-topsites-use-image-link = Defnyddio delwedd gyfaddas…
newtab-topsites-image-validation = Methodd y ddelwedd â llwytho. Defnyddiwch URL gwahanol.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Diddymu
newtab-topsites-delete-history-button = Dileu o'r Hanes
newtab-topsites-save-button = Cadw
newtab-topsites-preview-button = Rhagolwg
newtab-topsites-add-button = Ychwanegu

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Ydych chi'n siŵr eich bod chi am ddileu pob enghraifft o'r dudalen hon o'ch hanes?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Nid oes modd dadwneud y weithred hon.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Noddwyd

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Agor dewislen
    .aria-label = Agor dewislen
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Tynnu
    .aria-label = Tynnu
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Agor dewislen
    .aria-label = Agor dewislen cynnwys { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Golygu'r wefan
    .aria-label = Golygu'r wefan

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Golygu
newtab-menu-open-new-window = Agor mewn Ffenestr Newydd
newtab-menu-open-new-private-window = Agor mewn Ffenestr Preifat Newydd
newtab-menu-dismiss = Cau
newtab-menu-pin = Pinio
newtab-menu-unpin = Dad-binio
newtab-menu-delete-history = Dileu o'r Hanes
newtab-menu-save-to-pocket = Cadw i { -pocket-brand-name }
newtab-menu-delete-pocket = Dileu o { -pocket-brand-name }
newtab-menu-archive-pocket = Archifo i { -pocket-brand-name }
newtab-menu-show-privacy-info = Ein noddwyr a'ch preifatrwydd
newtab-menu-about-fakespot = Ynghylch { -fakespot-brand-name }
newtab-menu-report-content = Adrodd am y cynnwys hwn
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Rhwystro
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Dad-ddilyn Pwnc

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Rheoli cynnwys noddedig
newtab-menu-our-sponsors-and-your-privacy = Ein noddwyr a’ch preifatrwydd chi
newtab-menu-report-this-ad = Adrodd am yr hysbyseb hwn

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gorffen
newtab-privacy-modal-button-manage = Rheoli gosodiadau cynnwys wedi'i noddi
newtab-privacy-modal-header = Mae eich preifatrwydd yn bwysig.
newtab-privacy-modal-paragraph-2 =
    Yn ogystal â rhannu straeon cyfareddol, rydyn hefyd yn dangos i chi
    gynnwys perthnasol wedi'i ddewis yn ofalus gan noddwyr dethol. Peidiwch â phoeni,
    <strong>nid yw eich data pori byth yn gadael eich copi personol o { -brand-product-name }</strong> - nid ydym 
    yn ei weld, na'n
    noddwyr chwaith.
newtab-privacy-modal-link = Dysgwch sut mae preifatrwydd yn gweithio ar y tab newydd

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Tynnu Nod Tudalen
# Bookmark is a verb here.
newtab-menu-bookmark = Nod Tudalen

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copïo Dolen Llwytho i Lawr
newtab-menu-go-to-download-page = Mynd i'r Dudalen Llwytho i Lawr
newtab-menu-remove-download = Tynnu o'r Hanes

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Dangos yn Finder
       *[other] Agor Ffolder Cynhwysol
    }
newtab-menu-open-file = Agor Ffeil

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Ymwelwyd
newtab-label-bookmarked = Nod Tudalen
newtab-label-removed-bookmark = Wedi Tynnu'r Nod Tudalen
newtab-label-recommended = Trendio
newtab-label-saved = Cadwyd i { -pocket-brand-name }
newtab-label-download = Wedi eu Llwytho i Lawr
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = Noddir gan { $sponsorOrSource }
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Noddir gan { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } mun

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Tynnu'r Adran
newtab-section-menu-collapse-section = Cau'r Adran
newtab-section-menu-expand-section = Ehangu'r Adran
newtab-section-menu-manage-section = Rheoli'r Adran
newtab-section-menu-manage-webext = Rheoli Estyniad
newtab-section-menu-add-topsite = Ychwanegu Hoff Wefan
newtab-section-menu-add-search-engine = Ychwanegu Peiriant Chwilio
newtab-section-menu-move-up = Symud i Fyny
newtab-section-menu-move-down = Symud i Lawr
newtab-section-menu-privacy-notice = Hysbysiad Preifatrwydd

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Cau'r Adran
newtab-section-expand-section-label =
    .aria-label = Ehangu'r Adran

## Section Headers.

newtab-section-header-topsites = Hoff Wefannau
newtab-section-header-recent-activity = Gweithgaredd diweddar
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Argymhellwyd gan { $provider }
newtab-section-header-stories = Straeon sy’n procio’r meddwl
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dewisiadau heddiw i chi

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Cychwynnwch bori ac fe ddangoswn rhai erthyglau, fideos a thudalennau eraill difyr rydych wedi ymweld â nhw'n ddiweddar neu wedi gosod nod tudalen arnyn nhw yma.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Rydych wedi dal i fynDewch nôl rhywbryd eto am fwy o'r straeon pwysicaf gan { $provider }. Methu aros? Dewiswch bwnc poblogaidd i ganfod straeon da o ar draws y we.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Rydych yn gyfredol. Dewch nôl yn ddiweddarach am fwy o straeon. Methu aros? Dewiswch bwnc poblogaidd i ganfod rhagor o straeon difyr o bob rhan o'r we.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Wedi dal i fyny!
newtab-discovery-empty-section-topstories-content = Dewch nôl eto am ragor o straeon.
newtab-discovery-empty-section-topstories-try-again-button = Ceisiwch eto
newtab-discovery-empty-section-topstories-loading = Yn llwytho…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Wps! Bron a lwytho'r adran hon, ond nid yn llwyr.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Pynciau Poblogaidd:
newtab-pocket-new-topics-title = Am gael mwy fyth o straeon? Edrychwch ar y pynciau poblogaidd hyn gan { -pocket-brand-name }
newtab-pocket-more-recommendations = Rhagor o Argymhellion
newtab-pocket-learn-more = Darllen rhagor
newtab-pocket-cta-button = Defnyddio { -pocket-brand-name }
newtab-pocket-cta-text = Cadw'r straeon rydych yn eu hoffi i { -pocket-brand-name } a bwydo'ch meddwl á deunydd diddorol.
newtab-pocket-pocket-firefox-family = Mae { -pocket-brand-name } yn rhan o deulu { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Cadw
newtab-pocket-saved = Wedi'u Cadw

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Mwy fel hyn
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nid i mi
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Diolch. Bydd eich adborth yn ein helpu i wella'ch llif.
newtab-toast-dismiss-button =
    .title = Cau
    .aria-label = Cau

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Darganfod y gorau o'r we
newtab-pocket-onboarding-cta = Mae { -pocket-brand-name } yn archwilio ystod amrywiol o gyhoeddiadau i ddod â'r cynnwys mwyaf addysgiadol, ysbrydoledig a dibynadwy i'ch porwr { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Wps, aeth rhywbeth o'i le wrth llwytho'r cynnwys hwn.
newtab-error-fallback-refresh-link = Adnewyddu'r dudalen i geisio eto.

## Customization Menu

newtab-custom-shortcuts-title = Llwybrau Byr
newtab-custom-shortcuts-subtitle = Gwefannau rydych yn eu cadw neu'n ymweld â nhw
newtab-custom-shortcuts-toggle =
    .label = Llwybrau Byr
    .description = Gwefannau rydych yn eu cadw neu'n ymweld â nhw
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [zero] { $num } rhesi
        [one] { $num } rhes
        [two] { $num } res
        [few] { $num } rhes
        [many] { $num } rhes
       *[other] { $num } rhes
    }
newtab-custom-sponsored-sites = Llwybrau byr wedi'u noddi
newtab-custom-pocket-title = Argymhellir gan  { -pocket-brand-name }
newtab-custom-pocket-subtitle = Cynnwys eithriadol wedi'i guradu gan { -pocket-brand-name }, rhan o deulu { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Straeon cymeradwy
    .description = Cynnwys eithriadol wedi'i gasglu gan deulu { -brand-product-name }
newtab-custom-pocket-sponsored = Straeon wedi'u noddi
newtab-custom-pocket-show-recent-saves = Dangos pethau gadwyd yn ddiweddar
newtab-custom-recent-title = Gweithgaredd diweddar
newtab-custom-recent-subtitle = Detholiad o wefannau a chynnwys diweddar
newtab-custom-recent-toggle =
    .label = Gweithgaredd diweddar
    .description = Detholiad o wefannau a chynnwys diweddar
newtab-custom-weather-toggle =
    .label = Y Tywydd
    .description = Cipolwg ar ragolygon tywydd heddiw
newtab-custom-close-button = Cau
newtab-custom-settings = Rheoli rhagor o osodiadau

## New Tab Wallpapers

newtab-wallpaper-title = Papurau wal
newtab-wallpaper-reset = Ailosod i'r rhagosodiad
newtab-wallpaper-upload-image = Llwytho delwedd
newtab-wallpaper-custom-color = Dewis lliw
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Mae'r ddelwedd yn fwy na'r terfyn maint ffeil { $file_size }MB. Ceisiwch lwytho ffeil lai.
newtab-wallpaper-error-file-type = Nid oes modd i ni lwytho'ch ffeil. Ceisiwch eto gyda gwahanol fathau o ffeil.
newtab-wallpaper-light-red-panda = Panda coch
newtab-wallpaper-light-mountain = Mynydd gwyn
newtab-wallpaper-light-sky = Awyr gyda chymylau porffor a phinc
newtab-wallpaper-light-color = Siapiau glas, pinc a melyn
newtab-wallpaper-light-landscape = Tirwedd mynydd a niwlen las
newtab-wallpaper-light-beach = Traeth gyda phalmwydd
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Siapiau coch a glas
newtab-wallpaper-dark-panda = Panda coch wedi'i guddio yn y goedwig
newtab-wallpaper-dark-sky = Tirwedd y ddinas gydag awyr y nos
newtab-wallpaper-dark-mountain = Tirwedd mynydd
newtab-wallpaper-dark-city = Tirwedd dinas borffor
newtab-wallpaper-dark-fox-anniversary = Llwynog ar y palmant ger coedwig
newtab-wallpaper-light-fox-anniversary = Llwynog mewn cae glaswelltog gyda thirlun mynydd niwlog

## Solid Colors

newtab-wallpaper-category-title-colors = Lliwiau solet
newtab-wallpaper-blue = Glas
newtab-wallpaper-light-blue = Glas golau
newtab-wallpaper-light-purple = Porffor golau
newtab-wallpaper-light-green = Gwyrdd golau
newtab-wallpaper-green = Gwyrdd
newtab-wallpaper-beige = Llwydfelyn
newtab-wallpaper-yellow = Melyn
newtab-wallpaper-orange = Oren
newtab-wallpaper-pink = Pinc
newtab-wallpaper-light-pink = Pinc golau
newtab-wallpaper-red = Coch
newtab-wallpaper-dark-blue = Glas tywyll
newtab-wallpaper-dark-purple = Porffor tywyll
newtab-wallpaper-dark-green = Gwyrdd tywyll
newtab-wallpaper-brown = Brown

## Abstract

newtab-wallpaper-category-title-abstract = Haniaethol
newtab-wallpaper-abstract-green = Siapiau gwyrdd
newtab-wallpaper-abstract-blue = Siapiau glas
newtab-wallpaper-abstract-purple = Siapiau porffor
newtab-wallpaper-abstract-orange = Siapiau oren
newtab-wallpaper-gradient-orange = Graddiant oren a phinc
newtab-wallpaper-abstract-blue-purple = Siapiau glas a phorffor
newtab-wallpaper-abstract-white-curves = Gwyn gyda chromlinau cysgodol
newtab-wallpaper-abstract-purple-green = Graddiant golau porffor a gwyrdd
newtab-wallpaper-abstract-blue-purple-waves = Siapiau tonnog glas a phorffor
newtab-wallpaper-abstract-black-waves = Siapiau tonnog du

## Celestial

newtab-wallpaper-category-title-photographs = Ffotograffau
newtab-wallpaper-beach-at-sunrise = Traeth ar godiad haul
newtab-wallpaper-beach-at-sunset = Traeth ar fachlud haul
newtab-wallpaper-storm-sky = Awyr stormus
newtab-wallpaper-sky-with-pink-clouds = Awyr gyda chymylau pinc
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda coch yn dylyfu mewn coeden
newtab-wallpaper-white-mountains = Mynyddoedd gwyn
newtab-wallpaper-hot-air-balloons = Balwnau aer poeth o lliwiau amrywiol yn ystod y dydd
newtab-wallpaper-starry-canyon = Noson serennog las
newtab-wallpaper-suspension-bridge = Ffotograffau pont crog llwyd yn ystod y dydd
newtab-wallpaper-sand-dunes = Twyni tywod gwyn
newtab-wallpaper-palm-trees = Amlinell coed palmwydd cnau coco yn yr awr euraidd
newtab-wallpaper-blue-flowers = Ffotograffiaeth agos o flodau petalau glas yn eu blodau
newtab-wallpaper-forest-trees = Coed wedi'u gorchuddio â niwl
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Llun gan <a data-l10n-name="name-link">{ $author_string }</a> ar <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Ychwanegwch bach o liw
newtab-wallpaper-feature-highlight-content = Rhowch olwg newydd i'ch Tab Newydd gyda phapurau wal.
newtab-wallpaper-feature-highlight-button = Iawn
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Cau
    .aria-label = Cau'r llamlen
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Wybrennol
newtab-wallpaper-celestial-lunar-eclipse = Eclipse lleuad
newtab-wallpaper-celestial-earth-night = Llun nos o orbit daear isel
newtab-wallpaper-celestial-starry-sky = Awyr serennog
newtab-wallpaper-celestial-eclipse-time-lapse = Eclipse amser llithro lleuad
newtab-wallpaper-celestial-black-hole = Darlun galaeth twll du
newtab-wallpaper-celestial-river = Delwedd lloeren o'r afon

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Gweld y rhagolygon yn { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Wedi'i noddi
newtab-weather-menu-change-location = Newid lleoliad
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Chwilio am leoliad
    .aria-label = Chwilio am leoliad
newtab-weather-change-location-search-input = Chwilio am leoliad
newtab-weather-menu-weather-display = Dangos y tywydd
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Syml
newtab-weather-menu-change-weather-display-simple = Newid i'r golwg syml
newtab-weather-menu-weather-display-option-detailed = Manwl
newtab-weather-menu-change-weather-display-detailed = Newid i'r golwg manwl
newtab-weather-menu-temperature-units = Unedau tymheredd
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Newid i Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Newid i Celsius
newtab-weather-menu-hide-weather = Cuddio'r tywydd ar Dab Newydd
newtab-weather-menu-learn-more = Rhagor
# This message is shown if user is working offline
newtab-weather-error-not-available = Nid yw data tywydd ar gael ar hyn o bryd.

## Topic Labels

newtab-topic-label-business = Busnes
newtab-topic-label-career = Gyrfaoedd
newtab-topic-label-education = Addysg
newtab-topic-label-arts = Adloniant
newtab-topic-label-food = Bwyd
newtab-topic-label-health = Iechyd
newtab-topic-label-hobbies = Gemau
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Arian
newtab-topic-label-society-parenting = Rhiantu
newtab-topic-label-government = Gwleidyddiaeth
newtab-topic-label-education-science = Gwyddoniaeth
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Gwella'ch Bywyd
newtab-topic-label-sports = Chwaraeon
newtab-topic-label-tech = Technoleg
newtab-topic-label-travel = Teithio
newtab-topic-label-home = Cartref a Gardd

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Dewiswch bynciau i fireinio'ch llif
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Dewiswch ddau bwnc neu fwy. Mae ein curaduron arbenigol yn blaenoriaethu straeon sydd wedi'u teilwra i'ch diddordebau. Diweddarwch nhw ar unrhyw adeg.
newtab-topic-selection-save-button = Cadw
newtab-topic-selection-cancel-button = Diddymu
newtab-topic-selection-button-maybe-later = Rhywbryd eto
newtab-topic-selection-privacy-link = Dyma sut rydym yn diogelu ac yn rheoli data
newtab-topic-selection-button-update-interests = Diweddarwch eich diddordebau
newtab-topic-selection-button-pick-interests = Dewiswch eich diddordebau

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Dilyn
newtab-section-following-button = Yn dilyn
newtab-section-unfollow-button = Dad-ddilyn

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Rhwystro
newtab-section-blocked-button = Rhwystrwyd
newtab-section-unblock-button = Dadrwystro

## Confirmation modal for blocking a section

newtab-section-cancel-button = Nid nawr
newtab-section-confirm-block-topic-p1 = Ydych chi'n siŵr eich bod am rwystro'r pwnc hwn?
newtab-section-confirm-block-topic-p2 = Ni fydd pynciau sydd wedi'u rhwystro yn ymddangos yn eich llif bellach.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Rhwystro { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Pynciau
newtab-section-manage-topics-button-v2 =
    .label = Rheoli pynciau
newtab-section-mangage-topics-followed-topics = Dilynwyd
newtab-section-mangage-topics-followed-topics-empty-state = Nid ydych wedi dilyn unrhyw bynciau eto.
newtab-section-mangage-topics-blocked-topics = Rhwystrwyd
newtab-section-mangage-topics-blocked-topics-empty-state = Nid ydych wedi rhwystro unrhyw bynciau eto.
newtab-custom-wallpaper-title = Mae papurau wal cyfaddas yma
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Llwythwch i fyny eich papur wal eich hun neu dewiswch liw cyfaddas i wneud { -brand-product-name } deimlo'n gartrefol.
newtab-custom-wallpaper-cta = Rhowch gynnig arni
