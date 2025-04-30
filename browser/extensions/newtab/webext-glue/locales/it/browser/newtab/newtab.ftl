# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nuova scheda
newtab-settings-button =
    .title = Personalizza la pagina Nuova scheda
newtab-personalize-settings-icon-label =
    .title = Personalizza Nuova scheda
    .aria-label = Impostazioni
newtab-settings-dialog-label =
    .aria-label = Impostazioni
newtab-personalize-icon-label =
    .title = Personalizza Nuova scheda
    .aria-label = Personalizza Nuova scheda
newtab-personalize-dialog-label =
    .aria-label = Personalizza
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Cerca
    .aria-label = Cerca
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Cerca con { $engine } o inserisci un indirizzo
newtab-search-box-handoff-text-no-engine = Cerca o inserisci un indirizzo
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Cerca con { $engine } o inserisci un indirizzo
    .title = Cerca con { $engine } o inserisci un indirizzo
    .aria-label = Cerca con { $engine } o inserisci un indirizzo
newtab-search-box-handoff-input-no-engine =
    .placeholder = Cerca o inserisci un indirizzo
    .title = Cerca o inserisci un indirizzo
    .aria-label = Cerca o inserisci un indirizzo
newtab-search-box-text = Cerca sul Web
newtab-search-box-input =
    .placeholder = Cerca sul Web
    .aria-label = Cerca sul Web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Aggiungi motore di ricerca
newtab-topsites-add-shortcut-header = Nuova scorciatoia
newtab-topsites-edit-topsites-header = Modifica sito principale
newtab-topsites-edit-shortcut-header = Modifica scorciatoia
newtab-topsites-add-shortcut-label = Aggiungi scorciatoia
newtab-topsites-title-label = Titolo
newtab-topsites-title-input =
    .placeholder = Inserire un titolo
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Digitare o incollare un URL
newtab-topsites-url-validation = È necessario fornire un URL valido
newtab-topsites-image-url-label = Indirizzo immagine personalizzata
newtab-topsites-use-image-link = Utilizza un’immagine personalizzata…
newtab-topsites-image-validation = Errore durante il caricamento dell’immagine. Prova con un altro indirizzo.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Annulla
newtab-topsites-delete-history-button = Elimina dalla cronologia
newtab-topsites-save-button = Salva
newtab-topsites-preview-button = Anteprima
newtab-topsites-add-button = Aggiungi

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Eliminare tutte le occorrenze di questa pagina dalla cronologia?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Questa operazione non può essere annullata.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsorizzato

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Apri menu
    .aria-label = Apri menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Rimuovi
    .aria-label = Rimuovi
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Apri menu
    .aria-label = Apri menu contestuale per { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Modifica questo sito
    .aria-label = Modifica questo sito

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Modifica
newtab-menu-open-new-window = Apri in nuova finestra
newtab-menu-open-new-private-window = Apri in nuova finestra anonima
newtab-menu-dismiss = Rimuovi
newtab-menu-pin = Appunta
newtab-menu-unpin = Rilascia
newtab-menu-delete-history = Elimina dalla cronologia
newtab-menu-save-to-pocket = Salva in { -pocket-brand-name }
newtab-menu-delete-pocket = Elimina da { -pocket-brand-name }
newtab-menu-archive-pocket = Archivia in { -pocket-brand-name }
newtab-menu-show-privacy-info = I nostri sponsor e la tua privacy
newtab-menu-about-fakespot = Informazioni su { -fakespot-brand-name }
newtab-menu-report-content = Segnala questo contenuto
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blocca
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Smetti di seguire l’argomento

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Gestisci contenuti sponsorizzati
newtab-menu-our-sponsors-and-your-privacy = I nostri sponsor e la tua privacy
newtab-menu-report-this-ad = Segnala questo annuncio

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Fatto
newtab-privacy-modal-button-manage = Gestisci impostazioni per i contenuti sponsorizzati
newtab-privacy-modal-header = La tua privacy è importante.
newtab-privacy-modal-paragraph-2 =
    Oltre a servirti storie accattivanti, ti mostriamo anche contenuti,
    pertinenti e attentamente curati, promossi da un gruppo selezionato di
    sponsor. Ti garantiamo che <strong>nessun dato relativo alla tua navigazione
    viene condiviso dalla tua copia personale di { -brand-product-name }</strong>.
    Noi non abbiamo accesso a queste informazioni, e tantomeno ce l’hanno i
    nostri sponsor.
newtab-privacy-modal-link = Scopri come funziona la privacy nella pagina Nuova scheda

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Elimina segnalibro
# Bookmark is a verb here.
newtab-menu-bookmark = Aggiungi ai segnalibri

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copia indirizzo di origine
newtab-menu-go-to-download-page = Vai alla pagina di download
newtab-menu-remove-download = Elimina dalla cronologia

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Mostra nel Finder
       *[other] Apri cartella di destinazione
    }
