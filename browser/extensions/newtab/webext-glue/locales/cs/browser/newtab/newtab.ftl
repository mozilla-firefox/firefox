# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Nový panel
newtab-settings-button =
    .title = Přizpůsobení stránky nového panelu
newtab-personalize-settings-icon-label =
    .title = Přizpůsobení nového panelu
    .aria-label = Nastavení
newtab-settings-dialog-label =
    .aria-label = Nastavení
newtab-personalize-icon-label =
    .title = Přizpůsobení nového panelu
    .aria-label = Přizpůsobení nového panelu
newtab-personalize-dialog-label =
    .aria-label = Přizpůsobit
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Vyhledat
    .aria-label = Vyhledat
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Zadejte webovou adresu nebo dotaz pro vyhledávač { $engine }
newtab-search-box-handoff-text-no-engine = Zadejte webovou adresu nebo dotaz pro vyhledávač
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Zadejte webovou adresu nebo dotaz pro vyhledávač { $engine }
    .title = Zadejte webovou adresu nebo dotaz pro vyhledávač { $engine }
    .aria-label = Zadejte webovou adresu nebo dotaz pro vyhledávač { $engine }
newtab-search-box-handoff-input-no-engine =
    .placeholder = Zadejte webovou adresu nebo dotaz pro vyhledávač
    .title = Zadejte webovou adresu nebo dotaz pro vyhledávač
    .aria-label = Zadejte webovou adresu nebo dotaz pro vyhledávač
newtab-search-box-text = Vyhledat na webu
newtab-search-box-input =
    .placeholder = Vyhledat na webu
    .aria-label = Vyhledat na webu

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Přidat vyhledávač
newtab-topsites-add-shortcut-header = Nová zkratka
newtab-topsites-edit-topsites-header = Upravit top stránku
newtab-topsites-edit-shortcut-header = Upravit zkratku
newtab-topsites-add-shortcut-label = Přidat zkratku
newtab-topsites-title-label = Název stránky
newtab-topsites-title-input =
    .placeholder = Zadejte název
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Zadejte nebo vložte URL adresu
newtab-topsites-url-validation = Je vyžadována platná URL
newtab-topsites-image-url-label = URL adresa vlastního obrázku
newtab-topsites-use-image-link = Použít vlastní obrázek…
newtab-topsites-image-validation = Obrázek se nepodařilo načíst. Zkuste jinou URL adresu.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Zrušit
newtab-topsites-delete-history-button = Smazat z historie
newtab-topsites-save-button = Uložit
newtab-topsites-preview-button = Náhled
newtab-topsites-add-button = Přidat

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Opravdu chcete smazat všechny výskyty této stránky z historie vašeho prohlížení?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Tuto akci nelze vzít zpět.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponzorováno

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Otevře nabídku
    .aria-label = Otevře nabídku
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Odstranit
    .aria-label = Odstranit
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Otevře nabídku
    .aria-label = Otevřít kontextovou nabídku pro { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Upravit tuto stránku
    .aria-label = Upravit tuto stránku

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Upravit
newtab-menu-open-new-window = Otevřít v novém okně
newtab-menu-open-new-private-window = Otevřít v novém anonymním okně
newtab-menu-dismiss = Skrýt
newtab-menu-pin = Připnout
newtab-menu-unpin = Odepnout
newtab-menu-delete-history = Smazat z historie
newtab-menu-save-to-pocket = Uložit do { -pocket-brand-name(case: "gen") }
newtab-menu-delete-pocket = Smazat z { -pocket-brand-name(case: "gen") }
newtab-menu-archive-pocket = Archivovat do { -pocket-brand-name(case: "gen") }
newtab-menu-show-privacy-info = Naši sponzoři a vaše soukromí
newtab-menu-about-fakespot = Co je { -fakespot-brand-name }
newtab-menu-report-content = Nahlásit tento obsah
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Blokovat
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Přestat sledovat téma

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Správa sponzorovaného obsahu
newtab-menu-our-sponsors-and-your-privacy = Naši sponzoři a vaše soukromí
newtab-menu-report-this-ad = Nahlásit tento inzerát

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Hotovo
newtab-privacy-modal-button-manage = Nastavení sponzorovaného obsahu
newtab-privacy-modal-header = Na vašem soukromí záleží.
newtab-privacy-modal-paragraph-2 =
    { -brand-product-name.gender ->
        [masculine] Kromě zajímavých článků zobrazujeme také relevantní a prověřený obsah od vybraných partnerů. Nemusíte se ale bát, <strong>vaše údaje nikdy neopustí váš { -brand-product-name(case: "acc") }</strong> - neodesílají se nám ani našim partnerům.
        [feminine] Kromě zajímavých článků zobrazujeme také relevantní a prověřený obsah od vybraných partnerů. Nemusíte se ale bát, <strong>vaše údaje nikdy neopustí vaši { -brand-product-name(case: "acc") }</strong> - neodesílají se nám ani našim partnerům.
        [neuter] Kromě zajímavých článků zobrazujeme také relevantní a prověřený obsah od vybraných partnerů. Nemusíte se ale bát, <strong>vaše údaje nikdy neopustí vaše { -brand-product-name(case: "acc") }</strong> - neodesílají se nám ani našim partnerům.
       *[other] Kromě zajímavých článků zobrazujeme také relevantní a prověřený obsah od vybraných partnerů. Nemusíte se ale bát, <strong>vaše údaje nikdy neopustí vaši aplikaci { -brand-product-name }</strong> - neodesílají se nám ani našim partnerům.
    }
newtab-privacy-modal-link = Zjistěte, jak chráníme vaše soukromí na stránce nového panelu.

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Odebrat záložku
# Bookmark is a verb here.
newtab-menu-bookmark = Přidat do záložek

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopírovat stahovaný odkaz
newtab-menu-go-to-download-page = Přejít na stránku stahování
newtab-menu-remove-download = Odstranit z historie

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Zobrazit ve Finderu
       *[other] Otevřít složku
    }
