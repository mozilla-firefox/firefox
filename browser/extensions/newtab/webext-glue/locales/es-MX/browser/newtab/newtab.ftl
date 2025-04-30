# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nueva pestaña
newtab-settings-button =
    .title = Personaliza tu página de nueva pestaña
newtab-personalize-settings-icon-label =
    .title = Personalizar nueva pestaña
    .aria-label = Ajustes
newtab-settings-dialog-label =
    .aria-label = Ajustes
newtab-personalize-icon-label =
    .title = Personalizar la nueva pestaña
    .aria-label = Personalizar la nueva pestaña
newtab-personalize-dialog-label =
    .aria-label = Personalizar
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Buscar
    .aria-label = Buscar
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Buscar con { $engine } o ingresar dirección
newtab-search-box-handoff-text-no-engine = Buscar o ingresar dirección
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Buscar con { $engine } o ingresar dirección
    .title = Buscar con { $engine } o ingresar dirección
    .aria-label = Buscar con { $engine } o ingresar dirección
newtab-search-box-handoff-input-no-engine =
    .placeholder = Buscar o ingresar dirección
    .title = Buscar o ingresar dirección
    .aria-label = Buscar o ingresar dirección
newtab-search-box-text = Buscar en la web
newtab-search-box-input =
    .placeholder = Buscar en la web
    .aria-label = Buscar en la web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Agregar motor de búsqueda
