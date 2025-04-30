# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nova lapela
newtab-settings-button =
    .title = Personalice a páxina de nova lapela
newtab-personalize-icon-label =
    .title = personalizar lapela nova
    .aria-label = personalizar lapela nova
newtab-personalize-dialog-label =
    .aria-label = Personalizar

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Buscar
    .aria-label = Buscar
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Buscar con { $engine } ou escribir enderezo
newtab-search-box-handoff-text-no-engine = Buscar ou introducir enderezo
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Buscar con { $engine } ou introducir enderezo
    .title = Buscar con { $engine } ou introducir enderezo
    .aria-label = Buscar con { $engine } ou introducir enderezo
newtab-search-box-handoff-input-no-engine =
    .placeholder = Buscar ou introducir enderezo
    .title = Buscar ou introducir enderezo
    .aria-label = Buscar ou introducir enderezo
newtab-search-box-text = Buscar na Rede
newtab-search-box-input =
    .placeholder = Buscar na web
    .aria-label = Buscar na web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Engadir buscador
newtab-topsites-add-shortcut-header = Novo atallo
newtab-topsites-edit-topsites-header = Editar sitio favorito
newtab-topsites-edit-shortcut-header = Editar o atallo
newtab-topsites-add-shortcut-label = Engadir atallo
newtab-topsites-title-label = Título
newtab-topsites-title-input =
    .placeholder = Escribir un título
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Escribir ou pegar un URL
newtab-topsites-url-validation = Requírese un URL válido
newtab-topsites-image-url-label = URL da imaxe personalizada
newtab-topsites-use-image-link = Usar unha imaxe personalizada…
newtab-topsites-image-validation = Produciuse un fallo ao cargar a imaxe. Probe un URL diferente.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancelar
newtab-topsites-delete-history-button = Eliminar do historial
newtab-topsites-save-button = Gardar
newtab-topsites-preview-button = Previsualizar
newtab-topsites-add-button = Engadir

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Confirma que quere eliminar do historial todas as instancias desta páxina?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Non é posíbel desfacer esta acción.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Patrocinado

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Abrir menú
    .aria-label = Abrir menú
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Eliminar
    .aria-label = Eliminar
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Abrir menú
    .aria-label = Abrir o menú contextual para { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editar este sitio
    .aria-label = Editar este sitio

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editar
newtab-menu-open-new-window = Abrir nunha nova xanela
newtab-menu-open-new-private-window = Abrir nunha nova xanela privada
newtab-menu-dismiss = Rexeitar
newtab-menu-pin = Fixar
newtab-menu-unpin = Quitar
newtab-menu-delete-history = Eliminar do historial
newtab-menu-save-to-pocket = Gardar en { -pocket-brand-name }
newtab-menu-delete-pocket = Eliminar do { -pocket-brand-name }
newtab-menu-archive-pocket = Arquivar no { -pocket-brand-name }
newtab-menu-show-privacy-info = Os nosos patrocinadores e a súa privacidade

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Feito
newtab-privacy-modal-button-manage = Xestionar a configuración de contido patrocinado
newtab-privacy-modal-header = A súa privacidade importa.
newtab-privacy-modal-paragraph-2 =
    Ademais de servir historias engaiolantes, tamén lle mostramos
    contido relevante, e inspeccionado antes, de patrocinadores selectos. Teña a certeza de que <strong> a súa navegación
    os datos nunca deixan a súa copia persoal de { -brand-product-name } </strong>: non a vemos nós e os nosos
    patrocinadores tampouco a ven.
newtab-privacy-modal-link = Aprender como funciona a privacidade na nova lapela

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Retirar marcador
# Bookmark is a verb here.
newtab-menu-bookmark = Engadir aos marcadores

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar ligazón de descarga
newtab-menu-go-to-download-page = Ir á páxina de descargas
newtab-menu-remove-download = Retirar do historial

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Amosar no Finder
       *[other] Abrir o cartafol que o contén
    }