newtab-menu-open-file = Otevřít soubor

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Navštívené
newtab-label-bookmarked = V záložkách
newtab-label-removed-bookmark = Záložka odebrána
newtab-label-recommended = Populární
newtab-label-saved = Uloženo do { -pocket-brand-name(case: "gen") }
newtab-label-download = Staženo
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · sponzrováno
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponzorováno společností { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Odebrat sekci
newtab-section-menu-collapse-section = Sbalit sekci
newtab-section-menu-expand-section = Rozbalit sekci
newtab-section-menu-manage-section = Nastavení sekce
newtab-section-menu-manage-webext = Správa rozšíření
newtab-section-menu-add-topsite = Přidat mezi top stránky
newtab-section-menu-add-search-engine = Přidat vyhledávač
newtab-section-menu-move-up = Posunout nahoru
newtab-section-menu-move-down = Posunout dolů
newtab-section-menu-privacy-notice = Zásady ochrany osobních údajů

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Sbalit sekci
newtab-section-expand-section-label =
    .aria-label = Rozbalit sekci

## Section Headers.

newtab-section-header-topsites = Top stránky
newtab-section-header-recent-activity = Nedávná aktivita
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Doporučení ze služby { $provider }
newtab-section-header-stories = Podnětné články
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Dnešní výběr pro vás

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Začněte prohlížet a my vám zde ukážeme některé skvělé články, videa a další stránky, které jste nedávno viděli nebo uložili do záložek.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Už jste všechno přečetli. Další články ze služby { $provider } tu najdete zase později. Ale pokud se nemůžete dočkat, vyberte své oblíbené téma a podívejte se na další velké články z celého webu.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Už jste všechno přečetli. Další články zde najdete později. Nechcete čekat? Vyberte si oblíbené téma a najděte další skvělé články z celého webu.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Už jste všechno přečetli.
newtab-discovery-empty-section-topstories-content = Další články zde najdete později.
newtab-discovery-empty-section-topstories-try-again-button = Zkusit znovu
newtab-discovery-empty-section-topstories-loading = Načítání…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Jejda, při načítání obsahu se něco pokazilo.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Oblíbená témata:
newtab-pocket-new-topics-title = Chcete další články? Podívejte se na oblíbené témata v { -pocket-brand-name(case: "loc") }.
newtab-pocket-more-recommendations = Další doporučení
newtab-pocket-learn-more = Zjistit více
newtab-pocket-cta-button = Získejte { -pocket-brand-name(case: "acc") }
newtab-pocket-cta-text = Ukládejte si články do { -pocket-brand-name(case: "gen") } a užívejte si skvělé čtení.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } je součástí rodiny { -brand-product-name(case: "gen") }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Uložit
newtab-pocket-saved = Uloženo

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Další podobné
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ne pro mě
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Děkujeme. Vaše zpětná vazba nám pomůže váš informační kanál vylepšit.
newtab-toast-dismiss-button =
    .title = Zavřít
    .aria-label = Zavřít

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Objevte to nejlepší na webu
newtab-pocket-onboarding-cta =
    { -brand-product-name.case-status ->
        [with-cases] { -pocket-brand-name } prozkoumává rozmanitou škálu publikací a přináší informativní, inspirativní a důvěryhodný obsah přímo do { -brand-product-name(case: "gen") }.
       *[no-cases] { -pocket-brand-name } prozkoumává rozmanitou škálu publikací a přináší informativní, inspirativní a důvěryhodný obsah přímo do prohlížeče { -brand-product-name }.
    }

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Jejda, při načítání tohoto obsahu se něco pokazilo.
newtab-error-fallback-refresh-link = Opětovným načtením stránky to zkuste znovu.

