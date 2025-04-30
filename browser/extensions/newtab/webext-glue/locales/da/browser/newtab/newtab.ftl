# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nyt faneblad
newtab-settings-button =
    .title = Tilpas siden Nyt faneblad
newtab-personalize-settings-icon-label =
    .title = Tilpas nyt faneblad
    .aria-label = Indstillinger
newtab-settings-dialog-label =
    .aria-label = Indstillinger
newtab-personalize-icon-label =
    .title = Tilpas nyt faneblad
    .aria-label = Tilpas nyt faneblad
newtab-personalize-dialog-label =
    .aria-label = Tilpas
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Søg
    .aria-label = Søg
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Søg med { $engine } eller indtast adresse
newtab-search-box-handoff-text-no-engine = Søg eller indtast adresse
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Søg med { $engine } eller indtast adresse
    .title = Søg med { $engine } eller indtast adresse
    .aria-label = Søg med { $engine } eller indtast adresse
newtab-search-box-handoff-input-no-engine =
    .placeholder = Søg eller indtast adresse
    .title = Søg eller indtast adresse
    .aria-label = Søg eller indtast adresse
newtab-search-box-text = Søg på nettet
newtab-search-box-input =
    .placeholder = Søg på nettet
    .aria-label = Søg på nettet

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Tilføj søgetjeneste
newtab-topsites-add-shortcut-header = Ny genvej
newtab-topsites-edit-topsites-header = Rediger mest besøgte webside
newtab-topsites-edit-shortcut-header = Rediger genvej
newtab-topsites-add-shortcut-label = Tilføj genvej
newtab-topsites-title-label = Titel
newtab-topsites-title-input =
    .placeholder = Indtast en titel
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Indtast eller indsæt en URL
newtab-topsites-url-validation = Gyldig URL påkrævet
newtab-topsites-image-url-label = URL til selvvalgt billede
newtab-topsites-use-image-link = Brug selvvalgt billede…
newtab-topsites-image-validation = Kunne ikke indlæse billede. Prøv en anden URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Annuller
newtab-topsites-delete-history-button = Slet fra historik
newtab-topsites-save-button = Gem
newtab-topsites-preview-button = Vis prøve
newtab-topsites-add-button = Tilføj

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Er du sikker på, at du vil slette alle forekomster af denne side fra din historik?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Denne handling kan ikke fortrydes.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsoreret

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Åbn menu
    .aria-label = Åbn menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Fjern
    .aria-label = Fjern
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Åbn menu
    .aria-label = Åbn genvejsmenuen for { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Rediger denne webside
    .aria-label = Rediger denne webside

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Rediger
newtab-menu-open-new-window = Åbn i et nyt vindue
newtab-menu-open-new-private-window = Åbn i et nyt privat vindue
newtab-menu-dismiss = Afvis
newtab-menu-pin = Fastgør
newtab-menu-unpin = Frigør
newtab-menu-delete-history = Slet fra historik
newtab-menu-save-to-pocket = Gem til { -pocket-brand-name }
newtab-menu-delete-pocket = Slet fra { -pocket-brand-name }
newtab-menu-archive-pocket = Arkiver i { -pocket-brand-name }
newtab-menu-show-privacy-info = Vores sponsorer og dit privatliv
newtab-menu-about-fakespot = Om { -fakespot-brand-name }
newtab-menu-report-content = Rapporter indholdet
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloker
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Stop med at følge emne

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Håndter sponsoreret indhold
newtab-menu-our-sponsors-and-your-privacy = Vores sponsorer og dit privatliv
newtab-menu-report-this-ad = Rapporter reklamen

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Færdig
newtab-privacy-modal-button-manage = Håndter indstillinger for sponsoreret indhold
newtab-privacy-modal-header = Du har ret til et privatliv
newtab-privacy-modal-paragraph-2 =
    Udover at servere fængslende historier viser vi dig også relevant
    og grundigt undersøgt indhold fra udvalgte sponsorer. Du kan være 
    sikker på, at <strong>dine data aldrig kommer videre end den version af 
    { -brand-product-name }, du har på din computer </strong> — Vi ser ikke dine data, 
    og det gør vores sponsorer heller ikke.
newtab-privacy-modal-link = Læs mere om, hvordan sikring af dit privatliv fungerer i nyt faneblad

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Fjern bogmærke
# Bookmark is a verb here.
newtab-menu-bookmark = Bogmærk

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopier linkadresse
newtab-menu-go-to-download-page = Gå til siden, filen blev hentet fra
newtab-menu-remove-download = Fjern fra historik

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Vis i Finder
       *[other] Åbn hentningsmappe
    }
