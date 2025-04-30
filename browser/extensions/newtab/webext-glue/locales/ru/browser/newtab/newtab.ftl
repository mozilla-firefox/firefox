# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Новая вкладка
newtab-settings-button =
    .title = Настроить свою страницу новой вкладки
newtab-personalize-settings-icon-label =
    .title = Персонализировать Новую вкладку
    .aria-label = Настройки
newtab-settings-dialog-label =
    .aria-label = Настройки
newtab-personalize-icon-label =
    .title = Настроить новую вкладку
    .aria-label = Настроить новую вкладку
newtab-personalize-dialog-label =
    .aria-label = Настроить
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Искать
    .aria-label = Искать
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Найдите в { $engine } или введите адрес
newtab-search-box-handoff-text-no-engine = Введите запрос или адрес
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Найдите в { $engine } или введите адрес
    .title = Найдите в { $engine } или введите адрес
    .aria-label = Найдите в { $engine } или введите адрес
newtab-search-box-handoff-input-no-engine =
    .placeholder = Введите запрос или адрес
    .title = Введите запрос или адрес
    .aria-label = Введите запрос или адрес
newtab-search-box-text = Искать в Интернете
newtab-search-box-input =
    .placeholder = Поиск в Интернете
    .aria-label = Поиск в Интернете

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Добавить поисковую систему
newtab-topsites-add-shortcut-header = Новый ярлык
newtab-topsites-edit-topsites-header = Изменить сайт из топа
newtab-topsites-edit-shortcut-header = Изменить ярлык
newtab-topsites-add-shortcut-label = Добавить ярлык
newtab-topsites-title-label = Заголовок
newtab-topsites-title-input =
    .placeholder = Введите название
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Введите или вставьте URL
newtab-topsites-url-validation = Введите корректный URL
newtab-topsites-image-url-label = Свой URL изображения
newtab-topsites-use-image-link = Использовать своё изображение…
newtab-topsites-image-validation = Изображение не загрузилось. Попробуйте использовать другой URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Отмена
newtab-topsites-delete-history-button = Удалить из истории
newtab-topsites-save-button = Сохранить
newtab-topsites-preview-button = Предпросмотр
newtab-topsites-add-button = Добавить

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Вы действительно хотите удалить все записи об этой странице из вашей истории?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Это действие нельзя отменить.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Спонсировано

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Открыть меню
    .aria-label = Открыть меню
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Убрать
    .aria-label = Убрать
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Открыть меню
    .aria-label = Открыть контекстное меню для { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Изменить этот сайт
    .aria-label = Изменить этот сайт

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Изменить
newtab-menu-open-new-window = Открыть в новом окне
newtab-menu-open-new-private-window = Открыть в новом приватном окне
newtab-menu-dismiss = Скрыть
newtab-menu-pin = Прикрепить
newtab-menu-unpin = Открепить
newtab-menu-delete-history = Удалить из истории
newtab-menu-save-to-pocket = Сохранить в { -pocket-brand-name }
newtab-menu-delete-pocket = Удалить из { -pocket-brand-name }
newtab-menu-archive-pocket = Архивировать в { -pocket-brand-name }
newtab-menu-show-privacy-info = Наши спонсоры и ваша приватность
newtab-menu-about-fakespot = О { -fakespot-brand-name }
newtab-menu-report-content = Пожаловаться на этот контент
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Блокировать
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Отписаться от темы

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Управление рекламным содержимым
newtab-menu-our-sponsors-and-your-privacy = Наши спонсоры и ваша приватность
newtab-menu-report-this-ad = Пожаловаться на эту рекламу

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Готово
newtab-privacy-modal-button-manage = Управление настройками контента спонсоров
newtab-privacy-modal-header = Ваша приватность имеет значение.
newtab-privacy-modal-paragraph-2 =
    Помимо сохранения увлекательных статей, мы также показываем вам
    проверенный контент от избранных спонсоров. Будьте уверены, <strong>ваши данные
    веб-сёрфинга никогда не покинут вашу личную копию { -brand-product-name }</strong> — мы не имеем
    к ним доступа, и наши спонсоры тоже не имеют.
newtab-privacy-modal-link = Посмотрите, как работает приватность, в новой вкладке

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Удалить закладку
# Bookmark is a verb here.
newtab-menu-bookmark = Добавить в закладки

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Копировать ссылку на загрузку
newtab-menu-go-to-download-page = Перейти на страницу загрузки
newtab-menu-remove-download = Удалить из истории

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Показать в Finder
       *[other] Открыть папку с файлом
    }
