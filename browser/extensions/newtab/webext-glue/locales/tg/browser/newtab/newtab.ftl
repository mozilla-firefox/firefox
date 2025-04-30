# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Варақаи нав
newtab-settings-button =
    .title = Танзим кардани саҳифаи худ дар варақаи нав
newtab-personalize-settings-icon-label =
    .title = Шахсисозии варақаи нав
    .aria-label = Танзимот
newtab-settings-dialog-label =
    .aria-label = Танзимот
newtab-personalize-icon-label =
    .title = Танзимоти шахсии варақаи нав
    .aria-label = Танзимоти шахсии варақаи нав
newtab-personalize-dialog-label =
    .aria-label = Танзимоти шахсӣ
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Ҷустуҷӯ
    .aria-label = Ҷустуҷӯ
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Нишониеро тавассути { $engine } ҷустуҷӯ кунед ё ворид намоед
newtab-search-box-handoff-text-no-engine = Нишониеро ҷустуҷӯ кунед ё ворид намоед
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Нишониеро тавассути { $engine } ҷустуҷӯ кунед ё ворид намоед
    .title = Нишониеро тавассути { $engine } ҷустуҷӯ кунед ё ворид намоед
    .aria-label = Нишониеро тавассути { $engine } ҷустуҷӯ кунед ё ворид намоед
newtab-search-box-handoff-input-no-engine =
    .placeholder = Нишониеро ҷустуҷӯ кунед ё ворид намоед
    .title = Нишониеро ҷустуҷӯ кунед ё ворид намоед
    .aria-label = Нишониеро ҷустуҷӯ кунед ё ворид намоед
newtab-search-box-text = Ҷустуҷӯ дар Интернет
newtab-search-box-input =
    .placeholder = Ҷустуҷӯ дар Интернет
    .aria-label = Ҷустуҷӯ дар Интернет

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Илова кардани низоми ҷустуҷӯӣ
newtab-topsites-add-shortcut-header = Миёнбури нав
newtab-topsites-edit-topsites-header = Таҳрир кардани сомонаи беҳтарин
newtab-topsites-edit-shortcut-header = Таҳрир кардани миёнбур
newtab-topsites-add-shortcut-label = Илова кардани миёнбур
newtab-topsites-title-label = Сарлавҳа
newtab-topsites-title-input =
    .placeholder = Сарлавҳаро ворид намоед
newtab-topsites-url-label = Нишонии URL
newtab-topsites-url-input =
    .placeholder = Нишонии URL-ро ворид кунед ё гузоред