newtab-menu-open-file = Åbn fil

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Besøgt
newtab-label-bookmarked = Bogmærket
newtab-label-removed-bookmark = Bogmærke fjernet
newtab-label-recommended = Populært
newtab-label-saved = Gemt til { -pocket-brand-name }
newtab-label-download = Hentet
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsoreret
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsoreret af { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Fjern afsnit
newtab-section-menu-collapse-section = Sammenfold afsnit
newtab-section-menu-expand-section = Udvid afsnit
newtab-section-menu-manage-section = Håndter afsnit
newtab-section-menu-manage-webext = Håndter udvidelse
newtab-section-menu-add-topsite = Tilføj ny webside
newtab-section-menu-add-search-engine = Tilføj søgetjeneste
newtab-section-menu-move-up = Flyt op
newtab-section-menu-move-down = Flyt ned
newtab-section-menu-privacy-notice = Privatlivserklæring

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Sammenfold afsnit
newtab-section-expand-section-label =
    .aria-label = Udvid afsnit

## Section Headers.

newtab-section-header-topsites = Mest besøgte websider
newtab-section-header-recent-activity = Seneste aktivitet
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Anbefalet af { $provider }
newtab-section-header-stories = Tankevækkende historier
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dagens valg til dig

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Gå i gang med at browse, så vil vi vise dig nogle af de artikler, videoer og andre sider, du har besøgt eller gemt et bogmærke til for nylig.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Der er ikke flere nye historier. Kom tilbage senere for at se flere tophistorier fra { $provider }. Kan du ikke vente? Vælg et populært emne og find flere spændende historier fra hele verden.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Der er ikke flere nye historier. Kom tilbage senere for at se flere. Kan du ikke vente? Vælg et populært emne og find flere spændende historier fra hele verden.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Du har læst det hele!
newtab-discovery-empty-section-topstories-content = Kom tilbage senere for at se flere historier.
newtab-discovery-empty-section-topstories-try-again-button = Prøv igen
newtab-discovery-empty-section-topstories-loading = Indlæser…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hov. Det lykkedes ikke at indlæse afsnittet.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populære emner:
newtab-pocket-new-topics-title = Vil du have endnu flere historier? Se disse populære emner fra { -pocket-brand-name }
newtab-pocket-more-recommendations = Flere anbefalinger
newtab-pocket-learn-more = Læs mere
newtab-pocket-cta-button = Hent { -pocket-brand-name }
newtab-pocket-cta-text = Gem dine yndlingshistorier i { -pocket-brand-name } og hav dem altid ved hånden.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } er en del af { -brand-product-name }-familien
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Gem
newtab-pocket-saved = Gemt

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Mere som dette
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ikke noget for mig
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Tak. Din tilbagemelding hjælper os med at forbedre dit feed.
newtab-toast-dismiss-button =
    .title = Afvis
    .aria-label = Afvis

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Opdag det bedste fra nettet
newtab-pocket-onboarding-cta = { -pocket-brand-name } gennemsøger en lang række forskellige publikationer for at kunne vise dig det mest informative, inspirerende og troværdige indhold direkte i din { -brand-product-name }-browser.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Hovsa. Noget gik galt ved indlæsning af indholdet.
newtab-error-fallback-refresh-link = Prøv igen ved at genindlæse siden.

## Customization Menu

newtab-custom-shortcuts-title = Genveje
newtab-custom-shortcuts-subtitle = Gemte eller besøgte websteder
newtab-custom-shortcuts-toggle =
    .label = Genveje
    .description = Gemte eller besøgte websteder
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } række
       *[other] { $num } rækker
    }
newtab-custom-sponsored-sites = Sponsorerede genveje
newtab-custom-pocket-title = Anbefalet af { -pocket-brand-name }
newtab-custom-pocket-subtitle = Interessant indhold udvalgt af { -pocket-brand-name }, en del af { -brand-product-name }-familien
newtab-custom-stories-toggle =
    .label = Anbefalede historier
    .description = Interessant indhold udvalgt af { -brand-product-name }-holdet
