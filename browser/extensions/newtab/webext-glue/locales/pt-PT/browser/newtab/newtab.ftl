# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Novo separador
newtab-settings-button =
    .title = Personalizar a sua página de novo separador
newtab-personalize-settings-icon-label =
    .title = Personalizar o novo separador
    .aria-label = Definições
newtab-settings-dialog-label =
    .aria-label = Definições
newtab-personalize-icon-label =
    .title = Personalizar novo separador
    .aria-label = Personalizar novo separador
newtab-personalize-dialog-label =
    .aria-label = Personalizar
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Pesquisar
    .aria-label = Pesquisar
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Pesquisar com { $engine } ou introduzir endereço
newtab-search-box-handoff-text-no-engine = Pesquisar ou introduzir endereço
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Pesquisar com { $engine } ou introduzir endereço
    .title = Pesquisar com { $engine } ou introduzir endereço
    .aria-label = Pesquisar com { $engine } ou introduzir endereço
newtab-search-box-handoff-input-no-engine =
    .placeholder = Pesquisar ou introduzir endereço
    .title = Pesquisar ou introduzir endereço
    .aria-label = Pesquisar ou introduzir endereço
newtab-search-box-text = Pesquisar na Internet
newtab-search-box-input =
    .placeholder = Pesquisar na Internet
    .aria-label = Pesquisar na Internet

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Adicionar motor de pesquisa
newtab-topsites-add-shortcut-header = Novo atalho
newtab-topsites-edit-topsites-header = Editar site mais visitado
newtab-topsites-edit-shortcut-header = Editar atalho
newtab-topsites-add-shortcut-label = Adicionar atalho
newtab-topsites-title-label = Título
newtab-topsites-title-input =
    .placeholder = Digite um título
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Digite ou cole um URL
newtab-topsites-url-validation = URL válido requerido
newtab-topsites-image-url-label = URL de imagem personalizada
newtab-topsites-use-image-link = Utilizar uma imagem personalizada…
newtab-topsites-image-validation = A imagem falhou o carregamento. Tente um URL diferente.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancelar
newtab-topsites-delete-history-button = Apagar do histórico
newtab-topsites-save-button = Guardar
newtab-topsites-preview-button = Pré-visualizar
newtab-topsites-add-button = Adicionar

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Tem a certeza que pretende eliminar todas as instâncias desta página do seu histórico?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Esta ação não pode ser anulada.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Patrocinado

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Abrir menu
    .aria-label = Abrir menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Remover
    .aria-label = Remover
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Abrir menu
    .aria-label = Abrir menu de contexto para { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Editar este site
    .aria-label = Editar este site

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Editar
newtab-menu-open-new-window = Abrir numa nova janela
newtab-menu-open-new-private-window = Abrir numa nova janela privada
newtab-menu-dismiss = Dispensar
newtab-menu-pin = Afixar
newtab-menu-unpin = Desafixar
newtab-menu-delete-history = Apagar do histórico
newtab-menu-save-to-pocket = Guardar no { -pocket-brand-name }
newtab-menu-delete-pocket = Apagar do { -pocket-brand-name }
newtab-menu-archive-pocket = Arquivar no { -pocket-brand-name }
newtab-menu-show-privacy-info = Os nossos patrocinadores e a sua privacidade
newtab-menu-about-fakespot = Sobre o { -fakespot-brand-name }
newtab-menu-report-content = Reportar este conteúdo
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloquear
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Deixar de seguir tópico

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Gerir conteúdo patrocinado
newtab-menu-our-sponsors-and-your-privacy = Os nossos patrocinadores e a sua privacidade
newtab-menu-report-this-ad = Reportar este anúncio

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Concluído
newtab-privacy-modal-button-manage = Gerir configurações de conteúdo patrocinado
newtab-privacy-modal-header = As sua privacidade é importante.
newtab-privacy-modal-paragraph-2 =
    Para além de encontrar históricas cativantes, também lhe mostramos conteúdo relevante
    e altamente escrutinado a partir de patrocinadores selecionados. Fique descansado que <strong>os seus 
    dados de navegação nunca deixam a sua cópia pessoal do { -brand-product-name }</strong> — nem nós, 
    nem os nossos patrocinadores têm acesso a esses dados.
newtab-privacy-modal-link = Saiba como a privacidade funciona no novo separador

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Remover marcador
# Bookmark is a verb here.
newtab-menu-bookmark = Adicionar aos marcadores

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copiar ligação da transferência
newtab-menu-go-to-download-page = Ir para a página da transferência
newtab-menu-remove-download = Remover do histórico

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mostrar no Finder
       *[other] Abrir pasta de destino
    }
