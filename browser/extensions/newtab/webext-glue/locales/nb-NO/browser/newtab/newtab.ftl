# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Ny fane
newtab-settings-button =
    .title = Tilpass siden for Ny fane
newtab-personalize-settings-icon-label =
    .title = Tilpass ny fane
    .aria-label = Innstillinger
newtab-settings-dialog-label =
    .aria-label = Innstillinger
newtab-personalize-icon-label =
    .title = Tilpass ny fane-side
    .aria-label = Tilpass ny fane-side
newtab-personalize-dialog-label =
    .aria-label = Tilpass
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Søk
    .aria-label = Søk
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Søk med { $engine } eller skriv inn adresse
newtab-search-box-handoff-text-no-engine = Søk eller skriv inn adresse
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Søk med { $engine } eller skriv inn adresse
    .title = Søk med { $engine } eller skriv inn adresse
    .aria-label = Søk med { $engine } eller skriv inn adresse
newtab-search-box-handoff-input-no-engine =
    .placeholder = Søk eller skriv inn adresse
    .title = Søk eller skriv inn adresse
    .aria-label = Søk eller skriv inn adresse
newtab-search-box-text = Søk på nettet
newtab-search-box-input =
    .placeholder = Søk på nettet
    .aria-label = Søk på nettet

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Legg til søkemotor
newtab-topsites-add-shortcut-header = Ny snarvei
newtab-topsites-edit-topsites-header = Rediger toppsted
newtab-topsites-edit-shortcut-header = Rediger snarvei
newtab-topsites-add-shortcut-label = Legg til snarvei
newtab-topsites-title-label = Tittel
newtab-topsites-title-input =
    .placeholder = Oppgi en tittel
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Skriv eller lim inn en URL
newtab-topsites-url-validation = Gyldig URL er nødvendig
newtab-topsites-image-url-label = Egendefinert bilde-URL
newtab-topsites-use-image-link = Bruk et egendefinert bilde…
newtab-topsites-image-validation = Kunne ikke lese inn bildet. Prøv en annen URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Avbryt
newtab-topsites-delete-history-button = Slett fra historikk
newtab-topsites-save-button = Lagre
newtab-topsites-preview-button = Forhåndsvis
newtab-topsites-add-button = Legg til

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Er du sikker på at du vil slette alle forekomster av denne siden fra historikken?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Denne handlingen kan ikke angres.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponset

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Åpne meny
    .aria-label = Åpne meny
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Fjern
    .aria-label = Fjern
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Åpne meny
    .aria-label = Åpne kontekstmeny for { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Rediger denne nettsiden
    .aria-label = Rediger denne nettsiden

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Rediger
newtab-menu-open-new-window = Åpne i nytt vindu
newtab-menu-open-new-private-window = Åpne i nytt privat vindu
newtab-menu-dismiss = Avslå
newtab-menu-pin = Fest
newtab-menu-unpin = Løsne
newtab-menu-delete-history = Slett fra historikk
newtab-menu-save-to-pocket = Lagre til { -pocket-brand-name }
newtab-menu-delete-pocket = Slett fra { -pocket-brand-name }
newtab-menu-archive-pocket = Arkiver i { -pocket-brand-name }
newtab-menu-show-privacy-info = Våre sponsorer og ditt personvern
newtab-menu-about-fakespot = Om { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokker
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Slutt å følge emnet

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Ferdig
newtab-privacy-modal-button-manage = Behandle innstillinger for sponset innhold
newtab-privacy-modal-header = Personvernet ditt er viktig.
newtab-privacy-modal-paragraph-2 =
    I tillegg til å servere fengslende historier, viser vi deg også relevant og
    høyt kontrollert innhold fra utvalgte sponsorer. Du kan være sikker på, <strong>at dine surfedata
    aldri forlater ditt personlige eksemplar av  { -brand-product-name }</strong> — vi ser dem ikke, og sponsorerene våre ser dem ikke heller.
newtab-privacy-modal-link = Les mer om hvordan personvernet fungerer på den nye fanen

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Fjern bokmerke
# Bookmark is a verb here.
newtab-menu-bookmark = Bokmerke

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopier nedlastingslenke
newtab-menu-go-to-download-page = Gå til nedlastingssiden
newtab-menu-remove-download = Fjern fra historikk

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Vis i Finder
       *[other] Åpne mappen med filen
    }
newtab-menu-open-file = Åpne fil

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Besøkt
newtab-label-bookmarked = Bokmerket
newtab-label-removed-bookmark = Bokmerke fjernet
newtab-label-recommended = Trender
newtab-label-saved = Lagret til { -pocket-brand-name }
newtab-label-download = Lastet ned
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponset
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponset av { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Fjern seksjon
newtab-section-menu-collapse-section = Slå sammen seksjon
newtab-section-menu-expand-section = Utvid seksjon
newtab-section-menu-manage-section = Håndter seksjon
newtab-section-menu-manage-webext = Behandle utvidelse
newtab-section-menu-add-topsite = Legg til toppsted
newtab-section-menu-add-search-engine = Legg til søkemotor
newtab-section-menu-move-up = Flytt opp
newtab-section-menu-move-down = Flytt ned
newtab-section-menu-privacy-notice = Personvernbestemmelser

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Slå sammen seksjon
newtab-section-expand-section-label =
    .aria-label = Utvid seksjon

## Section Headers.

newtab-section-header-topsites = Mest besøkte nettsteder
newtab-section-header-recent-activity = Nylig aktivitet
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Anbefalt av { $provider }
newtab-section-header-stories = Tankevekkende artikler
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dagens utvalgte artikler for deg

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Begynn å surfe, og vi viser noen av de beste artiklene, videoer og andre sider du nylig har besøkt eller bokmerket her.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Du har tatt igjen. Kom tilbake senere for flere topphistorier fra { $provider }. Kan du ikke vente? Velg et populært emne for å finne flere gode artikler fra hele Internett.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Du har nå lest alt. Kom tilbake senere for flere artikler. Kan du ikke vente? Velg et populært emne for å finne flere flotte artikler fra nettet.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Du har lest alt!
newtab-discovery-empty-section-topstories-content = Kom tilbake senere for flere artikler.
newtab-discovery-empty-section-topstories-try-again-button = Prøv igjen
newtab-discovery-empty-section-topstories-loading = Laster…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ops! Vi lastet nesten denne delen, men ikke helt.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populære emner:
newtab-pocket-new-topics-title = Vil du ha enda flere artikler? Se disse populære emnene fra { -pocket-brand-name }
newtab-pocket-more-recommendations = Flere anbefalinger
newtab-pocket-learn-more = Les mer
newtab-pocket-cta-button = Hent { -pocket-brand-name }
newtab-pocket-cta-text = Lagre artiklene du synes er interessante i { -pocket-brand-name }, og stimuler dine tanker med fasinerende lesermateriell.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } er en del av { -brand-product-name }-familien.
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Lagre
newtab-pocket-saved = Lagret

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Mer som dette
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ikke for meg
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Takk. Tilbakemeldingen din vil hjelpe oss med å forbedre kilden din.
newtab-toast-dismiss-button =
    .title = Lukk
    .aria-label = Lukk

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Oppdag det beste fra nettet
newtab-pocket-onboarding-cta = { -pocket-brand-name } utforsker et mangfold av publikasjoner for å få det mest informative, inspirerende og pålitelige innholdet rett til { -brand-product-name }-nettleseren din.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ups, noe gikk galt når innholdet skulle lastes inn.
newtab-error-fallback-refresh-link = Oppdater siden for å prøve igjen.

## Customization Menu

newtab-custom-shortcuts-title = Snarveier
newtab-custom-shortcuts-subtitle = Nettsteder du lagrer eller besøker
newtab-custom-shortcuts-toggle =
    .label = Snarveier
    .description = Nettsteder du lagrer eller besøker
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rad
       *[other] { $num } rader
    }
