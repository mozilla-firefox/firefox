# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nowy rajtark
newtab-settings-button =
    .title = Stronu wašeho noweho rajtarka přiměrić
newtab-personalize-settings-icon-label =
    .title = Nowy rajtark personalizować
    .aria-label = Nastajenja
newtab-settings-dialog-label =
    .aria-label = Nastajenja
newtab-personalize-icon-label =
    .title = Nowy rajtark personalizować
    .aria-label = Nowy rajtark personalizować
newtab-personalize-dialog-label =
    .aria-label = Personalizować
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Pytać
    .aria-label = Pytać
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Pytajće z { $engine } abo zapodajće adresu
newtab-search-box-handoff-text-no-engine = Pytać abo adresu zapodać
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Pytajće z { $engine } abo zapodajće adresu
    .title = Pytajće z { $engine } abo zapodajće adresu
    .aria-label = Pytajće z { $engine } abo zapodajće adresu
newtab-search-box-handoff-input-no-engine =
    .placeholder = Pytać abo adresu zapodać
    .title = Pytać abo adresu zapodać
    .aria-label = Pytać abo adresu zapodać
newtab-search-box-text = Web přepytać
newtab-search-box-input =
    .placeholder = Web přepytać
    .aria-label = Web přepytać

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Pytawu přidać
newtab-topsites-add-shortcut-header = Nowe zwjazanje
newtab-topsites-edit-topsites-header = Najhusćišo wopytane sydło wobdźěłać
newtab-topsites-edit-shortcut-header = Zwjazanje wobdźěłać
newtab-topsites-add-shortcut-label = Skrótšenku přidać
newtab-topsites-title-label = Titul
newtab-topsites-title-input =
    .placeholder = Titul zapodać
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL zapodać abo zasadźić
newtab-topsites-url-validation = Płaćiwy URL trěbny
newtab-topsites-image-url-label = URL swójskeho wobraza
newtab-topsites-use-image-link = Swójski wobraz wužiwać…
newtab-topsites-image-validation = Wobraz njeda so začitać. Spytajće druhi URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Přetorhnyć
newtab-topsites-delete-history-button = Z historije zhašeć
newtab-topsites-save-button = Składować
newtab-topsites-preview-button = Přehlad
newtab-topsites-add-button = Přidać

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Chceće woprawdźe kóždu instancu tuteje strony ze swojeje historije zhašeć?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tuta akcija njeda so cofnyć.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponserowany

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Meni wočinić
    .aria-label = Meni wočinić
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Wotstronić
    .aria-label = Wotstronić
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Meni wočinić
    .aria-label = Kontekstowy meni za { $title } wočinić
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Tute sydło wobdźěłać
    .aria-label = Tute sydło wobdźěłać

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Wobdźěłać
newtab-menu-open-new-window = W nowym woknje wočinić
newtab-menu-open-new-private-window = W nowym priwatnym woknje wočinić
newtab-menu-dismiss = Zaćisnyć
newtab-menu-pin = Připjeć
newtab-menu-unpin = Wotpjeć
newtab-menu-delete-history = Z historije zhašeć
newtab-menu-save-to-pocket = Pola { -pocket-brand-name } składować
newtab-menu-delete-pocket = Z { -pocket-brand-name } zhašeć
newtab-menu-archive-pocket = W { -pocket-brand-name } archiwować
newtab-menu-show-privacy-info = Naši sponsorojo a waša priwatnosć
newtab-menu-about-fakespot = Wo { -fakespot-brand-name }
newtab-menu-report-content = Tutón wobsah zdźělić
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokować
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Temje hižo njeslědować

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Sponsorowany wobsah rjadować
newtab-menu-our-sponsors-and-your-privacy = Naši sponsorojo a waša priwatnosć
newtab-menu-report-this-ad = Tute wabjenje zdźělić

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Dokónčene
newtab-privacy-modal-button-manage = Nastajenja sponserowaneho wobsaha rjadować
newtab-privacy-modal-header = Waša priwatnosć je wažna
newtab-privacy-modal-paragraph-2 =
    Přidatnje k napowědanju putawych powědančkow, pokazujemy wam tež relewantny, 
    jara přepruwowany wobsah wot wubranych sponsorow. Budźće zawěsćeny, <strong>waše přehladowanske 
    daty wašu wosobinsku wersiju { -brand-product-name } ženje njewopušća</strong> ­­- njewidźimy je, a naši 
    sponsorojo tež nic.