newtab-topsites-add-shortcut-header = Nuevo acceso directo
newtab-topsites-edit-topsites-header = Editar sitio popular
newtab-topsites-edit-shortcut-header = Editar acceso directo
newtab-topsites-add-shortcut-label = Agregar acceso directo
newtab-topsites-title-label = Título
newtab-topsites-title-input =
    .placeholder = Introducir un título
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Escribir o pegar una URL
newtab-topsites-url-validation = Se requiere una URL válida
newtab-topsites-image-url-label = URL de imagen personalizada
newtab-topsites-use-image-link = Utilizar una imagen personalizada…
newtab-topsites-image-validation = La imagen no se pudo cargar. Intente una URL diferente.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancelar
newtab-topsites-delete-history-button = Eliminar del historial
newtab-topsites-save-button = Guardar
newtab-topsites-preview-button = Vista previa
newtab-topsites-add-button = Agregar

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ¿Estás seguro de que quieres eliminar de tu historial todas las instancias de esta página?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Esta acción no se puede deshacer.

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
    .aria-label = Abrir menú contextual para { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editar este sitio
    .aria-label = Editar este sitio

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editar
newtab-menu-open-new-window = Abrir en una nueva ventana
newtab-menu-open-new-private-window = Abrir en una nueva ventana privada
newtab-menu-dismiss = Descartar
newtab-menu-pin = Anclar
newtab-menu-unpin = Desanclar
newtab-menu-delete-history = Eliminar del historial
newtab-menu-save-to-pocket = Guardar en { -pocket-brand-name }
newtab-menu-delete-pocket = Eliminar de { -pocket-brand-name }
newtab-menu-archive-pocket = Archivar en { -pocket-brand-name }
newtab-menu-show-privacy-info = Nuestros patrocinadores y tu privacidad
newtab-menu-about-fakespot = Acerca de { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloquear
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Dejar de seguir tema

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Hecho
newtab-privacy-modal-button-manage = Administrar los ajustes de contenido patrocinado
newtab-privacy-modal-header = Tu privacidad importa
newtab-privacy-modal-paragraph-2 = Además de ofrecer historias cautivadoras, te mostramos contenido relevante y muy revisado de patrocinadores seleccionados. No te preocupes, <strong>tus datos de navegación jamás dejan una copia personal de { -brand-product-name }</strong> — nosotros los vemos, y tampoco lo hacen nuestros patrocinadores.
newtab-privacy-modal-link = Conoce cómo tu privacidad trabaja en la nueva pestaña

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Eliminar marcador
# Bookmark is a verb here.
newtab-menu-bookmark = Marcador

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar enlace de descarga
newtab-menu-go-to-download-page = Ir a la página de descarga
newtab-menu-remove-download = Eliminar del historial

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mostrar en Finder
       *[other] Abrir carpeta contenedora
    }
newtab-menu-open-file = Abrir archivo

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitados
newtab-label-bookmarked = En marcadores
newtab-label-removed-bookmark = Marcador eliminado
newtab-label-recommended = Tendencias
newtab-label-saved = Guardado en { -pocket-brand-name }
newtab-label-download = Descargado
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Patrocinado
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

newtab-section-menu-remove-section = Eliminar sección
newtab-section-menu-collapse-section = Sección de colapso
newtab-section-menu-expand-section = Ampliar la sección
newtab-section-menu-manage-section = Administrar sección
newtab-section-menu-manage-webext = Gestionar extensión
newtab-section-menu-add-topsite = Agregar sitio popular
newtab-section-menu-add-search-engine = Agregar motor de búsqueda
newtab-section-menu-move-up = Subir
newtab-section-menu-move-down = Bajar
newtab-section-menu-privacy-notice = Política de privacidad

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Contraer sección
newtab-section-expand-section-label =
    .aria-label = Expandir sección

## Section Headers.

newtab-section-header-topsites = Sitios favoritos
newtab-section-header-recent-activity = Actividad reciente
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recomendado por { $provider }
newtab-section-header-stories = Historias que invitan a reflexionar
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Las selecciones de hoy para ti

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Empieza a navegar, y nosotros te mostraremos aquí algunos de los mejores artículos, videos y otras páginas que hayas visitado recientemente o marcado.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ya estás al día. Vuelve luego y busca más historias de { $provider }. ¿No puedes esperar? Selecciona un tema popular y encontrarás más historias interesantes por toda la web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Te has puesto al día. Revisa más tarde para ver más historias. ¿No puedes esperar? Selecciona un tema popular para encontrar más historias de todo el mundo.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ¡Estás al día!
newtab-discovery-empty-section-topstories-content = Vuelve más tarde para más artículos.
newtab-discovery-empty-section-topstories-try-again-button = Intenta de nuevo
newtab-discovery-empty-section-topstories-loading = Cargando...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ¡Ups! Casi cargamos esta sección, pero no pudimos.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temas populares:
newtab-pocket-new-topics-title = ¿Quieres aún más historias? Mira estos temas populares de { -pocket-brand-name }
newtab-pocket-more-recommendations = Más recomendaciones
newtab-pocket-learn-more = Saber más
newtab-pocket-cta-button = Obtener { -pocket-brand-name }
newtab-pocket-cta-text = Guarda las historias que quieras en { -pocket-brand-name } y llena tu mente con fascinantes lecturas.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } es parte de la familia { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Guardar
newtab-pocket-saved = Guardado

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Más como esto
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = No es para mi
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Gracias. Tus comentarios nos ayudarán a mejorar tu feed.
newtab-toast-dismiss-button =
    .title = Ignorar
    .aria-label = Ignorar

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Descubre lo mejor de la web
newtab-pocket-onboarding-cta = { -pocket-brand-name } explora una amplia gama de publicaciones para traer el contenido más informativo, inspirador y de confianza directamente a su navegador { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ups, algo salió mal mientras se cargaba el contenido.
newtab-error-fallback-refresh-link = Actualiza la página e intenta de nuevo.

## Customization Menu

newtab-custom-shortcuts-title = Accesos directos
newtab-custom-shortcuts-subtitle = Sitios que guardas o visitas
newtab-custom-shortcuts-toggle =
    .label = Accesos directos
    .description = Sitios que guardas o visitas
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } fila
       *[other] { $num } filas
    }
