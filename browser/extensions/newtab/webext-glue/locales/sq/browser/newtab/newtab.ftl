# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Skedë e Re
newtab-settings-button =
    .title = Personalizoni faqen tuaj Skedë e Re
newtab-personalize-settings-icon-label =
    .title = Personalizoni Skedën e Re
    .aria-label = Rregullime
newtab-settings-dialog-label =
    .aria-label = Rregullime
newtab-personalize-icon-label =
    .title = Personalizoni skedën e re
    .aria-label = Personalizoni skedën e re
newtab-personalize-dialog-label =
    .aria-label = Personalizojeni
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Kërko
    .aria-label = Kërko
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Kërkoni me { $engine } ose jepni adresë
newtab-search-box-handoff-text-no-engine = Bëni kërkim, ose jepni adresë
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Kërkoni me { $engine } ose jepni adresë
    .title = Kërkoni me { $engine } ose jepni adresë
    .aria-label = Kërkoni me { $engine } ose jepni adresë
newtab-search-box-handoff-input-no-engine =
    .placeholder = Bëni kërkim, ose jepni adresë
    .title = Bëni kërkim, ose jepni adresë
    .aria-label = Bëni kërkim, ose jepni adresë
newtab-search-box-text = Kërkoni në Web
newtab-search-box-input =
    .placeholder = Kërkoni në Web
    .aria-label = Kërkoni në Web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Shtoni Motor Kërkimesh
newtab-topsites-add-shortcut-header = Shkurtore e Re
newtab-topsites-edit-topsites-header = Përpunoni Sajtin Kryesues
newtab-topsites-edit-shortcut-header = Përpunoni Shkurtore
newtab-topsites-add-shortcut-label = Shtoni Shkurtore
newtab-topsites-title-label = Titull
newtab-topsites-title-input =
    .placeholder = Jepni një titull
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Shtypni ose hidhni një URL
newtab-topsites-url-validation = Lypset URL e vlefshme
newtab-topsites-image-url-label = URL Figure Vetjake
newtab-topsites-use-image-link = Përdorni një figurë vetjake…
newtab-topsites-image-validation = Dështoi ngarkimi i figurës. Provoni një URL tjetër.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Anuloje
newtab-topsites-delete-history-button = Fshije nga Historiku
newtab-topsites-save-button = Ruaje
newtab-topsites-preview-button = Paraparje
newtab-topsites-add-button = Shtoje

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Jeni të sigurt se doni të fshini nga historiku çdo instancë të kësaj faqeje?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ky veprim s’mund të zhbëhet.

## Top Sites - Sponsored label

