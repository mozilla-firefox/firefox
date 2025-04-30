# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nueva pestaña
newtab-settings-button =
    .title = Personalizar la página Nueva pestaña
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
newtab-search-box-handoff-text = Buscar con { $engine } o introducir una dirección
newtab-search-box-handoff-text-no-engine = Buscar o escribir dirección
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Buscar con { $engine } o introducir una dirección
    .title = Buscar con { $engine } o introducir una dirección
    .aria-label = Buscar con { $engine } o introducir una dirección
newtab-search-box-handoff-input-no-engine =
    .placeholder = Buscar o escribir dirección
    .title = Buscar o escribir dirección
    .aria-label = Buscar o escribir dirección
newtab-search-box-text = Buscar en la web
newtab-search-box-input =
    .placeholder = Buscar en la web
    .aria-label = Buscar en la web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Añadir motor de búsqueda
newtab-topsites-add-shortcut-header = Nuevo acceso directo
newtab-topsites-edit-topsites-header = Editar sitio popular
newtab-topsites-edit-shortcut-header = Editar acceso directo
newtab-topsites-add-shortcut-label = Añadir acceso directo
newtab-topsites-title-label = Título
newtab-topsites-title-input =
    .placeholder = Introducir título
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Escribir o pegar una URL
newtab-topsites-url-validation = Se requiere una URL válida
newtab-topsites-image-url-label = URL de imagen personalizada
newtab-topsites-use-image-link = Utilizar una imagen personalizada…
newtab-topsites-image-validation = No se ha podido cargar la imagen. Pruebe con una URL diferente.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancelar
newtab-topsites-delete-history-button = Eliminar del historial
newtab-topsites-save-button = Guardar
newtab-topsites-preview-button = Vista preliminar
newtab-topsites-add-button = Agregar

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ¿Está seguro de que quiere eliminar de su historial todas las instancias de esta página?
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
    .aria-label = Abrir menú de contexto para { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editar este sitio
    .aria-label = Editar este sitio

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editar
newtab-menu-open-new-window = Abrir en una nueva ventana
newtab-menu-open-new-private-window = Abrir en una nueva ventana privada
newtab-menu-dismiss = Ignorar
newtab-menu-pin = Fijar
newtab-menu-unpin = Soltar
newtab-menu-delete-history = Eliminar del historial
newtab-menu-save-to-pocket = Guardar en { -pocket-brand-name }
newtab-menu-delete-pocket = Eliminar de { -pocket-brand-name }
newtab-menu-archive-pocket = Archivar en { -pocket-brand-name }
newtab-menu-show-privacy-info = Nuestros patrocinadores y su privacidad
newtab-menu-about-fakespot = Acerca de { -fakespot-brand-name }
newtab-menu-report-content = Informar sobre este contenido
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloquear
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Dejar de seguir el tema

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Administrar contenido patrocinado
newtab-menu-our-sponsors-and-your-privacy = Nuestros patrocinadores y su privacidad
newtab-menu-report-this-ad = Informar sobre este anuncio

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Hecho
newtab-privacy-modal-button-manage = Administrar la configuración de contenido patrocinado
newtab-privacy-modal-header = Su privacidad es importante.
newtab-privacy-modal-paragraph-2 = Además de ofrecer historias cautivadoras, también le mostramos contenido relevante y altamente revisado de patrocinadores seleccionados. Tenga la seguridad de que, <strong>sus datos de navegación nunca dejan su copia personal de { -brand-product-name }</strong>: nosotros no los vemos y nuestros patrocinadores tampoco.
newtab-privacy-modal-link = Conozca cómo funciona la privacidad en la nueva pestaña

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Eliminar marcador
# Bookmark is a verb here.
newtab-menu-bookmark = Marcador

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar el enlace de descarga
newtab-menu-go-to-download-page = Ir a la página de descarga
newtab-menu-remove-download = Eliminar del historial

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mostrar en Finder
       *[other] Abrir la carpeta que lo contiene
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
newtab-section-menu-collapse-section = Contraer sección
newtab-section-menu-expand-section = Expandir sección
newtab-section-menu-manage-section = Gestionar sección
newtab-section-menu-manage-webext = Gestionar extensión
newtab-section-menu-add-topsite = Añadir sitio popular
newtab-section-menu-add-search-engine = Añadir motor de búsqueda
newtab-section-menu-move-up = Subir
newtab-section-menu-move-down = Bajar
newtab-section-menu-privacy-notice = Aviso de privacidad

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
newtab-section-header-stories = Historias que hacen reflexionar
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Las selecciones de hoy para usted

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Empiece a navegar y nosotros le mostraremos aquí algunos de los mejores artículos, videos y otras páginas que haya visitado recientemente o agregado a marcadores.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ya está al día. Vuelva luego y busque más historias de { $provider }. ¿No puede esperar? Seleccione un tema popular y encontrará más historias alucinantes por toda la web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Ya se ha puesto al día. Vuelva más tarde para más historias. ¿No puede esperar? Seleccione un tema popular para encontrar más historias de todo el mundo.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ¡Está al día!
newtab-discovery-empty-section-topstories-content = Vuelva más tarde para obtener más historias.
newtab-discovery-empty-section-topstories-try-again-button = Volver a intentarlo
newtab-discovery-empty-section-topstories-loading = Cargando...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ¡Ups! Casi se carga la sección al completo.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Temas populares:
newtab-pocket-new-topics-title = ¿Quiere aún más historias? Vea estos temas populares de { -pocket-brand-name }
newtab-pocket-more-recommendations = Más recomendaciones
newtab-pocket-learn-more = Saber más
newtab-pocket-cta-button = Obtener { -pocket-brand-name }
newtab-pocket-cta-text = Guarde en { -pocket-brand-name } las historias que le gustan y alimente su mente con lecturas fascinantes.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } es parte de la familia { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Guardar
newtab-pocket-saved = Guardado

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Más contenido como éste
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = No es para mí
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Gracias Su opinión nos ayudará a mejorar su canal.
newtab-toast-dismiss-button =
    .title = Descartar
    .aria-label = Descartar

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Descubrir lo mejor de la web
newtab-pocket-onboarding-cta = { -pocket-brand-name } explora una amplia gama de publicaciones para traer el contenido más informativo, inspirador y de confianza directamente a su navegador { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Lo sentimos, algo salió mal al cargar el contenido.
newtab-error-fallback-refresh-link = Recargue la página e inténtelo de nuevo.

## Customization Menu

newtab-custom-shortcuts-title = Accesos directos
newtab-custom-shortcuts-subtitle = Sitios que guarda o visita
newtab-custom-shortcuts-toggle =
    .label = Accesos directos
    .description = Sitios que guarda o visita
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
    .label = El tiempo
    .description = El pronóstico de hoy de un vistazo
newtab-custom-close-button = Cerrar
newtab-custom-settings = Administrar más ajustes

## New Tab Wallpapers

newtab-wallpaper-title = Fondos de pantalla
newtab-wallpaper-reset = Restaurar valores predeterminados
newtab-wallpaper-upload-image = Subir una imagen
newtab-wallpaper-custom-color = Elegir un color
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = La imagen supera el límite de tamaño de archivo de { $file_size } MB. Intente cargar un archivo más pequeño.
newtab-wallpaper-error-file-type = No pudimos cargar su archivo. Vuelva a intentarlo con un tipo de archivo diferente.
newtab-wallpaper-light-red-panda = Panda rojo
newtab-wallpaper-light-mountain = Montaña blanca
newtab-wallpaper-light-sky = Cielo con nubes moradas y rosadas
newtab-wallpaper-light-color = Formas azules, rosas y amarillas
newtab-wallpaper-light-landscape = Paisaje de montaña con neblina azul
newtab-wallpaper-light-beach = Playa con palmera
newtab-wallpaper-dark-aurora = Aurora boreal
newtab-wallpaper-dark-color = Sombras rojas y azules
newtab-wallpaper-dark-panda = Panda rojo escondido en el bosque
newtab-wallpaper-dark-sky = Paisaje de ciudad con cielo nocturno
newtab-wallpaper-dark-mountain = Paisaje de montaña
newtab-wallpaper-dark-city = Paisaje de ciudad púrpura
newtab-wallpaper-dark-fox-anniversary = Un zorro en la acera cerca de un bosque
newtab-wallpaper-light-fox-anniversary = Un zorro en un campo de hierba con un paisaje montañoso brumoso

## Solid Colors

newtab-wallpaper-category-title-colors = Colores sólidos
newtab-wallpaper-blue = Azul
newtab-wallpaper-light-blue = Azul claro
newtab-wallpaper-light-purple = Púrpura claro
newtab-wallpaper-light-green = Verde claro
newtab-wallpaper-green = Verde
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Amarillo
newtab-wallpaper-orange = Naranja
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa claro
newtab-wallpaper-red = Rojo
newtab-wallpaper-dark-blue = Azul oscuro
newtab-wallpaper-dark-purple = Púrpura oscuro
newtab-wallpaper-dark-green = Verde oscuro
newtab-wallpaper-brown = Marrón

## Abstract

newtab-wallpaper-category-title-abstract = Abstracto
newtab-wallpaper-abstract-green = Formas verdes
newtab-wallpaper-abstract-blue = Formas azules
newtab-wallpaper-abstract-purple = Formas púrpuras
newtab-wallpaper-abstract-orange = Formas naranjas
newtab-wallpaper-gradient-orange = Degradado naranja y rosa
newtab-wallpaper-abstract-blue-purple = Formas azules y púrpuras
newtab-wallpaper-abstract-white-curves = Blanco con curvas sombreadas
newtab-wallpaper-abstract-purple-green = Gradiente de luz púrpura y verde
newtab-wallpaper-abstract-blue-purple-waves = Formas onduladas azules y púrpuras
newtab-wallpaper-abstract-black-waves = Formas onduladas negras

## Celestial

newtab-wallpaper-category-title-photographs = Fotografías
newtab-wallpaper-beach-at-sunrise = Playa al amanecer
newtab-wallpaper-beach-at-sunset = Playa al atardecer
newtab-wallpaper-storm-sky = Cielo tormentoso
newtab-wallpaper-sky-with-pink-clouds = Cielo con nubes rosas
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda rojo bosteza en un árbol
newtab-wallpaper-white-mountains = Montañas blancas
newtab-wallpaper-hot-air-balloons = Colores variados de globos aerostáticos durante el día.
newtab-wallpaper-starry-canyon = Noche estrellada azul
newtab-wallpaper-suspension-bridge = Fotografía gris de un puente colgante durante el día
newtab-wallpaper-sand-dunes = Dunas de arena blanca
newtab-wallpaper-palm-trees = Silueta de palmeras durante la hora dorada
newtab-wallpaper-blue-flowers = Fotografía de primer plano de flores de pétalos azules
newtab-wallpaper-forest-trees = Árboles cubiertos de niebla
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto de <a data-l10n-name="name-link">{ $author_string }</a> en <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Pruebe un toque de color
newtab-wallpaper-feature-highlight-content = Dele a su nueva pestaña una apariencia renovada con fondos de pantalla.
newtab-wallpaper-feature-highlight-button = Entendido
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Descartar
    .aria-label = Cerrar ventana emergente
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celestial
newtab-wallpaper-celestial-lunar-eclipse = Eclipse lunar
newtab-wallpaper-celestial-earth-night = Foto nocturna desde la órbita baja de la tierra
newtab-wallpaper-celestial-starry-sky = Cielo estrellado
newtab-wallpaper-celestial-eclipse-time-lapse = Time-lapse de un eclipse lunar
newtab-wallpaper-celestial-black-hole = Ilustración de una galaxia con un agujero negro
newtab-wallpaper-celestial-river = Imagen de satelite de un rio

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Ver pronóstico en { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Patrocinado
newtab-weather-menu-change-location = Cambiar ubicación
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Buscar ubicación
    .aria-label = Buscar ubicación
newtab-weather-change-location-search-input = Buscar ubicación
newtab-weather-menu-weather-display = Visualización del tiempo
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simple
newtab-weather-menu-change-weather-display-simple = Cambiar a vista simple
newtab-weather-menu-weather-display-option-detailed = Detallada
newtab-weather-menu-change-weather-display-detailed = Cambiar a vista detallada
newtab-weather-menu-temperature-units = Unidades de temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Cambiar a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Cambiar a Celsius
newtab-weather-menu-hide-weather = Ocultar el tiempo en Nueva pestaña
newtab-weather-menu-learn-more = Saber más
# This message is shown if user is working offline
newtab-weather-error-not-available = Los datos meteorológicos no están disponibles en este momento.

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
newtab-topic-label-government = Política
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
newtab-topic-selection-title = Seleccione temas para ajustar su feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Elija dos o más temas. Nuestros expertos priorizan las historias adaptadas a sus intereses. Puede actualizarlo en cualquier momento.
newtab-topic-selection-save-button = Guardar
newtab-topic-selection-cancel-button = Cancelar
newtab-topic-selection-button-maybe-later = Quizá más tarde
newtab-topic-selection-privacy-link = Descubra cómo protegemos y gestionamos los datos
newtab-topic-selection-button-update-interests = Actualice sus intereses
newtab-topic-selection-button-pick-interests = Elija sus intereses

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
newtab-section-confirm-block-topic-p1 = ¿Está seguro de querer bloquear este tema?
newtab-section-confirm-block-topic-p2 = Los temas bloqueados ya no aparecerán en los canales.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloquear { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Temas
newtab-section-manage-topics-button-v2 =
    .label = Administrar temas
newtab-section-mangage-topics-followed-topics = Seguido
newtab-section-mangage-topics-followed-topics-empty-state = Todavía no sigue ningún tema.
newtab-section-mangage-topics-blocked-topics = Bloqueado
newtab-section-mangage-topics-blocked-topics-empty-state = Todavía no ha bloqueado ningún tema.
newtab-custom-wallpaper-title = Los fondos de pantalla personalizados están aquí
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Suba su propio fondo de pantalla o elija un color personalizado para personalizar su { -brand-product-name }.
newtab-custom-wallpaper-cta = Inténtelo
