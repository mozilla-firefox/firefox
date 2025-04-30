# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nieuw tabblad
newtab-settings-button =
    .title = Uw Nieuw-tabbladpagina aanpassen
newtab-personalize-settings-icon-label =
    .title = Nieuw tabblad personaliseren
    .aria-label = Instellingen
newtab-settings-dialog-label =
    .aria-label = Instellingen
newtab-personalize-icon-label =
    .title = Nieuw tabblad personaliseren
    .aria-label = Nieuw tabblad personaliseren
newtab-personalize-dialog-label =
    .aria-label = Personaliseren
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Zoeken
    .aria-label = Zoeken
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Met { $engine } zoeken of voer adres in
newtab-search-box-handoff-text-no-engine = Voer zoekterm of adres in
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Met { $engine } zoeken of voer adres in
    .title = Met { $engine } zoeken of voer adres in
    .aria-label = Met { $engine } zoeken of voer adres in
newtab-search-box-handoff-input-no-engine =
    .placeholder = Voer zoekterm of adres in
    .title = Voer zoekterm of adres in
    .aria-label = Voer zoekterm of adres in
newtab-search-box-text = Zoeken op het web
newtab-search-box-input =
    .placeholder = Zoeken op het web
    .aria-label = Zoeken op het web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Zoekmachine toevoegen
newtab-topsites-add-shortcut-header = Nieuwe snelkoppeling
newtab-topsites-edit-topsites-header = Topwebsite bewerken
newtab-topsites-edit-shortcut-header = Snelkoppeling bewerken
newtab-topsites-add-shortcut-label = Snelkoppeling toevoegen
newtab-topsites-title-label = Titel
newtab-topsites-title-input =
    .placeholder = Voer een titel in
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Typ of plak een URL
newtab-topsites-url-validation = Geldige URL vereist
newtab-topsites-image-url-label = URL van aangepaste afbeelding
newtab-topsites-use-image-link = Een aangepaste afbeelding gebruiken…
newtab-topsites-image-validation = Afbeelding kon niet worden geladen. Probeer een andere URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Annuleren
newtab-topsites-delete-history-button = Verwijderen uit geschiedenis
newtab-topsites-save-button = Opslaan
newtab-topsites-preview-button = Voorbeeld
newtab-topsites-add-button = Toevoegen

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Weet u zeker dat u alle exemplaren van deze pagina uit uw geschiedenis wilt verwijderen?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Deze actie kan niet ongedaan worden gemaakt.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Gesponsord

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Menu openen
    .aria-label = Menu openen
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Verwijderen
    .aria-label = Verwijderen
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Menu openen
    .aria-label = Contextmenu openen voor { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Deze website bewerken
    .aria-label = Deze website bewerken

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Bewerken
newtab-menu-open-new-window = Openen in een nieuw venster
newtab-menu-open-new-private-window = Openen in een nieuw privévenster
newtab-menu-dismiss = Sluiten
newtab-menu-pin = Vastmaken
newtab-menu-unpin = Losmaken
newtab-menu-delete-history = Verwijderen uit geschiedenis
newtab-menu-save-to-pocket = Opslaan naar { -pocket-brand-name }
newtab-menu-delete-pocket = Verwijderen uit { -pocket-brand-name }
newtab-menu-archive-pocket = Archiveren in { -pocket-brand-name }
newtab-menu-show-privacy-info = Onze sponsors en uw privacy
newtab-menu-about-fakespot = Over { -fakespot-brand-name }
newtab-menu-report-content = Deze inhoud rapporteren
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokkeren
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Onderwerp niet meer volgen

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Gesponsorde inhoud beheren
newtab-menu-our-sponsors-and-your-privacy = Onze sponsors en uw privacy
newtab-menu-report-this-ad = Deze advertentie rapporteren

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gereed
newtab-privacy-modal-button-manage = Instellingen voor gesponsorde inhoud beheren
newtab-privacy-modal-header = Uw privacy is belangrijk.
newtab-privacy-modal-paragraph-2 =
    Naast het vertellen van boeiende verhalen, tonen we u ook relevante,
    goed doorgelichte inhoud van geselecteerde sponsors. Wees gerust, <strong>uw navigatiegegevens
    verlaten nooit uw persoonlijke exemplaar van { -brand-product-name }</strong> – wij krijgen ze niet te zien,
    en onze sponsors ook niet.
newtab-privacy-modal-link = Ontdek hoe privacy werkt op het nieuwe tabblad

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Bladwijzer verwijderen
# Bookmark is a verb here.
newtab-menu-bookmark = Bladwijzer maken

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Downloadkoppeling kopiëren
newtab-menu-go-to-download-page = Naar downloadpagina gaan
newtab-menu-remove-download = Verwijderen uit geschiedenis

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Tonen in Finder
       *[other] Bijbehorende map openen
    }