newtab-topsite-sponsored = E sponsorizuar

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Hapni menunë
    .aria-label = Hapni menunë
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Hiqe
    .aria-label = Hiqe
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Hapni menunë
    .aria-label = Hapni menu konteksti për { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Përpunoni këtë sajt
    .aria-label = Përpunoni këtë sajt

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Përpunoni
newtab-menu-open-new-window = Hape në Dritare të Re
newtab-menu-open-new-private-window = Hape në Dritare të Re Private
newtab-menu-dismiss = Hidhe tej
newtab-menu-pin = Fiksoje
newtab-menu-unpin = Shfiksoje
newtab-menu-delete-history = Fshije nga Historiku
newtab-menu-save-to-pocket = Ruaje te { -pocket-brand-name }
newtab-menu-delete-pocket = Fshije nga { -pocket-brand-name }
newtab-menu-archive-pocket = Arkivoje në { -pocket-brand-name }
newtab-menu-show-privacy-info = Sponsorët tanë & privatësia jonë
newtab-menu-about-fakespot = Mvi { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Bllokoje
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Hiqe Ndjekjen e Subjektit

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Administroni lëndë të sponsorizuar
newtab-menu-our-sponsors-and-your-privacy = Sponsorët tanë dhe privatësia jonë

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Kaq qe
newtab-privacy-modal-button-manage = Administroni rregullime lënde të sponsorizuar
newtab-privacy-modal-header = Privatësia juaj ka rëndësi.
newtab-privacy-modal-paragraph-2 = Jo vetëm ju shërbejmë histori tërheqëse, por ju shfaqim edhe lëndë me vlerë, të kontrolluar mirë, prej sponsorësh të përzgjedhur. Flijeni mendjen, <strong>të dhënat e shfletimit tuaj nuk ikin kurrë nga kopja juaj personale e { -brand-product-name }-it</strong> — as ne nuk i shohim dot, as sponsorët tanë.
newtab-privacy-modal-link = Mësoni se si funksionon privatësia në skedën e re

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Hiqe Faqerojtësin
# Bookmark is a verb here.
newtab-menu-bookmark = Faqerojtës

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopjo Lidhjen e Shkarkimit
newtab-menu-go-to-download-page = Shko Te Faqja e Shkarkimit
newtab-menu-remove-download = Hiqe nga Historiku

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Shfaqe Në Finder
       *[other] Hap Dosjen Përkatëse
    }
newtab-menu-open-file = Hape Kartelën

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Të vizituara
newtab-label-bookmarked = Të faqeruajtura
newtab-label-removed-bookmark = Faqerojtësi u hoq
newtab-label-recommended = Në modë
newtab-label-saved = U ruajt te { -pocket-brand-name }
newtab-label-download = Të shkarkuara
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · E sponsorizuar
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsorizuar nga { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } minuta

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Hiqe Ndarjen
newtab-section-menu-collapse-section = Tkurre Ndarjen
newtab-section-menu-expand-section = Zgjeroje Ndarjen
newtab-section-menu-manage-section = Administroni Ndarjen
newtab-section-menu-manage-webext = Administroni Zgjerimin
newtab-section-menu-add-topsite = Shtoni Sajt Kryesues
newtab-section-menu-add-search-engine = Shtoni Motor Kërkimesh
newtab-section-menu-move-up = Ngrije
newtab-section-menu-move-down = Ule
newtab-section-menu-privacy-notice = Shënim Privatësie

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Tkurre Ndarjen
newtab-section-expand-section-label =
    .aria-label = Zgjeroje Ndarjen

## Section Headers.

newtab-section-header-topsites = Sajte Kryesues
newtab-section-header-recent-activity = Veprimtari së fundi
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Rekomanduar nga { $provider }
newtab-section-header-stories = Histori që të vënë në mendim
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Zgjedhjet e sotme për ju

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Filloni shfletimin dhe do t'ju shfaqim disa nga artikujt, videot dhe të tjera faqe interesante që keni vizituar apo faqeruajtur këtu kohët e fundit.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Gjithë ç’kish, e dini. Rikontrolloni më vonë për më tepër histori nga { $provider }. S’pritni dot? Përzgjidhni një temë popullore që të gjenden në internet më tepër histori të goditura.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Gjithë ç’kishte, e dini. Rikontrolloni më vonë për më tepër histori. S’pritni dot? Përzgjidhni një temë popullore, që të gjenden në internet më tepër histori të goditura.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = S’ka tjetër!
newtab-discovery-empty-section-topstories-content = Kontrolloni më vonë për më tepër shembuj.
newtab-discovery-empty-section-topstories-try-again-button = Riprovoni
newtab-discovery-empty-section-topstories-loading = Po ngarkohet…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hëm! Thuajse e ngarkuam këtë ndarje, por jo dhe aq.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Tema Popullore:
newtab-pocket-new-topics-title = Doni më tepër gjëra? Shihni këto tema popullore prej { -pocket-brand-name }
newtab-pocket-more-recommendations = Më Tepër Rekomandime
newtab-pocket-learn-more = Mësoni më tepër
newtab-pocket-cta-button = Merreni { -pocket-brand-name }-in
newtab-pocket-cta-text = Ruajini në { -pocket-brand-name } shkrimet që doni dhe ushqejeni mendjen me lexime të mahnitshme.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } është pjesë e familjes { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Ruaje
newtab-pocket-saved = U ruajt

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Më tepër si kjo
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Jo për mua
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Faleminderit. Përshtypjet tuaja do të na ndihmojnë të përmirësojmë prurjen për ju.
newtab-toast-dismiss-button =
    .title = Hidhe tej
    .aria-label = Hidhe tej

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Zbuloni më të mirat e internetit
newtab-pocket-onboarding-cta = { -pocket-brand-name } eksploron një gamë të larmishme botimesh për të sjellë lëndën më informative, në frymëzuese dhe më të besueshme drejt e në shfletuesin tuaj { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Hëm, diç shkoi ters në ngarkimin e kësaj lënde.
newtab-error-fallback-refresh-link = Rifreskoni faqen që të riprovohet.

## Customization Menu

newtab-custom-shortcuts-title = Shkurtore
newtab-custom-shortcuts-subtitle = Sajte që ruani ose vizitoni
newtab-custom-shortcuts-toggle =
    .label = Shkurtore
    .description = Sajte që ruani ose vizitoni
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } rresht
       *[other] { $num } rreshta
    }
