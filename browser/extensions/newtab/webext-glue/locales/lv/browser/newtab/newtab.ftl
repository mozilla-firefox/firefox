# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Jauna cilne
newtab-settings-button =
    .title = Pielāgojiet jaunās cilnes lapu
newtab-personalize-settings-icon-label =
    .title = Personalizēt jauno cilni
    .aria-label = Iestatījumi
newtab-settings-dialog-label =
    .aria-label = Iestatījumi
newtab-personalize-icon-label =
    .title = Personalizēt jauno cilni
    .aria-label = Personalizēt jauno cilni
newtab-personalize-dialog-label =
    .aria-label = Personalizēt
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Meklēt
    .aria-label = Meklēt
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Meklējiet, izmantojot { $engine }, vai ievadiet adresi
newtab-search-box-handoff-text-no-engine = Meklējiet vai ievadiet adresi
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Meklējiet ar { $engine } vai ievadiet adresi
    .title = Meklējiet ar { $engine } vai ievadiet adresi
    .aria-label = Meklējiet ar { $engine } vai ievadiet adresi
newtab-search-box-handoff-input-no-engine =
    .placeholder = Meklējiet vai ievadiet adresi
    .title = Meklējiet vai ievadiet adresi
    .aria-label = Meklējiet vai ievadiet adresi
