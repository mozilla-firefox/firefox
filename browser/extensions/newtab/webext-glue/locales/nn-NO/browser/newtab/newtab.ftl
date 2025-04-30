# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Ny fane
newtab-settings-button =
    .title = Tilpass sida for Ny fane
newtab-personalize-settings-icon-label =
    .title = Tilpass ny fane
    .aria-label = Innstillingar
newtab-settings-dialog-label =
    .aria-label = Innstillingar
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
newtab-search-box-handoff-text = Søk med { $engine } eller skriv inn ei adresse
newtab-search-box-handoff-text-no-engine = Søk eller skriv inn ei adresse
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Søk med { $engine } eller skriv inn ei adresse
    .title = Søk med { $engine } eller skriv inn ei adresse
    .aria-label = Søk med { $engine } eller skriv inn ei adresse
newtab-search-box-handoff-input-no-engine =
    .placeholder = Søk eller skriv inn ei adresse
    .title = Søk eller skriv inn ei adresse
    .aria-label = Søk eller skriv inn ei adresse
newtab-search-box-text = Søk på nettet
newtab-search-box-input =
    .placeholder = Søk på nettet
    .aria-label = Søk på nettet

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Legg til søkjemotor
newtab-topsites-add-shortcut-header = Ny snarveg
newtab-topsites-edit-topsites-header = Rediger Mest besøkt
newtab-topsites-edit-shortcut-header = Rediger snarveg
newtab-topsites-add-shortcut-label = Legg til snarveg
newtab-topsites-title-label = Tittel
newtab-topsites-title-input =
    .placeholder = Skriv inn ein tittel
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Skriv eller lim inn ein URL
newtab-topsites-url-validation = Gyldig URL er påkravd
newtab-topsites-image-url-label = Tilpassa bilde-URL
newtab-topsites-use-image-link = Bruk eit tilpassa bilde…
newtab-topsites-image-validation = Klarte ikkje å lesa bildet. Prøv ein annan URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Avbryt
newtab-topsites-delete-history-button = Slett frå historikk
newtab-topsites-save-button = Lagre
newtab-topsites-preview-button = Førehandsvis
newtab-topsites-add-button = Legg til

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Er du sikker på at du vil slette alle førekomstar av denne sida frå historikken din?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Denne handlinga kan ikkje angrast.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsa

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Opne meny
    .aria-label = Opne meny
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Fjern
    .aria-label = Fjern
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Opne meny
    .aria-label = Opne kontekstmeny for { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Rediger denne nettsida
    .aria-label = Rediger denne nettsida

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Rediger
newtab-menu-open-new-window = Opne i nytt vindauge
newtab-menu-open-new-private-window = Opne i eit nytt privat vindauge
newtab-menu-dismiss = Avvis
newtab-menu-pin = Fest
newtab-menu-unpin = Løys
newtab-menu-delete-history = Slett frå historikk
newtab-menu-save-to-pocket = Lagre til { -pocket-brand-name }
newtab-menu-delete-pocket = Slett frå { -pocket-brand-name }
newtab-menu-archive-pocket = Arkiver i { -pocket-brand-name }
newtab-menu-show-privacy-info = Våre sponsorar og ditt personvern
newtab-menu-about-fakespot = Om { -fakespot-brand-name }
newtab-menu-report-content = Rapporter dette innhaldet
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokker
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Slutt å følgje emnet

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Handsam sponsa innhald
newtab-menu-our-sponsors-and-your-privacy = Sponsorane våre og ditt personvern
newtab-menu-report-this-ad = Rapporter denne annonsen

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Ferdig
newtab-privacy-modal-button-manage = Handsam innstillingar for sponsa innhald
newtab-privacy-modal-header = Personvernet ditt er viktig.
newtab-privacy-modal-paragraph-2 =
    I tillegg til å servere fengslande historier, viser vi deg også relevant og
    høgt kontrollert innhald frå utvalde sponsorar. Du kan vere sikker på, <strong>at surfedata dine
    aldri forlèt det personlege eksemplaret ditt av  { -brand-product-name }</strong> — vi ser dei ikkje, og sponsorane våre ser dei ikkje heller.
newtab-privacy-modal-link = Lær deg korleis personvernet fungerer på den nye fana

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Fjern bokmerke
# Bookmark is a verb here.
newtab-menu-bookmark = Bokmerke

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopier nedlastingslenke
newtab-menu-go-to-download-page = Gå til nedlastingsside
newtab-menu-remove-download = Fjern frå historikk

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Vis i Finder
       *[other] Opne innhaldsmappe
    }
