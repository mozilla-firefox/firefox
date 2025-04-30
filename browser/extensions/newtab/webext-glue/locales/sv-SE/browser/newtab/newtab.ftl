# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Ny flik
newtab-settings-button =
    .title = Anpassa sidan för Ny flik
newtab-personalize-settings-icon-label =
    .title = Anpassa ny flik
    .aria-label = Inställningar
newtab-settings-dialog-label =
    .aria-label = Inställningar
newtab-personalize-icon-label =
    .title = Anpassa ny flik
    .aria-label = Anpassa ny flik
newtab-personalize-dialog-label =
    .aria-label = Anpassa
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Sök
    .aria-label = Sök
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Sök med { $engine } eller ange en adress
newtab-search-box-handoff-text-no-engine = Sök eller ange adress
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Sök med { $engine } eller ange en adress
    .title = Sök med { $engine } eller ange en adress
    .aria-label = Sök med { $engine } eller ange en adress
newtab-search-box-handoff-input-no-engine =
    .placeholder = Sök eller ange adress
    .title = Sök eller ange adress
    .aria-label = Sök eller ange adress
newtab-search-box-text = Sök på webben
newtab-search-box-input =
    .placeholder = Sök på webben
    .aria-label = Sök på webben

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Lägg till sökmotor
newtab-topsites-add-shortcut-header = Ny genväg
newtab-topsites-edit-topsites-header = Redigera mest besökta
newtab-topsites-edit-shortcut-header = Redigera genväg
newtab-topsites-add-shortcut-label = Lägg till genväg
newtab-topsites-title-label = Titel
newtab-topsites-title-input =
    .placeholder = Ange en titel
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Skriv eller klistra in en URL
newtab-topsites-url-validation = Giltig URL krävs
newtab-topsites-image-url-label = Anpassa bild-URL
newtab-topsites-use-image-link = Använd en anpassad bild…
newtab-topsites-image-validation = Bilden misslyckades att ladda. Prova en annan URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Avbryt
newtab-topsites-delete-history-button = Ta bort från historik
newtab-topsites-save-button = Spara
newtab-topsites-preview-button = Förhandsvisa
newtab-topsites-add-button = Lägg till

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Är du säker på att du vill radera varje förekomst av den här sidan från din historik?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Den här åtgärden kan inte ångras.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsrad

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Öppna meny
    .aria-label = Öppna meny
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Ta bort
    .aria-label = Ta bort
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Öppna meny
    .aria-label = Öppna snabbmeny för { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Redigera denna webbplats
    .aria-label = Redigera denna webbplats

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Redigera
newtab-menu-open-new-window = Öppna i nytt fönster
newtab-menu-open-new-private-window = Öppna i nytt privat fönster
newtab-menu-dismiss = Ignorera
newtab-menu-pin = Fäst
newtab-menu-unpin = Lösgör
newtab-menu-delete-history = Ta bort från historik
newtab-menu-save-to-pocket = Spara till { -pocket-brand-name }
newtab-menu-delete-pocket = Ta bort från { -pocket-brand-name }
newtab-menu-archive-pocket = Arkivera i { -pocket-brand-name }
newtab-menu-show-privacy-info = Våra sponsorer & din integritet
newtab-menu-about-fakespot = Om { -fakespot-brand-name }
newtab-menu-report-content = Rapportera detta innehåll
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blockera
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Sluta följa ämne

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Hantera sponsrat innehåll
newtab-menu-our-sponsors-and-your-privacy = Våra sponsorer och din integritet
newtab-menu-report-this-ad = Rapportera denna annons

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Klar
newtab-privacy-modal-button-manage = Hantera sponsrade innehållsinställningar
newtab-privacy-modal-header = Din integritet är viktig.
newtab-privacy-modal-paragraph-2 =
    Förutom att servera fängslande berättelser, visar vi dig också relevant,
    högt kontrollerat innehåll från utvalda sponsorer. Du kan vara säker på att <strong>din surfinformation
    inte lämnar din personliga kopia av { -brand-product-name }</strong> — vi ser inte den och våra
    sponsorer gör det inte heller.
newtab-privacy-modal-link = Lär dig hur sekretess fungerar på den nya fliken

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Ta bort bokmärke
# Bookmark is a verb here.
newtab-menu-bookmark = Bokmärke

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopiera nedladdningslänk
newtab-menu-go-to-download-page = Gå till hämtningssida
newtab-menu-remove-download = Ta bort från historik

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Visa i Finder
       *[other] Öppna objektets mapp
    }