newtab-custom-sponsored-sites = Sponsede snarveier
newtab-custom-pocket-title = Anbefalt av { -pocket-brand-name }
newtab-custom-pocket-subtitle = Eksepsjonelt innhold satt sammen av { -pocket-brand-name }, en del av { -brand-product-name }-familien
newtab-custom-stories-toggle =
    .label = Anbefalte artikler
    .description = Enestående innhold kuratert av { -brand-product-name }-familien
newtab-custom-pocket-sponsored = Sponsede historier
newtab-custom-pocket-show-recent-saves = Se sist lagrede
newtab-custom-recent-title = Nylig aktivitet
newtab-custom-recent-subtitle = Et utvalg av nylige nettsteder og innhold
newtab-custom-recent-toggle =
    .label = Nylig aktivitet
    .description = Et utvalg av nylige nettsteder og innhold
newtab-custom-weather-toggle =
    .label = Vær
    .description = Dagens værmelding i korte trekk
newtab-custom-close-button = Lukk
newtab-custom-settings = Behandle flere innstillinger

## New Tab Wallpapers

newtab-wallpaper-title = Bakgrunnsbilder
newtab-wallpaper-reset = Tilbakestill til standard
newtab-wallpaper-upload-image = Last opp et bilde
newtab-wallpaper-custom-color = Velg en farge
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Bildet overskred filstørrelsesgrensen på { $file_size } MB. Prøv å laste opp en mindre fil.
newtab-wallpaper-error-file-type = Vi kunne ikke laste opp filen din. Prøv igjen med en annen filtype.
newtab-wallpaper-light-red-panda = Rødpanda
newtab-wallpaper-light-mountain = Hvitt fjell
newtab-wallpaper-light-sky = Himmel med lilla og rosa skyer
newtab-wallpaper-light-color = Blå, rosa og gule former
newtab-wallpaper-light-landscape = Blå tåke fjellandskap
newtab-wallpaper-light-beach = Strand med palmetre
newtab-wallpaper-dark-aurora = Nordlys
newtab-wallpaper-dark-color = Røde og blå former
newtab-wallpaper-dark-panda = Rødpanda gjemt i skogen
newtab-wallpaper-dark-sky = Bylandskap med nattehimmel
newtab-wallpaper-dark-mountain = Landskap fjell
newtab-wallpaper-dark-city = Lilla bylandskap
newtab-wallpaper-dark-fox-anniversary = En rev på fortauet nær en skog
newtab-wallpaper-light-fox-anniversary = En rev i en gressmark med et tåkete fjellandskap

