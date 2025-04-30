# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Ivinell nevez
newtab-settings-button =
    .title = Personelait ho pajenn Ivinell Nevez
newtab-personalize-settings-icon-label =
    .title = Personelaat an ivinell nevez
    .aria-label = Arventennoù
newtab-settings-dialog-label =
    .aria-label = Arventennoù
newtab-personalize-icon-label =
    .title = Personelaat un ivinell nevez
    .aria-label = Personelaat un ivinell nevez
newtab-personalize-dialog-label =
    .aria-label = Personelaat
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Klask
    .aria-label = Klask
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Klask gant { $engine } pe skrivañ ur chomlecʼh
newtab-search-box-handoff-text-no-engine = Klask pe skrivañ ur chomlecʼh
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Klask gant { $engine } pe skrivañ ur chomlecʼh
    .title = Klask gant { $engine } pe skrivañ ur chomlecʼh
    .aria-label = Klask gant { $engine } pe skrivañ ur chomlecʼh
newtab-search-box-handoff-input-no-engine =
    .placeholder = Klask pe skrivañ ur chomlecʼh
    .title = Klask pe skrivañ ur chomlecʼh
    .aria-label = Klask pe skrivañ ur chomlecʼh
newtab-search-box-text = Klask er web
newtab-search-box-input =
    .placeholder = Klask er web
    .aria-label = Klask er web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Ouzhpennañ ul lusker klask