newtab-menu-open-file = Bestand openen

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Bezocht
newtab-label-bookmarked = Bladwijzer gemaakt
newtab-label-removed-bookmark = Bladwijzer verwijderd
newtab-label-recommended = Trending
newtab-label-saved = Opgeslagen naar { -pocket-brand-name }
newtab-label-download = Gedownload
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Gesponsord
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Gesponsord door { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Sectie verwijderen
newtab-section-menu-collapse-section = Sectie samenvouwen
newtab-section-menu-expand-section = Sectie uitvouwen
newtab-section-menu-manage-section = Sectie beheren
newtab-section-menu-manage-webext = Extensie beheren
newtab-section-menu-add-topsite = Topwebsite toevoegen
newtab-section-menu-add-search-engine = Zoekmachine toevoegen
newtab-section-menu-move-up = Omhoog verplaatsen
newtab-section-menu-move-down = Omlaag verplaatsen
newtab-section-menu-privacy-notice = Privacyverklaring

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Sectie samenvouwen
newtab-section-expand-section-label =
    .aria-label = Sectie uitvouwen

## Section Headers.

newtab-section-header-topsites = Topwebsites
newtab-section-header-recent-activity = Recente activiteit
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Aanbevolen door { $provider }
newtab-section-header-stories = Verhalen die tot nadenken stemmen
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Keuzes van vandaag voor u

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Begin met surfen, en we tonen hier een aantal geweldige artikelen, video’s en andere pagina’s die u onlangs hebt bezocht of waarvoor u een bladwijzer hebt gemaakt.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = U bent weer bij. Kijk later nog eens voor meer topverhalen van { $provider }. Kunt u niet wachten? Selecteer een populair onderwerp voor meer geweldige verhalen van het hele web.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = U bent weer bij. Kijk later nog eens voor meer verhalen. Kunt u niet wachten? Selecteer een populair onderwerp voor meer geweldige verhalen van het hele web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = U bent helemaal bij!
newtab-discovery-empty-section-topstories-content = Kom later terug voor meer verhalen.
newtab-discovery-empty-section-topstories-try-again-button = Opnieuw proberen
newtab-discovery-empty-section-topstories-loading = Laden…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oeps! We hadden deze sectie bijna geladen, maar toch niet helemaal.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populaire onderwerpen:
newtab-pocket-new-topics-title = Wilt u nog meer verhalen? Bekijk deze populaire onderwerpen van { -pocket-brand-name }
newtab-pocket-more-recommendations = Meer aanbevelingen
newtab-pocket-learn-more = Meer info
newtab-pocket-cta-button = { -pocket-brand-name } gebruiken
newtab-pocket-cta-text = Bewaar de verhalen die u interessant vindt in { -pocket-brand-name }, en stimuleer uw gedachten met boeiende leesstof.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } maakt deel uit van de { -brand-product-name }-familie
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Opslaan
newtab-pocket-saved = Opgeslagen

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Meer zoals dit
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Niets voor mij
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Bedankt. Uw feedback helpt ons uw feed te verbeteren.
newtab-toast-dismiss-button =
    .title = Sluiten
    .aria-label = Sluiten

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Ontdek het beste van internet
newtab-pocket-onboarding-cta = { -pocket-brand-name } verkent een breed scala aan publicaties om de meest informatieve, inspirerende en betrouwbare inhoud rechtstreeks naar uw { -brand-product-name }-browser te brengen.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oeps, er is iets misgegaan bij het laden van deze inhoud.
newtab-error-fallback-refresh-link = Vernieuw de pagina om het opnieuw te proberen.

## Customization Menu

newtab-custom-shortcuts-title = Snelkoppelingen
newtab-custom-shortcuts-subtitle = Opgeslagen of bezochte websites
newtab-custom-shortcuts-toggle =
    .label = Snelkoppelingen
    .description = Opgeslagen of bezochte websites
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rij
       *[other] { $num } rijen
    }
newtab-custom-sponsored-sites = Gesponsorde snelkoppelingen
newtab-custom-pocket-title = Aanbevolen door { -pocket-brand-name }
newtab-custom-pocket-subtitle = Uitzonderlijke inhoud, samengesteld door { -pocket-brand-name }, onderdeel van de { -brand-product-name }-familie
newtab-custom-stories-toggle =
    .label = Aanbevolen verhalen
    .description = Uitzonderlijke inhoud, verzameld door de { -brand-product-name }-familie