## Customization Menu

newtab-custom-shortcuts-title = Zkratky
newtab-custom-shortcuts-subtitle = Uložené nebo navštěvované stránky
newtab-custom-shortcuts-toggle =
    .label = Zkratky
    .description = Uložené nebo navštěvované stránky
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } řádek
        [few] { $num } řádky
       *[other] { $num } řádků
    }
newtab-custom-sponsored-sites = Sponzorované zkratky
newtab-custom-pocket-title = Doporučeno službou { -pocket-brand-name }
newtab-custom-pocket-subtitle = Výjimečný obsah vybraný službou { -pocket-brand-name }, která je součástí rodiny { -brand-product-name(case: "gen") }
newtab-custom-stories-toggle =
    .label = Doporučené články
    .description = Výjimečný obsah spravovaný rodinou { -brand-product-name(case: "gen") }
newtab-custom-pocket-sponsored = Sponzorované články
newtab-custom-pocket-show-recent-saves = Zobrazit nedávno uložené
newtab-custom-recent-title = Nedávná aktivita
newtab-custom-recent-subtitle = Výběr z nedávných stránek a obsahu
newtab-custom-recent-toggle =
    .label = Nedávná aktivita
    .description = Výběr z nedávných stránek a obsahu
newtab-custom-weather-toggle =
    .label = Počasí
    .description = Dnešní předpověď ve zkratce
newtab-custom-close-button = Zavřít
newtab-custom-settings = Další nastavení

## New Tab Wallpapers

newtab-wallpaper-title = Tapety
newtab-wallpaper-reset = Obnovit výchozí nastavení
newtab-wallpaper-upload-image = Nahrát obrázek
newtab-wallpaper-custom-color = Vybrat barvu
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Obrázek překročil limit velikosti souboru { $file_size } MB. Zkuste nahrát menší soubor.
newtab-wallpaper-error-file-type = Váš soubor se nám nepodařilo nahrát. Zkuste to prosím znovu s jiným typem souboru.
newtab-wallpaper-light-red-panda = Panda červená
newtab-wallpaper-light-mountain = Bílá hora
newtab-wallpaper-light-sky = Obloha s fialovými a růžovými mraky
newtab-wallpaper-light-color = Modré, růžové a žluté tvary
newtab-wallpaper-light-landscape = Horská krajina s modrou mlhou
newtab-wallpaper-light-beach = Pláž s palmou
newtab-wallpaper-dark-aurora = Polární záře
newtab-wallpaper-dark-color = Červené a modré tvary
newtab-wallpaper-dark-panda = Panda červená ukrytá v lese
newtab-wallpaper-dark-sky = Městská krajina s noční oblohou
newtab-wallpaper-dark-mountain = Horská scenérie
newtab-wallpaper-dark-city = Fialová krajina města
newtab-wallpaper-dark-fox-anniversary = Liška na chodníku u lesa
newtab-wallpaper-light-fox-anniversary = Liška na louce se zamlženou horskou krajinou