newtab-menu-open-file = Apri file

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visitato
newtab-label-bookmarked = Nei segnalibri
newtab-label-removed-bookmark = Segnalibro eliminato
newtab-label-recommended = Di tendenza
newtab-label-saved = Salvato in { -pocket-brand-name }
newtab-label-download = Scaricata
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsorizzata
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsorizzata da { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Rimuovi sezione
newtab-section-menu-collapse-section = Comprimi sezione
newtab-section-menu-expand-section = Espandi sezione
newtab-section-menu-manage-section = Gestisci sezione
newtab-section-menu-manage-webext = Gestisci estensione
newtab-section-menu-add-topsite = Aggiungi sito principale
newtab-section-menu-add-search-engine = Aggiungi motore di ricerca
newtab-section-menu-move-up = Sposta su
newtab-section-menu-move-down = Sposta giù
newtab-section-menu-privacy-notice = Informativa sulla privacy

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Comprimi sezione
newtab-section-expand-section-label =
    .aria-label = Espandi sezione

## Section Headers.

newtab-section-header-topsites = Siti principali
newtab-section-header-recent-activity = Attività recente
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Consigliati da { $provider }
newtab-section-header-stories = Storie che fanno riflettere
# "picks" refers to recommended articles
newtab-section-header-todays-picks = I consigli di oggi per te

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Inizia a navigare e, in questa sezione, verranno visualizzati articoli, video e altre pagine visitate di recente o aggiunte ai segnalibri.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Non c’è altro. Controlla più tardi per altre storie da { $provider }. Non vuoi aspettare? Seleziona un argomento tra quelli più popolari per scoprire altre notizie interessanti dal Web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Non c’è altro. Controlla più tardi per altre storie. Non vuoi aspettare? Seleziona un argomento tra quelli più popolari per scoprire altre notizie interessanti dal Web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Non c’è altro.
newtab-discovery-empty-section-topstories-content = Controlla più tardi per altre storie.
newtab-discovery-empty-section-topstories-try-again-button = Riprova
newtab-discovery-empty-section-topstories-loading = Caricamento in corso…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oops. Sembra che la sezione non si sia caricata completamente.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Argomenti popolari:
newtab-pocket-new-topics-title = Vuoi ancora più storie? Dai un’occhiata agli argomenti più popolari in { -pocket-brand-name }
newtab-pocket-more-recommendations = Altri suggerimenti
newtab-pocket-learn-more = Ulteriori informazioni
newtab-pocket-cta-button = Ottieni { -pocket-brand-name }
newtab-pocket-cta-text = Salva le storie che ami in { -pocket-brand-name } e nutri la tua mente con letture appassionanti.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } è parte della famiglia { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Salva
newtab-pocket-saved = Salvato

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Più contenuti come questo
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Non mi interessa
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Grazie. Conoscere la tua opinione ci aiuta a migliorare il tuo feed.
newtab-toast-dismiss-button =
    .title = Chiudi
    .aria-label = Chiudi

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Scopri il meglio del Web
newtab-pocket-onboarding-cta = { -pocket-brand-name } esplora un’ampia gamma di pubblicazioni per portare i contenuti più istruttivi, stimolanti e affidabili direttamente nel tuo { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oops, qualcosa è andato storto durante il tentativo di caricare questo contenuto.
newtab-error-fallback-refresh-link = Aggiornare la pagina per riprovare.

## Customization Menu

newtab-custom-shortcuts-title = Scorciatoie
newtab-custom-shortcuts-subtitle = Siti che hai salvato oppure visitato
newtab-custom-shortcuts-toggle =
    .label = Scorciatoie
    .description = Siti che hai salvato oppure visitato
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } riga
       *[other] { $num } righe
    }
