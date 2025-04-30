# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nov tab
newtab-settings-button =
    .title = Persunalisar tia pagina per novs tabs
newtab-personalize-settings-icon-label =
    .title = Persunalisar novs tabs
    .aria-label = Parameters
newtab-settings-dialog-label =
    .aria-label = Parameters
newtab-personalize-icon-label =
    .title = Persunalisar novs tabs
    .aria-label = Persunalisar novs tabs
newtab-personalize-dialog-label =
    .aria-label = Persunalisar
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Tschertgar
    .aria-label = Tschertgar
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Tschertgar cun { $engine } u endatar in'adressa
newtab-search-box-handoff-text-no-engine = Tschertgar u endatar in'adressa
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Tschertgar cun { $engine } u endatar in'adressa
    .title = Tschertgar cun { $engine } u endatar in'adressa
    .aria-label = Tschertgar cun { $engine } u endatar in'adressa
newtab-search-box-handoff-input-no-engine =
    .placeholder = Tschertgar u endatar in'adressa
    .title = Tschertgar u endatar in'adressa
    .aria-label = Tschertgar u endatar in'adressa
newtab-search-box-text = Tschertgar en il web
newtab-search-box-input =
    .placeholder = Tschertgar en il web
    .aria-label = Tschertgar en il web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Agiuntar maschina da tschertgar
newtab-topsites-add-shortcut-header = Nova scursanida
newtab-topsites-edit-topsites-header = Modifitgar la pagina principala
newtab-topsites-edit-shortcut-header = Modifitgar la scursanida
newtab-topsites-add-shortcut-label = Agiuntar ina scursanida
newtab-topsites-title-label = Titel
newtab-topsites-title-input =
    .placeholder = Endatar in titel
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Tippar u encollar ina URL
newtab-topsites-url-validation = In URL valid è necessari
newtab-topsites-image-url-label = URL dal maletg persunalisà
newtab-topsites-use-image-link = Utilisar in maletg persunalisà…
newtab-topsites-image-validation = Impussibel da chargiar il maletg. Emprova in auter URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Interrumper
newtab-topsites-delete-history-button = Stizzar da la cronologia
newtab-topsites-save-button = Memorisar
newtab-topsites-preview-button = Prevista
newtab-topsites-add-button = Agiuntar

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Vuls ti propi stizzar mintga instanza da questa pagina ord la cronologia?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Questa acziun na po betg vegnir revocada.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsurisà

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Avrir il menu
    .aria-label = Avrir il menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Allontanar
    .aria-label = Allontanar
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Avrir il menu
    .aria-label = Avrir il menu contextual per { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Modifitgar questa pagina
    .aria-label = Modifitgar questa pagina

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Modifitgar
newtab-menu-open-new-window = Avrir en ina nova fanestra
newtab-menu-open-new-private-window = Avrir en ina nova fanestra privata
newtab-menu-dismiss = Sbittar
newtab-menu-pin = Fixar
newtab-menu-unpin = Betg pli fixar
newtab-menu-delete-history = Stizzar da la cronologia
newtab-menu-save-to-pocket = Memorisar en { -pocket-brand-name }
newtab-menu-delete-pocket = Stizzar da { -pocket-brand-name }
newtab-menu-archive-pocket = Archivar en { -pocket-brand-name }
newtab-menu-show-privacy-info = Noss sponsurs & tia sfera privata
newtab-menu-about-fakespot = Davart { -fakespot-brand-name }
newtab-menu-report-content = Rapportar quest cuntegn
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloccar
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Betg pli suandar il tema

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Administrar cuntegn sponsurisà
newtab-menu-our-sponsors-and-your-privacy = Noss sponsurs e tia sfera privata
newtab-menu-report-this-ad = Rapportar questa reclama

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Finì
newtab-privacy-modal-button-manage = Administrar ils parameters da cuntegn sponsurisà
newtab-privacy-modal-header = Tia sfera privata è impurtanta.
newtab-privacy-modal-paragraph-2 =
    Ultra dad istorgias captivantas, ta mussain nus era cuntegn relevant, 
    curà cun premura da sponsurs distinguids. Nus garantin che <strong>tias datas
    da navigaziun na bandunan mai tia copia persunala da { -brand-product-name }</strong>  —
    nus n'avain betg access a questas datas e noss sponsurs n'era betg.
newtab-privacy-modal-link = Ve a savair co la protecziun da datas funcziuna sin la pagina Nov tab

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Allontanar il segnapagina
# Bookmark is a verb here.
newtab-menu-bookmark = Marcar sco segnapagina

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar la colliaziun a la telechargiada
newtab-menu-go-to-download-page = Ir a la pagina da telechargiada
newtab-menu-remove-download = Allontanar da la cronologia

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mussar en il Finder
       *[other] Mussar l'ordinatur che cuntegna la datoteca
    }