newtab-topsites-url-validation = Нишонии URL-и эътибор лозим аст
newtab-topsites-image-url-label = Нишонии URL-и тасвири шахсӣ
newtab-topsites-use-image-link = Истифодаи тасвири шахсӣ…
newtab-topsites-image-validation = Тасвир бор карда нашуд. Нишонии URL-и дигареро кӯшиш кунед.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Бекор кардан
newtab-topsites-delete-history-button = Нест кардан аз таърих
newtab-topsites-save-button = Нигоҳ доштан
newtab-topsites-preview-button = Пешнамоиш
newtab-topsites-add-button = Илова кардан

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Шумо мутмаин ҳастед, ки мехоҳед ҳар як намунаи ин саҳифаро аз таърихи худ тоза намоед?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ин амал бекор карда намешавад.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Сарпарастӣ

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Кушодани меню
    .aria-label = Кушодани меню
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Тоза кардан
    .aria-label = Тоза кардан
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Кушодани меню
    .aria-label = Кушодани менюи муҳтавоӣ барои { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Таҳрир кардани ин сомона
    .aria-label = Таҳрир кардани ин сомона

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Таҳрир кардан
newtab-menu-open-new-window = Кушодан дар равзанаи нав
newtab-menu-open-new-private-window = Кушодан дар равзанаи хусусии нав
newtab-menu-dismiss = Нодида гузарондан
newtab-menu-pin = Васл кардан
newtab-menu-unpin = Ҷудо кардан
newtab-menu-delete-history = Нест кардан аз таърих
newtab-menu-save-to-pocket = Нигоҳ доштан ба { -pocket-brand-name }
newtab-menu-delete-pocket = Нест кардан аз { -pocket-brand-name }
newtab-menu-archive-pocket = Бойгонӣ кардан ба { -pocket-brand-name }
newtab-menu-show-privacy-info = Сарпарастони мо ва махфияти шумо
newtab-menu-about-fakespot = Дар бораи «{ -fakespot-brand-name }»
newtab-menu-report-content = Гузориш дар бораи ин муҳтаво
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Манъ кардан
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Бекор кардани обуна аз мавзуъ

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Идоракунии муҳтавои сарпарастӣ
newtab-menu-our-sponsors-and-your-privacy = Сарпарастони мо ва махфияти шумо
newtab-menu-report-this-ad = Гузориш дар бораи ин таблиғ

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Тайёр
newtab-privacy-modal-button-manage = Идоракунии танзимоти муҳтавои сарпарастӣ
newtab-privacy-modal-header = Махфияти шумо муҳим аст.
newtab-privacy-modal-paragraph-2 =
    Илова ба нигоҳдории ҳикояҳои ҷолиб, мо, инчунин, ба шумо муҳтавои мувофиқ ва тафтишшударо аз сарпарастони мунтахаб нишон медиҳем. Боварӣ ҳосил кунед, ки <strong>маълумоти тамошобинӣ ҳеҷ вақт нусхаи шахсии «{ -brand-product-name }»-и шуморо бесоҳиб намемонад</strong> — ҳатто мо ба маълумоти шахсии шумо дастрасӣ надорем, сарпарастони мо ҳам дастрасӣ надоранд.
    сарпарастон низ надоранд.
newtab-privacy-modal-link = Маълумот гиред, ки чӣ тавр махфият дар варақаи нав риоя карда мешавад

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Тоза кардани хатбаракҳо
# Bookmark is a verb here.
newtab-menu-bookmark = Хатбарак

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Нусха бардоштани пайванди боргирӣ
newtab-menu-go-to-download-page = Гузариш ба саҳифаи боргирӣ
newtab-menu-remove-download = Нест кардан аз таърих

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Намоиш додан дар ҷӯянда
       *[other] Кушодани ҷузвдони дорои файл
    }
newtab-menu-open-file = Кушодани файл

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Дидашуда
newtab-label-bookmarked = Дар хатбаракҳо
newtab-label-removed-bookmark = Хатбарак тоза карда шуд
newtab-label-recommended = Ҳавасангез
newtab-label-saved = Ба { -pocket-brand-name } нигоҳ дошта шуд
newtab-label-download = Боргиришуда
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · дорои реклама мебошад
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Аз тарафи сарпарасти { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } дақиқа

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Тоза кардани қисмат
newtab-section-menu-collapse-section = Пинҳон кардани қисмат
newtab-section-menu-expand-section = Нишон додани қисмат
newtab-section-menu-manage-section = Идоракунии қисмат
newtab-section-menu-manage-webext = Идоракунии васеъшавӣ
newtab-section-menu-add-topsite = Илова кардан ба сомонаҳои беҳтарин
newtab-section-menu-add-search-engine = Илова кардани низоми ҷустуҷӯӣ
newtab-section-menu-move-up = Ба боло гузоштан
newtab-section-menu-move-down = Ба поён гузоштан
newtab-section-menu-privacy-notice = Огоҳномаи махфият

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Пинҳон кардани қисмат
newtab-section-expand-section-label =
    .aria-label = Нишон додани қисмат

## Section Headers.