## Solid Colors

newtab-wallpaper-category-title-colors = Plné barvy
newtab-wallpaper-blue = Modrá
newtab-wallpaper-light-blue = Světle modrá
newtab-wallpaper-light-purple = Světle fialová
newtab-wallpaper-light-green = Světle zelená
newtab-wallpaper-green = Zelená
newtab-wallpaper-beige = Béžová
newtab-wallpaper-yellow = Žlutá
newtab-wallpaper-orange = Oranžová
newtab-wallpaper-pink = Růžová
newtab-wallpaper-light-pink = Světle růžová
newtab-wallpaper-red = Červená
newtab-wallpaper-dark-blue = Tmavě modrá
newtab-wallpaper-dark-purple = Tmavě fialová
newtab-wallpaper-dark-green = Tmavě zelená
newtab-wallpaper-brown = Hnědá

## Abstract

newtab-wallpaper-category-title-abstract = Abstraktní
newtab-wallpaper-abstract-green = Zelené tvary
newtab-wallpaper-abstract-blue = Modré tvary
newtab-wallpaper-abstract-purple = Fialové tvary
newtab-wallpaper-abstract-orange = Oranžové tvary
newtab-wallpaper-gradient-orange = Přechod oranžové a růžové
newtab-wallpaper-abstract-blue-purple = Modré a fialové tvary
newtab-wallpaper-abstract-white-curves = Bílá se stínovanými křivkami
newtab-wallpaper-abstract-purple-green = Přechod fialové a zelené barvy
newtab-wallpaper-abstract-blue-purple-waves = Modré a fialové zvlněné tvary
newtab-wallpaper-abstract-black-waves = Černé zvlněné tvary

## Celestial