newtab-menu-open-file = Avrir la datoteca

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visità
newtab-label-bookmarked = Cun segnapagina
newtab-label-removed-bookmark = Allontanà il segnapagina
newtab-label-recommended = Popular
newtab-label-saved = Memorisà en { -pocket-brand-name }
newtab-label-download = Telechargià
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsurà
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsurisà da { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Allontanar la secziun
newtab-section-menu-collapse-section = Reducir la secziun
newtab-section-menu-expand-section = Expander la secziun
newtab-section-menu-manage-section = Administrar la secziun
newtab-section-menu-manage-webext = Administrar l'extensiun
newtab-section-menu-add-topsite = Agiuntar ina pagina principala
newtab-section-menu-add-search-engine = Agiuntar maschina da tschertgar
newtab-section-menu-move-up = Spustar ensi
newtab-section-menu-move-down = Spustar engiu
newtab-section-menu-privacy-notice = Infurmaziuns davart la protecziun da datas

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Reducir la secziun
newtab-section-expand-section-label =
    .aria-label = Expander la secziun

## Section Headers.

newtab-section-header-topsites = Paginas preferidas
newtab-section-header-recent-activity = Activitad recenta
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recumandà da { $provider }
newtab-section-header-stories = Istorgias che dattan da pensar
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Noss tips dad oz per tai

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Cumenza a navigar e nus ta mussain qua artitgels, videos ed autras paginas che ti has visità dacurt u che ti has agiuntà dacurt sco segnapagina.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ussa has ti legì tut las novitads. Turna pli tard per ulteriuras novitads da { $provider }. Na pos betg spetgar? Tscherna in tema popular per chattar ulteriuras istorgias ord il web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Ti has legì tut las novitads. Turna pli tard per leger ulteriurs artitgels da vaglia. Na pos betg spetgar? Tscherna in tema popular per chattar autras bunas istorgias en il web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = I na dat nagut auter.
newtab-discovery-empty-section-topstories-content = Returna pli tard per scuvrir auters artitgels.
newtab-discovery-empty-section-topstories-try-again-button = Reempruvar
newtab-discovery-empty-section-topstories-loading = Chargiar…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oha! Nus avain quasi chargià il cuntegn, ma be quasi.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temas populars:
newtab-pocket-new-topics-title = Vul anc dapli istorgias? Vesair quests temas populars da { -pocket-brand-name }
newtab-pocket-more-recommendations = Dapli propostas
newtab-pocket-learn-more = Ulteriuras infurmaziuns
newtab-pocket-cta-button = Obtegnair { -pocket-brand-name }
newtab-pocket-cta-text = Memorisescha ils artitgels che ta plaschan en { -pocket-brand-name } e procura per inspiraziun cuntinuanta cun lectura fascinanta.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } fa part da la paletta da products { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Memorisar
newtab-pocket-saved = Memorisà

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Dapli da quai
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Na m’interessescha betg
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Grazia. Tes resun ans vegn a gidar a meglierar tes pavel.
newtab-toast-dismiss-button =
    .title = Serrar
    .aria-label = Serrar

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Scuvrir il meglier dal web
newtab-pocket-onboarding-cta = { -pocket-brand-name } intercurescha ina collecziun vasta da publicaziuns per purtar il cuntegn il pli infurmativ, inspirant e fidabel directamain en tes navigatur { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oha, igl è succedì in sbagl cun chargiar il cuntegn.
newtab-error-fallback-refresh-link = Rechargia la pagina per reempruvar.

## Customization Menu

newtab-custom-shortcuts-title = Scursanidas
newtab-custom-shortcuts-subtitle = Websites che ti memoriseschas u visitas
newtab-custom-shortcuts-toggle =
    .label = Scursanidas
    .description = Websites che ti memoriseschas u visitas
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } lingia
       *[other] { $num } lingias
    }