newtab-privacy-modal-link = Zhońće, kak priwatnosć w nowym rajtarku funguje

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Zapołožku wotstronić
# Bookmark is a verb here.
newtab-menu-bookmark = Jako zapołožku składować

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Sćehnjenski wotkaz kopěrować
newtab-menu-go-to-download-page = K sćehnjenskej stronje přeńć
newtab-menu-remove-download = Z historije wotstronić

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] W Finder pokazać
       *[other] Wobsahowacy rjadowak wočinić
    }
newtab-menu-open-file = Dataju wočinić

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Wopytany
newtab-label-bookmarked = Jako zapołožka składowany
newtab-label-removed-bookmark = Zapołožka je so wotstroniła
newtab-label-recommended = Popularny
newtab-label-saved = Do { -pocket-brand-name } składowany
newtab-label-download = Sćehnjeny
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } - sponserowane
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponserowany wot { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } mjeń.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Wotrězk wotstronić
newtab-section-menu-collapse-section = Wotrězk schować
newtab-section-menu-expand-section = Wotrězk pokazać
newtab-section-menu-manage-section = Wotrězk rjadować
newtab-section-menu-manage-webext = Rozšěrjenje rjadować
newtab-section-menu-add-topsite = Woblubowane sydło přidać
newtab-section-menu-add-search-engine = Pytawu přidać
newtab-section-menu-move-up = Horje
newtab-section-menu-move-down = Dele
newtab-section-menu-privacy-notice = Zdźělenka priwatnosće

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Wotrězk schować
newtab-section-expand-section-label =
    .aria-label = Wotrězk pokazać

## Section Headers.

newtab-section-header-topsites = Najhusćišo wopytane sydła
newtab-section-header-recent-activity = Najnowša aktiwita
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Wot { $provider } doporučeny
newtab-section-header-stories = Stawiznički, kotrež k přemyslowanju pohonjeja
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dźensniše doporučenki za was

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Započńće přehladować, a pokazamy někotre wulkotne nastawki, wideja a druhe strony, kotrež sće njedawno wopytał abo tu jako zapołožki składował.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = To je nachwilu wšitko. Wróćće so pozdźišo dalšich wulkotnych stawiznow dla wot { $provider }. Njemóžeće čakać? Wubjerće woblubowanu temu, zo byšće dalše wulkotne stawizny z weba namakał.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Sće wjaznył. Wróćće pozdźišo wjace stawiznow dla. Njemóžeće čakać? Wubjerće woblubowanu temu, zo byšće dalše wulkotne stawizny z weba namakał.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Sće dosćehnjeny!
newtab-discovery-empty-section-topstories-content = Hladajće pozdźišo za wjace stawiznami.
newtab-discovery-empty-section-topstories-try-again-button = Hišće raz spytać
newtab-discovery-empty-section-topstories-loading = Začita so…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Hopla! Smy tutón wotrězk bjezmała začitali, ale nic cyle.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Woblubowane temy:
newtab-pocket-new-topics-title = Chceće dalše stawiznički? Wobhladajće sej tute woblubowane temy z { -pocket-brand-name }
newtab-pocket-more-recommendations = Dalše doporučenja
newtab-pocket-learn-more = Dalše informacije
newtab-pocket-cta-button = { -pocket-brand-name } wobstarać
newtab-pocket-cta-text = Składujće stawizny, kotrež so wam spodobuja, w { -pocket-brand-name } a žiwće swój duch z fascinowacymi čitančkami.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } je dźěl swójby { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Składować
newtab-pocket-saved = Składowany

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Wjace kaž ta
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Nic za mnje
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Dźakujemy so. Waše měnjenje budźe nam pomhać, waš kanal polěpšić.
newtab-toast-dismiss-button =
    .title = Zaćisnyć
    .aria-label = Zaćisnyć

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Wotkryjće najlěpše interneta
newtab-pocket-onboarding-cta = { -pocket-brand-name } šěroku paletu publikacijow přeslědźuje, zo by najbóle informatiwny, inspirowacy a dowěry hódny wobsah direktnje do wašeho wobhladowaka { -brand-product-name } donjesł.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Hopla, při čitanju tutoho wobsaha je so něšto nimokuliło.
newtab-error-fallback-refresh-link = Aktualizujće stronu, zo byšće hišće raz spytał.

## Customization Menu

newtab-custom-shortcuts-title = Zwjazanja
newtab-custom-shortcuts-subtitle = Sydła, kotrež składujeće abo wopytujeće
newtab-custom-shortcuts-toggle =
    .label = Zwjazanja
    .description = Sydła, kotrež składujeće abo wopytujeće
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } linka
        [two] { $num } lince
        [few] { $num } linki
       *[other] { $num } linkow
    }