newtab-topsites-add-shortcut-header = Berradenn nevez
newtab-topsites-edit-topsites-header = Kemmañ al lec'hienn wellañ
newtab-topsites-edit-shortcut-header = Kemmañ ar verradenn
newtab-topsites-add-shortcut-label = Ouzhpennañ ur verradenn
newtab-topsites-title-label = Titl
newtab-topsites-title-input =
    .placeholder = Enankañ un titl
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Skrivit pe pegit un URL
newtab-topsites-url-validation = URL talvoudek azgoulennet
newtab-topsites-image-url-label = URL ar skeudenn personelaet
newtab-topsites-use-image-link = Ober gant ur skeudenn personelaet…
newtab-topsites-image-validation = N'haller ket kargan ar skeudenn. Klaskit gant un URL disheñvel.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Nullañ
newtab-topsites-delete-history-button = Dilemel eus ar roll istor
newtab-topsites-save-button = Enrollañ
newtab-topsites-preview-button = Alberz
newtab-topsites-add-button = Ouzhpennañ

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Sur oc'h e fell deoc'h dilemel kement eriol eus ar bajenn-mañ diouzh ho roll istor?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ne c'haller ket dizober ar gwezh-mañ.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Paeroniet

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Digeriñ al lañser
    .aria-label = Digeriñ al lañser
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Dilemel
    .aria-label = Dilemel
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Digeriñ al lañser
    .aria-label = Digeriñ al lañser kemperzhel evit { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Embann al lec'hienn-mañ
    .aria-label = Embann al lec'hienn-mañ

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Embann
newtab-menu-open-new-window = Digeriñ e-barzh ur prenestr nevez
newtab-menu-open-new-private-window = Digeriñ e-barzh ur prenestr merdeiñ prevez nevez
newtab-menu-dismiss = Argas
newtab-menu-pin = Spilhennañ
newtab-menu-unpin = Dispilhennañ
newtab-menu-delete-history = Dilemel eus ar roll istor
newtab-menu-save-to-pocket = Enrollañ etrezek { -pocket-brand-name }
newtab-menu-delete-pocket = Dilemel eus { -pocket-brand-name }
newtab-menu-archive-pocket = Diellaouiñ e { -pocket-brand-name }
newtab-menu-show-privacy-info = Hor c’hevelerien hag ho puhez prevez
newtab-menu-about-fakespot = A-zivout { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Stankañ

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Graet
newtab-privacy-modal-button-manage = Merañ an arventennoù endalc’had paeroniet
newtab-privacy-modal-header = Pouezus eo ho puhez prevez
newtab-privacy-modal-paragraph-2 = Kinnig a reomp deoc'h istorioù dedennus, met ivez danvezioù dibabet gant aked gant hor paeroned. Bezit dinec'het: <strong>morse ne vo kaset ho roadennoù merdeiñ e diavaez ho eilenn hiniennel { -brand-product-name }</strong> — ne welont ket anezho hag hor paeroned kennebeut.
newtab-privacy-modal-link = Deskit penaos ec'h a en-dro ar prevezded war an ivinell nevez

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Dilemel ar sined
# Bookmark is a verb here.
newtab-menu-bookmark = Sined

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Eilañ ere ar pellgargadur
newtab-menu-go-to-download-page = Mont da bajenn ar pellgargadur
newtab-menu-remove-download = Dilemel diwar ar roll

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Diskouez e Finder
       *[other] Digeriñ an teuliad a endalc'h ar restr
    }
newtab-menu-open-file = Digeriñ ar restr

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Gweladennet
newtab-label-bookmarked = Lakaet er sinedoù
newtab-label-removed-bookmark = Sined dilamet
newtab-label-recommended = Brudet
newtab-label-saved = Enrollet e { -pocket-brand-name }
newtab-label-download = Pellgarget
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · paeroniet
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Paeroniet gant { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } munutenn

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Dilemel ar gevrenn
newtab-section-menu-collapse-section = Bihanaat ar gevrenn
newtab-section-menu-expand-section = Astenn ar gevrenn
newtab-section-menu-manage-section = Merañ ar gevrenn
newtab-section-menu-manage-webext = Merañ an askouezh
newtab-section-menu-add-topsite = Ouzhpennañ ul lec'hienn gwellañ din
newtab-section-menu-add-search-engine = Ouzhpennañ ul lusker klask
newtab-section-menu-move-up = Dilec'hiañ etrezek ar c'hrec'h
newtab-section-menu-move-down = Dilec'hiañ etrezek an traoñ
newtab-section-menu-privacy-notice = Evezhiadennoù a-fet buhez prevez

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Bihanaat ar gevrenn
newtab-section-expand-section-label =
    .aria-label = Astenn ar gevrenn

## Section Headers.

newtab-section-header-topsites = Lec'hiennoù pennañ
newtab-section-header-recent-activity = Oberiantiz a-nevez
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Erbedet gant { $provider }
newtab-section-header-stories = Boued spered

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Krogit da verdeiñ hag e tiskouezimp deoc’h pennadoù, videoioù ha pajennoù all gweladennet pe lakaet er sinedoù nevez ’zo.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Aet oc'h betek penn. Distroit diwezhatoc'h evit muioc’h a istorioù digant { $provider }. N’oc'h ket evit gortoz? Dibabit un danvez brudet evit klask muioc’h a bennadoù dedennus eus pep lec’h er web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Echuet eo ganeoc'h!
newtab-discovery-empty-section-topstories-content = Distroit amañ diwezhatoc'h evit lenn pennadoù all.
newtab-discovery-empty-section-topstories-try-again-button = Klaskit en-dro
newtab-discovery-empty-section-topstories-loading = O kargañ…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Chaous! N'eo ket bet karget ar gevrenn en he fezh.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Danvezioù brudet:
newtab-pocket-new-topics-title = Fellout a ra deoc’h kaout pennadoù ouzhpenn? Sellit ouzh ar sujedoù brudet e { -pocket-brand-name }
newtab-pocket-more-recommendations = Erbedadennoù ouzhpenn
newtab-pocket-learn-more = Gouzout hiroc’h
newtab-pocket-cta-button = Staliañ { -pocket-brand-name }
newtab-pocket-cta-text = Enrollit pennadoù a-zoare e { -pocket-brand-name } ha magit ho spered gant lennadennoù boemus.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } a zo ul lodenn eus familh { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Enrollañ
newtab-pocket-saved = Enrollet

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

newtab-toast-dismiss-button =
    .title = Argas
    .aria-label = Argas

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Dizoleiñ ar pep gwellañ eus ar web
newtab-pocket-onboarding-cta = Furchal a ra { -pocket-brand-name } en embannadurioù liesseurt evit degas deoc'h an titouroù pouezusañ, awenusañ ha fiziadusañ, war-eeun war ho merdeer { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Chaous, un dra bennak a zo a-dreuz en ur gargañ an endalc'had.
newtab-error-fallback-refresh-link = Adkargit ar bajenn evit klask en-dro.

## Customization Menu

newtab-custom-shortcuts-title = Berradennoù
newtab-custom-shortcuts-subtitle = Lec'hiennoù bet enrollet pe gweladennet ganeoc'h
newtab-custom-shortcuts-toggle =
    .label = Berradennoù
    .description = Lec'hiennoù bet enrollet pe gweladennet ganeoc'h
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } renk
        [two] { $num } renk
        [few] { $num } renk
        [many] { $num } a renkoù
       *[other] { $num } renk
    }
newtab-custom-sponsored-sites = Berradennoù paeroniet
newtab-custom-pocket-title = Erbedet gant { -pocket-brand-name }
newtab-custom-pocket-subtitle = Danvezioù dibar dibabet gant { -pocket-brand-name }, ezel familh { -brand-product-name }
newtab-custom-pocket-sponsored = Istorioù paeroniet
newtab-custom-pocket-show-recent-saves = Diskouez an enrolladennoù diwezhañ
newtab-custom-recent-title = Oberiantiz nevesañ
newtab-custom-recent-subtitle = Un dibab lec'hiennoù ha danvezioù nevez
newtab-custom-recent-toggle =
    .label = Oberiantiz nevesañ
    .description = Un dibab lec'hiennoù ha danvezioù nevez
