# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Нов раздел
newtab-settings-button =
    .title = Настройки на новия раздел
newtab-personalize-icon-label =
    .title = Приспособяване на новите раздели
    .aria-label = Приспособяване на новите раздели
newtab-personalize-dialog-label =
    .aria-label = Приспособяване
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Търсене
    .aria-label = Търсене
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Търсете с { $engine } или въведете адрес
newtab-search-box-handoff-text-no-engine = Търсете или въведете адрес
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Търсете с { $engine } или въведете адрес
    .title = Търсете с { $engine } или въведете адрес
    .aria-label = Търсете с { $engine } или въведете адрес
newtab-search-box-handoff-input-no-engine =
    .placeholder = Търсете или въведете адрес
    .title = Търсете или въведете адрес
    .aria-label = Търсете или въведете адрес
newtab-search-box-text = Търсене в интернет
newtab-search-box-input =
    .placeholder = Търсене в мрежата
    .aria-label = Търсене в мрежата

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Добавяне на търсеща машина
newtab-topsites-add-shortcut-header = Нова клавишна комбинация
newtab-topsites-edit-topsites-header = Променяне на често посещавана страница
newtab-topsites-edit-shortcut-header = Промяна на икона
newtab-topsites-add-shortcut-label = Добавяне на пряк път
newtab-topsites-title-label = Заглавие
newtab-topsites-title-input =
    .placeholder = Въведете заглавие
newtab-topsites-url-label = Адрес
newtab-topsites-url-input =
    .placeholder = Адрес
newtab-topsites-url-validation = Необходим е валиден URL
newtab-topsites-image-url-label = Адрес на изображение по желание
newtab-topsites-use-image-link = Използване изображение по желание…
newtab-topsites-image-validation = Изображението не може да бъде заредено. Опитайте с друг адрес.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Отказ
newtab-topsites-delete-history-button = Премахване от историята
newtab-topsites-save-button = Запазване
newtab-topsites-preview-button = Преглед
newtab-topsites-add-button = Добавяне

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Сигурни ли сте, че желаете да премахнете страницата навсякъде от историята?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Действието е необратимо.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Спонсорирано

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Отваряне на меню
    .aria-label = Отваряне на меню
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Премахване
    .aria-label = Премахване
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Отваряне на меню
    .aria-label = Отваряне на менюто за { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Променяне
    .aria-label = Променяне

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Променяне
newtab-menu-open-new-window = Отваряне в раздел
newtab-menu-open-new-private-window = Отваряне в поверителен прозорец
newtab-menu-dismiss = Затваряне
newtab-menu-pin = Закачане
newtab-menu-unpin = Откачане
newtab-menu-delete-history = Премахване от историята
newtab-menu-save-to-pocket = Запазване в { -pocket-brand-name }
newtab-menu-delete-pocket = Изтриване от { -pocket-brand-name }
newtab-menu-archive-pocket = Архивиране в { -pocket-brand-name }
newtab-menu-show-privacy-info = Спонсори и поверителност

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Готово
newtab-privacy-modal-button-manage = Управление на настройките за спонсорирано съдържание
newtab-privacy-modal-header = Вашата поверителност е от значение.
newtab-privacy-modal-paragraph-2 =
    Като допълнение на това, че намираме завладяващи истории,
    ние ви показваме и подходящо, проверено съдържание от избрани
    спонсори. Бъдете спокойни, <strong>данните ви от разглежданията никога
    не напускат вашето копие на { -brand-product-name }</strong> - ние не ги виждаме
    нашите спонсори също.
newtab-privacy-modal-link = Научете как работи поверителността на новия раздел

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Премахване на отметка
# Bookmark is a verb here.
newtab-menu-bookmark = Добавяне в отметки

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Копиране на препратка за изтегляне
newtab-menu-go-to-download-page = Към страницата за изтегляне
newtab-menu-remove-download = Премахване от историята

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Показване във Finder
       *[other] Отваряне на съдържащата папка
    }