newtab-custom-pocket-sponsored = Gesponsorde verhalen
newtab-custom-pocket-show-recent-saves = Onlangs opgeslagen items tonen
newtab-custom-recent-title = Recente activiteit
newtab-custom-recent-subtitle = Een selectie van recente websites en inhoud
newtab-custom-recent-toggle =
    .label = Recente activiteit
    .description = Een selectie van recente websites en inhoud
newtab-custom-weather-toggle =
    .label = Weer
    .description = De weersverwachting van vandaag in een oogopslag
newtab-custom-close-button = Sluiten
newtab-custom-settings = Meer instellingen beheren

## New Tab Wallpapers

newtab-wallpaper-title = Achtergronden
newtab-wallpaper-reset = Standaardwaarden
newtab-wallpaper-upload-image = Een afbeelding uploaden
newtab-wallpaper-custom-color = Kies een kleur
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = De afbeelding heeft de bestandsgroottelimiet van { $file_size } MB overschreden. Probeer een kleiner bestand te uploaden.
newtab-wallpaper-error-file-type = We konden uw bestand niet uploaden. Probeer het opnieuw met een ander bestandstype.
newtab-wallpaper-light-red-panda = Rode panda
newtab-wallpaper-light-mountain = Witte berg
newtab-wallpaper-light-sky = Lucht met paarse en roze wolken
newtab-wallpaper-light-color = Blauwe, roze en gele vormen
newtab-wallpaper-light-landscape = Berglandschap met blauwe mist
newtab-wallpaper-light-beach = Strand met palmboom
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Rode en blauwe vormen
newtab-wallpaper-dark-panda = Rode panda verborgen in bos
newtab-wallpaper-dark-sky = Stadslandschap met een nachtelijke hemel
newtab-wallpaper-dark-mountain = Landschap met berg
newtab-wallpaper-dark-city = Paars stadslandschap
newtab-wallpaper-dark-fox-anniversary = Een vos op de stoep bij een bos
newtab-wallpaper-light-fox-anniversary = Een vos in een grasveld met een mistig berglandschap

## Solid Colors

newtab-wallpaper-category-title-colors = Effen kleuren
newtab-wallpaper-blue = Blauw
newtab-wallpaper-light-blue = Lichtblauw
newtab-wallpaper-light-purple = Lichtpaars
newtab-wallpaper-light-green = Lichtgroen
newtab-wallpaper-green = Groen
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Geel
newtab-wallpaper-orange = Oranje
newtab-wallpaper-pink = Roze
newtab-wallpaper-light-pink = Lichtroze
newtab-wallpaper-red = Rood
newtab-wallpaper-dark-blue = Donkerblauw
newtab-wallpaper-dark-purple = Donkerpaars
newtab-wallpaper-dark-green = Donkergroen
newtab-wallpaper-brown = Bruin

## Abstract

newtab-wallpaper-category-title-abstract = Abstract
newtab-wallpaper-abstract-green = Groene vormen
newtab-wallpaper-abstract-blue = Blauwe vormen
newtab-wallpaper-abstract-purple = Paarse vormen
newtab-wallpaper-abstract-orange = Oranje vormen
newtab-wallpaper-gradient-orange = Verloop oranje en roze
newtab-wallpaper-abstract-blue-purple = Blauwe en paarse vormen
newtab-wallpaper-abstract-white-curves = Wit met gearceerde rondingen
newtab-wallpaper-abstract-purple-green = Paars en groene lichtgradiënt
newtab-wallpaper-abstract-blue-purple-waves = Blauwe en paarse golvende vormen
newtab-wallpaper-abstract-black-waves = Zwarte golvende vormen

## Celestial