newtab-menu-open-file = Abrir o ficheiro

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitados
newtab-label-bookmarked = Nos marcadores
newtab-label-removed-bookmark = Retirouse o marcador
newtab-label-recommended = Tendencias
newtab-label-saved = Gardado no { -pocket-brand-name }
newtab-label-download = Descargado
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource }· Patrocinado
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Patrocinado por { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Retirar sección
newtab-section-menu-collapse-section = Contraer sección
newtab-section-menu-expand-section = Expandir sección
newtab-section-menu-manage-section = Xestionar sección
newtab-section-menu-manage-webext = Xestionar extensión
newtab-section-menu-add-topsite = Engadir sitio favorito
newtab-section-menu-add-search-engine = Engadir buscador
newtab-section-menu-move-up = Subir
newtab-section-menu-move-down = Baixar
newtab-section-menu-privacy-notice = Política de privacidade

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Contraer sección
newtab-section-expand-section-label =
    .aria-label = Expandir sección

## Section Headers.

newtab-section-header-topsites = Sitios favoritos
newtab-section-header-recent-activity = Actividade recente
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recomendado por { $provider }
newtab-section-header-stories = Historias para reflexionar

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Comece a navegar e aquí amosarémoslle algúns dos mellores artigos, vídeos e outras páxinas que visitara recentemente ou que engadira aos marcadores.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Xa está ao día. Regrese máis tarde para ver máis historias de { $provider }. Non pode agardar? Seleccione un tema popular e atopará máis historias interesantes da web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Está atrapado!
newtab-discovery-empty-section-topstories-content = Volva máis tarde para ver máis historias.
newtab-discovery-empty-section-topstories-try-again-button = Intentar de novo
newtab-discovery-empty-section-topstories-loading = Cargando…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Carafio! Case cargamos esta sección, pero non de todo.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temas populares:
newtab-pocket-new-topics-title = Quere máis historias? Vexa estes temas populares de { -pocket-brand-name }
newtab-pocket-more-recommendations = Máis recomendacións
newtab-pocket-learn-more = Máis información
newtab-pocket-cta-button = Obter { -pocket-brand-name }
newtab-pocket-cta-text = Garde no { -pocket-brand-name } as historias que lle gusten, e alimente a súa imaxinación con lecturas fascinantes.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } forma parte da familia { -brand-product-name }
newtab-pocket-save = Gardar
newtab-pocket-saved = Gardado

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Descubrir o mellor da web
newtab-pocket-onboarding-cta = { -pocket-brand-name } explora unha ampla gama de publicacións para achegar o contido máis informativo, inspirador e fiable ao seu navegador { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Vaites, produciuse un erro ao cargar este contido.
newtab-error-fallback-refresh-link = Actualice a páxina para tentalo de novo.

## Customization Menu

newtab-custom-shortcuts-title = Atallos
newtab-custom-shortcuts-subtitle = Sitios gardados ou visitados
newtab-custom-shortcuts-toggle =
    .label = Atallos
    .description = Sitios gardados ou visitados
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } fila
       *[other] { $num } filas
    }
newtab-custom-sponsored-sites = Atallos patrocinados
newtab-custom-pocket-title = Recomendado por { -pocket-brand-name }
newtab-custom-pocket-subtitle = Contido excepcional patrocinado por { -pocket-brand-name }, parte da familia { -brand-product-name }
newtab-custom-pocket-sponsored = Historias patrocinadas
newtab-custom-pocket-show-recent-saves = Mostrar o gardado recentemente
newtab-custom-recent-title = Actividade recente
newtab-custom-recent-subtitle = Unha selección de sitios e contido recentes
newtab-custom-recent-toggle =
    .label = Actividade recente
    .description = Unha selección de sitios e contido recentes
newtab-custom-close-button = Pechar
newtab-custom-settings = Xestionar máis axustes

## New Tab Wallpapers

newtab-wallpaper-title = Fondos de pantalla
newtab-wallpaper-reset = Restablecer os valores predeterminados
newtab-wallpaper-light-red-panda = Panda vermello
newtab-wallpaper-light-mountain = Montaña branca

## Solid Colors

newtab-wallpaper-category-title-colors = Cores sólidas
newtab-wallpaper-blue = Azul
newtab-wallpaper-light-blue = Azul claro
newtab-wallpaper-green = Verde
newtab-wallpaper-yellow = Amarelo
newtab-wallpaper-orange = Laranxa
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa claro
newtab-wallpaper-red = Vermello
newtab-wallpaper-dark-blue = Azul escuro
newtab-wallpaper-dark-green = Verde escuro

## Abstract


## Celestial

newtab-wallpaper-feature-highlight-button = Entendido

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Ver a previsión en { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Patrocinado
newtab-weather-menu-change-location = Cambiar localización
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Busca localización
    .aria-label = Busca localización
newtab-weather-change-location-search-input = Busca localización
newtab-weather-menu-weather-display = Pantalla do tempo
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simple
newtab-weather-menu-change-weather-display-simple = Cambia á vista simple
newtab-weather-menu-weather-display-option-detailed = Detallado
newtab-weather-menu-change-weather-display-detailed = Cambia á vista detallada
newtab-weather-menu-temperature-units = Unidades de temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Cambiar a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Cambiar a Celsius
newtab-weather-menu-hide-weather = Ocultar o tempo na nova pestana
newtab-weather-menu-learn-more = Máis información
# This message is shown if user is working offline
newtab-weather-error-not-available = Os datos meteorolóxicos non están dispoñibles neste momento.

## Topic Labels

newtab-topic-label-business = Negocios
newtab-topic-label-career = Emprego
newtab-topic-label-education = Educación
newtab-topic-label-arts = Entretemento
newtab-topic-label-food = Comida
newtab-topic-label-health = Saúde
newtab-topic-label-hobbies = Xogos
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Diñeiro
newtab-topic-label-society-parenting = Paternidade
newtab-topic-label-government = Política
newtab-topic-label-education-science = Ciencia
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Trucos para a vida
newtab-topic-label-sports = Deportes
newtab-topic-label-tech = Tecnoloxía
newtab-topic-label-travel = Viaxes
newtab-topic-label-home = Casa e xardín

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Seleccionar temas para afinar o seu feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Escolla dous ou máis temas. Os nosos comisarios expertos priorizan historias adaptadas aos seus intereses. Actualice en calquera momento.
newtab-topic-selection-save-button = Gardar
newtab-topic-selection-cancel-button = Cancelar
newtab-topic-selection-button-maybe-later = Quizais máis tarde
newtab-topic-selection-privacy-link = Aprender como protexemos e xestionamos os datos
newtab-topic-selection-button-update-interests = Actualizar os seus intereses
newtab-topic-selection-button-pick-interests = Seleccionar os seus intereses

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