newtab-menu-open-file = Отваряне на файла

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Посетена
newtab-label-bookmarked = Отметната
newtab-label-removed-bookmark = Отметката е премахната
newtab-label-recommended = Тенденции
newtab-label-saved = Запазено в { -pocket-brand-name }
newtab-label-download = Изтеглено
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Спонсорирано
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Спонсорирано от { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } мин.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Премахване на раздела
newtab-section-menu-collapse-section = Свиване на раздела
newtab-section-menu-expand-section = Разгъване на раздела
newtab-section-menu-manage-section = Управление на раздела
newtab-section-menu-manage-webext = Управление на добавката
newtab-section-menu-add-topsite = Добавете предпочитана страница
newtab-section-menu-add-search-engine = Добавяне на търсеща машина
newtab-section-menu-move-up = Преместване нагоре
newtab-section-menu-move-down = Преместване надолу
newtab-section-menu-privacy-notice = Политика за личните данни

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Свиване на раздела
newtab-section-expand-section-label =
    .aria-label = Разгъване на раздела

## Section Headers.

newtab-section-header-topsites = Предпочитани страници
newtab-section-header-recent-activity = Последна активност
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Препоръчано от { $provider }
newtab-section-header-stories = Истории, провокиращи размисъл
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Днешният избор за вас

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Разглеждайте и тук ще ви покажем някои от най-добрите статии, видео и други страници, които сте посетили или отметнали наскоро.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Разгледахте всичко. Проверете по-късно за повече истории от { $provider }. Нямате търпение? Изберете популярна тема, за да откриете повече истории от цялата Мрежа.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Разгледахте всичко. Проверете по-късно за още истории. Нямате търпение? Изберете популярна тема, за да откриете повече в интернет.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Изчетохте всичко!
newtab-discovery-empty-section-topstories-content = Проверете по-късно за повече статии.
newtab-discovery-empty-section-topstories-try-again-button = Нов опит
newtab-discovery-empty-section-topstories-loading = Зареждане…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ами сега! Почти заредихме тази секция, но не съвсем.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Популярни теми:
newtab-pocket-new-topics-title = Искате ли още истории? Вижте тези популярни теми избрани от { -pocket-brand-name }
newtab-pocket-more-recommendations = Повече препоръчани
newtab-pocket-learn-more = Научете повече
newtab-pocket-cta-button = Вземете { -pocket-brand-name }
newtab-pocket-cta-text = Запазете статиите, които харесвате в { -pocket-brand-name } и заредете ума си с увлекателни четива.
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Запазване
newtab-pocket-saved = Запазено

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Повече като това
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Не и за мен
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Благодаря! Вашата обратна връзка ще ни помогне в подбора за новини.
newtab-toast-dismiss-button =
    .title = Отхвърляне
    .aria-label = Отхвърляне

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Открийте най-доброто от интернет

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ааах, нещо се обърка и съдържанието не е заредено.
newtab-error-fallback-refresh-link = Презаредете страницата за повторен опит.

## Customization Menu

newtab-custom-shortcuts-title = Препратки
newtab-custom-shortcuts-subtitle = Страници за преглед по-късно
newtab-custom-shortcuts-toggle =
    .label = Препратки
    .description = Страници за преглед по-късно
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } ред
       *[other] { $num } реда
    }
