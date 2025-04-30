# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nij ljepblêd
newtab-settings-button =
    .title = Jo side foar nije ljepblêden oanpasse
newtab-personalize-settings-icon-label =
    .title = Nij ljepblêd personalisearje
    .aria-label = Ynstellingen
newtab-settings-dialog-label =
    .aria-label = Ynstellingen
newtab-personalize-icon-label =
    .title = Nij ljepblêd personalisearje
    .aria-label = Nij ljepblêd personalisearje
newtab-personalize-dialog-label =
    .aria-label = Personalisearje
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Sykje
    .aria-label = Sykje
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Mei { $engine } sykje of fier adres yn
newtab-search-box-handoff-text-no-engine = Fier sykterm of adres yn
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Mei { $engine } sykje of fier adres yn
    .title = Mei { $engine } sykje of fier adres yn
    .aria-label = Mei { $engine } sykje of fier adres yn
newtab-search-box-handoff-input-no-engine =
    .placeholder = Fier sykterm of adres yn
    .title = Fier sykterm of adres yn
    .aria-label = Fier sykterm of adres yn
newtab-search-box-text = Sykje op it web
newtab-search-box-input =
    .placeholder = Sykje op it web
    .aria-label = Sykje op it web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Sykmasine tafoegje
newtab-topsites-add-shortcut-header = Nije fluchkeppeling
newtab-topsites-edit-topsites-header = Topwebsite tafoegje
newtab-topsites-edit-shortcut-header = Fluchkeppeling bewurkje
newtab-topsites-add-shortcut-label = Fluchkeppeling tafoegje
newtab-topsites-title-label = Titel
newtab-topsites-title-input =
    .placeholder = Titel ynfiere
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Typ of plak in URL
newtab-topsites-url-validation = Jildige URL fereaske
newtab-topsites-image-url-label = URL fan oanpaste ôfbylding
newtab-topsites-use-image-link = In oanpaste ôfbylding brûke…
newtab-topsites-image-validation = Ofbylding koe net laden wurde. Probearje in oare URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Annulearje
newtab-topsites-delete-history-button = Fuortsmite út skiednis
newtab-topsites-save-button = Bewarje
newtab-topsites-preview-button = Foarbyld
newtab-topsites-add-button = Tafoegje

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Binne jo wis dat jo elke ferwizing fan dizze side út jo skiednis fuortsmite wolle?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Dizze aksje kin net ûngedien makke wurde.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsore

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Menu iepenje
    .aria-label = Menu iepenje
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Fuortsmite
    .aria-label = Fuortsmite
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Menu iepenje
    .aria-label = Kontekstmenu foar { $title } iepenje
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Dizze side bewurkje
    .aria-label = Dizze side bewurkje

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Bewurkje
newtab-menu-open-new-window = Iepenje yn in nij finster
newtab-menu-open-new-private-window = Iepenje yn in nij priveefinster
newtab-menu-dismiss = Fuortsmite
newtab-menu-pin = Fêstsette
newtab-menu-unpin = Losmeitsje
newtab-menu-delete-history = Fuortsmite út skiednis
newtab-menu-save-to-pocket = Bewarje nei { -pocket-brand-name }
newtab-menu-delete-pocket = Fuortsmite út { -pocket-brand-name }
newtab-menu-archive-pocket = Argivearje yn { -pocket-brand-name }
newtab-menu-show-privacy-info = Us sponsors en jo privacy
newtab-menu-about-fakespot = Oer { -fakespot-brand-name }
newtab-menu-report-content = Dizze ynhâld rapportearje
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokkearje
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Underwerp net mear folgje

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Sponsore ynhâld beheare
newtab-menu-our-sponsors-and-your-privacy = Us sponsors en jo privacy
newtab-menu-report-this-ad = Dizze advertinsje rapportearje

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Dien
newtab-privacy-modal-button-manage = Ynstellingen foar sponsore ynhâld beheare
newtab-privacy-modal-header = Jo privacy is wichtich.
newtab-privacy-modal-paragraph-2 =
    Neist it fertellen fan boeiende ferhalen, toane wy jo ek relevante,
    goed trochljochte ynhâld fan selektearre sponsors. Wês gerêst, <strong>jo navigaasjegegevens
    ferlitte nea jo persoanlike eksimplaar fan { -brand-product-name }</strong> – wy krije se net te sjen,
    en ús sponsors ek net.
newtab-privacy-modal-link = Untdek hoe’t privacy wurket op it nije ljepblêd

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Blêdwizer fuortsmite
# Bookmark is a verb here.
newtab-menu-bookmark = Blêdwizer

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Downloadkeppeling kopiearje
newtab-menu-go-to-download-page = Nei downloadside gean
newtab-menu-remove-download = Fuortsmite út skiednis

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Toane yn Finder
       *[other] Byhearrende map iepenje
    }
