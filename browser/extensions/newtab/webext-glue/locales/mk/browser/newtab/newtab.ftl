# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Ново јазиче
newtab-settings-button =
    .title = Прилагодете ја страницата на вашето Ново јазиче

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Барај
    .aria-label = Барај

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Додај сервис за пребарување
newtab-topsites-edit-topsites-header = Уреди врвно мрежно место
newtab-topsites-title-label = Наслов
newtab-topsites-title-input =
    .placeholder = Внесете наслов
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Внесете или вметнете URL
newtab-topsites-url-validation = Потребен е валиден URL
newtab-topsites-use-image-link = Користи сопствена слика…

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Откажи
newtab-topsites-delete-history-button = Избриши од историја
newtab-topsites-save-button = Сними
newtab-topsites-preview-button = Преглед
newtab-topsites-add-button = Додај

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Дали сте сигурни дека сакате да ја избришете оваа страница отсекаде во вашата историја на прелистување?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ова дејство не може да се одврати.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Отвори мени
    .aria-label = Отвори мени
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Отвори мени
    .aria-label = Отвори мени за констект за { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Уреди го ова место
    .aria-label = Уреди го ова место

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Уреди
newtab-menu-open-new-window = Отвори во нов прозорец
newtab-menu-open-new-private-window = Отвори во нов приватен прозорец
newtab-menu-dismiss = Откажи
newtab-menu-pin = Прикачи
newtab-menu-unpin = Откачи
newtab-menu-delete-history = Избриши од историја
newtab-menu-save-to-pocket = Зачувај во { -pocket-brand-name }
newtab-menu-delete-pocket = Избриши од { -pocket-brand-name }
newtab-menu-archive-pocket = Архивирај во { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Готово
newtab-privacy-modal-header = Вашата приватност е важна.
newtab-privacy-modal-link = Дознајте како работи приватноста на новиот таб

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Отстрани обележувач
# Bookmark is a verb here.
newtab-menu-bookmark = Обележувач

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Копирај врска за преземање
newtab-menu-go-to-download-page = Оди до страницата за преземање
newtab-menu-remove-download = Избриши од историјата

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Покажи во Finder
       *[other] Отвори ја папката со преземања
    }
newtab-menu-open-file = Отвори датотека

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Посетени
newtab-label-bookmarked = Обележани
newtab-label-removed-bookmark = Обележувачот е остранет
newtab-label-recommended = Во тренд
newtab-label-saved = Снимено во { -pocket-brand-name }
newtab-label-download = Преземено

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Отстранете го делот
newtab-section-menu-move-up = Помести нагоре
newtab-section-menu-move-down = Помести надолу
newtab-section-menu-privacy-notice = Белешка за приватност

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = Популарни мрежни места
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Препорачано од { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Започнете со прелистување и ние овде ќе ви прикажеме некои од одличните написи, видеа и други страници што неодамна сте ги поселите или обележале.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Имате видено сѐ! Навратете се подоцна за нови содржини од { $provider }. Не можете да чекате? Изберете популарна тема и откријте уште одлични содржини ширум Интернет.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-content = Проверете подоцна за повеќе приказни.
newtab-discovery-empty-section-topstories-try-again-button = Обиди се повторно
newtab-discovery-empty-section-topstories-loading = Се вчитува…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Упс! Скоро го вчитавме овој дел, но не баш.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Популарни теми:
newtab-pocket-more-recommendations = Повеќе препораки
newtab-pocket-cta-button = Превземете го { -pocket-brand-name }

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Упс, нешто отиде погрешно со прикажување на оваа содржина
newtab-error-fallback-refresh-link = Освежете ја страницата за да се обидете повторно.

## Customization Menu


## New Tab Wallpapers


## Solid Colors


## Abstract


## Celestial


## Celestial


## New Tab Weather


## Topic Labels


## Topic Selection Modal


## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