newtab-menu-open-file = Opne fil

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Besøkt
newtab-label-bookmarked = Bokmerkte
newtab-label-removed-bookmark = Bokmerke fjerna
newtab-label-recommended = Trendar
newtab-label-saved = Lagra til { -pocket-brand-name }
newtab-label-download = Nedlasta
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsa
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsa av { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Fjern seksjon
newtab-section-menu-collapse-section = Slå saman seksjon
newtab-section-menu-expand-section = Utvid seksjon
newtab-section-menu-manage-section = Handsam seksjon
newtab-section-menu-manage-webext = Handsam utviding
newtab-section-menu-add-topsite = Legg til mest besøkte
newtab-section-menu-add-search-engine = Legg til søkjemotor
newtab-section-menu-move-up = Flytt opp
newtab-section-menu-move-down = Flytt ned
newtab-section-menu-privacy-notice = Personvernpraksis

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Slå saman seksjon
newtab-section-expand-section-label =
    .aria-label = Utvid seksjon

## Section Headers.

newtab-section-header-topsites = Mest besøkte nettstadar
newtab-section-header-recent-activity = Nyleg aktivitet
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Tilrådd av { $provider }
newtab-section-header-stories = Tankevekkjande artiklar
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dagens utvalde artiklar for deg

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Begynn å surfe, og vi vil vise deg nokre av dei beste artiklane, videoane og andre sider du nyleg har besøkt eller bokmerka her.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Det finst ikkje fleire. Kom tilbake seinare for fleire topphistoriar frå { $provider }. Kan du ikkje vente? Vel eit populært emne for å finne fleire gode artiklar frå heile nettet.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Du har no lest alt. Kom tilbake seinare for fleire artiklar. Kan du ikkje vente? Vel eit populært emne for å finne fleire flotte artiklar frå nettet.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Du har lest alt!
newtab-discovery-empty-section-topstories-content = Kom tilbake seinare for fleire artiklar.
newtab-discovery-empty-section-topstories-try-again-button = Prøv på nytt
newtab-discovery-empty-section-topstories-loading = Lastar…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ops! Vi lasta nesten denne delen, men ikkje heilt.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populære emne:
newtab-pocket-new-topics-title = Vil du ha endå fleire artiklar? Sjå desse populære emna frå { -pocket-brand-name }
newtab-pocket-more-recommendations = Fleire tilrådingar
newtab-pocket-learn-more = Les meir
newtab-pocket-cta-button = Last ned { -pocket-brand-name }
newtab-pocket-cta-text = Lagre artiklane du synest er interessante i { -pocket-brand-name }, og stimuler tankane dine med fasinerande lesemateriell.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } er ein del av { -brand-product-name }-familien.
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Lagre
newtab-pocket-saved = Lagra

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Meir som dette
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ikkje for meg
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Takk. Tilbakemeldinga di  vil hjelpe oss med å gjere kjelda di betre.
newtab-toast-dismiss-button =
    .title = Avvis
    .aria-label = Avvis

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Oppdag det beste på nettet
newtab-pocket-onboarding-cta = { -pocket-brand-name } utforskar ei mengde ulike publikasjonar for å få det mest informative, inspirerande og pålitelege innhaldet direkte til { -brand-product-name }-nettlesaren din.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ops, noko gjekk gale då innhaldet skulle lastast inn.
newtab-error-fallback-refresh-link = Oppdater sida for å prøve på nytt.

## Customization Menu

newtab-custom-shortcuts-title = Snarvegar
newtab-custom-shortcuts-subtitle = Nettstadar du lagrar eller besøkjer
newtab-custom-shortcuts-toggle =
    .label = Snarvegar
    .description = Nettstadar du lagrar eller besøkjer
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rad
       *[other] { $num } rader
    }
newtab-custom-sponsored-sites = Sponsa snarvegar
newtab-custom-pocket-title = Tilrådd av { -pocket-brand-name }
newtab-custom-pocket-subtitle = Eksepsjonelt innhald sett saman av { -pocket-brand-name }, ein del av { -brand-product-name }-familien
newtab-custom-stories-toggle =
    .label = Tilrådde artiklar
    .description = Eineståande innhald utvalt av { -brand-product-name } familien