newtab-menu-open-file = Abrir ficheiro

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitados
newtab-label-bookmarked = Adicionados aos marcadores
newtab-label-removed-bookmark = Marcador removido
newtab-label-recommended = Tendência
newtab-label-saved = Guardado no { -pocket-brand-name }
newtab-label-download = Transferido
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

newtab-section-menu-remove-section = Remover secção
newtab-section-menu-collapse-section = Colapsar secção
newtab-section-menu-expand-section = Expandir secção
newtab-section-menu-manage-section = Gerir secção
newtab-section-menu-manage-webext = Gerir extensão
newtab-section-menu-add-topsite = Adicionar site mais visitado
newtab-section-menu-add-search-engine = Adicionar motor de pesquisa
newtab-section-menu-move-up = Mover para cima
newtab-section-menu-move-down = Mover para baixo
newtab-section-menu-privacy-notice = Aviso de privacidade

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Colapsar secção
newtab-section-expand-section-label =
    .aria-label = Expandir secção

## Section Headers.

newtab-section-header-topsites = Sites mais visitados
newtab-section-header-recent-activity = Atividade recente
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recomendado por { $provider }
newtab-section-header-stories = Histórias que fazem pensar
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Escolhas de hoje para si

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Comece a navegar, e iremos mostrar-lhe alguns dos ótimos artigos, vídeos, e outras páginas que visitou recentemente ou adicionou aos marcadores aqui.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Já apanhou tudo. Verifique mais tarde para mais histórias principais de { $provider }. Não pode esperar? Selecione um tópico popular para encontrar mais boas histórias de toda a web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Está em dia. Volte mais tarde para mais histórias. Não pode esperar? Selecione um tópico popular para encontrar mais histórias fantásticas da Internet.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Está em dia!
newtab-discovery-empty-section-topstories-content = Volte mais tarde para mais histórias.
newtab-discovery-empty-section-topstories-try-again-button = Tentar novamente
newtab-discovery-empty-section-topstories-loading = A carregar…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oops! Quase carregámos esta secção, por pouco.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Tópicos populares:
newtab-pocket-new-topics-title = Quer ainda mais histórias? Consulte estes tópicos populares do { -pocket-brand-name }
newtab-pocket-more-recommendations = Mais recomendações
newtab-pocket-learn-more = Saber mais
newtab-pocket-cta-button = Obter o { -pocket-brand-name }
newtab-pocket-cta-text = Guarde as histórias que adora no { -pocket-brand-name }, e abasteça a sua mente com leituras fascinantes.
newtab-pocket-pocket-firefox-family = O { -pocket-brand-name } faz parte da família { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Guardar
newtab-pocket-saved = Guardado

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Mais assim
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Não é para mim
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Obrigado. O seu comentário irá ajudar-nos a melhorar a sua fonte.
newtab-toast-dismiss-button =
    .title = Ignorar
    .aria-label = Ignorar

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Conheça o melhor da Internet
newtab-pocket-onboarding-cta = O { -pocket-brand-name } explora uma ampla gama de publicações para trazer o conteúdo mais informativo, inspirador e confiável, diretamente para o seu navegador { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oops, algo correu mal ao carregar este conteúdo.
newtab-error-fallback-refresh-link = Atualize a página para tentar novamente.

## Customization Menu

newtab-custom-shortcuts-title = Atalhos
newtab-custom-shortcuts-subtitle = Sites que guarda ou visita
newtab-custom-shortcuts-toggle =
    .label = Atalhos
    .description = Sites que guarda ou visita
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } linha
       *[other] { $num } linhas
    }