newtab-custom-pocket-sponsored = Sponsorerede historier
newtab-custom-pocket-show-recent-saves = Vis seneste gemte
newtab-custom-recent-title = Seneste aktivitet
newtab-custom-recent-subtitle = Et udvalg af seneste websteder og indhold
newtab-custom-recent-toggle =
    .label = Seneste aktivitet
    .description = Et udvalg af seneste websteder og indhold
newtab-custom-weather-toggle =
    .label = Vejr
    .description = Dagens vejrudsigt
newtab-custom-close-button = Luk
newtab-custom-settings = Håndter flere indstillinger

## New Tab Wallpapers

newtab-wallpaper-title = Baggrunde
newtab-wallpaper-reset = Nulstil til standard
newtab-wallpaper-upload-image = Upload et billede
newtab-wallpaper-custom-color = Vælg en farve
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Billedet overskrider grænsen for filstørrelse på { $file_size } MB. Prøv at uploade en mindre fil.
newtab-wallpaper-error-file-type = Vi kunne ikke uploade din fil. Prøv igen med en anden filtype.
newtab-wallpaper-light-red-panda = Rød panda
newtab-wallpaper-light-mountain = Hvidt bjerg
newtab-wallpaper-light-sky = Himmel med lilla og lyserøde skyer
newtab-wallpaper-light-color = Blå, lyserøde og gule former
newtab-wallpaper-light-landscape = Bjerglandskab med blå tåge
newtab-wallpaper-light-beach = Strand med palme
newtab-wallpaper-dark-aurora = Nordlys
newtab-wallpaper-dark-color = Røde og blå former
newtab-wallpaper-dark-panda = Rød panda skjult i en skov
newtab-wallpaper-dark-sky = Udsigt over by med nattehimmel
newtab-wallpaper-dark-mountain = Bjerglandskab
newtab-wallpaper-dark-city = Lilla bylandskab
newtab-wallpaper-dark-fox-anniversary = En ræv på fortovet i nærheden af en skov
newtab-wallpaper-light-fox-anniversary = En ræv på en græsmark i et tåget bjerglandskab

## Solid Colors

newtab-wallpaper-category-title-colors = Ensfarvede
newtab-wallpaper-blue = Blå
newtab-wallpaper-light-blue = Lyseblå
newtab-wallpaper-light-purple = Lyslilla
newtab-wallpaper-light-green = Lysegrøn
newtab-wallpaper-green = Grøn
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Gul
newtab-wallpaper-orange = Orange
newtab-wallpaper-pink = Pink
newtab-wallpaper-light-pink = Lyserød
newtab-wallpaper-red = Rød
newtab-wallpaper-dark-blue = Mørkeblå
newtab-wallpaper-dark-purple = Mørklilla
newtab-wallpaper-dark-green = Mørkegrøn
newtab-wallpaper-brown = Brun

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakt
newtab-wallpaper-abstract-green = Grønne former
newtab-wallpaper-abstract-blue = Blå former
newtab-wallpaper-abstract-purple = Lilla former
newtab-wallpaper-abstract-orange = Orange former
newtab-wallpaper-gradient-orange = Farveforløb i orange og pink
newtab-wallpaper-abstract-blue-purple = Blå og lilla former
newtab-wallpaper-abstract-white-curves = Hvid med skraverede kurver
newtab-wallpaper-abstract-purple-green = Gradient med lilla og grønt lys
newtab-wallpaper-abstract-blue-purple-waves = Blå og lilla bølgeformer
newtab-wallpaper-abstract-black-waves = Sorte bølgeformer

## Celestial

