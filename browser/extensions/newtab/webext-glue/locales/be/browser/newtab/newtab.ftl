# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Новая картка
newtab-settings-button =
    .title = Наладзіць вашу старонку новай карткі
newtab-personalize-settings-icon-label =
    .title = Персаналізаваць новую картку
    .aria-label = Налады
newtab-settings-dialog-label =
    .aria-label = Налады
newtab-personalize-icon-label =
    .title = Персаналізаваць новую картку
    .aria-label = Персаналізаваць новую картку
newtab-personalize-dialog-label =
    .aria-label = Персаналізаваць
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Шукаць
    .aria-label = Шукаць
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Шукайце ў { $engine } або ўвядзіце адрас
newtab-search-box-handoff-text-no-engine = Увядзіце запыт або адрас
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Шукайце ў { $engine } або ўвядзіце адрас
    .title = Шукайце ў { $engine } або ўвядзіце адрас
    .aria-label = Шукайце ў { $engine } або ўвядзіце адрас
newtab-search-box-handoff-input-no-engine =
    .placeholder = Увядзіце запыт або адрас
    .title = Увядзіце запыт або адрас
    .aria-label = Увядзіце запыт або адрас
newtab-search-box-text = Шукаць у Iнтэрнэце
newtab-search-box-input =
    .placeholder = Пошук у інтэрнэце
    .aria-label = Шукайце ў Інтэрнэце

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Дадаць пашукавік
newtab-topsites-add-shortcut-header = Новы цэтлік
newtab-topsites-edit-topsites-header = Рэдагаваць папулярны сайт
newtab-topsites-edit-shortcut-header = Рэдагаваць цэтлік
newtab-topsites-add-shortcut-label = Дадаць цэтлік
newtab-topsites-title-label = Загаловак
newtab-topsites-title-input =
    .placeholder = Увядзіце назву
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Увядзіце або ўстаўце URL
newtab-topsites-url-validation = Патрабуецца сапраўдны URL
newtab-topsites-image-url-label = Уласны URL выявы
newtab-topsites-use-image-link = Выкарыстоўваць уласную выяву…
newtab-topsites-image-validation = Не ўдалося атрымаць выяву. Паспрабуйце іншы URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Скасаваць
newtab-topsites-delete-history-button = Выдаліць з гісторыі
newtab-topsites-save-button = Захаваць
newtab-topsites-preview-button = Перадпрагляд
newtab-topsites-add-button = Дадаць

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Вы сапраўды жадаеце выдаліць усе запісы аб гэтай старонцы з гісторыі?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Гэта дзеянне немагчыма адмяніць.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Спонсарскі

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Адкрыць меню
    .aria-label = Адкрыць меню
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Выдаліць
    .aria-label = Выдаліць
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Адкрыць меню
    .aria-label = Адкрыць кантэкстнае меню для { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Рэдагаваць гэты сайт
    .aria-label = Рэдагаваць гэты сайт

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Правіць
newtab-menu-open-new-window = Адкрыць у новым акне
newtab-menu-open-new-private-window = Адкрыць у новым прыватным акне
newtab-menu-dismiss = Адхіліць
newtab-menu-pin = Замацаваць
newtab-menu-unpin = Адмацаваць
newtab-menu-delete-history = Выдаліць з гісторыі
newtab-menu-save-to-pocket = Захаваць у { -pocket-brand-name }
newtab-menu-delete-pocket = Выдаліць з { -pocket-brand-name }
newtab-menu-archive-pocket = Архіваваць у { -pocket-brand-name }
newtab-menu-show-privacy-info = Нашы спонсары і ваша прыватнасць
newtab-menu-about-fakespot = Пра { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Блакаваць
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Адпісацца ад тэмы

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Кіраваць спонсарскім змесцівам
newtab-menu-our-sponsors-and-your-privacy = Нашы спонсары і ваша прыватнасць

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Зроблена
newtab-privacy-modal-button-manage = Кіраваць наладамі спонсарскага змесціва
newtab-privacy-modal-header = Ваша прыватнасць мае значэнне.
newtab-privacy-modal-paragraph-2 =
    У дадатак да захапляльных гісторый, мы таксама паказваем вам рэлевантны,
    правераны змест ад выбраных спонсараў. Будзьце ўпэўненыя, <strong>вашы дадзеныя
    аглядання ніколі не пакідаюць вашу копію { -brand-product-name }</strong> — мы іх не бачым,
    гэтаксама і нашы спонсары.
newtab-privacy-modal-link = Даведайцеся, як працуе прыватнасць на новай картцы

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Выдаліць закладку
# Bookmark is a verb here.
newtab-menu-bookmark = У закладкі

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Капіяваць спасылку сцягвання
newtab-menu-go-to-download-page = Перайсці на старонку сцягвання
newtab-menu-remove-download = Выдаліць з гісторыі

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Паказаць у Finder
       *[other] Адкрыць змяшчальную папку
    }
newtab-menu-open-file = Адкрыць файл

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Наведанае
newtab-label-bookmarked = У закладках
newtab-label-removed-bookmark = Закладка выдалена
newtab-label-recommended = Тэндэнцыі
newtab-label-saved = Захавана ў { -pocket-brand-name }
newtab-label-download = Сцягнута
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Спансаравана
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Ад спонсара { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } хв

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Выдаліць раздзел
newtab-section-menu-collapse-section = Згарнуць раздзел
newtab-section-menu-expand-section = Разгарнуць раздзел
newtab-section-menu-manage-section = Наладзіць раздзел
newtab-section-menu-manage-webext = Кіраваць пашырэннем
newtab-section-menu-add-topsite = Дадаць папулярны сайт
newtab-section-menu-add-search-engine = Дадаць пашукавік
newtab-section-menu-move-up = Пасунуць вышэй
newtab-section-menu-move-down = Пасунуць ніжэй
newtab-section-menu-privacy-notice = Паведамленне аб прыватнасці

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Згарнуць раздзел
newtab-section-expand-section-label =
    .aria-label = Разгарнуць раздзел

## Section Headers.

newtab-section-header-topsites = Папулярныя сайты
newtab-section-header-recent-activity = Апошняя актыўнасць
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Рэкамендавана { $provider }
newtab-section-header-stories = Гісторыі, якія прымушаюць задумацца
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Сённяшняя падборка для вас

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Пачніце агляданне, і мы пакажам вам тут некаторыя з найлепшых артыкулаў, відэаролікаў і іншых старонак, якія вы нядаўна наведалі або зрабілі закладкі.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Гатова. Праверце пазней, каб убачыць больш матэрыялаў ад { $provider }. Не жадаеце чакаць? Выберыце папулярную тэму, каб знайсці больш цікавых матэрыялаў з усяго Інтэрнэту.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Гатова. Праверце пазней, каб убачыць больш матэрыялаў. Не жадаеце чакаць? Выберыце папулярную тэму, каб знайсці больш цікавых матэрыялаў з усяго Інтэрнэту.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Вы ўсё прачыталі!
newtab-discovery-empty-section-topstories-content = Звярніцеся пазней, каб пабачыць больш артыкулаў.
newtab-discovery-empty-section-topstories-try-again-button = Паспрабаваць зноў
newtab-discovery-empty-section-topstories-loading = Чытаецца…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ой! Мы амаль загрузілі гэты раздзел, але не зусім.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Папулярныя тэмы:
newtab-pocket-new-topics-title = Хочаце яшчэ больш гісторый? Глядзіце гэтыя папулярныя тэмы ад { -pocket-brand-name }
newtab-pocket-more-recommendations = Больш рэкамендацый
newtab-pocket-learn-more = Падрабязней
newtab-pocket-cta-button = Атрымаць { -pocket-brand-name }
newtab-pocket-cta-text = Захоўвайце ўлюбёныя гісторыі ў { -pocket-brand-name }, і сілкуйце свой розум добрай чытанкай.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } уваходзіць у сямейства { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Захаваць
newtab-pocket-saved = Захавана

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Больш падобных
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Не для мяне
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Дзякуй. Ваш водгук дапаможа нам палепшыць вашу стужку.
newtab-toast-dismiss-button =
    .title = Схаваць
    .aria-label = Схаваць

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Адкрыйце для сябе лепшае з Інтэрнэту
newtab-pocket-onboarding-cta = { -pocket-brand-name } даследуе разнастайныя публікацыі, каб прынесці найбольш інфарматыўнае, натхняльнае і вартае даверу змесціва прама ў ваш браўзер { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ох, нешта пайшло не так пры загрузцы гэтага змесціва.
newtab-error-fallback-refresh-link = Абнавіць старонку, каб паўтарыць спробу.

## Customization Menu

newtab-custom-shortcuts-title = Цэтлікі
newtab-custom-shortcuts-subtitle = Сайты, якія вы захоўваеце або наведваеце
newtab-custom-shortcuts-toggle =
    .label = Цэтлікі
    .description = Сайты, якія вы захоўваеце або наведваеце
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } радок
        [few] { $num } радкі
       *[many] { $num } радкоў
    }