newtab-section-header-topsites = Сомонаҳои беҳтарин
newtab-section-header-recent-activity = Фаъолияти охирин
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Аз тарафи «{ $provider }» тавсия дода мешавад
newtab-section-header-stories = Ҳикояҳои андешаангез
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Маҷмуи маълумоти интихобшуда барои шумо

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Тамошобинии сомонаҳоро оғоз намоед ва мо баъзеи мақолаҳои шавқовар, видеоҳо ва саҳифаҳои дигареро, ки шумо тамошо кардед ё ба хатбаракҳо гузоштед, дар ин ҷо намоиш медиҳем.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Шумо ҳамаро хондед. Барои хондани ҳикояҳои ҷолиби дигар аз «{ $provider }» дертар биёед. Интизор шуда наметавонед? Барои пайдо кардани ҳикояҳои бузург аз саросари Интернет, мавзуи маълумеро интихоб намоед.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Шумо ҳамаро хондед. Барои хондани ҳикояҳои дигар дертар биёед. Интизор шуда наметавонед? Барои пайдо кардани ҳикояҳои бузург аз саросари Интернет, мавзуи маълумеро интихоб намоед.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Шумо ҳамаро хондед!
newtab-discovery-empty-section-topstories-content = Барои ҳикоятҳои бештар дертар баргардед.
newtab-discovery-empty-section-topstories-try-again-button = Аз нав кӯшиш кардан
newtab-discovery-empty-section-topstories-loading = Бор шуда истодааст…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Хуш! Мо ин қисматро қариб бор кардем, аммо на он қадар зиёд.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Мавзуъҳои маъмул:
newtab-pocket-new-topics-title = Ҳикоятҳои боз ҳам бештар лозиманд? Ба ҳамин мавзуъҳои оммавӣ аз { -pocket-brand-name } нигаред
newtab-pocket-more-recommendations = Тавсияҳои бештар
newtab-pocket-learn-more = Маълумоти бештар
newtab-pocket-cta-button = «{ -pocket-brand-name }»-ро бор кунед
newtab-pocket-cta-text = Ҳикояҳоеро, ки дӯст медоред, дар { -pocket-brand-name } нигоҳ доред ва ба зеҳни худ аз хониши дилрабо қувват диҳед.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } қисми оилаи { -brand-product-name } мебошад
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Нигоҳ доштан
newtab-pocket-saved = Нигоҳ дошта шуд

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Бештар ба ин монанд
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Ман ҳавасманд нестам
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Ташаккур. Фикру мулоҳизаҳои шумо ба мо барои беҳтар кардани навори хабарҳои шумо ёрӣ медиҳанд.
newtab-toast-dismiss-button =
    .title = Нодида гузарондан
    .aria-label = Нодида гузарондан

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Маводи беҳтаринро дар Интернет пайдо намоед
newtab-pocket-onboarding-cta = «{ -pocket-brand-name }» ҳаҷми васеи нашрияҳои гуногунро баррасӣ карда, ба браузери «{ -brand-product-name }»-и шумо муҳтавои ахборотӣ, илҳомбахш ва боэътимодро таъмин менамояд.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Оҳ, ҳангоми боркунии ин муҳтаво чизе нодуруст ба миён омад.
newtab-error-fallback-refresh-link = Барои аз нав кӯшиш кардан саҳифаро навсозӣ намоед.

## Customization Menu

newtab-custom-shortcuts-title = Миёнбурҳо
newtab-custom-shortcuts-subtitle = Сомонаҳое, ки шумо нигоҳ медоред ё ба онҳо ворид мешавед
newtab-custom-shortcuts-toggle =
    .label = Миёнбурҳо
    .description = Сомонаҳое, ки шумо нигоҳ медоред ё ба онҳо ворид мешавед
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } сатр
       *[other] { $num } сатр
    }
newtab-custom-sponsored-sites = Миёнбурҳои сарпарастӣ
newtab-custom-pocket-title = Аз тарафи { -pocket-brand-name } тавсия дода мешавад
newtab-custom-pocket-subtitle = Муҳтавои мустасно аз тарафи { -pocket-brand-name }, қисми оилаи { -brand-product-name } дастгирӣ карда мешавад
newtab-custom-stories-toggle =
    .label = Ҳикояҳои тавсияшуда
    .description = Маводҳои истисноӣ, ки аз ҷониби оилаи «{ -brand-product-name }» таҳия шудааст