newtab-custom-sponsored-sites = Shkurtore të sponsorizuara
newtab-custom-pocket-title = Rekomanduar nga { -pocket-brand-name }
newtab-custom-pocket-subtitle = Lëndë e jashtëzakonshme, në kujdesin e { -pocket-brand-name }, pjesë e familjes { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Histori të rekomanduara
    .description = Lëndë e veçantë, nën kujdesin e familjes { -brand-product-name }
newtab-custom-pocket-sponsored = Histori të sponsorizuara
newtab-custom-pocket-show-recent-saves = Shfaq të ruajturat së fundi
newtab-custom-recent-title = Veprimtari së fundi
newtab-custom-recent-subtitle = Një përzgjedhje sajtesh dhe lënde së fundi
newtab-custom-recent-toggle =
    .label = Veprimtari së fundi
    .description = Një përzgjedhje sajtesh dhe lënde së fundi
newtab-custom-weather-toggle =
    .label = Moti
    .description = Parashikimi i motit për sot me një vështrim
newtab-custom-close-button = Mbylle
newtab-custom-settings = Administroni më tepër rregullime

## New Tab Wallpapers

newtab-wallpaper-title = Sfonde
newtab-wallpaper-reset = Riktheje te parazgjedhjet
newtab-wallpaper-upload-image = Ngarkoni një figurë
newtab-wallpaper-custom-color = Zgjidhni një ngjyrë
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Figura tejkalonte kufirin { $file_size }MB e madhësive të kartelave. Ju lutemi, provoni të ngarkoni një kartelë më të vogël.
newtab-wallpaper-error-file-type = S’e ngarkuam dot kartelën tuaj. Ju lutemi, riprovoni me një lloj tjetër kartele.
newtab-wallpaper-light-red-panda = Panda e kuqe
newtab-wallpaper-light-mountain = Mal i bardhë
newtab-wallpaper-light-sky = Qiell me re të purpurta dhe të trëndafilta
newtab-wallpaper-light-color = Forma në ngjyrë blu, të trëndafiltë dhe të verdhë
newtab-wallpaper-light-landscape = Peizazh malor me mjegull të kaltër
newtab-wallpaper-light-beach = Plazh me palma
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Forma në ngjyrë të kuqe dhe blu
newtab-wallpaper-dark-panda = Panda e kuqe e fshehur në pyll
newtab-wallpaper-dark-sky = Reliev qyteti me qiell nate
newtab-wallpaper-dark-mountain = Peizazh malor
newtab-wallpaper-dark-city = Peizazh qyteti i purpurt
newtab-wallpaper-dark-fox-anniversary = Një dhelpër në shesh pranë një pylli
newtab-wallpaper-light-fox-anniversary = Një dhelpër në një lëndinë, në një peizazh malor të mjegullt

## Solid Colors

newtab-wallpaper-category-title-colors = Ngjyra të plota
newtab-wallpaper-blue = Blu
newtab-wallpaper-light-blue = Blu e çelët
newtab-wallpaper-light-purple = E purpur e çelët
newtab-wallpaper-light-green = E gjelbër e çelët
newtab-wallpaper-green = E gjelbër
newtab-wallpaper-beige = Bezhë
newtab-wallpaper-yellow = E verdhë
newtab-wallpaper-orange = Portokalli
newtab-wallpaper-pink = Rozë
newtab-wallpaper-light-pink = Rozë e çelët
newtab-wallpaper-red = E kuqe
newtab-wallpaper-dark-blue = Blu e errët
newtab-wallpaper-dark-purple = E purpur e errët
newtab-wallpaper-dark-green = E gjelbër e errët
newtab-wallpaper-brown = Bojë kafe

## Abstract

newtab-wallpaper-category-title-abstract = Abstrakte
newtab-wallpaper-abstract-green = Forma të gjelbra
newtab-wallpaper-abstract-blue = Forma blu
newtab-wallpaper-abstract-purple = Forma të purpurta
newtab-wallpaper-abstract-orange = Forma portokalli
newtab-wallpaper-gradient-orange = Gradient portokalli dhe rozë
newtab-wallpaper-abstract-blue-purple = Forma blu dhe të purpurta
newtab-wallpaper-abstract-white-curves = E bardhë me lakore të hijezuara
newtab-wallpaper-abstract-purple-green = Gradient ndriçimi të purpur dhe të gjelbër
newtab-wallpaper-abstract-blue-purple-waves = Forma të valëzuara blu dhe të purpurta
newtab-wallpaper-abstract-black-waves = Forma të valëzuara të zeza

## Celestial

newtab-wallpaper-category-title-photographs = Fotografi
newtab-wallpaper-beach-at-sunrise = Plazh në agim
newtab-wallpaper-beach-at-sunset = Plazh në perëndim
newtab-wallpaper-storm-sky = Qiell me furtunë
newtab-wallpaper-sky-with-pink-clouds = Qiell me re rozë
newtab-wallpaper-red-panda-yawns-in-a-tree = Pandë e kuqe në majë të pemës
newtab-wallpaper-white-mountains = Male të bardhë
newtab-wallpaper-hot-air-balloons = Aerostate me ngjyra të ndryshme të parë gjatë ditës
newtab-wallpaper-starry-canyon = Natë blu me yje
newtab-wallpaper-suspension-bridge = Fotografi e një ure të varur gri gjatë ditës
newtab-wallpaper-sand-dunes = Duna ranore të bardha
newtab-wallpaper-palm-trees = Siluetë pemësh arrash kokosi gjatë orës së artë
newtab-wallpaper-blue-flowers = Foto nga afër lulesh me petale të kaltra në lulëzim
newtab-wallpaper-forest-trees = Pemë të mbuluara nga mjegull
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto nga <a data-l10n-name="name-link">{ $author_string }</a> on <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Shtoni pakëz ngjyrë
newtab-wallpaper-feature-highlight-content = Jepini Skedës tuaj të Re një pamje të freskët me sfonde.
newtab-wallpaper-feature-highlight-button = E mora vesh
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Hidhe tej
    .aria-label = Mbylleni flluskën
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Qiellor
newtab-wallpaper-celestial-lunar-eclipse = Eklips hënor
newtab-wallpaper-celestial-earth-night = Foto nate nga orbitë e ulët e Tokës
newtab-wallpaper-celestial-starry-sky = Qiell me yje
newtab-wallpaper-celestial-eclipse-time-lapse = Rrjedhë kohore eklipsi hënor
newtab-wallpaper-celestial-black-hole = Ilustrim galaktike vrimë e zezë
newtab-wallpaper-celestial-river = Pamje satelitore e një lumi

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Shihni parashikimin në { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ E sponsorizuar
newtab-weather-menu-change-location = Ndryshoni vendndodhje
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Kërkoni për vendndodhje
    .aria-label = Kërkoni për vendndodhje
newtab-weather-change-location-search-input = Kërkoni për vendndodhje
newtab-weather-menu-weather-display = Shfaqje moti
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = E thjeshtë
newtab-weather-menu-change-weather-display-simple = Kalo te shfaqje e thjeshtë
newtab-weather-menu-weather-display-option-detailed = E hollësishme
newtab-weather-menu-change-weather-display-detailed = Kalo te shfaqje e hollësishme
newtab-weather-menu-temperature-units = Njësi temperature
newtab-weather-menu-temperature-option-fahrenheit = Farenajt
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Kalo në Farenajt
newtab-weather-menu-change-temperature-units-celsius = Kalo në Celsius
newtab-weather-menu-hide-weather = Fshihe motin në Skedë të Re
newtab-weather-menu-learn-more = Mësoni më tepër
# This message is shown if user is working offline
newtab-weather-error-not-available = S’ka të dhëna moti tani për tani.

## Topic Labels

newtab-topic-label-business = Biznes
newtab-topic-label-career = Punësime
newtab-topic-label-education = Edukim
newtab-topic-label-arts = Spektakël
newtab-topic-label-food = Ushqim
newtab-topic-label-health = Shëndet
newtab-topic-label-hobbies = Lojëra
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Para
newtab-topic-label-government = Politikë
newtab-topic-label-education-science = Shkencë
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Vetëpërmirësim
newtab-topic-label-sports = Sporte
newtab-topic-label-tech = Teknologji
newtab-topic-label-travel = Udhëtime
newtab-topic-label-home = Shtëpi & Kopsht

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Përzgjidhni subjekte, që të përimtohet prurja për ju
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Zgjidhni dy ose më shumë subjekte. Ekspertët tanë u japin përparësi historive që përkojnë me interesat tuaja. Përditësojeni kur të doni.
newtab-topic-selection-save-button = Ruaje
newtab-topic-selection-cancel-button = Anuloje
newtab-topic-selection-button-maybe-later = Ndoshta më vonë
newtab-topic-selection-privacy-link = Mësoni se si i mbrojmë dhe administrojmë të dhënat
newtab-topic-selection-button-update-interests = Përditësoni interesat tuaja
newtab-topic-selection-button-pick-interests = Zgjidhni interesat tuaja

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Ndiqe
newtab-section-following-button = Po e Ndiqni
newtab-section-unfollow-button = Hiqi Ndjekjen

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Bllokoje
newtab-section-blocked-button = E bllokuar
newtab-section-unblock-button = Zhbllokoje

## Confirmation modal for blocking a section

newtab-section-cancel-button = Jo tani
newtab-section-confirm-block-topic-p1 = Jeni i sigurt se doni të bllokohet ky subjekt?
newtab-section-confirm-block-topic-p2 = Subjektet e bllokuar s’do të shfaqen më në prurjen tuaj.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Bllokoje { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Tema
newtab-section-manage-topics-button-v2 =
    .label = Administroni subjekte
newtab-section-mangage-topics-followed-topics = Të ndjekur
newtab-section-mangage-topics-followed-topics-empty-state = S’keni ende ndonjë temë të ndjekur.
newtab-section-mangage-topics-blocked-topics = Të bllokuar
newtab-section-mangage-topics-blocked-topics-empty-state = S’’keni ende ndonjë temë të bllokuar.
newtab-custom-wallpaper-title = Mbërritën sfonde vetjake
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Ngarkoni sfondin tuaj vetjak, ose zgjidhni një ngjyrë vetjake, për ta bërë { -brand-product-name }-in si e doni.
newtab-custom-wallpaper-cta = Provojeni