newtab-menu-open-file = Открыть файл

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Посещено
newtab-label-bookmarked = В закладках
newtab-label-removed-bookmark = Закладка удалена
newtab-label-recommended = Популярные
newtab-label-saved = Сохранено в { -pocket-brand-name }
newtab-label-download = Загружено
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · На правах рекламы
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = От спонсора { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } мин.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Удалить раздел
newtab-section-menu-collapse-section = Свернуть раздел
newtab-section-menu-expand-section = Развернуть раздел
newtab-section-menu-manage-section = Управление разделом
newtab-section-menu-manage-webext = Управление расширением
newtab-section-menu-add-topsite = Добавить в топ сайтов
newtab-section-menu-add-search-engine = Добавить поисковую систему
newtab-section-menu-move-up = Вверх
newtab-section-menu-move-down = Вниз
newtab-section-menu-privacy-notice = Уведомление о конфиденциальности

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Свернуть раздел
newtab-section-expand-section-label =
    .aria-label = Развернуть раздел

## Section Headers.

newtab-section-header-topsites = Топ сайтов
newtab-section-header-recent-activity = Последние действия
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Рекомендовано { $provider }
newtab-section-header-stories = Истории, наводящие на размышления
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Сегодняшняя подборка для вас

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Начните веб-сёрфинг, и мы покажем вам здесь некоторые из интересных статей, видеороликов и других страниц, которые вы недавно посетили или добавили в закладки.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Вы всё прочитали. Зайдите попозже, чтобы увидеть больше лучших статей от { $provider }. Не можете ждать? Выберите популярную тему, чтобы найти больше интересных статей со всего Интернета.
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = Вы всё прочитали. Зайдите попозже, чтобы увидеть больше статей. Не можете подождать? Выберите популярную тему, чтобы найти больше интересных статей со всего Интернета.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Вы всё прочитали!
newtab-discovery-empty-section-topstories-content = Зайдите попозже, чтобы увидеть больше статей.
newtab-discovery-empty-section-topstories-try-again-button = Попробовать снова
newtab-discovery-empty-section-topstories-loading = Загрузка…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ой! Мы почти загрузили этот раздел, но не совсем.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Популярные темы:
newtab-pocket-new-topics-title = Хотите увидеть ещё больше историй? Вот самые популярные темы от { -pocket-brand-name }
newtab-pocket-more-recommendations = Ещё рекомендации
newtab-pocket-learn-more = Подробнее
newtab-pocket-cta-button = Загрузить { -pocket-brand-name }
newtab-pocket-cta-text = Сохраняйте интересные статьи в { -pocket-brand-name } и подпитывайте свой ум увлекательным чтением.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } является частью семейства { -brand-product-name }
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = Сохранить
newtab-pocket-saved = Сохранено

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = Больше похожих
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Не для меня
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Спасибо. Ваш отзыв поможет нам улучшить вашу ленту.
newtab-toast-dismiss-button =
    .title = Убрать
    .aria-label = Убрать

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Находите лучшее в сети
newtab-pocket-onboarding-cta = { -pocket-brand-name } исследует широкий спектр публикаций, чтобы предоставить вам самый информативный, вдохновляющий и заслуживающий доверия контент прямо в вашем браузере { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = К сожалению что-то пошло не так при загрузке этого содержимого.
newtab-error-fallback-refresh-link = Обновить страницу, чтобы попробовать ещё раз.

## Customization Menu

newtab-custom-shortcuts-title = Ярлыки
newtab-custom-shortcuts-subtitle = Сохранённые или посещаемые сайты
newtab-custom-shortcuts-toggle =
    .label = Ярлыки
    .description = Сохранённые или посещаемые сайты
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } строка
        [few] { $num } строки
       *[many] { $num } строк
    }
newtab-custom-sponsored-sites = Спонсируемые ярлыки
newtab-custom-pocket-title = Рекомендуемые { -pocket-brand-name }
newtab-custom-pocket-subtitle = Особый контент, курируемый { -pocket-brand-name }, частью семейства { -brand-product-name }
newtab-custom-stories-toggle =
    .label = Рекомендуемые истории
    .description = Исключительный контент, курируемый семейством { -brand-product-name }
newtab-custom-pocket-sponsored = Статьи спонсоров
newtab-custom-pocket-show-recent-saves = Отображать последние сохранения
newtab-custom-recent-title = Последние действия
newtab-custom-recent-subtitle = Подборка недавних сайтов и контента
newtab-custom-recent-toggle =
    .label = Последние действия
    .description = Подборка недавних сайтов и контента