newtab-custom-sponsored-sites = Спонсарскія цэтлікі
newtab-custom-pocket-title = Рэкамендавана { -pocket-brand-name }
newtab-custom-pocket-subtitle = Выключнае змесціва, куратарам якога з'яўляецца { -pocket-brand-name }, частка сям'і { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Рэкамендаваныя гісторыі
    .description = Выключнае змесціва, курыраванае сямействам { -brand-product-name }
newtab-custom-pocket-sponsored = Артыкулы ад спонсараў
newtab-custom-pocket-show-recent-saves = Паказваць апошнія захаванні
newtab-custom-recent-title = Апошняя актыўнасць
newtab-custom-recent-subtitle = Падборка нядаўніх сайтаў і змесціва
newtab-custom-recent-toggle =
    .label = Апошняя актыўнасць
    .description = Падборка нядаўніх сайтаў і змесціва
newtab-custom-weather-toggle =
    .label = Надвор'е
    .description = Кароткі прагноз на сёння
newtab-custom-close-button = Закрыць
newtab-custom-settings = Кіраваць дадатковымі наладамі

## New Tab Wallpapers

newtab-wallpaper-title = Шпалеры
newtab-wallpaper-reset = Скінуць да прадвызначаных
newtab-wallpaper-upload-image = Зацягнуць выяву
newtab-wallpaper-custom-color = Выберыце колер
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Памер выявы перавышае абмежаванне ў { $file_size } МБ. Калі ласка, паспрабуйце загрузіць файл меншага памеру.
newtab-wallpaper-error-file-type = Мы не змаглі зацягнуць ваш файл. Паўтарыце спробу з іншым тыпам файла.
newtab-wallpaper-light-red-panda = Чырвоная панда
newtab-wallpaper-light-mountain = Белая гара
newtab-wallpaper-light-sky = Неба з фіялетавымі і ружовымі аблокамі
newtab-wallpaper-light-color = Сінія, ружовыя і жоўтыя формы
newtab-wallpaper-light-landscape = Горны пейзаж з блакітнага туману
newtab-wallpaper-light-beach = Пляж з пальмамі
newtab-wallpaper-dark-aurora = Палярнае ззянне
newtab-wallpaper-dark-color = Чырвоныя і сінія фігуры
newtab-wallpaper-dark-panda = Чырвоная панда схаваная ў лесе
newtab-wallpaper-dark-sky = Гарадскі пейзаж з начным небам
newtab-wallpaper-dark-mountain = Горны пейзаж
newtab-wallpaper-dark-city = Пурпурны гарадскі пейзаж
newtab-wallpaper-dark-fox-anniversary = Ліса на тратуары каля лесу
newtab-wallpaper-light-fox-anniversary = Ліса ў травяністым полі з туманным горным ландшафтам

## Solid Colors

newtab-wallpaper-category-title-colors = Аднатонныя колеры
newtab-wallpaper-blue = Сіні
newtab-wallpaper-light-blue = Блакітны
newtab-wallpaper-light-purple = Светла-фіялетавы
newtab-wallpaper-light-green = Светла-зялёны
newtab-wallpaper-green = Зялёны
newtab-wallpaper-beige = Бэжавы
newtab-wallpaper-yellow = Жоўты
newtab-wallpaper-orange = Аранжавы
newtab-wallpaper-pink = Ружовы
newtab-wallpaper-light-pink = Светла-ружовы
newtab-wallpaper-red = Чырвоны
newtab-wallpaper-dark-blue = Цёмна-сіні
newtab-wallpaper-dark-purple = Цёмна-фіялетавы
newtab-wallpaper-dark-green = Цёмна-зялёны
newtab-wallpaper-brown = Карычневы

## Abstract

newtab-wallpaper-category-title-abstract = Абстракцыя
newtab-wallpaper-abstract-green = Зялёныя формы
newtab-wallpaper-abstract-blue = Сінія формы
newtab-wallpaper-abstract-purple = Фіялетавыя формы
newtab-wallpaper-abstract-orange = Аранжавыя формы
newtab-wallpaper-gradient-orange = Градыент аранжавага і ружовага
newtab-wallpaper-abstract-blue-purple = Сінія і фіялетавыя формы
newtab-wallpaper-abstract-white-curves = Белы з зацененымі крывымі
newtab-wallpaper-abstract-purple-green = Градыент фіялетавага і зялёнага святла
newtab-wallpaper-abstract-blue-purple-waves = Сінія і фіялетавыя хвалістыя формы
newtab-wallpaper-abstract-black-waves = Чорныя хвалістыя формы

## Celestial

newtab-wallpaper-category-title-photographs = Фатаграфіі
newtab-wallpaper-beach-at-sunrise = Пляж на ўсходзе сонца
newtab-wallpaper-beach-at-sunset = Пляж на заходзе сонца
newtab-wallpaper-storm-sky = Навальнічнае неба
newtab-wallpaper-sky-with-pink-clouds = Неба з ружовымі аблокамі
newtab-wallpaper-red-panda-yawns-in-a-tree = Чырвоная панда пазяхае на дрэве
newtab-wallpaper-white-mountains = Белыя горы
newtab-wallpaper-hot-air-balloons = Розныя колеры паветраных шароў удзень
newtab-wallpaper-starry-canyon = Сіняя зорная ноч
newtab-wallpaper-suspension-bridge = Фатаграфія шэрага поўнападвеснага моста ў дзённы час
newtab-wallpaper-sand-dunes = Белыя пясчаныя выдмы
newtab-wallpaper-palm-trees = Сілуэт какосавых пальмаў у залаты час
newtab-wallpaper-blue-flowers = Фатаграфія буйным планам кветак з блакітнымі пялёсткамі
newtab-wallpaper-forest-trees = Дрэвы, пакрытыя туманам
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Фота <a data-l10n-name="name-link">{ $author_string }</a> з <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Паспрабуйце ўсплёск колеру
newtab-wallpaper-feature-highlight-content = Абнавіце выгляд новай карткі з дапамогай шпалер.
newtab-wallpaper-feature-highlight-button = Зразумела
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Адхіліць
    .aria-label = Закрыць выплыўное акно
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Нябесны
newtab-wallpaper-celestial-lunar-eclipse = Месяцовае зацьменне
newtab-wallpaper-celestial-earth-night = Начная фатаграфія з нізкай калязямной арбіты
newtab-wallpaper-celestial-starry-sky = Зорнае неба
newtab-wallpaper-celestial-eclipse-time-lapse = Прамежак часу месяцовага зацьмення
newtab-wallpaper-celestial-black-hole = Ілюстрацыя галактыкі з чорнай дзіркай
newtab-wallpaper-celestial-river = Спадарожнікавы здымак ракі

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Паглядзець прагноз у { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Спонсар
newtab-weather-menu-change-location = Змяніць месцазнаходжанне
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Шукаць месцазнаходжанне
    .aria-label = Шукаць месцазнаходжанне
newtab-weather-change-location-search-input = Шукаць месцазнаходжанне
newtab-weather-menu-weather-display = Паказ надвор'я
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Просты
newtab-weather-menu-change-weather-display-simple = Пераключыцца на просты выгляд
newtab-weather-menu-weather-display-option-detailed = Падрабязны
newtab-weather-menu-change-weather-display-detailed = Пераключыцца на падрабязны выгляд
newtab-weather-menu-temperature-units = Адзінкі вымярэння тэмпературы
newtab-weather-menu-temperature-option-fahrenheit = Фарэнгейт
newtab-weather-menu-temperature-option-celsius = Цэльсій
newtab-weather-menu-change-temperature-units-fahrenheit = Пераключыць на фарэнгейты
newtab-weather-menu-change-temperature-units-celsius = Пераключыць на градусы Цэльсія
newtab-weather-menu-hide-weather = Схаваць надвор'е на новай картцы
newtab-weather-menu-learn-more = Даведацца больш
# This message is shown if user is working offline
newtab-weather-error-not-available = Звесткі пра надвор'е зараз недаступныя.

## Topic Labels

newtab-topic-label-business = Бізнес
newtab-topic-label-career = Кар'ера
newtab-topic-label-education = Адукацыя
newtab-topic-label-arts = Забавы
newtab-topic-label-food = Ежа
newtab-topic-label-health = Здароўе
newtab-topic-label-hobbies = Гульні
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Грошы
newtab-topic-label-society-parenting = Выхаванне
newtab-topic-label-government = Палітыка
newtab-topic-label-education-science = Навука
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Лайфхакі
newtab-topic-label-sports = Спорт
newtab-topic-label-tech = Тэхналогіі
newtab-topic-label-travel = Падарожжы
newtab-topic-label-home = Дом і сад

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Выберыце тэмы, каб наладзіць сваю стужку
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Выберыце дзве або больш тэм. Нашы эксперты-куратары аддаюць перавагу гісторыям, якія адпавядаюць вашым інтарэсам. Абнаўляйце ў любы час.
newtab-topic-selection-save-button = Захаваць
newtab-topic-selection-cancel-button = Скасаваць
newtab-topic-selection-button-maybe-later = Магчыма пазней
newtab-topic-selection-privacy-link = Даведайцеся, як мы ахоўваем дадзеныя і распараджаемся імі
newtab-topic-selection-button-update-interests = Абнавіце свае зацікаўленасці
newtab-topic-selection-button-pick-interests = Выберыце свае зацікаўленасці

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Падпісацца
newtab-section-following-button = Падпісаны
newtab-section-unfollow-button = Адпісацца

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Блакаваць
newtab-section-blocked-button = Заблакаваны
newtab-section-unblock-button = Разблакаваць

## Confirmation modal for blocking a section

newtab-section-cancel-button = Не зараз
newtab-section-confirm-block-topic-p1 = Вы сапраўды хочаце заблакаваць гэтую тэму?
newtab-section-confirm-block-topic-p2 = Заблакаваныя тэмы больш не будуць з'яўляцца ў вашай стужцы.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Заблакаваць { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Тэмы
newtab-section-manage-topics-button-v2 =
    .label = Кіраванне тэмамі
newtab-section-mangage-topics-followed-topics = Падпіскі
newtab-section-mangage-topics-followed-topics-empty-state = Вы яшчэ не падпісаліся ні на адну тэму.
newtab-section-mangage-topics-blocked-topics = Заблакаваны
newtab-section-mangage-topics-blocked-topics-empty-state = Вы яшчэ не заблакавалі ніводнай тэмы.