newtab-wallpaper-category-title-photographs = Foto’s
newtab-wallpaper-beach-at-sunrise = Strand bij zonsopgang
newtab-wallpaper-beach-at-sunset = Strand bij zonsondergang
newtab-wallpaper-storm-sky = Onweerslucht
newtab-wallpaper-sky-with-pink-clouds = Lucht met roze wolken
newtab-wallpaper-red-panda-yawns-in-a-tree = Rode panda gaapt in een boom
newtab-wallpaper-white-mountains = Witte bergen
newtab-wallpaper-hot-air-balloons = Heteluchtballonnen in diverse kleuren overdag
newtab-wallpaper-starry-canyon = Blauwe sterrennacht
newtab-wallpaper-suspension-bridge = Foto’s van een volledige hangbrug overdag
newtab-wallpaper-sand-dunes = Witte zandduinen
newtab-wallpaper-palm-trees = Silhouet van kokospalmen tijdens het gouden uur
newtab-wallpaper-blue-flowers = Close-upfotografie van blauwe bloemen in bloei
newtab-wallpaper-forest-trees = Bomen bedekt met mist
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto door <a data-l10n-name="name-link">{ $author_string }</a> op <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Probeer een vleugje kleur
newtab-wallpaper-feature-highlight-content = Geef uw Nieuw-tabbladpagina een frisse uitstraling met achtergronden.
newtab-wallpaper-feature-highlight-button = Begrepen
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Sluiten
    .aria-label = Pop-up sluiten
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Kosmisch
newtab-wallpaper-celestial-lunar-eclipse = Maansverduistering
newtab-wallpaper-celestial-earth-night = Nachtfoto vanuit een lage baan om de aarde
newtab-wallpaper-celestial-starry-sky = Sterrenhemel
newtab-wallpaper-celestial-eclipse-time-lapse = Time-lapse van maansverduistering
newtab-wallpaper-celestial-black-hole = Illustratie van een zwart-gatsterrenstelsel
newtab-wallpaper-celestial-river = Satellietfoto van rivier

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Weersverwachting bekijken voor { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Gesponsord
newtab-weather-menu-change-location = Locatie wijzigen
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Locatie zoeken
    .aria-label = Locatie zoeken
newtab-weather-change-location-search-input = Locatie zoeken
newtab-weather-menu-weather-display = Weerweergave
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Eenvoudig
newtab-weather-menu-change-weather-display-simple = Wisselen naar eenvoudige weergave
newtab-weather-menu-weather-display-option-detailed = Gedetailleerd
newtab-weather-menu-change-weather-display-detailed = Wisselen naar gedetailleerde weergave
newtab-weather-menu-temperature-units = Temperatuureenheden
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Wisselen naar Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Wisselen naar Celsius
newtab-weather-menu-hide-weather = Weer op nieuw tabblad verbergen
newtab-weather-menu-learn-more = Meer info
# This message is shown if user is working offline
newtab-weather-error-not-available = Weergegevens zijn momenteel niet beschikbaar.

## Topic Labels

newtab-topic-label-business = Zakelijk
newtab-topic-label-career = Loopbaan
newtab-topic-label-education = Onderwijs
newtab-topic-label-arts = Amusement
newtab-topic-label-food = Voeding
newtab-topic-label-health = Gezondheid
newtab-topic-label-hobbies = Gaming
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Geld
newtab-topic-label-society-parenting = Ouderschap en opvoeding
newtab-topic-label-government = Politiek
newtab-topic-label-education-science = Wetenschap
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Lifehacks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Technologie
newtab-topic-label-travel = Reizen
newtab-topic-label-home = Huis en tuin

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Selecteer onderwerpen om uw feed te verfijnen
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Kies twee of meer onderwerpen. Onze deskundige curatoren geven prioriteit aan verhalen die zijn afgestemd op uw interesses. Werk op elk gewenst moment bij.
newtab-topic-selection-save-button = Opslaan
newtab-topic-selection-cancel-button = Annuleren
newtab-topic-selection-button-maybe-later = Misschien later
newtab-topic-selection-privacy-link = Lees hoe we gegevens beschermen en beheren
newtab-topic-selection-button-update-interests = Werk uw interesses bij
newtab-topic-selection-button-pick-interests = Kies uw interesses

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Volgen
newtab-section-following-button = Volgend
newtab-section-unfollow-button = Ontvolgen

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokkeren
newtab-section-blocked-button = Geblokkeerd
newtab-section-unblock-button = Blokkering opheffen

## Confirmation modal for blocking a section

newtab-section-cancel-button = Niet nu
newtab-section-confirm-block-topic-p1 = Weet u zeker dat u dit onderwerp wilt blokkeren?
newtab-section-confirm-block-topic-p2 = Geblokkeerde onderwerpen verschijnen niet meer in uw feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = { $topic } blokkeren

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Onderwerpen
newtab-section-manage-topics-button-v2 =
    .label = Onderwerpen beheren
newtab-section-mangage-topics-followed-topics = Gevolgd
newtab-section-mangage-topics-followed-topics-empty-state = U hebt nog geen onderwerpen gevolgd.
newtab-section-mangage-topics-blocked-topics = Geblokkeerd
newtab-section-mangage-topics-blocked-topics-empty-state = U hebt nog geen onderwerpen geblokkeerd.
newtab-custom-wallpaper-title = Hier vindt u aangepaste achtergronden
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Upload uw eigen achtergrond of kies een aangepaste kleur om { -brand-product-name } van uzelf te maken.
newtab-custom-wallpaper-cta = Uitproberen