newtab-search-box-text = Meklēt tīmeklī
newtab-search-box-input =
    .placeholder = Meklēt tīmeklī
    .aria-label = Meklēt tīmeklī

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Pievienot meklētāju
newtab-topsites-add-shortcut-header = Jauna saīsne
newtab-topsites-edit-topsites-header = Rediģēt populārās vietnes
newtab-topsites-edit-shortcut-header = Rediģēt saīsni
newtab-topsites-add-shortcut-label = Pievienot saīsni
newtab-topsites-title-label = Virsraksts
newtab-topsites-title-input =
    .placeholder = Ievadīt nosaukumu
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Ievadiet vai iekopējiet adresi
newtab-topsites-url-validation = Nepieciešama derīga adrese
newtab-topsites-image-url-label = Pielāgota attēla adrese
newtab-topsites-use-image-link = Izmantot pielāgotu attēlu…
newtab-topsites-image-validation = Neizdevās ielādēt attēlu. Izmēģiniet citu adresi.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Atcelt
newtab-topsites-delete-history-button = Dzēst no vēstures
newtab-topsites-save-button = Saglabāt
newtab-topsites-preview-button = Priekšskatījums
newtab-topsites-add-button = Pievienot

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Vai tiešām vēlaties dzēst visas šīs lapas versijas no jūsu vēstures?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Šī ir neatgriezeniska darbība.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Apmaksāts

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Atvērt izvēlni
    .aria-label = Atvērt izvēlni
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Noņemt
    .aria-label = Noņemt
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Atvērt izvēlni
    .aria-label = Atvērt izvēlni { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Rediģēt šo vietni
    .aria-label = Rediģēt šo vietni

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Rediģēt
newtab-menu-open-new-window = Atvērt jaunā logā
newtab-menu-open-new-private-window = Atvērt jaunā privātajā logā
newtab-menu-dismiss = Noraidīt
newtab-menu-pin = Piespraust
newtab-menu-unpin = Atspraust
newtab-menu-delete-history = Dzēst no vēstures
newtab-menu-save-to-pocket = Saglabāt { -pocket-brand-name }
newtab-menu-delete-pocket = Dzēst no { -pocket-brand-name }
newtab-menu-archive-pocket = Arhivēt { -pocket-brand-name }
newtab-menu-show-privacy-info = Mūsu sponsori un jūsu privātums
newtab-menu-about-fakespot = Par { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bloķēt
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Pārtraukt sekot tēmai

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gatavs
newtab-privacy-modal-button-manage = Pārvaldīt apmaksāta satura iestatījumus
newtab-privacy-modal-header = Jūsu privātumam ir nozīme.
newtab-privacy-modal-paragraph-2 =
    Papildus aizraujošiem stāstiem mēs rādām arī atbilstošu,
    kārtīgi pārbaudītu saturu no atlasītiem sponsoriem. Satraukumam nav pamata, jo <strong>pārlūkošanas
    dati nekad neatstāj personīgo { -brand-product-name } kopiju</strong> — ne mēs, ne mūsu sponsori tos neredz.
newtab-privacy-modal-link = Jaunā cilnē uzziniet, kā darbojas privātums

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Noņemt grāmatzīmi
# Bookmark is a verb here.
newtab-menu-bookmark = Saglabāt grāmatzīmēs

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopēt lejupielādes saiti
newtab-menu-go-to-download-page = Iet uz lejupielādes lapu
newtab-menu-remove-download = Noņemt no vēstures

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Parādīt Finder
       *[other] Atvērt mapi
    }
newtab-menu-open-file = Atvērt datni

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Apmeklēta
newtab-label-bookmarked = Grāmatzīmēs
newtab-label-removed-bookmark = Grāmatzīme noņemta
newtab-label-recommended = Populāri
newtab-label-saved = Saglabāts { -pocket-brand-name }
newtab-label-download = Lejupielādēts
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · sponsorēts
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsorē { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Izņemt sadaļu
newtab-section-menu-collapse-section = Sakļaut sadaļu
newtab-section-menu-expand-section = Izvērst sadaļu
newtab-section-menu-manage-section = Pārvaldīt sadaļu
newtab-section-menu-manage-webext = Pārvaldīt paplašinājumu
newtab-section-menu-add-topsite = Pievienot populāru vietni
newtab-section-menu-add-search-engine = Pievienot meklētāju
newtab-section-menu-move-up = Pārvietot augšup
newtab-section-menu-move-down = Pārvietot lejup
newtab-section-menu-privacy-notice = Privātuma politika

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Sakļaut sadaļu
newtab-section-expand-section-label =
    .aria-label = Izvērst sadaļu

## Section Headers.

newtab-section-header-topsites = Populārākās lapas
newtab-section-header-recent-activity = Nesenās aktivitātes
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Iesaka { $provider }
newtab-section-header-stories = Pārdomas rosinoši stāsti
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Šodienas izlase jums

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Sāciet pārlūkošanu un mēs šeit parādīsim lieliskus rakstus, video un citas apmeklētās lapas.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Viss ir apskatīts! Atnāciet atpakaļ nedaudz vēlāk, lai redzētu populāros stāstus no { $provider }. Nevarat sagaidīt? Izvēlieties kādu no tēmām jau tagad.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Esat visu apskatījis. Atgriezieties vēlāk, lai skatītu citus stāstus. Nevarat sagaidīt? Atlasiet populāru tēmu, lai atrastu vairāk lielisku stāstu no visa tīmekļa.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Jūs esat visu apskatījis!
newtab-discovery-empty-section-topstories-content = Atgriezieties vēlāk, lai skatītu citus stāstus.
newtab-discovery-empty-section-topstories-try-again-button = Mēģināt vēlreiz
newtab-discovery-empty-section-topstories-loading = Ielādē…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hmm! Mēs gandrīz ielādējām šo sadaļu, bet ne gluži.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Populārās tēmas:
newtab-pocket-new-topics-title = Vai vēlaties vēl vairāk stāstu? Skatiet šīs populārās tēmas no { -pocket-brand-name }
newtab-pocket-more-recommendations = Vairāk ieteikumu
newtab-pocket-learn-more = Uzzināt vairāk
newtab-pocket-cta-button = Izmēģiniet { -pocket-brand-name }
newtab-pocket-cta-text = Saglabājiet interesantus stāstus { -pocket-brand-name } un barojiet savu prātu ar interesantu lasāmvielu.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } ir daļa no { -brand-product-name } saimes
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Saglabāt
newtab-pocket-saved = Saglabāts

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Vairāk šādus
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Tas nav man
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Paldies. Jūsu atsauksmes palīdzēs mums uzlabot jūsu plūsmu.
newtab-toast-dismiss-button =
    .title = Noraidīt
    .aria-label = Noraidīt

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Atklājiet labāko no tīmekļa
newtab-pocket-onboarding-cta = { -pocket-brand-name } izpēta daudzveidīgu publikāciju klāstu, lai sniegtu visinformatīvāko, iedvesmojošāko un uzticamāko saturu tieši jūsu { -brand-product-name } pārlūkā.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ak vai, ielādējot saturu kaut kas nogājis greizi.
newtab-error-fallback-refresh-link = Pārlādējiet lapu, lai mēģinātu vēlreiz.

## Customization Menu

newtab-custom-shortcuts-title = Saīsnes
newtab-custom-shortcuts-subtitle = Saglabātās vai apmeklētās vietnes
newtab-custom-shortcuts-toggle =
    .label = Saīsnes
    .description = Saglabātās vai apmeklētās vietnes
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [zero] { $num } rinda
        [one] { $num } rindas
       *[other] { $num } rindu
    }