newtab-wallpaper-category-title-photographs = Fotografier
newtab-wallpaper-beach-at-sunrise = Strand ved solopgang
newtab-wallpaper-beach-at-sunset = Strand ved solnedgang
newtab-wallpaper-storm-sky = Stormfuld himmel
newtab-wallpaper-sky-with-pink-clouds = Himmel med lyserøde skyer
newtab-wallpaper-red-panda-yawns-in-a-tree = Rød panda gaber i et træ
newtab-wallpaper-white-mountains = Hvide bjerge
newtab-wallpaper-hot-air-balloons = Luftballoner i forskellige farver om dagen
newtab-wallpaper-starry-canyon = Blå stjernehimmel
newtab-wallpaper-suspension-bridge = Fotografi af grå hængebro om dagen
newtab-wallpaper-sand-dunes = Hvide klitter
newtab-wallpaper-palm-trees = Silhuet med kokospalmer i den gyldne time
newtab-wallpaper-blue-flowers = Nærbillede af blomster med blå kronblade.
newtab-wallpaper-forest-trees = Træer indhyllet i tåge
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto af <a data-l10n-name="name-link">{ $author_string }</a> fra <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Tilføj lidt farve
newtab-wallpaper-feature-highlight-content = Opdater siden Nyt faneblad med baggrundsbilleder.
newtab-wallpaper-feature-highlight-button = Forstået
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Annuller
    .aria-label = Luk pop op
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Rummet
newtab-wallpaper-celestial-lunar-eclipse = Måneformørkelse
newtab-wallpaper-celestial-earth-night = Nattefotografi fra lavt kredsløb om Jorden
newtab-wallpaper-celestial-starry-sky = Stjernehimmel
newtab-wallpaper-celestial-eclipse-time-lapse = Tidsforløb måneformørkelse
newtab-wallpaper-celestial-black-hole = Illustration af galakse med sort hul
newtab-wallpaper-celestial-river = Satellitfotografi af flod

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Se vejrudsigter på { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsoreret
newtab-weather-menu-change-location = Skift sted
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Søg efter sted
    .aria-label = Søg efter sted
newtab-weather-change-location-search-input = Søg efter sted
newtab-weather-menu-weather-display = Visning af vejr
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Enkel
newtab-weather-menu-change-weather-display-simple = Skift til enkel visning
newtab-weather-menu-weather-display-option-detailed = Detaljeret
newtab-weather-menu-change-weather-display-detailed = Skift til detaljeret visning
newtab-weather-menu-temperature-units = Temperaturenheder
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Skift til Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Skift til Celsius
newtab-weather-menu-hide-weather = Skjul vejr på Nyt faneblad
newtab-weather-menu-learn-more = Læs mere
# This message is shown if user is working offline
newtab-weather-error-not-available = Vejrdata er ikke tilgængelige lige nu.

## Topic Labels

newtab-topic-label-business = Forretning
newtab-topic-label-career = Karriere
newtab-topic-label-education = Uddannelse
newtab-topic-label-arts = Underholdning
newtab-topic-label-food = Mad
newtab-topic-label-health = Sundhed
newtab-topic-label-hobbies = Spil
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Penge
newtab-topic-label-society-parenting = Forældreskab
newtab-topic-label-government = Politik
newtab-topic-label-education-science = Videnskab
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Lifehacks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Teknologi
newtab-topic-label-travel = Rejser
newtab-topic-label-home = Hus og have

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Vælg emner for at finjustere dit feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Vælg to eller flere emner. Vores ekspertkuratorer prioriterer historier målrettet dine interesser. Opdater når som helst.
newtab-topic-selection-save-button = Gem
newtab-topic-selection-cancel-button = Annuller
newtab-topic-selection-button-maybe-later = Måske senere
newtab-topic-selection-privacy-link = Lær, hvordan vi beskytter og håndterer data
newtab-topic-selection-button-update-interests = Opdater dine interesser
newtab-topic-selection-button-pick-interests = Vælg dine interesser

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Følg
newtab-section-following-button = Følger
newtab-section-unfollow-button = Stop med at følge

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bloker
newtab-section-blocked-button = Blokeret
newtab-section-unblock-button = Fjern blokering

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ikke nu
newtab-section-confirm-block-topic-p1 = Er du sikker på, at du vil blokere dette emne?
newtab-section-confirm-block-topic-p2 = Det blokerede emner vil ikke længere blive vist i dit feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bloker { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Emner
newtab-section-manage-topics-button-v2 =
    .label = Håndter emner
newtab-section-mangage-topics-followed-topics = Fulgt
newtab-section-mangage-topics-followed-topics-empty-state = Du har ikke fulgt nogle emner endnu.
newtab-section-mangage-topics-blocked-topics = Blokeret
newtab-section-mangage-topics-blocked-topics-empty-state = Du har ikke blokeret nogen emner endnu.
newtab-custom-wallpaper-title = Nu kan du vælge din egen baggrund
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Upload din egen baggrund eller vælg en farve for at gøre { -brand-product-name } til din egen.
newtab-custom-wallpaper-cta = Prøv det