newtab-custom-sponsored-sites = Sponserowane zwjazanja
newtab-custom-pocket-title = Wot { -pocket-brand-name } doporučene
newtab-custom-pocket-subtitle = Wosebite wobsah, wubrany přez { -pocket-brand-name }, dźěla swójby { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Doporučene stawiznički
    .description = Wuwzaćowy wobsah, kotryž so přez swójbu { -brand-product-name } hlada
newtab-custom-pocket-sponsored = Sponserowane stawizny
newtab-custom-pocket-show-recent-saves = Najnowše składowanja pokazać
newtab-custom-recent-title = Najnowša aktiwita
newtab-custom-recent-subtitle = Wuběr najnowšich sydłow a najnowšeho wobsaha
newtab-custom-recent-toggle =
    .label = Najnowša aktiwita
    .description = Wuběr najnowšich sydłow a najnowšeho wobsaha
newtab-custom-weather-toggle =
    .label = Wjedro
    .description = Dźensniša wjedrowa předpowědź na jedyn pohlad
newtab-custom-close-button = Začinić
newtab-custom-settings = Dalše nastajenja rjadować

## New Tab Wallpapers

newtab-wallpaper-title = Pozadkowe wobrazy
newtab-wallpaper-reset = Na standard wróćo stajić
newtab-wallpaper-upload-image = Wobraz nahrać
newtab-wallpaper-custom-color = Barbu wubrać
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Wobraz je limit datajoweje wulkosće { $file_size } překročił. Nahrajće prošu mjeńšu dataju.
newtab-wallpaper-error-file-type = Njemóžachmy wašu dataju nahrać. Spytajće prošu z druhim datajowym typom hišće raz.
newtab-wallpaper-light-red-panda = Čerwjeny panda
newtab-wallpaper-light-mountain = Běła hora
newtab-wallpaper-light-sky = Njebjo z wioletnymi a róžowymi mróčelemi
newtab-wallpaper-light-color = Módre, róžowe a žołte twary
newtab-wallpaper-light-landscape = Módra kurjawojta horinska krajina
newtab-wallpaper-light-beach = Přibrjóh z palmu
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Čerwjene a módre twary
newtab-wallpaper-dark-panda = Čerwjeny panda w lěsu schowany
newtab-wallpaper-dark-sky = Měšćanska krajina z nócnym njebjom
newtab-wallpaper-dark-mountain = Horinska krajina
newtab-wallpaper-dark-city = Wioletna měšćanska krajina
newtab-wallpaper-dark-fox-anniversary = Liška na dłóžbje blisko lěsa
newtab-wallpaper-light-fox-anniversary = Liška w trawnym polu z młowej horinskej krajinu

## Solid Colors

newtab-wallpaper-category-title-colors = Jednotne barby
newtab-wallpaper-blue = Módry
newtab-wallpaper-light-blue = Swětłomódry
newtab-wallpaper-light-purple = Swětłowioletny
newtab-wallpaper-light-green = Swětłozeleny
newtab-wallpaper-green = Zeleny
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Žołty
newtab-wallpaper-orange = Oranžowy
newtab-wallpaper-pink = Pink
newtab-wallpaper-light-pink = Swětłopink
newtab-wallpaper-red = Čerwjeny
newtab-wallpaper-dark-blue = Ćmowomódry
newtab-wallpaper-dark-purple = Ćmowowioletny
newtab-wallpaper-dark-green = Ćmowozeleny
newtab-wallpaper-brown = Bruny

## Abstract

newtab-wallpaper-category-title-abstract = Abstraktne
newtab-wallpaper-abstract-green = Zelene twary
newtab-wallpaper-abstract-blue = Módre twary
newtab-wallpaper-abstract-purple = Wioletne twary
newtab-wallpaper-abstract-orange = Oranžowe twary
newtab-wallpaper-gradient-orange = Přeběžk oranžowy a pink
newtab-wallpaper-abstract-blue-purple = Módre a wioletne twary
newtab-wallpaper-abstract-white-curves = Běły z wotsćinjenymi kulojćinami
newtab-wallpaper-abstract-purple-green = Wioletny a zeleny swětłowy přeběh
newtab-wallpaper-abstract-blue-purple-waves = Módre a wioletne žołmate twary
newtab-wallpaper-abstract-black-waves = Čorne žołmate twary

## Celestial

newtab-wallpaper-category-title-photographs = Fota
newtab-wallpaper-beach-at-sunrise = Brjóh při schadźenju słónca
newtab-wallpaper-beach-at-sunset = Brjóh při chowanju słónca
newtab-wallpaper-storm-sky = Wichorowe njebjo
newtab-wallpaper-sky-with-pink-clouds = Njebjo z róžowymi mróčelemi
newtab-wallpaper-red-panda-yawns-in-a-tree = Čerwjeny panda w štomje zywa
newtab-wallpaper-white-mountains = Běłe hory
newtab-wallpaper-hot-air-balloons = Rozdźělna barba horcopowětrowych balonow wodnjo
newtab-wallpaper-starry-canyon = Módra hwězdna nóc
newtab-wallpaper-suspension-bridge = Šěra fotografija wisateho mosta wodnjo
newtab-wallpaper-sand-dunes = Běłe pěskowe nawěwy
newtab-wallpaper-palm-trees = Silueta štomow kokosowych palmow w złotej hodźinje
newtab-wallpaper-blue-flowers = Fotografija kwětkow z módrymi łopješkami w kćěwje z bliskosće
newtab-wallpaper-forest-trees = Z kurjawu pokryte štomy
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto wot <a data-l10n-name="name-link">{ $author_string }</a> na <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Wupruwujće barbowy woplusk
newtab-wallpaper-feature-highlight-content = Dajće swojemu nowemu rajtarkej čerstwy napohlad z pozadkowymi wobrazami.
newtab-wallpaper-feature-highlight-button = Sym zrozumił
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Zaćisnyć
    .aria-label = Wuskakowace wokno začinić
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Njebjeski
newtab-wallpaper-celestial-lunar-eclipse = Zaćmiće měsačka
newtab-wallpaper-celestial-earth-night = Nócne foto z delnjeho zemskeho orbita
newtab-wallpaper-celestial-starry-sky = Hwězdne njebjo
newtab-wallpaper-celestial-eclipse-time-lapse = Časowy wotběh zaćmića měsačka
newtab-wallpaper-celestial-black-hole = Zwobraznjenje galaksije z čornej dźěru
newtab-wallpaper-celestial-river = Satelitowy wobraz rěki

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Wjedrowu předpowědź w { $provider } pokazać
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ sponsorowany
newtab-weather-menu-change-location = Městno změnić
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Městno pytać
    .aria-label = Městno pytać
newtab-weather-change-location-search-input = Městno pytać
newtab-weather-menu-weather-display = Wjedrowe pokazanje
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Jednore
newtab-weather-menu-change-weather-display-simple = Jednory napohlad wužiwać
newtab-weather-menu-weather-display-option-detailed = Detailne
newtab-weather-menu-change-weather-display-detailed = Detailny napohlad wužiwać
newtab-weather-menu-temperature-units = Temperaturne jednotki
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Fahrenheit wužiwać
newtab-weather-menu-change-temperature-units-celsius = Celsius wužiwać
newtab-weather-menu-hide-weather = Wjedro na nowym rajtarku schować
newtab-weather-menu-learn-more = Dalše informacije
# This message is shown if user is working offline
newtab-weather-error-not-available = Wjedrowe daty tuchwilu k dispoziciji njejsu.

## Topic Labels

newtab-topic-label-business = Wobchodnistwo
newtab-topic-label-career = Karjera
newtab-topic-label-education = Zdźěłanje
newtab-topic-label-arts = Zabawjenje
newtab-topic-label-food = Cyroba
newtab-topic-label-health = Strowosć
newtab-topic-label-hobbies = Hraće
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Pjenjezy
newtab-topic-label-society-parenting = Kubłanje
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Wědomosć
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Swójske polěpšenja
newtab-topic-label-sports = Sport
newtab-topic-label-tech = Technologija
newtab-topic-label-travel = Pućowanje
newtab-topic-label-home = Dom a zahroda

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Wubjerće temy, zo byšće swój kanal optimował
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Wubjerće dwě temje abo wjace z nich. Naši nazhonjeni kuratorojo prioritu na stawiznički kładu, kotrež su na waše zajimy wusměrjene. Přiměrće to kóždy čas.
newtab-topic-selection-save-button = Składować
newtab-topic-selection-cancel-button = Přetorhnyć
newtab-topic-selection-button-maybe-later = Snano pozdźišo
newtab-topic-selection-privacy-link = Zhońće, kak daty škitamy a rjadujemy
newtab-topic-selection-button-update-interests = Zaktualizujće swoje zajimy
newtab-topic-selection-button-pick-interests = Wubjerće swoje zajimy

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Slědować
newtab-section-following-button = Slědowacy
newtab-section-unfollow-button = Hižo njeslědować

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokować
newtab-section-blocked-button = Zablokowany
newtab-section-unblock-button = Hižo njeblokować

## Confirmation modal for blocking a section

newtab-section-cancel-button = Nic nětko
newtab-section-confirm-block-topic-p1 = Chceće woprawdźe tutu temu blokować?
newtab-section-confirm-block-topic-p2 = Zablokowane temy so hižo we wašim kanalu njejewja.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = { $topic } blokować

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Temy
newtab-section-manage-topics-button-v2 =
    .label = Temy rjadować
newtab-section-mangage-topics-followed-topics = Slědowany
newtab-section-mangage-topics-followed-topics-empty-state = Hišće žanym temam njeslědujeće.
newtab-section-mangage-topics-blocked-topics = Zablokowany
newtab-section-mangage-topics-blocked-topics-empty-state = Hišće njejsće žane temy zablokował.
newtab-custom-wallpaper-title = Tu su swójske pozadkowe wobrazy
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Nahrajće swójski pozadkowy wobraz abo wubjerće swójsku barbu, zo byšće sej { -brand-product-name } přiswojił.
newtab-custom-wallpaper-cta = Wupruwujće jón