newtab-custom-sponsored-sites = Scorciatoie sponsorizzate
newtab-custom-pocket-title = Consigliati da { -pocket-brand-name }
newtab-custom-pocket-subtitle = Contenuti eccezionali a cura di { -pocket-brand-name }, un membro della famiglia { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Storie consigliate
    .description = Contenuti eccezionali curati dalla famiglia di prodotti { -brand-product-name }
newtab-custom-pocket-sponsored = Storie sponsorizzate
newtab-custom-pocket-show-recent-saves = Mostra elementi salvati di recente
newtab-custom-recent-title = Attività recente
newtab-custom-recent-subtitle = Una selezione di siti e contenuti visualizzati di recente
newtab-custom-recent-toggle =
    .label = Attività recente
    .description = Una selezione di siti e contenuti visualizzati di recente
newtab-custom-weather-toggle =
    .label = Meteo
    .description = Panoramica delle previsioni meteo per oggi
newtab-custom-close-button = Chiudi
newtab-custom-settings = Gestisci altre impostazioni

## New Tab Wallpapers

newtab-wallpaper-title = Sfondi
newtab-wallpaper-reset = Ripristina predefinito
newtab-wallpaper-upload-image = Carica immagine
newtab-wallpaper-custom-color = Scegli un colore
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = L’immagine eccede la dimensione massima consentita ({ $file_size } MB). Prova a caricare un file più piccolo.
newtab-wallpaper-error-file-type = Impossibile caricare il file. Riprova con un altro tipo di file.
newtab-wallpaper-light-red-panda = Panda rosso
newtab-wallpaper-light-mountain = Montagna imbiancata
newtab-wallpaper-light-sky = Cielo con nuvole viola e rosa
newtab-wallpaper-light-color = Forme gialle, blu e rosa
newtab-wallpaper-light-landscape = Paesaggio con montagna avvolta da foschia blu
newtab-wallpaper-light-beach = Spiaggia con palma
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Forme rosse e blu
newtab-wallpaper-dark-panda = Panda rosso nascosto in una foresta
newtab-wallpaper-dark-sky = Paesaggio cittadino con cielo notturno
newtab-wallpaper-dark-mountain = Paesaggio con montagne
newtab-wallpaper-dark-city = Paesaggio cittadino con tonalità viola
newtab-wallpaper-dark-fox-anniversary = Una volpe sul marciapiede vicino a una foresta
newtab-wallpaper-light-fox-anniversary = Una volpe in un campo erboso con un paesaggio di montagna avvolto nella nebbia

## Solid Colors

newtab-wallpaper-category-title-colors = Colori solidi
newtab-wallpaper-blue = Blu
newtab-wallpaper-light-blue = Blu chiaro
newtab-wallpaper-light-purple = Viola chiaro
newtab-wallpaper-light-green = Verde chiaro
newtab-wallpaper-green = Verde
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Giallo
newtab-wallpaper-orange = Arancio
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Rosa chiaro
newtab-wallpaper-red = Rosso
newtab-wallpaper-dark-blue = Blu scuro
newtab-wallpaper-dark-purple = Viola scuro
newtab-wallpaper-dark-green = Verde scuro
newtab-wallpaper-brown = Marrone

## Abstract

newtab-wallpaper-category-title-abstract = Astratti
newtab-wallpaper-abstract-green = Forme verdi
newtab-wallpaper-abstract-blue = Forme blu
newtab-wallpaper-abstract-purple = Forme viola
newtab-wallpaper-abstract-orange = Forme arancioni
newtab-wallpaper-gradient-orange = Sfumatura arancione e rosa
newtab-wallpaper-abstract-blue-purple = Forme blu e viola
newtab-wallpaper-abstract-white-curves = Bianco con curve sfumate
newtab-wallpaper-abstract-purple-green = Sfumatura di luce viola e verde
newtab-wallpaper-abstract-blue-purple-waves = Forme ondulate blu e viola
newtab-wallpaper-abstract-black-waves = Forme ondulate nere

## Celestial

newtab-wallpaper-category-title-photographs = Fotografie
newtab-wallpaper-beach-at-sunrise = Spiaggia all’alba
newtab-wallpaper-beach-at-sunset = Spiaggia al tramonto
newtab-wallpaper-storm-sky = Cielo tempestoso
newtab-wallpaper-sky-with-pink-clouds = Cielo con nuvole rosa
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda rosso che sbadiglia su un albero
newtab-wallpaper-white-mountains = Montagne bianche
newtab-wallpaper-hot-air-balloons = Mongolfiere con colori assortiti riprese in pieno giorno
newtab-wallpaper-starry-canyon = Notte stellata blu
newtab-wallpaper-suspension-bridge = Fotografia di un ponte grigio sospeso scattata durante il giorno
newtab-wallpaper-sand-dunes = Dune di sabbia bianca
newtab-wallpaper-palm-trees = Sagome di palme da cocco riprese durante l’ora d’oro
newtab-wallpaper-blue-flowers = Fotografia ravvicinata di fiori con petali blu in fioritura
newtab-wallpaper-forest-trees = Alberi avvolti nella nebbia
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto di <a data-l10n-name="name-link">{ $author_string }</a> da <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Prova un tocco di colore
newtab-wallpaper-feature-highlight-content = Dai un look diverso alle nuove schede con gli sfondi.
newtab-wallpaper-feature-highlight-button = OK
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Chiudi
    .aria-label = Chiudi pop-up
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Spazio
newtab-wallpaper-celestial-lunar-eclipse = Eclissi lunare
newtab-wallpaper-celestial-earth-night = Foto notturna dall’orbita terrestre bassa
newtab-wallpaper-celestial-starry-sky = Cielo stellato
newtab-wallpaper-celestial-eclipse-time-lapse = Time-lapse dell’eclissi lunare
newtab-wallpaper-celestial-black-hole = Illustrazione di una galassia
newtab-wallpaper-celestial-river = Immagine satellitare di un fiume

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Mostre le previsioni meteo in { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsorizzato
newtab-weather-menu-change-location = Modifica località
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Cerca località
    .aria-label = Cerca località
newtab-weather-change-location-search-input = Cerca località
newtab-weather-menu-weather-display = Visualizzazione meteo
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Sintetica
newtab-weather-menu-change-weather-display-simple = Passa alla visualizzazione sintetica
newtab-weather-menu-weather-display-option-detailed = Dettagliata
newtab-weather-menu-change-weather-display-detailed = Passa alla visualizzazione dettagliata
newtab-weather-menu-temperature-units = Unità di temperatura
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Passa a Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Passa a Celsius
newtab-weather-menu-hide-weather = Nascondi meteo in Nuova scheda
newtab-weather-menu-learn-more = Ulteriori informazioni
# This message is shown if user is working offline
newtab-weather-error-not-available = I dati sul meteo non sono al momento disponibili.

## Topic Labels

newtab-topic-label-business = Economia
newtab-topic-label-career = Carriera
newtab-topic-label-education = Educazione
newtab-topic-label-arts = Intrattenimento
newtab-topic-label-food = Alimentazione
newtab-topic-label-health = Salute
newtab-topic-label-hobbies = Videogiochi
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Finanze personali
newtab-topic-label-society-parenting = Educazione dei figli
newtab-topic-label-government = Politica
newtab-topic-label-education-science = Scienza
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Life hacks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Tecnologia
newtab-topic-label-travel = Viaggi
newtab-topic-label-home = Casa e giardino

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Seleziona degli argomenti per personalizzare il tuo feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Scegli due o più argomenti. I nostri esperti curatori daranno priorità alle storie più adatte ai tuoi interessi. Puoi aggiornare le tue preferenze in qualsiasi momento.
newtab-topic-selection-save-button = Salva
newtab-topic-selection-cancel-button = Annulla
newtab-topic-selection-button-maybe-later = Magari più tardi
newtab-topic-selection-privacy-link = Scopri come proteggiamo i tuoi dati e la tua privacy
newtab-topic-selection-button-update-interests = Aggiorna i tuoi interessi
newtab-topic-selection-button-pick-interests = Scegli i tuoi interessi

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Segui
newtab-section-following-button = Stai seguendo
newtab-section-unfollow-button = Smetti di seguire

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blocca
newtab-section-blocked-button = Bloccato
newtab-section-unblock-button = Sblocca

## Confirmation modal for blocking a section

newtab-section-cancel-button = Non adesso
newtab-section-confirm-block-topic-p1 = Bloccare questo argomento?
newtab-section-confirm-block-topic-p2 = Gli argomenti bloccati non verranno più visualizzati nel feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blocca { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Argomenti
newtab-section-manage-topics-button-v2 =
    .label = Gestisci argomenti
newtab-section-mangage-topics-followed-topics = Seguito
newtab-section-mangage-topics-followed-topics-empty-state = Non hai ancora seguito alcun argomento.
newtab-section-mangage-topics-blocked-topics = Bloccato
newtab-section-mangage-topics-blocked-topics-empty-state = Non hai ancora bloccato alcun argomento.
newtab-custom-wallpaper-title = Ora puoi utilizzare sfondi personalizzati
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Carica il tuo sfondo o scegli un colore personalizzato per personalizzare { -brand-product-name }.
newtab-custom-wallpaper-cta = Prova