newtab-custom-pocket-sponsored = Sponsa historier
newtab-custom-pocket-show-recent-saves = Vis siste lagra
newtab-custom-recent-title = Nyleg aktivitet
newtab-custom-recent-subtitle = Eit utval av nylege nettstadar og innhald
newtab-custom-recent-toggle =
    .label = Nyleg aktivitet
    .description = Eit utval av nylege nettstadar og innhald
newtab-custom-weather-toggle =
    .label = Vêr
    .description = Dagens vêrmelding i korte trekk
newtab-custom-close-button = Lat att
newtab-custom-settings = Handsam fleire innstillingar

## New Tab Wallpapers

newtab-wallpaper-title = Bakgrunnsbilde
newtab-wallpaper-reset = Still tilbake til standard
newtab-wallpaper-upload-image = Last opp eit bilde
newtab-wallpaper-custom-color = Vel ein farge
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Bildet overskreid filstorleiksgrensa på { $file_size }MB. Prøv å laste opp ei mindre fil.
newtab-wallpaper-light-red-panda = Raudpanda
newtab-wallpaper-light-mountain = Kvitt fjell
newtab-wallpaper-light-sky = Himmel med lilla og rosa skyer
newtab-wallpaper-light-color = Blå, rosa og gule former
newtab-wallpaper-light-landscape = Fjellandskap med blå tåke
newtab-wallpaper-light-beach = Strand med palmetre
newtab-wallpaper-dark-aurora = Nordlys
newtab-wallpaper-dark-color = Raude og blå former
newtab-wallpaper-dark-panda = Raudpanda gøymt i skogen
newtab-wallpaper-dark-sky = Bylandskap med nattehimmel
newtab-wallpaper-dark-mountain = Fjellandskap
newtab-wallpaper-dark-city = Lilla bylandskap
newtab-wallpaper-dark-fox-anniversary = Ein rev på fortauet nær ein skog
newtab-wallpaper-light-fox-anniversary = Ein rev i ei graskledd mark med eit tåkete fjellandskap

## Solid Colors

newtab-wallpaper-category-title-colors = Einsfarga
newtab-wallpaper-blue = Blå
newtab-wallpaper-light-blue = Lyseblå
newtab-wallpaper-light-purple = Lyselilla
newtab-wallpaper-light-green = Lysegrøn
newtab-wallpaper-green = Grøn
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Gul
newtab-wallpaper-orange = Oransje
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Lyserosa
newtab-wallpaper-red = Raud
newtab-wallpaper-dark-blue = Mørkeblå
newtab-wallpaper-dark-purple = Mørkelilla
newtab-wallpaper-dark-green = Mørkegrøn
newtab-wallpaper-brown = Brun

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakt
newtab-wallpaper-abstract-green = Grøne former
newtab-wallpaper-abstract-blue = Blåe former
newtab-wallpaper-abstract-purple = Lilla former
newtab-wallpaper-abstract-orange = Oransje former
newtab-wallpaper-gradient-orange = Fargeovergang oransje og rosa
newtab-wallpaper-abstract-blue-purple = Blå og lilla former
newtab-wallpaper-abstract-white-curves = Kvit med skraverte kurver
newtab-wallpaper-abstract-purple-green = Fargeovergang med lilla og grønt lys
newtab-wallpaper-abstract-blue-purple-waves = Blå og lilla bølgjeformer
newtab-wallpaper-abstract-black-waves = Svarte bølgjeformer

## Celestial