newtab-custom-sponsored-sites = Sponsorētās saīsnes
newtab-custom-pocket-title = Ieteica { -pocket-brand-name }
newtab-custom-pocket-subtitle = Izcils saturs, ko atlasījis { -pocket-brand-name }, kas ir daļa no { -brand-product-name } saimes
newtab-custom-stories-toggle =
    .label = Ieteiktie stāsti
    .description = Izcils saturs, ko atlasīja { -brand-product-name } saime
newtab-custom-pocket-sponsored = Sponsorētie stāsti
newtab-custom-pocket-show-recent-saves = Rādīt nesen saglabāto
newtab-custom-recent-title = Nesenās aktivitātes
newtab-custom-recent-subtitle = Neseno vietņu un satura izlase
newtab-custom-recent-toggle =
    .label = Nesenās aktivitātes
    .description = Neseno vietņu un satura izlase
newtab-custom-weather-toggle =
    .label = Laikapstākļi
    .description = Šodienas prognoze ātrā acu uzmetienā
newtab-custom-close-button = Aizvērt
newtab-custom-settings = Pārvaldīt vairāk iestatījumu

## New Tab Wallpapers

newtab-wallpaper-title = Tapetes
newtab-wallpaper-reset = Atiestatīt uz noklusējumu
newtab-wallpaper-light-red-panda = Sarkana panda
newtab-wallpaper-light-mountain = Balts kalns
newtab-wallpaper-light-sky = Debesis ar violetiem un rozā mākoņiem
newtab-wallpaper-light-color = Zilas, rozā un dzeltenas formas
newtab-wallpaper-light-landscape = Zilas miglas kalnu ainava
newtab-wallpaper-light-beach = Pludmale ar palmu
newtab-wallpaper-dark-aurora = Ziemeļblāzma
newtab-wallpaper-dark-color = Sarkanas un zilas formas
newtab-wallpaper-dark-panda = Sarkanā panda paslēpta mežā
newtab-wallpaper-dark-sky = Pilsētas ainava ar nakts debesīm
newtab-wallpaper-dark-mountain = Ainavisks kalns
newtab-wallpaper-dark-city = Violeta pilsētas ainava
newtab-wallpaper-dark-fox-anniversary = Lapsa uz ceļa pie meža
newtab-wallpaper-light-fox-anniversary = Lapsa pļavā ar dūmakainu kalnu ainavu

## Solid Colors

newtab-wallpaper-category-title-colors = Vienkrāsains
newtab-wallpaper-blue = Zils
newtab-wallpaper-light-blue = Gaiši zils
newtab-wallpaper-light-purple = Gaiši violets
newtab-wallpaper-light-green = Gaiši zaļš
newtab-wallpaper-green = Zaļš
newtab-wallpaper-beige = Bēšs
newtab-wallpaper-yellow = Dzeltens
newtab-wallpaper-orange = Oranžs
newtab-wallpaper-pink = Rozā
newtab-wallpaper-light-pink = Gaiši rozā
newtab-wallpaper-red = Sarkans
newtab-wallpaper-dark-blue = Tumši zils
newtab-wallpaper-dark-purple = Tumši violets
newtab-wallpaper-dark-green = Tumši zaļš
newtab-wallpaper-brown = Brūns

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakts
newtab-wallpaper-abstract-green = Zaļas formas
newtab-wallpaper-abstract-blue = Zilas formas
newtab-wallpaper-abstract-purple = Violetas formas
newtab-wallpaper-abstract-orange = Oranžas formas
newtab-wallpaper-gradient-orange = Oranža un rozā krāsu pāreja
newtab-wallpaper-abstract-blue-purple = Zilas un violetas formas