newtab-custom-sponsored-sites = Accesos directos patrocinados
newtab-custom-pocket-title = Recomendado por { -pocket-brand-name }
newtab-custom-pocket-subtitle = Contenido excepcional seleccionado por { -pocket-brand-name }, parte de la familia { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Historias recomendadas
    .description = Contenido excepcional seleccionado por la familia { -brand-product-name }
newtab-custom-pocket-sponsored = Historias patrocinadas
newtab-custom-pocket-show-recent-saves = Mostrar guardados recientemente
newtab-custom-recent-title = Actividad reciente
newtab-custom-recent-subtitle = Una selección de sitios y contenidos recientes
newtab-custom-recent-toggle =
    .label = Actividad reciente
    .description = Una selección de sitios y contenidos recientes
newtab-custom-weather-toggle =
    .label = Clima
    .description = El pronóstico estimado para hoy
newtab-custom-close-button = Cerrar
newtab-custom-settings = Administrar más ajustes

## New Tab Wallpapers

newtab-wallpaper-title = Fondos de pantalla
newtab-wallpaper-reset = Restablecer a predeterminados
newtab-wallpaper-upload-image = Subir una imagen
newtab-wallpaper-custom-color = Elige un color
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = La imagen superó el límite de tamaño de archivo de { $file_size } MB. Por favor, intenta cargar un archivo más pequeño.
newtab-wallpaper-error-file-type = No pudimos cargar tu archivo. Por favor, vuelve a intentarlo con un tipo de archivo diferente.
newtab-wallpaper-light-red-panda = Panda rojo
newtab-wallpaper-light-mountain = Montaña Blanca
newtab-wallpaper-light-sky = Cielo con nubes moradas y rosas
newtab-wallpaper-light-color = Formas azules, rosas y amarillas.
newtab-wallpaper-light-landscape = Paisaje de montaña con niebla azulada
newtab-wallpaper-light-beach = Playa con palmera
newtab-wallpaper-dark-aurora = Aurora Boreal
newtab-wallpaper-dark-color = Formas azules y rojas
newtab-wallpaper-dark-panda = Panda rojo escondido en el bosque
newtab-wallpaper-dark-sky = Paisaje citadino con un cielo nocturno
newtab-wallpaper-dark-mountain = Paisaje montañoso
newtab-wallpaper-dark-city = Paisaje de ciudad púrpura
newtab-wallpaper-dark-fox-anniversary = Un zorro en la acera cerca de un bosque
newtab-wallpaper-light-fox-anniversary = Un zorro en un campo de hierba con un paisaje montañoso brumoso

## Solid Colors

newtab-wallpaper-category-title-colors = Colores sólidos
newtab-wallpaper-blue = Azul
newtab-wallpaper-light-blue = Azul claro
newtab-wallpaper-light-purple = Morado claro
newtab-wallpaper-light-green = Verde claro
newtab-wallpaper-green = Verde
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Amarillo
newtab-wallpaper-orange = Naranja
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa claro
newtab-wallpaper-red = Rojo
newtab-wallpaper-dark-blue = Azul oscuro
newtab-wallpaper-dark-purple = Morado oscuro
newtab-wallpaper-dark-green = Verde oscuro
newtab-wallpaper-brown = Café

## Abstract

newtab-wallpaper-category-title-abstract = Abstracto
newtab-wallpaper-abstract-green = Formas verdes
newtab-wallpaper-abstract-blue = Formas azules
newtab-wallpaper-abstract-purple = Formas moradas
newtab-wallpaper-abstract-orange = Formas naranjas
newtab-wallpaper-gradient-orange = Naranja degradado y rosa
newtab-wallpaper-abstract-blue-purple = Formas azules y moradas
newtab-wallpaper-abstract-white-curves = Blanco con curvas sombreadas
newtab-wallpaper-abstract-purple-green = Gradiente de luz violeta y verde
newtab-wallpaper-abstract-blue-purple-waves = Formas onduladas de color azul y morado
newtab-wallpaper-abstract-black-waves = Formas onduladas negras

## Celestial

newtab-wallpaper-category-title-photographs = Fotografías
newtab-wallpaper-beach-at-sunrise = Playa al amanecer
newtab-wallpaper-beach-at-sunset = Playa al atardecer
newtab-wallpaper-storm-sky = Cielo de tormenta
newtab-wallpaper-sky-with-pink-clouds = Cielo con nubes rosadas
newtab-wallpaper-red-panda-yawns-in-a-tree = El panda rojo bosteza en un árbol
newtab-wallpaper-white-mountains = Montañas blancas
newtab-wallpaper-hot-air-balloons = Colores variados de globos aerostáticos durante el día.
newtab-wallpaper-starry-canyon = Noche estrellada azul
newtab-wallpaper-suspension-bridge = Fotografía de un puente colgante gris durante el día
newtab-wallpaper-sand-dunes = Dunas de arena blanca
newtab-wallpaper-palm-trees = Silueta de palmeras de coco durante la hora dorada
newtab-wallpaper-forest-trees = Árboles cubiertos de niebla
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto de <a data-l10n-name="name-link">{ $author_string }</a> en <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Prueba un toque de color
newtab-wallpaper-feature-highlight-content = Dale a tu nueva pestaña una apariencia renovada con fondos de pantalla.
newtab-wallpaper-feature-highlight-button = Lo tengo
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Descartar
    .aria-label = Cerrar popup
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celestial
newtab-wallpaper-celestial-lunar-eclipse = Eclipse lunar
newtab-wallpaper-celestial-earth-night = Fotografía nocturna desde la órbita terrestre baja
newtab-wallpaper-celestial-starry-sky = Cielo estrellado
newtab-wallpaper-celestial-eclipse-time-lapse = Lapso de tiempo del eclipse lunar
newtab-wallpaper-celestial-black-hole = Ilustración de una galaxia con un agujero negro
newtab-wallpaper-celestial-river = Imagen satelital del río

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Mira el pronóstico en { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Patrocinador
newtab-weather-menu-change-location = Cambiar ubicación
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Buscar ubicación
    .aria-label = Buscar ubicación
newtab-weather-change-location-search-input = Buscar ubicación
newtab-weather-menu-weather-display = Mostrar el clima
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simple
newtab-weather-menu-change-weather-display-simple = Cambiar a vista simple
newtab-weather-menu-weather-display-option-detailed = Detallado
newtab-weather-menu-change-weather-display-detailed = Cambiar a vista detallada
newtab-weather-menu-temperature-units = Unidades de temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Cambiar a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Cambiar a Celsius
newtab-weather-menu-hide-weather = Ocultar el clima en la nueva pestaña
newtab-weather-menu-learn-more = Saber más
# This message is shown if user is working offline
newtab-weather-error-not-available = Los datos meteorológicos no están disponibles de momento.

## Topic Labels

newtab-topic-label-business = Negocios
newtab-topic-label-career = Empleo
newtab-topic-label-education = Educación
newtab-topic-label-arts = Entretenimiento
newtab-topic-label-food = Comida
newtab-topic-label-health = Salud
newtab-topic-label-hobbies = Juegos
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Dinero
newtab-topic-label-society-parenting = Paternidad
newtab-topic-label-government = Políticas
newtab-topic-label-education-science = Ciencia
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Trucos para la vida
newtab-topic-label-sports = Deportes
newtab-topic-label-tech = Tecnología
newtab-topic-label-travel = Viajes
newtab-topic-label-home = Hogar y jardín

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Selecciona temas para mejorar tu canal
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Selecciona dos o más temas. Nuestros curadores expertos priorizan las historias adaptadas a tus intereses. Puedes actualizar en cualquier momento.
newtab-topic-selection-save-button = Guardar
newtab-topic-selection-cancel-button = Cancelar
newtab-topic-selection-button-maybe-later = Quizá más tarde
newtab-topic-selection-privacy-link = Conoce cómo protegemos y administramos los datos
newtab-topic-selection-button-update-interests = Actualiza tus intereses
newtab-topic-selection-button-pick-interests = Elige tus intereses

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Seguir
newtab-section-following-button = Siguiendo
newtab-section-unfollow-button = Dejar de seguir

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloquear
newtab-section-blocked-button = Bloqueado
newtab-section-unblock-button = Desbloquear

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ahora no
newtab-section-confirm-block-topic-p1 = ¿Estás seguro de que deseas bloquear este tema?
newtab-section-confirm-block-topic-p2 = Los temas bloqueados ya no aparecerán en tu feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloquear { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Temas
newtab-section-manage-topics-button-v2 =
    .label = Administrar temas
newtab-section-mangage-topics-followed-topics = Seguido
newtab-section-mangage-topics-followed-topics-empty-state = Aún no sigues ningún tema.
newtab-section-mangage-topics-blocked-topics = Bloqueado
newtab-section-mangage-topics-blocked-topics-empty-state = Aún no has bloqueado ningún tema.