newtab-custom-pocket-sponsored = Мақолаҳои сарпарастӣ
newtab-custom-pocket-show-recent-saves = Намоиш додани маводҳои охирин
newtab-custom-recent-title = Фаъолияти охирин
newtab-custom-recent-subtitle = Интихоби сомонаҳо ва муҳтавои охирин
newtab-custom-recent-toggle =
    .label = Фаъолияти охирин
    .description = Интихоби сомонаҳо ва муҳтавои охирин
newtab-custom-weather-toggle =
    .label = Обу ҳаво
    .description = Ҳолати обу ҳаво барои имрӯз
newtab-custom-close-button = Пӯшидан
newtab-custom-settings = Идоракунии танзимоти бештар

## New Tab Wallpapers

newtab-wallpaper-title = Тасвирҳои замина
newtab-wallpaper-reset = Ба ҳолати пешфарз барқарор кунед
newtab-wallpaper-upload-image = Бор кардани тасвир
newtab-wallpaper-custom-color = Рангеро интихоб кунед
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Андозаи тасвир зиёда аз маҳдудияти андозаи файли { $file_size } МБ мебошад. Лутфан, кӯшиш кунед, ки файлеро бо андозаи хурдтар бор намоед.
newtab-wallpaper-error-file-type = Мо файли шуморо бор карда натавонистем. Лутфан, бо навъи дигари файл аз нав кӯшиш намоед.
newtab-wallpaper-light-red-panda = Пандаи сурх
newtab-wallpaper-light-mountain = Кӯҳи сафед
newtab-wallpaper-light-sky = Осмон бо абрҳои лоҷувард ва гулобӣ
newtab-wallpaper-light-color = Шаклҳои кабуд, гулобӣ ва зард
newtab-wallpaper-light-landscape = Манзараи кӯҳӣ бо тумани кабуд
newtab-wallpaper-light-beach = Соҳил бо дарахти хурмо
newtab-wallpaper-dark-aurora = Дурахши қутбӣ
newtab-wallpaper-dark-color = Шаклҳои сурх ва кабуд
newtab-wallpaper-dark-panda = Пандаи сурх дар ҷангал пинҳон шудааст
newtab-wallpaper-dark-sky = Манзараи шаҳр бо осмони шабона
newtab-wallpaper-dark-mountain = Манзараи кӯҳӣ
newtab-wallpaper-dark-city = Манзараи шаҳри лоҷувард
newtab-wallpaper-dark-fox-anniversary = Рӯбоҳи ҷилодор дар роҳи сангфарш дар назди ҷангал
newtab-wallpaper-light-fox-anniversary = Рӯбоҳи ҷилодор дар саҳрои сералаф бо манзараи тумани кӯҳӣ

## Solid Colors

newtab-wallpaper-category-title-colors = Рангҳои яклухт
newtab-wallpaper-blue = Кабуд
newtab-wallpaper-light-blue = Кабуди равшан
newtab-wallpaper-light-purple = Лоҷуварди равшан
newtab-wallpaper-light-green = Сабзи равшан
newtab-wallpaper-green = Сабз
newtab-wallpaper-beige = Қаҳваранг
newtab-wallpaper-yellow = Зард
newtab-wallpaper-orange = Норинҷӣ
newtab-wallpaper-pink = Гулобӣ
newtab-wallpaper-light-pink = Гулобии равшан
newtab-wallpaper-red = Сурх
newtab-wallpaper-dark-blue = Кабди торик
newtab-wallpaper-dark-purple = Лоҷуварди торик
newtab-wallpaper-dark-green = Сабзи торик
newtab-wallpaper-brown = Қаҳвагӣ

## Abstract