newtab-custom-close-button = Serriñ
newtab-custom-settings = Merañ muioc'h a arventennoù

## New Tab Wallpapers

newtab-wallpaper-title = Drekleurioù
newtab-wallpaper-upload-image = Kas ur skeudenn
newtab-wallpaper-custom-color = Dibab ul liv
newtab-wallpaper-light-red-panda = Panda ruz
newtab-wallpaper-light-mountain = Menez gwenn
newtab-wallpaper-light-sky = Oabl gant koumoul limestra ha roz
newtab-wallpaper-light-color = Furmoù glas, roz ha melen
newtab-wallpaper-light-landscape = Menezioù gant ur vrumenn c’hlas
newtab-wallpaper-light-beach = Traezhenn gant ur balmezenn
newtab-wallpaper-dark-aurora = Tarzh-gouloù
newtab-wallpaper-dark-color = Furmoù ruz ha glas
newtab-wallpaper-dark-panda = Panda ruz kuzhet er c’hoad
newtab-wallpaper-dark-mountain = Menezioù

## Solid Colors

newtab-wallpaper-blue = Glas
newtab-wallpaper-light-blue = Glas sklaer
newtab-wallpaper-light-purple = Limestra sklaer
newtab-wallpaper-light-green = Gwer sklaer
newtab-wallpaper-green = Gwer
newtab-wallpaper-yellow = Melen
newtab-wallpaper-orange = Orañjez
newtab-wallpaper-pink = Roz
newtab-wallpaper-light-pink = Roz sklaer
newtab-wallpaper-red = Ruz
newtab-wallpaper-dark-blue = Glas teñval
newtab-wallpaper-dark-purple = Limestra teñval
newtab-wallpaper-dark-green = Gwer teñval
newtab-wallpaper-brown = Gell

## Abstract

newtab-wallpaper-category-title-abstract = Difetis
newtab-wallpaper-abstract-green = Furmoù gwer
newtab-wallpaper-abstract-blue = Furmoù glas
newtab-wallpaper-abstract-purple = Furmoù limestra
newtab-wallpaper-abstract-orange = Furmoù orañjez
newtab-wallpaper-abstract-blue-purple = Furmoù limestra hag orañjez

## Celestial

newtab-wallpaper-category-title-photographs = Fotoioù
newtab-wallpaper-beach-at-sunrise = Traezhenn e-pad ar sav-heol
newtab-wallpaper-beach-at-sunset = Traezhenn e-pad ar c’huzh-heol
newtab-wallpaper-storm-sky = Oabl arnevek
newtab-wallpaper-sky-with-pink-clouds = Oabl gant koumoul roz
newtab-wallpaper-red-panda-yawns-in-a-tree = Panda ruz o vazailhat en ur wezenn
newtab-wallpaper-white-mountains = Menezioù gwenn
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Foto gant <a data-l10n-name="name-link">{ $author_string }</a> war <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-button = Komprenet am eus
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Argas
    .aria-label = Serriñ an diflugell
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Paeroniet
newtab-weather-menu-change-location = Cheñch al lec’hiadur
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Klask ul lec’h
    .aria-label = Klask ul lec’h
newtab-weather-change-location-search-input = Klask ul lec’h
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Eeun
newtab-weather-menu-weather-display-option-detailed = Munudoù
newtab-weather-menu-temperature-units = Unanenn wrezverk
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-learn-more = Gouzout hiroc’h

## Topic Labels

newtab-topic-label-education = Deskadurezh
newtab-topic-label-arts = Dudi
newtab-topic-label-food = Boued
newtab-topic-label-health = Yec’hed
newtab-topic-label-hobbies = C’hoarioù video
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Arc’hant
newtab-topic-label-government = Politikerezh
newtab-topic-label-education-science = Skiantoù
newtab-topic-label-sports = Sportoù
newtab-topic-label-tech = Teknologiezh
newtab-topic-label-travel = Beajiñ

## Topic Selection Modal

newtab-topic-selection-save-button = Enrollañ
newtab-topic-selection-cancel-button = Nullañ
newtab-topic-selection-button-maybe-later = Diwezhatoc’h marteze

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Heuliañ
newtab-section-following-button = O heuliañ

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Stankañ
newtab-section-blocked-button = Stanket

## Confirmation modal for blocking a section

# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Stankañ { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-followed-topics = O heuliañ
newtab-section-mangage-topics-blocked-topics = Stanket