## Celestial

newtab-wallpaper-category-title-photographs = Fotogrāfijas
newtab-wallpaper-beach-at-sunrise = Pludmale saullēktā
newtab-wallpaper-beach-at-sunset = Pludmale saulrietā
newtab-wallpaper-storm-sky = Vētras debesis
newtab-wallpaper-sky-with-pink-clouds = Debesis ar rozā mākoņiem
newtab-wallpaper-red-panda-yawns-in-a-tree = Sarkanā panda žāvājas kokā
newtab-wallpaper-white-mountains = Balti kalni
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Fotografēja <a data-l10n-name="name-link">{ $author_string }</a>, vietne <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Izmēģiniet krāsu akcentu
newtab-wallpaper-feature-highlight-content = Piešķiriet savai jaunajai cilnei svaigu izskatu ar tapetēm.
newtab-wallpaper-feature-highlight-button = Sapratu
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Noraidīt
    .aria-label = Aizvērt uznirstošo logu
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Skatīt prognozi { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ sponsorēts
newtab-weather-menu-change-location = Mainīt atrašanās vietu
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Meklēt atrašanās vietu
    .aria-label = Meklēt atrašanās vietu
newtab-weather-change-location-search-input = Meklēt atrašanās vietu
newtab-weather-menu-weather-display = Laikapstākļu attēlotājs
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Vienkāršs
newtab-weather-menu-change-weather-display-simple = Pārslēgties uz vienkāršo skatu
newtab-weather-menu-weather-display-option-detailed = Detalizēts
newtab-weather-menu-change-weather-display-detailed = Pārslēgties uz detalizēto skatu
newtab-weather-menu-temperature-units = Temperatūras mērvienības
newtab-weather-menu-temperature-option-fahrenheit = Fārenheita
newtab-weather-menu-temperature-option-celsius = Celsija
newtab-weather-menu-change-temperature-units-fahrenheit = Pārslēgties uz Fārenheita skalu
newtab-weather-menu-change-temperature-units-celsius = Pārslēgties uz Celsija skalu
newtab-weather-menu-hide-weather = Paslēpt laikapstākļus jaunā cilnē
newtab-weather-menu-learn-more = Uzzināt vairāk
# This message is shown if user is working offline
newtab-weather-error-not-available = Laikapstākļu dati pašlaik nav pieejami.

## Topic Labels

newtab-topic-label-business = Bizness
newtab-topic-label-career = Karjera
newtab-topic-label-education = Izglītība
newtab-topic-label-arts = Izklaide
newtab-topic-label-food = Ēdiens
newtab-topic-label-health = Veselība
newtab-topic-label-hobbies = Spēles
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Nauda
newtab-topic-label-society-parenting = Audzināšana
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Zinātne
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Sevis pilnveidošana
newtab-topic-label-sports = Sports
newtab-topic-label-tech = Tehnoloģijas
newtab-topic-label-travel = Ceļošana
newtab-topic-label-home = Māja un dārzs

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Atlasiet tēmas, lai pielāgotu plūsmu
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Izvēlieties divas vai vairākas tēmas. Mūsu kuratori piešķir prioritāti stāstiem, kas atbilst jūsu interesēm. Atjauniniet jebkurā laikā.
newtab-topic-selection-save-button = Saglabāt
newtab-topic-selection-cancel-button = Atcelt
newtab-topic-selection-button-maybe-later = Varbūt vēlāk
newtab-topic-selection-privacy-link = Uzziniet, kā mēs aizsargājam un pārvaldām datus
newtab-topic-selection-button-update-interests = Atjauniniet savas intereses
newtab-topic-selection-button-pick-interests = Izvēlieties savas intereses

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sekot
newtab-section-following-button = Seko
newtab-section-unfollow-button = Pārtraukt sekošanu

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section

newtab-section-cancel-button = Ne tagad

## Panel in the Customize menu section to manage followed and blocked topics