newtab-wallpaper-category-title-photographs = Fotografi
newtab-wallpaper-beach-at-sunrise = Strand ved soloppgang
newtab-wallpaper-beach-at-sunset = Strand ved solnedgang
newtab-wallpaper-storm-sky = Stormhimmel
newtab-wallpaper-sky-with-pink-clouds = Himmel med rosa skyer
newtab-wallpaper-red-panda-yawns-in-a-tree = Raud panda som geispar i eit tre
newtab-wallpaper-white-mountains = Kvite fjell
newtab-wallpaper-hot-air-balloons = Fargespel av luftballongar på dagtid
newtab-wallpaper-starry-canyon = Blå stjerneklar kveld
newtab-wallpaper-suspension-bridge = Foto av grå hengebru på dagtid
newtab-wallpaper-sand-dunes = Kvite sanddyner
newtab-wallpaper-palm-trees = Silhuett av kokospalmar i den gylne timen
newtab-wallpaper-blue-flowers = Nærbiletfotografering av blåblada blomstrar i bløming
newtab-wallpaper-forest-trees = Tre dekte av tåke
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Bilde av <a data-l10n-name="name-link">{ $author_string }</a> på <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Prøv ein fargeklatt
newtab-wallpaper-feature-highlight-content = Gje ny fane-sida ein friskt utsjånad med bakgrunnsbilde.
newtab-wallpaper-feature-highlight-button = Eg forstår
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Lat att
    .aria-label = Lat att sprettoppvindauge
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Verdsrommet
newtab-wallpaper-celestial-lunar-eclipse = Måneformørking
newtab-wallpaper-celestial-earth-night = Nattfoto frå låg bane rundt jorda
newtab-wallpaper-celestial-starry-sky = Stjerneklar himmel
newtab-wallpaper-celestial-eclipse-time-lapse = Intervallfoto av måneformørking
newtab-wallpaper-celestial-black-hole = Illustrasjon av galakse med svart hol
newtab-wallpaper-celestial-river = Satellittbilde av elv

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Sjå vêrmelding hos { $provider }.
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsa
newtab-weather-menu-change-location = Endre plassering
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Søk plassering
    .aria-label = Søk plassering
newtab-weather-change-location-search-input = Søk plassering
newtab-weather-menu-weather-display = Vêrvising
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Enkel
newtab-weather-menu-change-weather-display-simple = Byt til enkel vising
newtab-weather-menu-weather-display-option-detailed = Detaljert
newtab-weather-menu-change-weather-display-detailed = Byt til detaljert vising
newtab-weather-menu-temperature-units = Temperatureiningar
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Byt til Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Byt til Celsius
newtab-weather-menu-hide-weather = Skjul vêret på ny fane
newtab-weather-menu-learn-more = Les meir
# This message is shown if user is working offline
newtab-weather-error-not-available = Vêrdata er ikkje tilgjengeleg akkurat no.

## Topic Labels

newtab-topic-label-business = Business
newtab-topic-label-career = Karriere
newtab-topic-label-education = Utdanning
newtab-topic-label-arts = Underhaldning
newtab-topic-label-food = Mat
newtab-topic-label-health = Helse
newtab-topic-label-hobbies = Spel
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Pengar
newtab-topic-label-society-parenting = Foreldreskap
newtab-topic-label-government = Politikk
newtab-topic-label-education-science = Vitskap
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Kvardagsknep og småtriks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Teknologi
newtab-topic-label-travel = Reise
newtab-topic-label-home = Heim og hage

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Vel emne for å finjustere feed-en din
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Vel to eller fleire emne. Ekspertkuratorane våre prioriterer artiklar tilpassa etter dine interesser. Oppdater når som helst.
newtab-topic-selection-save-button = Lagre
newtab-topic-selection-cancel-button = Avbryt
newtab-topic-selection-button-maybe-later = Kanskje seinare
newtab-topic-selection-privacy-link = FInn ut korleis vi vernar og handsamar data
newtab-topic-selection-button-update-interests = Oppdater interessene dine
newtab-topic-selection-button-pick-interests = Vel interessene dine

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Følg
newtab-section-following-button = Følgjer
newtab-section-unfollow-button = Slutt å følgje

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokker
newtab-section-blocked-button = Blokkert
newtab-section-unblock-button = Opphev blokkeringa

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ikkje no
newtab-section-confirm-block-topic-p1 = Er du sikker på at du vil blokkere dette emnet?
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokker { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Emne
newtab-section-manage-topics-button-v2 =
    .label = Handsam emne
newtab-section-mangage-topics-followed-topics = Følgt
newtab-section-mangage-topics-followed-topics-empty-state = Du har ikkje følgt nokon emne enno.
newtab-section-mangage-topics-blocked-topics = Blokkert
newtab-section-mangage-topics-blocked-topics-empty-state = Du har ikkje blokkert nokon emne enno.
newtab-custom-wallpaper-cta = Prøv det
