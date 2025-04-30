# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Ischeda noa
newtab-settings-button =
    .title = Personaliza sa pàgina de s'ischeda noa
newtab-personalize-settings-icon-label =
    .title = Personaliza s’ischeda noa
    .aria-label = Cunfiguratziones
newtab-settings-dialog-label =
    .aria-label = Cunfiguratzione
newtab-personalize-icon-label =
    .title = Personaliza s'ischeda noa
    .aria-label = Personaliza s'ischeda noa
newtab-personalize-dialog-label =
    .aria-label = Personaliza
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Chirca
    .aria-label = Chirca
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Chirca cun { $engine } o inserta un'indiritzu
newtab-search-box-handoff-text-no-engine = Chirca o inserta un'indiritzu
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Chirca cun { $engine } o inserta un'indiritzu
    .title = Chirca cun { $engine } o inserta un'indiritzu
    .aria-label = Chirca cun { $engine } o inserta un'indiritzu
newtab-search-box-handoff-input-no-engine =
    .placeholder = Chirca o inserta un'indiritzu
    .title = Chirca o inserta un'indiritzu
    .aria-label = Chirca o inserta un'indiritzu
newtab-search-box-text = Chirca in rete
newtab-search-box-input =
    .placeholder = Chirca in rete
    .aria-label = Chirca in rete

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Agiunghe unu motore de chirca
newtab-topsites-add-shortcut-header = Incurtzadura noa
newtab-topsites-edit-topsites-header = Modìfica su situ populare
newtab-topsites-edit-shortcut-header = Modìfica s'incurtzadura
newtab-topsites-add-shortcut-label = Agiunghe curtzadòrgiu
newtab-topsites-title-label = Tìtulu
newtab-topsites-title-input =
    .placeholder = Inserta unu tìtulu
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Iscrie o incolla un'URL
newtab-topsites-url-validation = Ddoe est bisòngiu de un'URL vàlidu
newtab-topsites-image-url-label = URL de s'immàgine personalizada
newtab-topsites-use-image-link = Imprea un'immàgine personalizada…
newtab-topsites-image-validation = Carrigamentu de s'immàgine fallidu. Proa un'URL diferente.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Annulla
newtab-topsites-delete-history-button = Cantzella dae sa cronologia
newtab-topsites-save-button = Sarva
newtab-topsites-preview-button = Previsualizatzione
newtab-topsites-add-button = Agiunghe

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Seguru chi boles cantzellare ònnia istàntzia de custa pàgina dae sa cronologia tua?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Custa atzione no dda podes annullare.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Patrotzinadu

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Aberi su menù
    .aria-label = Aberi su menù
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Boga
    .aria-label = Boga
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Aberi su menù
    .aria-label = Aberi su menù de cuntestu pro { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Modìfica custu situ
    .aria-label = Modìfica custu situ

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Modìfica
newtab-menu-open-new-window = Aberi in una ventana noa
newtab-menu-open-new-private-window = Aberi in una ventana privada noa
newtab-menu-dismiss = Iscarta
newtab-menu-pin = Apica
newtab-menu-unpin = Isbloca
newtab-menu-delete-history = Cantzella dae sa cronologia
newtab-menu-save-to-pocket = Sarva in { -pocket-brand-name }
newtab-menu-delete-pocket = Cantzella dae { -pocket-brand-name }
newtab-menu-archive-pocket = Archìvia in { -pocket-brand-name }
newtab-menu-show-privacy-info = Is patrotzinadores nostros e sa riservadesa tua
newtab-menu-about-fakespot = Informatziones in pitzus de { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloca
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Non sigas prus s’argumentu

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Gesti su cuntenutu patrotzinadu
newtab-menu-our-sponsors-and-your-privacy = Is patrotzinadores nostros e sa riservadesa tua

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Fatu
newtab-privacy-modal-button-manage = Amministra is cunfiguratziones pro is cuntenutos patrotzinados
newtab-privacy-modal-header = Sa riservadesa tua est de importu.
newtab-privacy-modal-paragraph-2 =
    Paris a su de t'ammustrare istòrias geniosas, t'ammustramus fintzas cuntenutos rilevantes e curados in manera primorosa,
    dae is patrotzinadores nostros. Non ti preocupes, <strong>sa cronologia tua non lassat mai sa còpia personale tua de { -brand-product-name }</strong>: no dda bidimus, e no dda bident
    nemmancu is patrotzinadores nostros.
newtab-privacy-modal-link = Impara comente funtzionat sa riservadesa in s'ischeda noa

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Boga·nche su sinnalibru
# Bookmark is a verb here.
newtab-menu-bookmark = Agiunghe a is sinnalibros

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Còpia su ligòngiu de iscarrigamentu
newtab-menu-go-to-download-page = Bae a sa pàgina de iscarrigamentu
newtab-menu-remove-download = Boga dae sa cronologia

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Ammustra in Finder
       *[other] Aberi sa cartella de destinatzione
    }
newtab-menu-open-file = Aberi s'archìviu

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Bisitadu
newtab-label-bookmarked = Agiuntu a sinnalibros
newtab-label-removed-bookmark = Sinnalibru bogadu
newtab-label-recommended = De tendèntzia
newtab-label-saved = Sarvadu in { -pocket-brand-name }
newtab-label-download = Iscarrigadu
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Patrotzinadu
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Patrotzinadu dae { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Boga sa setzione
newtab-section-menu-collapse-section = Mìnima sa setzione
newtab-section-menu-expand-section = Ismànnia sa setzione
newtab-section-menu-manage-section = Gesti sa setzione
newtab-section-menu-manage-webext = Gesti is estensiones
newtab-section-menu-add-topsite = Agiunghe unu situ populare
newtab-section-menu-add-search-engine = Agiunghe unu motore de chirca
newtab-section-menu-move-up = Move in artu
newtab-section-menu-move-down = Move in bàsciu
newtab-section-menu-privacy-notice = Avisu de riservadesa

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Mìnima sa setzione
newtab-section-expand-section-label =
    .aria-label = Ismànnia sa setzione

## Section Headers.

newtab-section-header-topsites = Sitos populares
newtab-section-header-recent-activity = Atividade reghente
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Cussigiados dae { $provider }
newtab-section-header-stories = Istòrias chi faghent pensare
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Is cussìgios de sa die

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Comente cumintzes a navigare, amus a ammustrare inoghe is mègius artìculos, vìdeos, e àteras pàginas chi as bisitadu o agiuntu a is sinnalibros de reghente.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = As giai bidu totu. Torra a chircare prus a tardu àteras istòrias dae { $provider }. Non bides s'ora? Seletziona unu faeddu populare pro agatare àteras istòrias bellas de sa rete.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = As giai bidu totu. Torra a chircare prus a tardu àteras istòrias. Non bides s'ora? Seletziona unu faeddu populare pro agatare àteras istòrias bellas de sa rete.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = As giai bidu totu!
newtab-discovery-empty-section-topstories-content = Controlla prus a tardu si ddoe at a èssere àteras istòrias.
newtab-discovery-empty-section-topstories-try-again-button = Torra a nche proare
newtab-discovery-empty-section-topstories-loading = Carrighende...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ohi! Paret chi sa setzione non si siat carrigada de su totu.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Argumentos populares:
newtab-pocket-new-topics-title = Boles ancora àteras istòrias? Càstia custos faeddos populares de { -pocket-brand-name }
newtab-pocket-more-recommendations = Àteros cussìgios
newtab-pocket-learn-more = Leghe àteru
newtab-pocket-cta-button = Otene { -pocket-brand-name }
newtab-pocket-cta-text = Sarva is istòrias tuas preferidas in { -pocket-brand-name }, e ispàssia·ti cun leturas incantadoras.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } est parte de sa famìlia de { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Sarva
newtab-pocket-saved = Sarvadu

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Àteros cuntenutos comente custu
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Non pro mene
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Gràtzias. Torrende·nos cumprou nos agiudas a megiorare su feed tuo.
newtab-toast-dismiss-button =
    .title = Iscarta
    .aria-label = Iscarta

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Iscoberi su mègius de su web
newtab-pocket-onboarding-cta = { -pocket-brand-name } esplorat una cantidade manna de publicatziones pro ti nde leare su cuntènnidu prus istrutivu, ispiradu e de cunfiare deretu a su navigadore { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ohi, ddoe est istada una faddina in su carrigamentu de custu cuntenutu.
newtab-error-fallback-refresh-link = Agiorna sa pàgina pro torrare a proare.

## Customization Menu

newtab-custom-shortcuts-title = Curtzadòrgios
newtab-custom-shortcuts-subtitle = Sitos chi as sarvadu o bisitadu
newtab-custom-shortcuts-toggle =
    .label = Curtzadòrgios
    .description = Sitos chi as sarvadu o bisitadu
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } riga
       *[other] { $num } rigas
    }
newtab-custom-sponsored-sites = Curtzadòrgios patrotzinados
newtab-custom-pocket-title = Cussigiadu dae { -pocket-brand-name }
newtab-custom-pocket-subtitle = Cuntenutos de primore curados dae { -pocket-brand-name }, parte de sa famìlia de { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Istòrias cussigiadas
    .description = Cuntenutu de primore curadu dae sa famìlia de { -brand-product-name }
newtab-custom-pocket-sponsored = Istòrias patrotzinadas
newtab-custom-pocket-show-recent-saves = Ammustra is elementos sarvados de reghente
newtab-custom-recent-title = Atividade reghente
newtab-custom-recent-subtitle = Una seletzione de sitos e cuntenutos reghentes
newtab-custom-recent-toggle =
    .label = Atividade reghente
    .description = Una seletzione de sitos e cuntenutos reghentes
newtab-custom-weather-toggle =
    .label = Tempus
    .description = Is previsiones de oe in curtzu
newtab-custom-close-button = Serra
newtab-custom-settings = Gesti prus cunfiguratziones

## New Tab Wallpapers

newtab-wallpaper-title = Isfundos de ischermu
newtab-wallpaper-reset = Ripristina comente predefinidu
newtab-wallpaper-upload-image = Càrriga un’immàgine
newtab-wallpaper-custom-color = Sèbera unu colore
newtab-wallpaper-light-red-panda = Panda ruju
newtab-wallpaper-light-mountain = Monte biancu
newtab-wallpaper-light-sky = Chelu cun nues tanadas e colore de rosa
newtab-wallpaper-light-color = Formas biaitas, colore de rosa e grogas
newtab-wallpaper-light-landscape = Paesàgiu de monte cun neulita biaita
newtab-wallpaper-light-beach = Ispiàgia cun palma
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Formas rujas e biaitas
newtab-wallpaper-dark-panda = Panda ruju cuadu in su padente
newtab-wallpaper-dark-sky = Paesàgiu de tzitade cun chelu de note
newtab-wallpaper-dark-mountain = Paesàgiu de monte
newtab-wallpaper-dark-city = Paesàgiu de tzitade tanada

## Solid Colors

newtab-wallpaper-category-title-colors = Colores uniformes
newtab-wallpaper-blue = Asulu
newtab-wallpaper-light-blue = Asulu craru
newtab-wallpaper-light-purple = Tanadu craru
newtab-wallpaper-light-green = Birde craru
newtab-wallpaper-green = Birde
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Grogu
newtab-wallpaper-orange = Arantzu
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa craru
newtab-wallpaper-red = Ruju
newtab-wallpaper-dark-blue = Asulu iscuru
newtab-wallpaper-dark-purple = Tanadu iscuru
newtab-wallpaper-dark-green = Birde iscuru
newtab-wallpaper-brown = Marrone

## Abstract

newtab-wallpaper-category-title-abstract = Astratu
newtab-wallpaper-abstract-green = Formas birdes
newtab-wallpaper-abstract-blue = Formas asulas
newtab-wallpaper-abstract-purple = Formas tanadas
newtab-wallpaper-abstract-orange = Formas arantzu
newtab-wallpaper-gradient-orange = Gradatzione dae arantzu a rosa
newtab-wallpaper-abstract-blue-purple = Formas asulas e tanadas

## Celestial

newtab-wallpaper-category-title-photographs = Fotografias
newtab-wallpaper-beach-at-sunrise = Ispiàgia in s'arbeschidòrgiu
newtab-wallpaper-beach-at-sunset = Ispiàgia in s'iscurigadòrgiu
newtab-wallpaper-storm-sky = Chelu in traschia
newtab-wallpaper-sky-with-pink-clouds = Chelu cun nues rosas
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda ruju cascat a subra de un'àrbore
newtab-wallpaper-white-mountains = Montes biancos
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Fotografia de <a data-l10n-name="name-link">{ $author_string }</a> subra <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Proa un’istrichiddu de colore
newtab-wallpaper-feature-highlight-content = Dona a s'ischeda noa una bisura frisca cun isfundos.
newtab-wallpaper-feature-highlight-button = Apo cumprèndidu
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Serra
    .aria-label = Serra ventanedda
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Càstia is previsiones in { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Isponsorizadu
newtab-weather-menu-change-location = Càmbia positzione
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Chirca positzione
    .aria-label = Chirca positzione
newtab-weather-change-location-search-input = Chirca positzione
newtab-weather-menu-weather-display = Vista de su tempus
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Sèmplitze
newtab-weather-menu-change-weather-display-simple = Passa a sa vista sèmplitze
newtab-weather-menu-weather-display-option-detailed = A sa minuda
newtab-weather-menu-change-weather-display-detailed = Passa a sa vista a sa minuda
newtab-weather-menu-temperature-units = Unidades de temperadura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Passa a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Passa a Celsius
newtab-weather-menu-hide-weather = Cua su tempus in s’ischeda noa
newtab-weather-menu-learn-more = Àteras informatziones
# This message is shown if user is working offline
newtab-weather-error-not-available = Is datos de su tempus non sunt a disponimentu immoe.

## Topic Labels

newtab-topic-label-business = Economia
newtab-topic-label-career = Carriera
newtab-topic-label-education = Formatzione
newtab-topic-label-arts = Ispàssiu
newtab-topic-label-food = Cosa de papare
newtab-topic-label-health = Salude
newtab-topic-label-hobbies = Giogos
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Dinare
newtab-topic-label-government = Polìtica
newtab-topic-label-education-science = Sièntzia
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Cussìgios pro sa vida
newtab-topic-label-sports = Isport
newtab-topic-label-tech = Tecnologia
newtab-topic-label-travel = Biàgios
newtab-topic-label-home = Domo e giardinu

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Seletziona un’argumentu pro personalizare su feed tuo
newtab-topic-selection-save-button = Sarva
newtab-topic-selection-cancel-button = Annulla
newtab-topic-selection-button-maybe-later = Forsis prus tardu
newtab-topic-selection-privacy-link = Iscoberi comente amparamus is datos tuos e sa riservadesa tua
newtab-topic-selection-button-update-interests = Atualiza is interessos tuos
newtab-topic-selection-button-pick-interests = Sèbera is interessos tuos

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sighi
newtab-section-following-button = Sighende
newtab-section-unfollow-button = Non sigas prus

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloca
newtab-section-blocked-button = Blocadu
newtab-section-unblock-button = Isbloca

## Confirmation modal for blocking a section

newtab-section-cancel-button = Immoe nono
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloca { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Argumentos
newtab-section-manage-topics-button-v2 =
    .label = Gesti is argumentos
newtab-section-mangage-topics-followed-topics = Sighidu
newtab-section-mangage-topics-followed-topics-empty-state = Non ses ancora sighende nissunu argumentu
newtab-section-mangage-topics-blocked-topics = Blocadu
newtab-section-mangage-topics-blocked-topics-empty-state = No as ancora blocadu nissunu argumentu