newtab-menu-open-file = Öppna fil

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Besökta
newtab-label-bookmarked = Bokmärkta
newtab-label-removed-bookmark = Bokmärke har tagits bort
newtab-label-recommended = Trend
newtab-label-saved = Spara till { -pocket-brand-name }
newtab-label-download = Hämtat
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsrad
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsrad av { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Ta bort sektion
newtab-section-menu-collapse-section = Fäll ihop sektion
newtab-section-menu-expand-section = Expandera sektion
newtab-section-menu-manage-section = Hantera sektion
newtab-section-menu-manage-webext = Hantera tillägg
newtab-section-menu-add-topsite = Lägg till mest besökta
newtab-section-menu-add-search-engine = Lägg till sökmotor
newtab-section-menu-move-up = Flytta upp
newtab-section-menu-move-down = Flytta ner
newtab-section-menu-privacy-notice = Sekretessmeddelande

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Fäll ihop sektion
newtab-section-expand-section-label =
    .aria-label = Expandera sektion

## Section Headers.

newtab-section-header-topsites = Mest besökta
newtab-section-header-recent-activity = Senaste aktivitet
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Rekommenderas av { $provider }
newtab-section-header-stories = Tankeväckande berättelser
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dagens val för dig

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Börja surfa, och vi visar några av de bästa artiklarna, videoklippen och andra sidor du nyligen har besökt eller bokmärkt här.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Det finns inte fler. Kom tillbaka senare för fler huvudnyheter från { $provider }. Kan du inte vänta? Välj ett populärt ämne för att hitta fler bra nyheter från hela världen.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Det finns inte fler. Kom tillbaka senare för fler berättelser. Kan du inte vänta? Välj ett populärt ämne för att hitta fler bra berättelser från hela webben.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Du är ikapp!
newtab-discovery-empty-section-topstories-content = Kom tillbaka senare för fler berättelser.
newtab-discovery-empty-section-topstories-try-again-button = Försök igen
newtab-discovery-empty-section-topstories-loading = Laddar…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hoppsan! Vi laddade nästan detta avsnitt, men inte riktigt.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populära ämnen:
newtab-pocket-new-topics-title = Vill du ha fler berättelser? Se dessa populära ämnen från { -pocket-brand-name }
newtab-pocket-more-recommendations = Fler rekommendationer
newtab-pocket-learn-more = Läs mer
newtab-pocket-cta-button = Hämta { -pocket-brand-name }
newtab-pocket-cta-text = Spara de historier som du tycker är intressant i { -pocket-brand-name } och stimulera dina tankar med fascinerande läsmaterial.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } är en del av familjen { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Spara
newtab-pocket-saved = Sparad

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Mer sånt här
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Inte för mig
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Tack. Din feedback hjälper oss att förbättra ditt flöde.
newtab-toast-dismiss-button =
    .title = Ignorera
    .aria-label = Ignorera

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Upptäck det bästa från webben
newtab-pocket-onboarding-cta = { -pocket-brand-name } utforskar en mängd olika publikationer för att få det mest informativa, inspirerande och pålitliga innehållet direkt till din { -brand-product-name }-webbläsare.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oj, något gick fel när innehållet skulle laddas.
newtab-error-fallback-refresh-link = Uppdatera sidan för att försöka igen.

## Customization Menu

newtab-custom-shortcuts-title = Genvägar
newtab-custom-shortcuts-subtitle = Webbplatser du sparar eller besöker
newtab-custom-shortcuts-toggle =
    .label = Genvägar
    .description = Webbplatser du sparar eller besöker
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rad
       *[other] { $num } rader
    }