newtab-custom-weather-toggle =
    .label = Погода
    .description = Краткий прогноз на сегодня
newtab-custom-close-button = Закрыть
newtab-custom-settings = Управление дополнительными настройками

## New Tab Wallpapers

newtab-wallpaper-title = Обои
newtab-wallpaper-reset = Восстановить по умолчанию
newtab-wallpaper-upload-image = Загрузить изображение
newtab-wallpaper-custom-color = Выберите цвет
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = Размер файла изображения превысил лимит в { $file_size }МБ. Пожалуйста, попробуйте загрузить файл меньшего размера.
newtab-wallpaper-error-file-type = Мы не смогли загрузить ваш файл. Пожалуйста, попробуйте ещё раз с другим типом файла.
newtab-wallpaper-light-red-panda = Красная панда
newtab-wallpaper-light-mountain = Белая гора
newtab-wallpaper-light-sky = Небо с фиолетовыми и розовыми облаками
newtab-wallpaper-light-color = Синие, розовые и жёлтые формы
newtab-wallpaper-light-landscape = Горный пейзаж из синего дыма
newtab-wallpaper-light-beach = Пляж с пальмами
newtab-wallpaper-dark-aurora = Северное сияние
newtab-wallpaper-dark-color = Красные и синие формы
newtab-wallpaper-dark-panda = Красная панда, прячущаяся в лесу
newtab-wallpaper-dark-sky = Городской пейзаж с ночным небом
newtab-wallpaper-dark-mountain = Горный пейзаж
newtab-wallpaper-dark-city = Фиолетовый городской пейзаж
newtab-wallpaper-dark-fox-anniversary = Лиса на дороге рядом с лесом
newtab-wallpaper-light-fox-anniversary = Лиса на травяном поле с туманным горным ландшафтом

## Solid Colors

newtab-wallpaper-category-title-colors = Сплошные цвета
newtab-wallpaper-blue = Синий
newtab-wallpaper-light-blue = Голубой
newtab-wallpaper-light-purple = Светло-фиолетовый
newtab-wallpaper-light-green = Светло-зелёный
newtab-wallpaper-green = Зелёный
newtab-wallpaper-beige = Бежевый
newtab-wallpaper-yellow = Жёлтый
newtab-wallpaper-orange = Оранжевый
newtab-wallpaper-pink = Розовый
newtab-wallpaper-light-pink = Светло-розовый
newtab-wallpaper-red = Красный
newtab-wallpaper-dark-blue = Тёмно-синий
newtab-wallpaper-dark-purple = Тёмно-фиолетовый
newtab-wallpaper-dark-green = Тёмно-зелёный
newtab-wallpaper-brown = Коричневый

## Abstract

newtab-wallpaper-category-title-abstract = Абстракция
newtab-wallpaper-abstract-green = Зелёные формы
newtab-wallpaper-abstract-blue = Синие формы
newtab-wallpaper-abstract-purple = Фиолетовые формы
newtab-wallpaper-abstract-orange = Оранжевые формы
newtab-wallpaper-gradient-orange = Градиент оранжевого и розового
newtab-wallpaper-abstract-blue-purple = Синие и фиолетовые формы
newtab-wallpaper-abstract-white-curves = Белый с заштрихованными кривыми
newtab-wallpaper-abstract-purple-green = Фиолетово-зеленый световой градиент
newtab-wallpaper-abstract-blue-purple-waves = Синие и фиолетовые волнистые формы
newtab-wallpaper-abstract-black-waves = Чёрные волнообразные формы

## Celestial