newtab-menu-open-file = Bestân iepenje

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Besocht
newtab-label-bookmarked = Blêdwizer makke
newtab-label-removed-bookmark = Blêdwizer fuortsmiten
newtab-label-recommended = Trending
newtab-label-saved = Bewarre nei { -pocket-brand-name }
newtab-label-download = Download
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsore
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsore troch { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Seksje fuortsmite
newtab-section-menu-collapse-section = Seksje ynklappe
newtab-section-menu-expand-section = Seksje útklappe
newtab-section-menu-manage-section = Seksje beheare
newtab-section-menu-manage-webext = Utwreiding beheare
newtab-section-menu-add-topsite = Topwebsite tafoegje
newtab-section-menu-add-search-engine = Sykmasine tafoegje
newtab-section-menu-move-up = Omheech ferpleatse
newtab-section-menu-move-down = Omleech ferpleatse
newtab-section-menu-privacy-notice = Privacyferklearring

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Seksje ynklappe
newtab-section-expand-section-label =
    .aria-label = Seksje útklappe

## Section Headers.

newtab-section-header-topsites = Topwebsites
newtab-section-header-recent-activity = Resinte aktiviteit
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Oanrekommandearre troch { $provider }
newtab-section-header-stories = Ferhalen dy’t ta neitinken stimme
# "picks" refers to recommended articles
newtab-section-header-todays-picks = De karren fan hjoed foar jo

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Start mei sneupen en wy toane jo guon moaie artikelen, fideo’s en oare siden dy’t jo resint besocht hawwe of in blêdwizer fan makke hawwe.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Jo binne by. Kom letter werom foar mear ferhalen fan { $provider }. Kin jo net wachtsje? Selektearje in populêr ûnderwerp om mear ferhalen fan it ynternet te finen.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Jo binne wer by. Kom letter werom foar mear ferhalen. Kin jo net wachtsje? Selektearje in populêr ûnderwerp om mear bjusterbaarlike ferhalen fan it hiele web te finen.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Jo binne folslein by!
newtab-discovery-empty-section-topstories-content = Kom letter werom foar mear ferhalen.
newtab-discovery-empty-section-topstories-try-again-button = Opnij probearje
newtab-discovery-empty-section-topstories-loading = Lade…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oeps! Wy hiene dizze seksje hast laden, mar dochs net hielendal.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populêre ûnderwerpen:
newtab-pocket-new-topics-title = Wolle jo noch mear ferhalen? Besjoch dizze populêre ûnderwerpen fan { -pocket-brand-name }
newtab-pocket-more-recommendations = Mear oanrekommandaasjes
newtab-pocket-learn-more = Mear ynfo
newtab-pocket-cta-button = { -pocket-brand-name } brûke
newtab-pocket-cta-text = Bewarje de ferhalen dy’t jo ynteressant fine yn { -pocket-brand-name }, en stimulearje jo tinzen mei boeiende lêsstof.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } is ûnderdiel fan de { -brand-product-name }-famylje
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Bewarje
newtab-pocket-saved = Bewarre

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Mear lykas dit
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Neat foar my
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Tank. Jo feedback sil ús helpe om jo feed te ferbetterjen.
newtab-toast-dismiss-button =
    .title = Slute
    .aria-label = Slute

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Untdek it bêste fan ynternet
newtab-pocket-onboarding-cta = { -pocket-brand-name } ferkent in breed skala oan publikaasjes om de meast ynformative, ynspirearjende en betroubere ynhâld streekrjocht nei jo { -brand-product-name }-browser te bringen.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oeps, der is wat misgien by it laden fan dizze ynhâld.
newtab-error-fallback-refresh-link = Fernij de side om it opnij te probearjen.

## Customization Menu

newtab-custom-shortcuts-title = Fluchkeppelingen
newtab-custom-shortcuts-subtitle = Bewarre of besochte websites
newtab-custom-shortcuts-toggle =
    .label = Fluchkeppelingen
    .description = Bewarre of besochte websites
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rige
       *[other] { $num } rigen
    }
newtab-custom-sponsored-sites = Sponsore fluchkeppelingen
newtab-custom-pocket-title = Oanrekommandearre troch { -pocket-brand-name }
newtab-custom-pocket-subtitle = Utsûnderlike ynhâld, gearstald troch { -pocket-brand-name }, ûnderdiel fan de { -brand-product-name }-famylje
newtab-custom-stories-toggle =
    .label = Oanrekommandearre ferhalen
    .description = Utsûnderlike ynhâld, sammele troch de { -brand-product-name }-famylje