newtab-custom-sponsored-sites = Sponsrade genvägar
newtab-custom-pocket-title = Rekommenderas av { -pocket-brand-name }
newtab-custom-pocket-subtitle = Särskilt innehåll valt av { -pocket-brand-name }, en del av familjen { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Rekommenderade berättelser
    .description = Exceptionellt innehåll kurerat av { -brand-product-name }-familjen
newtab-custom-pocket-sponsored = Sponsrade berättelser
newtab-custom-pocket-show-recent-saves = Visa senast sparade
newtab-custom-recent-title = Senaste aktivitet
newtab-custom-recent-subtitle = Ett urval av senaste webbplatser och innehåll
newtab-custom-recent-toggle =
    .label = Senaste aktivitet
    .description = Ett urval av senaste webbplatser och innehåll
newtab-custom-weather-toggle =
    .label = Väder
    .description = Dagens prognos i korthet
newtab-custom-close-button = Stäng
newtab-custom-settings = Hantera fler inställningar

## New Tab Wallpapers

newtab-wallpaper-title = Bakgrundsbilder
newtab-wallpaper-reset = Återställ till standardvärden
newtab-wallpaper-upload-image = Ladda upp en bild
newtab-wallpaper-custom-color = Välj en färg
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Bilden överskred gränsen för filstorleken på { $file_size } MB. Prova att ladda upp en mindre fil.
newtab-wallpaper-error-file-type = Vi kunde inte ladda upp din fil. Försök igen med en annan filtyp.
newtab-wallpaper-light-red-panda = Röd panda
newtab-wallpaper-light-mountain = Vita berg
newtab-wallpaper-light-sky = Himmel med lila och rosa moln
newtab-wallpaper-light-color = Blå, rosa och gula former
newtab-wallpaper-light-landscape = Berglandskap med blå dimma
newtab-wallpaper-light-beach = Strand med palmträd
newtab-wallpaper-dark-aurora = Norrsken
newtab-wallpaper-dark-color = Röda och blå former
newtab-wallpaper-dark-panda = Röd panda dold i skogen
newtab-wallpaper-dark-sky = Stadslandskap med en natthimmel
newtab-wallpaper-dark-mountain = Landskap med berg
newtab-wallpaper-dark-city = Lila stadslandskap
newtab-wallpaper-dark-fox-anniversary = En räv på trottoaren nära en skog
newtab-wallpaper-light-fox-anniversary = En räv i ett gräsbevuxet fält med ett dimmigt bergslandskap

## Solid Colors

newtab-wallpaper-category-title-colors = Enfärgade färger
newtab-wallpaper-blue = Blå
newtab-wallpaper-light-blue = Ljusblå
newtab-wallpaper-light-purple = Ljuslila
newtab-wallpaper-light-green = Ljusgrön
newtab-wallpaper-green = Grön
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Gul
newtab-wallpaper-orange = Orange
newtab-wallpaper-pink = Rosa
newtab-wallpaper-light-pink = Ljusrosa
newtab-wallpaper-red = Röd
newtab-wallpaper-dark-blue = Mörkblå
newtab-wallpaper-dark-purple = Mörklila
newtab-wallpaper-dark-green = Mörkgrön
newtab-wallpaper-brown = Brun

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakt
newtab-wallpaper-abstract-green = Gröna former
newtab-wallpaper-abstract-blue = Blå former
newtab-wallpaper-abstract-purple = Lila former
newtab-wallpaper-abstract-orange = Orange former
newtab-wallpaper-gradient-orange = Gradient orange och rosa
newtab-wallpaper-abstract-blue-purple = Blå och lila former
newtab-wallpaper-abstract-white-curves = Vit med skuggade kurvor
newtab-wallpaper-abstract-purple-green = Lila och grön ljusgradient
newtab-wallpaper-abstract-blue-purple-waves = Blå och lila vågiga former
newtab-wallpaper-abstract-black-waves = Svarta vågiga former

## Celestial

newtab-wallpaper-category-title-photographs = Fotografier
newtab-wallpaper-beach-at-sunrise = Strand vid soluppgång
newtab-wallpaper-beach-at-sunset = Strand vid solnedgång
newtab-wallpaper-storm-sky = Stormhimlen
newtab-wallpaper-sky-with-pink-clouds = Himmel med rosa moln
newtab-wallpaper-red-panda-yawns-in-a-tree = Röd panda gäspar i ett träd
newtab-wallpaper-white-mountains = Vita berg
newtab-wallpaper-hot-air-balloons = Blandad färg på luftballonger under dagtid
newtab-wallpaper-starry-canyon = Blå stjärnklar natt
newtab-wallpaper-suspension-bridge = Grå fotografering av helhängbro under dagtid
newtab-wallpaper-sand-dunes = Vita sanddyner
newtab-wallpaper-palm-trees = Silhuett av kokospalmer under gyllene timmen
newtab-wallpaper-blue-flowers = Närbild fotografi av blommor med blå kronblad i blom
newtab-wallpaper-forest-trees = Träd täckta med dimma
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto av <a data-l10n-name="name-link">{ $author_string }</a> från <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Prova en skvätt färg
newtab-wallpaper-feature-highlight-content = Ge din Nya flik ett fräscht utseende med bakgrundsbilder.
newtab-wallpaper-feature-highlight-button = Jag förstår
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Ignorera
    .aria-label = Stäng popup
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Rymden
newtab-wallpaper-celestial-lunar-eclipse = Månförmörkelse
newtab-wallpaper-celestial-earth-night = Nattfoto från låg omloppsbana runt jorden
newtab-wallpaper-celestial-starry-sky = Stjärnklara himlen
newtab-wallpaper-celestial-eclipse-time-lapse = Tidsförlopp för månförmörkelse
newtab-wallpaper-celestial-black-hole = Illustration av svarta hål i galaxen
newtab-wallpaper-celestial-river = Satellitbild av floden

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Se prognos i { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsrad
newtab-weather-menu-change-location = Ändra plats
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Sök plats
    .aria-label = Sök plats
newtab-weather-change-location-search-input = Sök plats
newtab-weather-menu-weather-display = Vädervisning
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Enkel
newtab-weather-menu-change-weather-display-simple = Växla till enkel vy
newtab-weather-menu-weather-display-option-detailed = Detaljerad
newtab-weather-menu-change-weather-display-detailed = Växla till detaljerad vy
newtab-weather-menu-temperature-units = Temperaturenheter
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Byt till Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Byt till Celsius
newtab-weather-menu-hide-weather = Dölj väder på ny flik
newtab-weather-menu-learn-more = Läs mer
# This message is shown if user is working offline
newtab-weather-error-not-available = Väderdata är inte tillgänglig just nu.

## Topic Labels

newtab-topic-label-business = Företag
newtab-topic-label-career = Karriär
newtab-topic-label-education = Utbildning
newtab-topic-label-arts = Underhållning
newtab-topic-label-food = Livsmedel
newtab-topic-label-health = Hälsa
newtab-topic-label-hobbies = Spel
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Pengar
newtab-topic-label-society-parenting = Föräldraskap
newtab-topic-label-government = Politik
newtab-topic-label-education-science = Vetenskap
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Lifehack
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Teknik
newtab-topic-label-travel = Resa
newtab-topic-label-home = Hem & trädgård

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Välj ämnen för att finjustera ditt flöde
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Välj två eller flera ämnen. Våra expertkuratorer prioriterar nyheter anpassade efter dina intressen. Uppdatera när som helst.
newtab-topic-selection-save-button = Spara
newtab-topic-selection-cancel-button = Avbryt
newtab-topic-selection-button-maybe-later = Kanske senare
newtab-topic-selection-privacy-link = Lär dig hur vi skyddar och hanterar data
newtab-topic-selection-button-update-interests = Uppdatera dina intressen
newtab-topic-selection-button-pick-interests = Välj dina intressen

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Följ
newtab-section-following-button = Följer
newtab-section-unfollow-button = Sluta följa

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blockera
newtab-section-blocked-button = Blockerad
newtab-section-unblock-button = Blockera inte

## Confirmation modal for blocking a section

newtab-section-cancel-button = Inte nu
newtab-section-confirm-block-topic-p1 = Är du säker på att du vill blockera det här ämnet?
newtab-section-confirm-block-topic-p2 = Blockerade ämnen kommer inte längre att visas i ditt flöde.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blockera { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Ämnen
newtab-section-manage-topics-button-v2 =
    .label = Hantera ämnen
newtab-section-mangage-topics-followed-topics = Följd
newtab-section-mangage-topics-followed-topics-empty-state = Du har inte följt några ämnen än.
newtab-section-mangage-topics-blocked-topics = Blockerad
newtab-section-mangage-topics-blocked-topics-empty-state = Du har inte blockerat några ämnen än.
newtab-custom-wallpaper-title = Anpassade bakgrundsbilder finns här
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Ladda upp din egen bakgrundsbild eller välj en anpassad färg för att göra { -brand-product-name } till din.
newtab-custom-wallpaper-cta = Prova den