newtab-custom-sponsored-sites = Scursanidas sponsuradas
newtab-custom-pocket-title = Recumandà da { -pocket-brand-name }
newtab-custom-pocket-subtitle = Cuntegn excepziunal, tschernì da { -pocket-brand-name }, in product da { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Istorgias recumandadas
    .description = Cuntegn excepziunal curà da { -brand-product-name }
newtab-custom-pocket-sponsored = Artitgels sponsurads
newtab-custom-pocket-show-recent-saves = Mussar ils elements memorisads dacurt
newtab-custom-recent-title = Activitad recenta
newtab-custom-recent-subtitle = Ina selecziun da websites e cuntegn visità dacurt
newtab-custom-recent-toggle =
    .label = Activitad recenta
    .description = Ina selecziun da websites e cuntegn visità dacurt
newtab-custom-weather-toggle =
    .label = Aura
    .description = La previsiun da l’aura actuala en in’egliada
newtab-custom-close-button = Serrar
newtab-custom-settings = Administrar ulteriurs parameters

## New Tab Wallpapers

newtab-wallpaper-title = Maletgs dal fund davos
newtab-wallpaper-reset = Restaurar il standard
newtab-wallpaper-upload-image = Transferir in maletg
newtab-wallpaper-custom-color = Tscherner ina colur
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Il maletg surpassa la grondezza maximala permessa da { $file_size } MB. Emprova per plaschair da transferir ina datoteca pli pitschna.
newtab-wallpaper-error-file-type = I n’è betg reussì da transferir tia datoteca. Emprova per plaschair anc ina giada cun in auter tip da datoteca.
newtab-wallpaper-light-red-panda = Panda cotschen
newtab-wallpaper-light-mountain = Muntogna alva
newtab-wallpaper-light-sky = Tschiel cun nivels violets e rosas
newtab-wallpaper-light-color = Furmas blauas, rosas e melnas
newtab-wallpaper-light-landscape = Cuntrada da muntognas en tschajera blaua
newtab-wallpaper-light-beach = Splagia cun palma
newtab-wallpaper-dark-aurora = Glisch polara
newtab-wallpaper-dark-color = Furmas cotschnas e blauas
newtab-wallpaper-dark-panda = Panda cotschen zuppà en il guaud
newtab-wallpaper-dark-sky = Cuntrada da citad cun tschiel nocturn
newtab-wallpaper-dark-mountain = Cuntrada da muntognas
newtab-wallpaper-dark-city = Cuntrada da citad violetta
newtab-wallpaper-dark-fox-anniversary = Ina vulp sin la sulada datiers dad in guaud
newtab-wallpaper-light-fox-anniversary = Ina vulp sin in prà en ina cuntrada muntagnarda cun brentina

## Solid Colors

newtab-wallpaper-category-title-colors = Colurs uni
newtab-wallpaper-blue = Blau
newtab-wallpaper-light-blue = Blau cler
newtab-wallpaper-light-purple = Violet cler
newtab-wallpaper-light-green = Verd cler
newtab-wallpaper-green = Verd
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Mellen
newtab-wallpaper-orange = Oransch
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa cler
newtab-wallpaper-red = Cotschen
newtab-wallpaper-dark-blue = Blau stgir
newtab-wallpaper-dark-purple = Violet stgir
newtab-wallpaper-dark-green = Verd stgir
newtab-wallpaper-brown = Brin

## Abstract

newtab-wallpaper-category-title-abstract = Abstract
newtab-wallpaper-abstract-green = Furmas verdas
newtab-wallpaper-abstract-blue = Furmas blauas
newtab-wallpaper-abstract-purple = Furmas violetas
newtab-wallpaper-abstract-orange = Furmas oranschas
newtab-wallpaper-gradient-orange = Dissoluziun dad oransch e rosa
newtab-wallpaper-abstract-blue-purple = Furmas blauas e violetas
newtab-wallpaper-abstract-white-curves = Alv cun curvas nianzadas
newtab-wallpaper-abstract-purple-green = Dissoluziun da glisch violetta e verda
newtab-wallpaper-abstract-blue-purple-waves = Furmas undegiadas blauas e violettas
newtab-wallpaper-abstract-black-waves = Furmas nairas undegiadas

## Celestial

newtab-wallpaper-category-title-photographs = Fotografias
newtab-wallpaper-beach-at-sunrise = Splagia sin il far di
newtab-wallpaper-beach-at-sunset = Splagia sin il far notg
newtab-wallpaper-storm-sky = Tschiel stemprà
newtab-wallpaper-sky-with-pink-clouds = Tschiel cun nivels rosa
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda cotschen che susda sin ina planta
newtab-wallpaper-white-mountains = Muntognas alvas
newtab-wallpaper-hot-air-balloons = Balluns ad aria chauda en colurs assortidas da di
newtab-wallpaper-starry-canyon = Notg blaua stailida
newtab-wallpaper-suspension-bridge = Fotografia d’ina punt pendenta grischa da di
newtab-wallpaper-sand-dunes = Dunas da sablun alvas
newtab-wallpaper-palm-trees = Siluetta da palmas da cocos sin il far notg
newtab-wallpaper-blue-flowers = Flurs cun petals blaus en fluriziun fotografads da datiers
newtab-wallpaper-forest-trees = Plantas en la tschajera
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto da <a data-l10n-name="name-link">{ $author_string }</a> sin <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Emprova cun in zic colur
newtab-wallpaper-feature-highlight-content = Embellescha tes nov tab cun in nov look e maletgs dal fund davos.
newtab-wallpaper-feature-highlight-button = Chapì
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Serrar
    .aria-label = Serrar il pop-up
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Astronomic
newtab-wallpaper-celestial-lunar-eclipse = Stgiradetgna da la glina
newtab-wallpaper-celestial-earth-night = Fotografia nocturna da l’orbit terrester bass
newtab-wallpaper-celestial-starry-sky = Firmament
newtab-wallpaper-celestial-eclipse-time-lapse = Stgiradetgna da la glina a temp accelerà
newtab-wallpaper-celestial-black-hole = Illustraziun dad ina galaxia cun rusna naira
newtab-wallpaper-celestial-river = Maletg da satellit dad in flum

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Mussar la previsiun da l’aura en { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsurà
newtab-weather-menu-change-location = Midar la posiziun
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Tschertgar in lieu
    .aria-label = Tschertgar in lieu
newtab-weather-change-location-search-input = Tschertgar in lieu
newtab-weather-menu-weather-display = Visualisaziun da l’aura
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simpla
newtab-weather-menu-change-weather-display-simple = Midar a la vista simpla
newtab-weather-menu-weather-display-option-detailed = Detagliada
newtab-weather-menu-change-weather-display-detailed = Midar a la vista detagliada
newtab-weather-menu-temperature-units = Unitads da temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Midar a fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Midar a celsius
newtab-weather-menu-hide-weather = Zuppentar l’aura sin ils novs tabs
newtab-weather-menu-learn-more = Ulteriuras infurmaziuns
# This message is shown if user is working offline
newtab-weather-error-not-available = Datas meteorologicas n’èn actualmain betg disponiblas.

## Topic Labels

newtab-topic-label-business = Economia
newtab-topic-label-career = Carriera
newtab-topic-label-education = Furmaziun
newtab-topic-label-arts = Divertiment
newtab-topic-label-food = Nutriment
newtab-topic-label-health = Sanadad
newtab-topic-label-hobbies = Gieus
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Daners
newtab-topic-label-society-parenting = Educaziun
newtab-topic-label-government = Politica
newtab-topic-label-education-science = Scienza
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Life hacks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Tecnologia
newtab-topic-label-travel = Viagiar
newtab-topic-label-home = Chasa e curtin

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Tscherna temas per persunalisar tes feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Tscherna dus u dapli temas. Noss curaturs experts prioriseschan istorgias che correspundan a tes interess. Ils temas pos ti adattar da tut temp.
newtab-topic-selection-save-button = Memorisar
newtab-topic-selection-cancel-button = Interrumper
newtab-topic-selection-button-maybe-later = Forsa pli tard
newtab-topic-selection-privacy-link = Ve a savair co nus protegin e gestin las datas
newtab-topic-selection-button-update-interests = Actualisescha tes interess
newtab-topic-selection-button-pick-interests = Tscherna tes interess

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Suandar
newtab-section-following-button = Ti suondas
newtab-section-unfollow-button = Chalar da suandar

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloccar
newtab-section-blocked-button = Bloccà
newtab-section-unblock-button = Debloccar

## Confirmation modal for blocking a section

newtab-section-cancel-button = Betg ussa
newtab-section-confirm-block-topic-p1 = Vuls ti propi bloccar quest tema?
newtab-section-confirm-block-topic-p2 = Temas bloccads na vegnan betg pli a cumparair en tes feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloccar { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Temas
newtab-section-manage-topics-button-v2 =
    .label = Administrar ils temas
newtab-section-mangage-topics-followed-topics = Suandà
newtab-section-mangage-topics-followed-topics-empty-state = Ti na suondas anc nagins temas.
newtab-section-mangage-topics-blocked-topics = Bloccà
newtab-section-mangage-topics-blocked-topics-empty-state = Ti n’has anc bloccà nagins temas.
newtab-custom-wallpaper-title = Ussa pos ti utilisar funds davos persunalisads
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Transferescha tes agen maletg per il fund davos u tscherna ina colur tenor giavisch per persunalisar tes { -brand-product-name }.
newtab-custom-wallpaper-cta = Empruvar