newtab-wallpaper-category-title-photographs = Фотографии
newtab-wallpaper-beach-at-sunrise = Пляж на восходе
newtab-wallpaper-beach-at-sunset = Пляж на закате
newtab-wallpaper-storm-sky = Грозовое небо
newtab-wallpaper-sky-with-pink-clouds = Небо с розовыми облаками
newtab-wallpaper-red-panda-yawns-in-a-tree = Красная панда зевает на дереве
newtab-wallpaper-white-mountains = Белые горы
newtab-wallpaper-hot-air-balloons = Различные цвета воздушных шаров в дневное время
newtab-wallpaper-starry-canyon = Синяя звёздная ночь
newtab-wallpaper-suspension-bridge = Фотография серого подвесного моста в дневное время
newtab-wallpaper-sand-dunes = Белые песчаные дюны
newtab-wallpaper-palm-trees = Силуэт кокосовых пальм в золотой час
newtab-wallpaper-blue-flowers = Крупный план распускающихся цветов с голубыми цветами
newtab-wallpaper-forest-trees = Деревья, окутанные туманом
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Фото <a data-l10n-name="name-link">{ $author_string }</a> на <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Попробуйте всплеск цвета
newtab-wallpaper-feature-highlight-content = Обновите вид Новой вкладки с помощью обоев.
newtab-wallpaper-feature-highlight-button = Понятно
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Убрать
    .aria-label = Закрыть окно
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Небесный
newtab-wallpaper-celestial-lunar-eclipse = Лунное затмение
newtab-wallpaper-celestial-earth-night = Ночное фото с низкой околоземной орбиты
newtab-wallpaper-celestial-starry-sky = Звёздное небо
newtab-wallpaper-celestial-eclipse-time-lapse = Хронометраж лунного затмения
newtab-wallpaper-celestial-black-hole = Иллюстрация галактики с черной дырой
newtab-wallpaper-celestial-river = Космический снимок реки

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = Посмотреть прогноз в { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ На правах рекламы
newtab-weather-menu-change-location = Изменить местоположение
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Поиск местоположения
    .aria-label = Поиск местоположения
newtab-weather-change-location-search-input = Поиск местоположения
newtab-weather-menu-weather-display = Отображение погоды
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Простой
newtab-weather-menu-change-weather-display-simple = Переключиться в простой вид
newtab-weather-menu-weather-display-option-detailed = Подробный
newtab-weather-menu-change-weather-display-detailed = Переключиться в подробный вид
newtab-weather-menu-temperature-units = Единицы измерения температуры
newtab-weather-menu-temperature-option-fahrenheit = Фаренгейт
newtab-weather-menu-temperature-option-celsius = Цельсий
newtab-weather-menu-change-temperature-units-fahrenheit = Переключиться на градусы Фаренгейта
newtab-weather-menu-change-temperature-units-celsius = Переключиться на градусы Цельсия
newtab-weather-menu-hide-weather = Скрыть погоду на новой вкладке
newtab-weather-menu-learn-more = Подробнее
# This message is shown if user is working offline
newtab-weather-error-not-available = Данные о погоде сейчас недоступны.

## Topic Labels

newtab-topic-label-business = Бизнес
newtab-topic-label-career = Карьера
newtab-topic-label-education = Образование
newtab-topic-label-arts = Развлечения
newtab-topic-label-food = Еда
newtab-topic-label-health = Здоровье
newtab-topic-label-hobbies = Игры
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Деньги
newtab-topic-label-society-parenting = Воспитание
newtab-topic-label-government = Политика
newtab-topic-label-education-science = Наука
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Лайфхаки
newtab-topic-label-sports = Спорт
newtab-topic-label-tech = Техника
newtab-topic-label-travel = Путешествия
newtab-topic-label-home = Дом и сад

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Выберите темы для точной настройки вашей ленты
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Выберите две или более тем. Наши опытные кураторы расставляют приоритеты для статей с учётом ваших интересов. Обновляйте в любое время.
newtab-topic-selection-save-button = Сохранить
newtab-topic-selection-cancel-button = Отменить
newtab-topic-selection-button-maybe-later = Возможно, позже
newtab-topic-selection-privacy-link = Узнайте, как мы защищаем данные и управляем ими
newtab-topic-selection-button-update-interests = Обновите свои интересы
newtab-topic-selection-button-pick-interests = Выберите ваши интересы

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Подписаться
newtab-section-following-button = Подписан
newtab-section-unfollow-button = Отписаться

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Блокировать
newtab-section-blocked-button = Заблокировано
newtab-section-unblock-button = Разблокировать

## Confirmation modal for blocking a section

newtab-section-cancel-button = Не сейчас
newtab-section-confirm-block-topic-p1 = Вы уверены, что хотите заблокировать эту тему?
newtab-section-confirm-block-topic-p2 = Заблокированные темы больше не будут появляться в вашей ленте.
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Заблокировать { $topic }

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Темы
newtab-section-manage-topics-button-v2 =
    .label = Управление темами
newtab-section-mangage-topics-followed-topics = Подписки
newtab-section-mangage-topics-followed-topics-empty-state = Вы пока не отслеживаете ни одну тему.
newtab-section-mangage-topics-blocked-topics = Заблокированы
newtab-section-mangage-topics-blocked-topics-empty-state = Вы пока не заблокировали ни одной темы.
newtab-custom-wallpaper-title = Пользовательские обои здесь
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Загрузите свои обои или выберите цвет оформления, чтобы настроить { -brand-product-name } под себя.
newtab-custom-wallpaper-cta = Попробовать