## Solid Colors

newtab-wallpaper-category-title-colors = Solide farger
newtab-wallpaper-blue = Blå
newtab-wallpaper-light-blue = Lyseblå
newtab-wallpaper-light-purple = Lyselilla
newtab-wallpaper-light-green = Lysegrønn
newtab-wallpaper-green = Grønn
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Gul
newtab-wallpaper-orange = Oransje
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Lyserosa
newtab-wallpaper-red = Rød
newtab-wallpaper-dark-blue = Mørkeblå
newtab-wallpaper-dark-purple = Mørkelilla
newtab-wallpaper-dark-green = Mørkegrønn
newtab-wallpaper-brown = Brun

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakt
newtab-wallpaper-abstract-green = Grønne former
newtab-wallpaper-abstract-blue = Blåe former
newtab-wallpaper-abstract-purple = Lilla former
newtab-wallpaper-abstract-orange = Oransje former
newtab-wallpaper-gradient-orange = Fargeovergang oransje og rosa
newtab-wallpaper-abstract-blue-purple = Blå og lilla former
newtab-wallpaper-abstract-white-curves = Hvit med skraverte kurver
newtab-wallpaper-abstract-purple-green = Fargeovergang med lilla og grønt lys

## Celestial

newtab-wallpaper-category-title-photographs = Fotografier
newtab-wallpaper-beach-at-sunrise = Strand ved soloppgang
newtab-wallpaper-beach-at-sunset = Strand ved solnedgang
newtab-wallpaper-storm-sky = Stormhimmel
newtab-wallpaper-sky-with-pink-clouds = Himmel med rosa skyer
newtab-wallpaper-red-panda-yawns-in-a-tree = Rød panda som gjesper i et tre
newtab-wallpaper-white-mountains = Hvite fjell
newtab-wallpaper-starry-canyon = Blå stjerneklar natt
newtab-wallpaper-sand-dunes = Hvite sanddyner
newtab-wallpaper-palm-trees = Silhuett av kokospalmer under den gyldne timen
newtab-wallpaper-forest-trees = Trær dekket med tåke
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Bilde av <a data-l10n-name="name-link">{ $author_string }</a> på <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Prøv en fargeklatt
newtab-wallpaper-feature-highlight-content = Gi ny fane-siden et friskt utseende med bakgrunnsbilder.
newtab-wallpaper-feature-highlight-button = Jeg forstår
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Lukk
    .aria-label = Lukk sprettoppvindu
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Rommet
newtab-wallpaper-celestial-lunar-eclipse = Måneformørkelse
newtab-wallpaper-celestial-earth-night = Nattbilde fra lav jordbane
newtab-wallpaper-celestial-starry-sky = Stjernehimmel
newtab-wallpaper-celestial-river = Satellittbilde av elv

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Se værmelding hos { $provider }.
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponset
newtab-weather-menu-change-location = Endre plassering
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Søk plassering
    .aria-label = Søk plassering