newtab-custom-pocket-sponsored = Sponsore ferhalen
newtab-custom-pocket-show-recent-saves = Koartlyn bewarre items toane
newtab-custom-recent-title = Resinte aktiviteit
newtab-custom-recent-subtitle = In seleksje fan resinte websites en ynhâld
newtab-custom-recent-toggle =
    .label = Resinte aktiviteit
    .description = In seleksje fan resinte websites en ynhâld
newtab-custom-weather-toggle =
    .label = It waar
    .description = De waarsferwachting fan hjoed yn ien eachopslach
newtab-custom-close-button = Slute
newtab-custom-settings = Mear ynstellingen beheare

## New Tab Wallpapers

newtab-wallpaper-title = Eftergrûnen
newtab-wallpaper-reset = Standertwearden
newtab-wallpaper-upload-image = In ôfbylding oplade
newtab-wallpaper-custom-color = Kies in kleur
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = De ôfbylding giet oer de limyt fan { $file_size } MB. Probearje in lytser bestân op te laden.
newtab-wallpaper-error-file-type = Wy koene jo bestân net oplade. Probearje it nochris mei in oar bestânstype.
newtab-wallpaper-light-red-panda = Reade panda
newtab-wallpaper-light-mountain = Wite berch
newtab-wallpaper-light-sky = Himel mei pearze en rôze wolken
newtab-wallpaper-light-color = Blauwe, rôze en giele foarmen
newtab-wallpaper-light-landscape = Berch lânskip mei blauwe mist
newtab-wallpaper-light-beach = Strân mei palmbeam
newtab-wallpaper-dark-aurora = Noarderljocht
newtab-wallpaper-dark-color = Reade en blauwe foarmen
newtab-wallpaper-dark-panda = Reade panda ferburgen yn bosk
newtab-wallpaper-dark-sky = Stedslânskip mei in nachthimel
newtab-wallpaper-dark-mountain = Lânskip mei berch
newtab-wallpaper-dark-city = Pears stêdslânskip
newtab-wallpaper-dark-fox-anniversary = In foks op de stoepe by in bosk
newtab-wallpaper-light-fox-anniversary = In foks yn in gersfjild mei in mistich berchlânskip

## Solid Colors

newtab-wallpaper-category-title-colors = Effen kleuren
newtab-wallpaper-blue = Blau
newtab-wallpaper-light-blue = Ljochtblau
newtab-wallpaper-light-purple = Ljochtpears
newtab-wallpaper-light-green = Ljochtgrien
newtab-wallpaper-green = Grien
newtab-wallpaper-beige = Bêzje
newtab-wallpaper-yellow = Giel
newtab-wallpaper-orange = Oranje
newtab-wallpaper-pink = Rôze
newtab-wallpaper-light-pink = Ljochtrôze
newtab-wallpaper-red = Read
newtab-wallpaper-dark-blue = Donkerblau
newtab-wallpaper-dark-purple = Donkerpears
newtab-wallpaper-dark-green = Donkergrien
newtab-wallpaper-brown = Brún

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakt
newtab-wallpaper-abstract-green = Griene foarmen
newtab-wallpaper-abstract-blue = Blauwe foarmen
newtab-wallpaper-abstract-purple = Pearze foarmen
newtab-wallpaper-abstract-orange = Oranje foarmen
newtab-wallpaper-gradient-orange = Ferrin oranje en rôze
newtab-wallpaper-abstract-blue-purple = Blauwe en pearze foarmen
newtab-wallpaper-abstract-white-curves = Wyt mei arsearre rûningen
newtab-wallpaper-abstract-purple-green = Pears en griene ljochtgradiïnt
newtab-wallpaper-abstract-blue-purple-waves = Blauwe en pearze golvjende foarmen
newtab-wallpaper-abstract-black-waves = Swarte golvjende foarmen

## Celestial