newtab-wallpaper-category-title-abstract = Мавҳум
newtab-wallpaper-abstract-green = Шаклҳои сабз
newtab-wallpaper-abstract-blue = Шаклҳои кабуд
newtab-wallpaper-abstract-purple = Шаклҳои лоҷувард
newtab-wallpaper-abstract-orange = Шаклҳои норинҷӣ
newtab-wallpaper-gradient-orange = Тағйирёбии норинҷӣ ва голубӣ
newtab-wallpaper-abstract-blue-purple = Шаклҳои кабуд ва норинҷӣ
newtab-wallpaper-abstract-white-curves = Сафед бо хатҳои каҷи сояандоз
newtab-wallpaper-abstract-purple-green = Тобиши лоҷувард ва сабзи равшан
newtab-wallpaper-abstract-blue-purple-waves = Шаклҳои мавҷноки кабуд ва норинҷӣ
newtab-wallpaper-abstract-black-waves = Шаклҳои мавҷноки сиёҳ

## Celestial

newtab-wallpaper-category-title-photographs = Суратҳо
newtab-wallpaper-beach-at-sunrise = Соҳил дар тулӯи офтоб
newtab-wallpaper-beach-at-sunset = Соҳил дар ғуруби офтоб
newtab-wallpaper-storm-sky = Осмони тӯфонӣ
newtab-wallpaper-sky-with-pink-clouds = Осмон бо абрҳои гулобӣ
newtab-wallpaper-red-panda-yawns-in-a-tree = Пандаи сурх дар дарахт хамёза мекашад
newtab-wallpaper-white-mountains = Кӯҳҳои сафед
newtab-wallpaper-hot-air-balloons = Рангҳои гуногуни пуфакҳои ҳавоӣ дар давоми рӯз
newtab-wallpaper-starry-canyon = Шаби ситоразори кабуд
newtab-wallpaper-suspension-bridge = Акси пули хокистариранги овезон дар давоми рӯз
newtab-wallpaper-sand-dunes = Хомаҳои регии сафед
newtab-wallpaper-palm-trees = Акси сиёҳи дарахтҳои ҷавзи ҳиндӣ дар соати тиллоӣ
newtab-wallpaper-blue-flowers = Аксҳои наздиктарини гулҳо бо гулбаргҳои кабуд дар гулгулшукуфоӣ
newtab-wallpaper-forest-trees = Дарахтҳои бо туман пӯшидашуда
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Акс аз ҷониби <a data-l10n-name="name-link">{ $author_string }</a> дар <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Чакраҳои рангро кӯшиш намоед
newtab-wallpaper-feature-highlight-content = Бо истифода аз тасвирҳои замина ба варақаи нави худ намуди зоҳирии наверо диҳед.
newtab-wallpaper-feature-highlight-button = Фаҳмидам
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Нодида гузарондан
    .aria-label = Пӯшидани равзанаҳои зоҳиршаванда
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Осмонӣ
newtab-wallpaper-celestial-lunar-eclipse = Гирифти Моҳ
newtab-wallpaper-celestial-earth-night = Акси шабона аз мадори пасти кураи Замин
newtab-wallpaper-celestial-starry-sky = Осмони ситоразор
newtab-wallpaper-celestial-eclipse-time-lapse = Вақти фарогирии гирифти Моҳ
newtab-wallpaper-celestial-black-hole = Тасвири роҳи каҳкашон бо сӯрохи сиёҳ
newtab-wallpaper-celestial-river = Акси дарё аз моҳвораи алоқа

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Дидани обу ҳаво дар { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Реклама
newtab-weather-menu-change-location = Иваз кардани макон
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Ҷустуҷӯи макон
    .aria-label = Ҷустуҷӯи макон
newtab-weather-change-location-search-input = Ҷустуҷӯи макон
newtab-weather-menu-weather-display = Намоиши обу ҳаво
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Одӣ
newtab-weather-menu-change-weather-display-simple = Гузариш ба намуди одӣ
newtab-weather-menu-weather-display-option-detailed = Ботафсил
newtab-weather-menu-change-weather-display-detailed = Гузариш ба намуди ботафсил
newtab-weather-menu-temperature-units = Воҳидҳои ченаки ҳарорат
newtab-weather-menu-temperature-option-fahrenheit = Фаренгейт
newtab-weather-menu-temperature-option-celsius = Селсий
newtab-weather-menu-change-temperature-units-fahrenheit = Гузариш ба Фаренгейт
newtab-weather-menu-change-temperature-units-celsius = Гузариш ба Селсий
newtab-weather-menu-hide-weather = Нинҳон кардани обу ҳаво дар варақаи нав
newtab-weather-menu-learn-more = Маълумоти бештар
# This message is shown if user is working offline
newtab-weather-error-not-available = Айни ҳол маълумот дар бораи обу ҳаво дастнорас аст.

## Topic Labels

newtab-topic-label-business = Тиҷорат
newtab-topic-label-career = Пешравӣ
newtab-topic-label-education = Илму маърифат
newtab-topic-label-arts = Вақтхушӣ
newtab-topic-label-food = Ғизо
newtab-topic-label-health = Тандурустӣ
newtab-topic-label-hobbies = Бозиҳо
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Молия
newtab-topic-label-society-parenting = Тарбия
newtab-topic-label-government = Сиёсат
newtab-topic-label-education-science = Илм
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Таҷрибаи ҳаёт
newtab-topic-label-sports = Варзишҳо
newtab-topic-label-tech = Технологияҳо
newtab-topic-label-travel = Сайёҳӣ
newtab-topic-label-home = Хона ва боғ

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Барои танзими дақиқи навори хабарҳои худ, мавзуъҳоеро интихоб намоед
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Ду ё зиёда мавзуи дӯстдоштаро интихоб намоед. Нозирони коршиноси мо ба ҳикоятҳое, ки ба шавқу завқи шумо мувофиқанд, афзалият медиҳанд. Дар вақти дилхоҳ навсозӣ кунед.
newtab-topic-selection-save-button = Нигоҳ доштан
newtab-topic-selection-cancel-button = Бекор кардан
newtab-topic-selection-button-maybe-later = Шояд дертар
newtab-topic-selection-privacy-link = Бифаҳмед, ки чӣ тавр мо маълумотро ҳифз ва идора мекунем
newtab-topic-selection-button-update-interests = Манфиатҳои худро навсозӣ кунед
newtab-topic-selection-button-pick-interests = Манфиатҳои худро интихоб кунед

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Обуна шавед
newtab-section-following-button = Обуна шуд
newtab-section-unfollow-button = Бекор кардани обуна

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Манъ кардан
newtab-section-blocked-button = Манъ карда шуд
newtab-section-unblock-button = Кушодан

## Confirmation modal for blocking a section

newtab-section-cancel-button = Ҳоло не
newtab-section-confirm-block-topic-p1 = Шумо мутмаин ҳастед, ки мехоҳед ин мавзуъро манъ кунед?
newtab-section-confirm-block-topic-p2 = Мавзуъҳои манъшуда дигар дар навори хабарҳои шумо пайдо намешаванд.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Манъ кардани { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Мавзуъҳо
newtab-section-manage-topics-button-v2 =
    .label = Идоракунии мавзуъҳо
newtab-section-mangage-topics-followed-topics = Пайгирӣ карда мешавад
newtab-section-mangage-topics-followed-topics-empty-state = Шумо то ҳол ягон мавзуъро пайгирӣ накардаед.
newtab-section-mangage-topics-blocked-topics = Манъ карда мешавад
newtab-section-mangage-topics-blocked-topics-empty-state = Шумо то ҳол ягон мавзуъро манъ накардаед.
newtab-custom-wallpaper-title = Тасвирҳои заминаи фармоишӣ дар ин ҷой мебошанд
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Барои ба таври худ танзим кардани «{ -brand-product-name }», тасвири заминаи худро бор кунед ё ранги дилхоҳеро интихоб намоед.
newtab-custom-wallpaper-cta = Озмоед