newtab-weather-change-location-search-input = Søk plassering
newtab-weather-menu-weather-display = Værvisning
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Enkel
newtab-weather-menu-change-weather-display-simple = Bytt til enkel visning
newtab-weather-menu-weather-display-option-detailed = Detaljert
newtab-weather-menu-change-weather-display-detailed = Bytt til detaljert visning
newtab-weather-menu-temperature-units = Temperaturenheter
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Bytt til Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Bytt til Celsius
newtab-weather-menu-hide-weather = Skjul været på ny fane
newtab-weather-menu-learn-more = Les mer
# This message is shown if user is working offline
newtab-weather-error-not-available = Værdata er ikke tilgjengelig akkurat nå.

## Topic Labels

newtab-topic-label-business = Forretning
newtab-topic-label-career = Karriere
newtab-topic-label-education = Utdannelse
newtab-topic-label-arts = Underholdning
newtab-topic-label-food = Mat
newtab-topic-label-health = Helse
newtab-topic-label-hobbies = Spill
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Penger
newtab-topic-label-society-parenting = Foreldreskap
newtab-topic-label-government = Politikk
newtab-topic-label-education-science = Vitenskap
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Life hacks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Teknologi
newtab-topic-label-travel = Reise
newtab-topic-label-home = Hjem og hage

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Velg emner for å finjustere kilden din
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Velg to eller flere emner. Våre ekspertkuratorer prioriterer artikler tilpasset dine interesser. Oppdater når som helst.
newtab-topic-selection-save-button = Lagre
newtab-topic-selection-cancel-button = Avbryt
newtab-topic-selection-button-maybe-later = Kanskje senere
newtab-topic-selection-privacy-link = Finn ut hvordan vi beskytter og behandler data
newtab-topic-selection-button-update-interests = Oppdater dine interesser
newtab-topic-selection-button-pick-interests = Velg dine interesser

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Følge
newtab-section-following-button = Følger
newtab-section-unfollow-button = Slutt å følge

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokker
newtab-section-blocked-button = Blokkert
newtab-section-unblock-button = Opphev blokkering

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ikke nå
newtab-section-confirm-block-topic-p1 = Er du sikker på at du vil blokkere dette emnet?
newtab-section-confirm-block-topic-p2 = Blokkerte emner vil ikke lenger vises i kilden din.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokker { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Emner
newtab-section-manage-topics-button-v2 =
    .label = Behandle emner
newtab-section-mangage-topics-followed-topics = Fulgt
newtab-section-mangage-topics-followed-topics-empty-state = Du har ikke fulgt noen emner ennå.
newtab-section-mangage-topics-blocked-topics = Blokkert
newtab-section-mangage-topics-blocked-topics-empty-state = Du har ikke blokkert noen emner ennå.