newtab-wallpaper-category-title-photographs = Foto’s
newtab-wallpaper-beach-at-sunrise = Strân by sinneopgong
newtab-wallpaper-beach-at-sunset = Strân by sinneûndergong
newtab-wallpaper-storm-sky = Tongerloft
newtab-wallpaper-sky-with-pink-clouds = Loft mei rôze wolken
newtab-wallpaper-red-panda-yawns-in-a-tree = Reade panda gappet yn in beam
newtab-wallpaper-white-mountains = Wite bergen
newtab-wallpaper-hot-air-balloons = Hjitteluchtballonnen in ferskate kleuren oerdeis
newtab-wallpaper-starry-canyon = Blauwe stjerrenacht
newtab-wallpaper-suspension-bridge = Foto’s fan in folsleine hingbrêge oerdeis
newtab-wallpaper-sand-dunes = Wite sândunen
newtab-wallpaper-palm-trees = Silhûet fan kokospalms wylst gouden oere
newtab-wallpaper-blue-flowers = Close-upfotografy fan blauwe blommen yn bloei
newtab-wallpaper-forest-trees = Beammen bedekt mei mist
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto troch <a data-l10n-name="name-link">{ $author_string }</a> op <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Probearje in bytsje kleur
newtab-wallpaper-feature-highlight-content = Jou jo Nije-ljepblêdside in frisse útstrieling mei eftergrûnen.
newtab-wallpaper-feature-highlight-button = Begrepen
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Slute
    .aria-label = Pop-up slute
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Kosmysk
newtab-wallpaper-celestial-lunar-eclipse = Moannefertsjustering
newtab-wallpaper-celestial-earth-night = Nachtfoto fan in lege baan om de ierde út
newtab-wallpaper-celestial-starry-sky = Stjerrehimel
newtab-wallpaper-celestial-eclipse-time-lapse = Time-lapse fan moannefertsjustering
newtab-wallpaper-celestial-black-hole = Yllustraasje fan in swart-gatstjerrestelsel
newtab-wallpaper-celestial-river = Satellytfoto fan rivier

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = It waar besjen foar { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsore
newtab-weather-menu-change-location = Lokaasje wizigje
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Lokaasje sykje
    .aria-label = Lokaasje sykje
newtab-weather-change-location-search-input = Lokaasje sykje
newtab-weather-menu-weather-display = Waarwerjefte
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Ienfâldich
newtab-weather-menu-change-weather-display-simple = Wikselje nei ienfâldige werjefte
newtab-weather-menu-weather-display-option-detailed = Detaillearre
newtab-weather-menu-change-weather-display-detailed = Wikselje nei detaillearre werjefte
newtab-weather-menu-temperature-units = Temperatuerienheden
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Wikselje nei Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Wikselje nei Celsius
newtab-weather-menu-hide-weather = It waar op nij ljepblêd ferstopje
newtab-weather-menu-learn-more = Mear ynfo
# This message is shown if user is working offline
newtab-weather-error-not-available = Waargegevens binne op dit stuit net beskikber.

## Topic Labels

newtab-topic-label-business = Saaklik
newtab-topic-label-career = Karriêren
newtab-topic-label-education = Underwiis
newtab-topic-label-arts = Ferdivedaasje
newtab-topic-label-food = Iten
newtab-topic-label-health = Sûnens
newtab-topic-label-hobbies = Gaming
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Jild
newtab-topic-label-society-parenting = Alderskip en opfieding
newtab-topic-label-government = Polityk
newtab-topic-label-education-science = Wittenskip
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Lifehacks
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Technology
newtab-topic-label-travel = Reizgjen
newtab-topic-label-home = Hûs en tún

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Selektearje ûnderwerpen om jo feed te ferfynjen
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Kies twa of mear ûnderwerpen. Us saakkundige kurators prioritearje ferhalen ôfstimd op jo ynteressen. Wurkje op elk momint by.
newtab-topic-selection-save-button = Bewarje
newtab-topic-selection-cancel-button = Annulearje
newtab-topic-selection-button-maybe-later = Miskien letter
newtab-topic-selection-privacy-link = Lês hoe’t wy gegevens beskermje en beheare
newtab-topic-selection-button-update-interests = Wurkje jo ynteressen by
newtab-topic-selection-button-pick-interests = Kies jo ynteressen

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Folgje
newtab-section-following-button = Folgjend
newtab-section-unfollow-button = Untfolgje

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokkearje
newtab-section-blocked-button = Blokkearre
newtab-section-unblock-button = Blokkearring opheffe

## Confirmation modal for blocking a section

newtab-section-cancel-button = No net
newtab-section-confirm-block-topic-p1 = Binne jo wis dat jo dit ûnderwerp blokkearje wolle?
newtab-section-confirm-block-topic-p2 = Blokkearre ûnderwerpen ferskine net mear yn jo feed.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = { $topic } blokkearje

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Underwerpen
newtab-section-manage-topics-button-v2 =
    .label = Underwerpen beheare
newtab-section-mangage-topics-followed-topics = Folge
newtab-section-mangage-topics-followed-topics-empty-state = Jo hawwe noch gjin ûnderwerpen folge.
newtab-section-mangage-topics-blocked-topics = Blokkearre
newtab-section-mangage-topics-blocked-topics-empty-state = Jo hawwe noch gjin ûnderwerpen blokkearre.
newtab-custom-wallpaper-title = Hjir fine jo oanpaste eftergrûnen
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Laad jo eigen eftergrûn op of kies in oanpaste kleur om { -brand-product-name } fan josels te meitsjen.
newtab-custom-wallpaper-cta = Probearje