newtab-custom-sponsored-sites = Спонсорирани препратки
newtab-custom-pocket-title = Препоръчани от { -pocket-brand-name }
newtab-custom-pocket-subtitle = Изключително съдържание, подбрано от { -pocket-brand-name }, част от семейството на { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Препоръчани истории
    .description = Изключително съдържание подбрано от семейството на { -brand-product-name }
newtab-custom-pocket-sponsored = Платени публикации
newtab-custom-pocket-show-recent-saves = Показване на последните запазени
newtab-custom-recent-title = Последна активност
newtab-custom-recent-subtitle = Избрани страници и съдържание
newtab-custom-recent-toggle =
    .label = Последна активност
    .description = Избрани страници и съдържание
newtab-custom-weather-toggle =
    .label = Времето
    .description = Времето днес накратко
newtab-custom-close-button = Затваряне
newtab-custom-settings = Настройки

## New Tab Wallpapers

newtab-wallpaper-title = Тапети
newtab-wallpaper-light-red-panda = Червена панда
newtab-wallpaper-light-mountain = Бяла планина
newtab-wallpaper-light-sky = Небе с лилави и розови облаци
newtab-wallpaper-light-color = Сини, розови и жълти фигури
newtab-wallpaper-light-landscape = Планински пейзаж със синя мъгла
newtab-wallpaper-light-beach = Плаж с палма
newtab-wallpaper-dark-aurora = Северно сияние
newtab-wallpaper-dark-color = Червени и сини фигури
newtab-wallpaper-dark-panda = Червена панда, скрита в гора
newtab-wallpaper-dark-sky = Градски пейзаж с нощно небе
newtab-wallpaper-dark-mountain = Планински пейзаж
newtab-wallpaper-dark-city = Лилав градски пейзаж

## Solid Colors

newtab-wallpaper-category-title-colors = Едноцветни
newtab-wallpaper-blue = Синьо
newtab-wallpaper-light-blue = Светлосиньо
newtab-wallpaper-light-purple = Светло лилаво
newtab-wallpaper-light-green = Светлозелено
newtab-wallpaper-green = Зелено
newtab-wallpaper-beige = Бежово
newtab-wallpaper-yellow = Жълто
newtab-wallpaper-orange = Оранжево
newtab-wallpaper-pink = Розово
newtab-wallpaper-light-pink = Светло розово
newtab-wallpaper-red = Червено
newtab-wallpaper-dark-blue = Тъмно синьо
newtab-wallpaper-dark-purple = Тъмно лилаво
newtab-wallpaper-dark-green = Тъмно зелено
newtab-wallpaper-brown = Кафяво

## Abstract

newtab-wallpaper-category-title-abstract = Абстрактно
newtab-wallpaper-abstract-green = Зелени фигури
newtab-wallpaper-abstract-blue = Сини фигури
newtab-wallpaper-abstract-purple = Лилави фигури
newtab-wallpaper-abstract-orange = Оранжеви фигури
newtab-wallpaper-gradient-orange = Преливащо се оранжево и розово
newtab-wallpaper-abstract-blue-purple = Сини и лилави фигури

## Celestial

newtab-wallpaper-category-title-photographs = Снимки
newtab-wallpaper-beach-at-sunrise = Плаж при изгрев
newtab-wallpaper-beach-at-sunset = Плаж по залез
newtab-wallpaper-storm-sky = Бурно небе
newtab-wallpaper-sky-with-pink-clouds = Небе с розови облаци
newtab-wallpaper-red-panda-yawns-in-a-tree = Червена панда се прозява на дърво
newtab-wallpaper-white-mountains = Бели планини
newtab-wallpaper-feature-highlight-button = Разбрах
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Вижте прогнозата в { $provider }
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Накратко
newtab-weather-menu-change-weather-display-simple = Превключване към опростен изглед
newtab-weather-menu-weather-display-option-detailed = Подробно
newtab-weather-menu-change-weather-display-detailed = Превключване към подробен изглед
newtab-weather-menu-temperature-units = Единици за температура
newtab-weather-menu-temperature-option-fahrenheit = Фаренхайт
newtab-weather-menu-temperature-option-celsius = Целзий
newtab-weather-menu-change-temperature-units-fahrenheit = Превключване към Фаренхайт
newtab-weather-menu-change-temperature-units-celsius = Превключване към Целзий
newtab-weather-menu-learn-more = Научете повече
# This message is shown if user is working offline
newtab-weather-error-not-available = В момента няма данни за времето.

## Topic Labels

newtab-topic-label-business = Бизнес
newtab-topic-label-career = Кариера
newtab-topic-label-education = Образование
newtab-topic-label-arts = Развлечение
newtab-topic-label-food = Храна
newtab-topic-label-health = Здраве
newtab-topic-label-hobbies = Игри
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Пари
newtab-topic-label-society-parenting = Възпитание
newtab-topic-label-government = Политика
newtab-topic-label-education-science = Наука
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Лайфхакове
newtab-topic-label-sports = Спорт
newtab-topic-label-tech = Технологии
newtab-topic-label-travel = Пътуване
newtab-topic-label-home = Дом и градина

## Topic Selection Modal

newtab-topic-selection-button-maybe-later = Може би по-късно
newtab-topic-selection-button-pick-interests = Изберете вашите интереси

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