newtab-wallpaper-category-title-photographs = Fotografie
newtab-wallpaper-beach-at-sunrise = Pláž při východu slunce
newtab-wallpaper-beach-at-sunset = Pláž při západu slunce
newtab-wallpaper-storm-sky = Bouřková obloha
newtab-wallpaper-sky-with-pink-clouds = Obloha s růžovými obláčky
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda červená zívá na stromě
newtab-wallpaper-white-mountains = Bílé hory
newtab-wallpaper-hot-air-balloons = Různé barvy horkovzdušných balonů během dne
newtab-wallpaper-starry-canyon = Modrá hvězdná noc
newtab-wallpaper-suspension-bridge = Šedivé fotografování celé visuté můstky během dne
newtab-wallpaper-sand-dunes = Bílé písečné duny
newtab-wallpaper-palm-trees = Silueta kokosových palem během zlaté hodiny
newtab-wallpaper-blue-flowers = Detailní fotografie modrých okvětních lístků v květu
newtab-wallpaper-forest-trees = Stromy v mlze
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Fotografie od autora <a data-l10n-name="name-link">{ $author_string }</a> z webu <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Zkuste barevný nádech
newtab-wallpaper-feature-highlight-content = Dejte svému novému panelu svěží vzhled pomocí tapet.
newtab-wallpaper-feature-highlight-button = Rozumím
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Zavřít
    .aria-label = Zavře okno
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Nebeská
newtab-wallpaper-celestial-lunar-eclipse = Zatmění Měsíce
newtab-wallpaper-celestial-earth-night = Noční fotografie z nízké oběžné dráhy Země
newtab-wallpaper-celestial-starry-sky = Hvězdná obloha
newtab-wallpaper-celestial-eclipse-time-lapse = Časosběrné snímání zatmění Měsíce
newtab-wallpaper-celestial-black-hole = Ilustrace galaxie Černá díra
newtab-wallpaper-celestial-river = Satelitní snímek řeky

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Podívejte se na předpověď od { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙Sponzorované
newtab-weather-menu-change-location = Změnit místo
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Hledat umístění
    .aria-label = Hledat umístění
newtab-weather-change-location-search-input = Hledat umístění
newtab-weather-menu-weather-display = Zobrazení počasí
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Jednoduché
newtab-weather-menu-change-weather-display-simple = Přepnout na jednoduché rozhraní
newtab-weather-menu-weather-display-option-detailed = Podrobné
newtab-weather-menu-change-weather-display-detailed = Přepnout na podrobné zobrazení
newtab-weather-menu-temperature-units = Jednotky teploty
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Přepnout na stupně Fahrenheita
newtab-weather-menu-change-temperature-units-celsius = Přepnout na stupně Celsia
newtab-weather-menu-hide-weather = Skrýt počasí na novém panelu
newtab-weather-menu-learn-more = Zjistit více
# This message is shown if user is working offline
newtab-weather-error-not-available = Údaje o počasí nejsou momentálně dostupné.

## Topic Labels

newtab-topic-label-business = Podnikání
newtab-topic-label-career = Kariéra
newtab-topic-label-education = Vzdělávání
newtab-topic-label-arts = Zábava
newtab-topic-label-food = Jídlo
newtab-topic-label-health = Zdraví
newtab-topic-label-hobbies = Hraní her
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Finance
newtab-topic-label-society-parenting = Rodičovství
newtab-topic-label-government = Politika
newtab-topic-label-education-science = Věda
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Zajímavé tipy
newtab-topic-label-sports = Sporty
newtab-topic-label-tech = Technologie
newtab-topic-label-travel = Cestování
newtab-topic-label-home = Dům a zahrada

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Vyberte témata pro vyladění svého kanálu
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Vyberte dvě nebo více témat. Naši odborní kurátoři upřednostňují články přizpůsobené vašim zájmům. Aktualizovat můžete kdykoliv.
newtab-topic-selection-save-button = Uložit
newtab-topic-selection-cancel-button = Zrušit
newtab-topic-selection-button-maybe-later = Možná později
newtab-topic-selection-privacy-link = Zjistěte, jak chráníme a spravujeme data
newtab-topic-selection-button-update-interests = Aktualizujte své zájmy
newtab-topic-selection-button-pick-interests = Vyberte, co vás zajímá

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Sledovat
newtab-section-following-button = Sledované
newtab-section-unfollow-button = Přestat sledovat

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Blokovat
newtab-section-blocked-button = Blokováno
newtab-section-unblock-button = Odblokovat

## Confirmation modal for blocking a section

newtab-section-cancel-button = Teď ne
newtab-section-confirm-block-topic-p1 = Opravdu chcete zablokovat toto téma?
newtab-section-confirm-block-topic-p2 = Zablokovaná témata se již nebudou zobrazovat ve vašem kanálu.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Blokovat { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Témata
newtab-section-manage-topics-button-v2 =
    .label = Správa témat
newtab-section-mangage-topics-followed-topics = Sledováno
newtab-section-mangage-topics-followed-topics-empty-state = Zatím nesledujete žádné téma.
newtab-section-mangage-topics-blocked-topics = Blokováno
newtab-section-mangage-topics-blocked-topics-empty-state = Zatím jste nezablokovali žádná témata.
newtab-custom-wallpaper-title = Vlastní tapety jsou zde
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle =
    { -brand-product-name.case-status ->
        [with-cases] Nahrajte si vlastní tapetu nebo si vyberte vlastní barvu, aby { -brand-product-name } byl podle vás.
       *[no-cases] Nahrajte si vlastní tapetu nebo si vyberte vlastní barvu, aby aplikace { -brand-product-name } byla podle vás.
    }
newtab-custom-wallpaper-cta = Vyzkoušejte ho