newtab-custom-sponsored-sites = Atalhos patrocinados
newtab-custom-pocket-title = Recomendado por { -pocket-brand-name }
newtab-custom-pocket-subtitle = Conteúdo excecional com curadoria de { -pocket-brand-name }, parte da família { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Histórias recomendadas
    .description = Conteúdo excepcional com curadoria da família { -brand-product-name }
newtab-custom-pocket-sponsored = Histórias patrocinadas
newtab-custom-pocket-show-recent-saves = Mostrar coisas guardadas recentemente
newtab-custom-recent-title = Atividade recente
newtab-custom-recent-subtitle = Uma seleção de sites e conteúdos recentes
newtab-custom-recent-toggle =
    .label = Atividade recente
    .description = Uma seleção de sites e conteúdos recentes
newtab-custom-weather-toggle =
    .label = Meteorologia
    .description = Visão geral da meteorologia para hoje
newtab-custom-close-button = Fechar
newtab-custom-settings = Gerir mais definições

## New Tab Wallpapers

newtab-wallpaper-title = Fundos
newtab-wallpaper-reset = Repor predefinições
newtab-wallpaper-upload-image = Carregar uma imagem
newtab-wallpaper-custom-color = Escolha uma cor
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = A imagem excedeu o limite de tamanho de ficheiro de { $file_size } MB. Tente enviar um ficheiro mais pequeno.
newtab-wallpaper-error-file-type = Não foi possível carregar o seu ficheiro. Tente novamente com outro tipo de ficheiro diferente.
newtab-wallpaper-light-red-panda = Panda vermelho
newtab-wallpaper-light-mountain = Montanha Branca
newtab-wallpaper-light-sky = Céu com nuvens cor-de-rosa e roxas.
newtab-wallpaper-light-color = Formas azuis, cor-de-rosa e amarelas.
newtab-wallpaper-light-landscape = Paisagem montanhosa envolta em névoa azul.
newtab-wallpaper-light-beach = Praia com uma palmeira
newtab-wallpaper-dark-aurora = Aurora Boreal
newtab-wallpaper-dark-color = Formas em vermelho e azul
newtab-wallpaper-dark-panda = Panda-vermelho escondido na floresta
newtab-wallpaper-dark-sky = Paisagem da cidade com um pôr do sol
newtab-wallpaper-dark-mountain = Paisagem montanhosa
newtab-wallpaper-dark-city = Paisagem urbana em tons de roxo
newtab-wallpaper-dark-fox-anniversary = Uma raposa no passeio junto a uma floresta
newtab-wallpaper-light-fox-anniversary = Uma raposa num campo verdejante com uma paisagem de montanhas envoltas em nevoeiro.

## Solid Colors

newtab-wallpaper-category-title-colors = Cores sólidas
newtab-wallpaper-blue = Azul
newtab-wallpaper-light-blue = Azul claro
newtab-wallpaper-light-purple = Roxo claro
newtab-wallpaper-light-green = Verde claro
newtab-wallpaper-green = Verde
newtab-wallpaper-beige = Bege
newtab-wallpaper-yellow = Amarelo
newtab-wallpaper-orange = Laranja
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa claro
newtab-wallpaper-red = Vermelho
newtab-wallpaper-dark-blue = Azul escuro
newtab-wallpaper-dark-purple = Roxo escuro
newtab-wallpaper-dark-green = Verde escuro
newtab-wallpaper-brown = Castanho

## Abstract

newtab-wallpaper-category-title-abstract = Abstrato
newtab-wallpaper-abstract-green = Formas verdes
newtab-wallpaper-abstract-blue = Formas azuis
newtab-wallpaper-abstract-purple = Formas roxas
newtab-wallpaper-abstract-orange = Formas alaranjadas
newtab-wallpaper-gradient-orange = Gradiente laranja e rosa
newtab-wallpaper-abstract-blue-purple = Formas azuis e roxas
newtab-wallpaper-abstract-white-curves = Branco com curvas sombreadas
newtab-wallpaper-abstract-purple-green = Gradiente roxo e verde claro
newtab-wallpaper-abstract-blue-purple-waves = Formas onduladas azuis e roxas
newtab-wallpaper-abstract-black-waves = Formas onduladas pretas

## Celestial

newtab-wallpaper-category-title-photographs = Fotografias
newtab-wallpaper-beach-at-sunrise = Praia ao nascer do sol
newtab-wallpaper-beach-at-sunset = Praia ao pôr do sol
newtab-wallpaper-storm-sky = Céu tempestuoso
newtab-wallpaper-sky-with-pink-clouds = Céu com nuvens rosa.
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda vermelho boceja numa árvore
newtab-wallpaper-white-mountains = Montanhas brancas
newtab-wallpaper-hot-air-balloons = Cores variadas de balões de ar quente durante o dia
newtab-wallpaper-starry-canyon = Noite estrelada azul
newtab-wallpaper-suspension-bridge = Fotografia de ponte suspensa cinzenta durante o dia
newtab-wallpaper-sand-dunes = Dunas de areia brancas
newtab-wallpaper-palm-trees = Silhueta de coqueiros durante a hora dourada
newtab-wallpaper-blue-flowers = Fotografia em detalhe de flores com pétalas azuis em flor
newtab-wallpaper-forest-trees = Árvores cobertas de nevoeiro
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Fotografia de <a data-l10n-name="name-link">{ $author_string }</a> em <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Experimente um toque de cor
newtab-wallpaper-feature-highlight-content = Dê um novo visual ao seu novo separador com fundos.
newtab-wallpaper-feature-highlight-button = Percebi
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Dispensar
    .aria-label = Fechar popup
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celestial
newtab-wallpaper-celestial-lunar-eclipse = Eclipse lunar
newtab-wallpaper-celestial-earth-night = Fotografia noturna da órbita baixa da Terra
newtab-wallpaper-celestial-starry-sky = Céu estrelado
newtab-wallpaper-celestial-eclipse-time-lapse = Time-lapse de eclipse lunar
newtab-wallpaper-celestial-black-hole = Ilustração de uma galáxia com um buraco negro
newtab-wallpaper-celestial-river = Imagem satélite de rio

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Ver a previsão em { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } · Patrocinado
newtab-weather-menu-change-location = Alterar localização
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Pesquisar localização
    .aria-label = Pesquisar localização
newtab-weather-change-location-search-input = Pesquisar localização
newtab-weather-menu-weather-display = Apresentação da meteorologia
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simples
newtab-weather-menu-change-weather-display-simple = Alterar para a vista simples
newtab-weather-menu-weather-display-option-detailed = Detalhada
newtab-weather-menu-change-weather-display-detailed = Alterar para a vista detalhada
newtab-weather-menu-temperature-units = Unidades de temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Alterar para Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Alterar para Celsius
newtab-weather-menu-hide-weather = Ocultar a meteorologia no novo separador
newtab-weather-menu-learn-more = Saber mais
# This message is shown if user is working offline
newtab-weather-error-not-available = Atualmente não estão disponíveis informações de meteorologia.

## Topic Labels

newtab-topic-label-business = Negócios
newtab-topic-label-career = Carreiras
newtab-topic-label-education = Educação
newtab-topic-label-arts = Entretenimento
newtab-topic-label-food = Comida
newtab-topic-label-health = Saúde
newtab-topic-label-hobbies = Jogos
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Dinheiro
newtab-topic-label-society-parenting = Parentalidade
newtab-topic-label-government = Política
newtab-topic-label-education-science = Ciência
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Dicas
newtab-topic-label-sports = Desporto
newtab-topic-label-tech = Tecnologia
newtab-topic-label-travel = Viagens
newtab-topic-label-home = Casa e jardim

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Escolha os temas para personalizar a seu feed.
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Escolha dois ou mais temas. Os nossos curadores especializados priorizam histórias adaptadas aos seus interesses. Atualize a qualquer momento.
newtab-topic-selection-save-button = Guardar
newtab-topic-selection-cancel-button = Cancelar
newtab-topic-selection-button-maybe-later = Talvez mais tarde
newtab-topic-selection-privacy-link = Descubra como protegemos e gerimos os seus dados
newtab-topic-selection-button-update-interests = Atualize os seus interesses
newtab-topic-selection-button-pick-interests = Escolha os seus interesses

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Seguir
newtab-section-following-button = A seguir
newtab-section-unfollow-button = Deixar de seguir

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloquear
newtab-section-blocked-button = Bloqueado
newtab-section-unblock-button = Desbloquear

## Confirmation modal for blocking a section

newtab-section-cancel-button = Agora não
newtab-section-confirm-block-topic-p1 = Tem a certeza que pretende bloquear este tópico?
newtab-section-confirm-block-topic-p2 = Os tópicos bloqueados deixarão de aparecer no seu feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloquear { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Tópicos
newtab-section-manage-topics-button-v2 =
    .label = Gerir tópicos
newtab-section-mangage-topics-followed-topics = Seguido
newtab-section-mangage-topics-followed-topics-empty-state = Ainda não seguiu quaisquer tópicos.
newtab-section-mangage-topics-blocked-topics = Bloqueado
newtab-section-mangage-topics-blocked-topics-empty-state = Ainda não bloqueou quaisquer tópicos.
newtab-custom-wallpaper-title = Os fundos personalizados estão aqui
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Envie o seu próprio fundo ou escolha uma cor personalizada para tornar o { -brand-product-name } seu.
newtab-custom-wallpaper-cta = Experimentar
