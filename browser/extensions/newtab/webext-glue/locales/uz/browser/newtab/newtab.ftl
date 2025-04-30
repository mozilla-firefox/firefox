# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Yangi varaq
newtab-settings-button =
    .title = Yangi varaq sahifasini sozlash
newtab-personalize-icon-label =
    .title = Yangi varaqni moslashtirish
    .aria-label = Yangi varaqni moslashtirish
newtab-personalize-dialog-label =
    .aria-label = Moslashtirish

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Qidiruv
    .aria-label = Qidiruv
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = { $engine } orqali qidiring yoki manzilni kiriting
newtab-search-box-handoff-text-no-engine = Izlang yoki manzilni kiriting
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = { $engine } orqali qidiring yoki manzilni kiriting
    .title = { $engine } orqali qidiring yoki manzilni kiriting
    .aria-label = { $engine } orqali qidiring yoki manzilni kiriting
newtab-search-box-handoff-input-no-engine =
    .placeholder = Qidiring yoki manzilni kiriting
    .title = Qidiring yoki manzilni kiriting
    .aria-label = Qidiring yoki manzilni kiriting
newtab-search-box-text = Internetdan qidirish
newtab-search-box-input =
    .placeholder = Internetdan qidirish
    .aria-label = Internetdan izlash

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Qidiruv tizimini qoʻshish
newtab-topsites-add-shortcut-header = Yangi tugmalar birikmasi
newtab-topsites-edit-topsites-header = Ommabop saytni tahrirlash
newtab-topsites-edit-shortcut-header = Tugmalar birikmasini tahrirlash
newtab-topsites-title-label = Nomi
newtab-topsites-title-input =
    .placeholder = Nomini kiriting
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL manzilini kiriting
newtab-topsites-url-validation = URL manzilini bexato kiriting
newtab-topsites-image-url-label = Rasmning URL manzili
newtab-topsites-use-image-link = Boshqa rasmdan foydalaning…
newtab-topsites-image-validation = Rasm yuklanmadi. Boshqa URL manzildan foydalaning.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Bekor qilish
newtab-topsites-delete-history-button = Tarixdan oʻchirish
newtab-topsites-save-button = Saqlash
newtab-topsites-preview-button = Koʻrib chiqish
newtab-topsites-add-button = Qoʻshish

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Ushbu sahifaning har bir nusxasini tarixingizdan oʻchirmoqchimisiz?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Bu amalni ortga qaytarib boʻlmaydi.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Homiylik qilgan

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Menyuni ochish
    .aria-label = Menyuni ochish
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Olib tashlash
    .aria-label = Olib tashlash
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Menyuni ochish
    .aria-label = { $title } uchun matn menyusini ochish
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Bu saytni tahrirlash
    .aria-label = Bu saytni tahrirlash

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Tahrirlash
newtab-menu-open-new-window = Yangi oynada ochish
newtab-menu-open-new-private-window = Yangi maxfiy oynada ochish
newtab-menu-dismiss = Rad etish
newtab-menu-pin = Yopishtirish
newtab-menu-unpin = Ajratish
newtab-menu-delete-history = Tarixdan oʻchirish
newtab-menu-save-to-pocket = { -pocket-brand-name } xizmatiga saqlash
newtab-menu-delete-pocket = { -pocket-brand-name } xizmatidan oʻchirish
newtab-menu-archive-pocket = { -pocket-brand-name } xizmatiga arxivlash
newtab-menu-show-privacy-info = Bizning homiylarimiz va sizning maxfiyligingiz

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Tayyor
newtab-privacy-modal-button-manage = Reklama kontentni sozlamalarni boshqarish
newtab-privacy-modal-header = Maxfiyligingiz juda muhim.
newtab-privacy-modal-link = Yangi varaqda maxfiylik qanday boʻlishi haqida batafsil maʼlumot oling

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Xatcho‘pni olib tashlash
# Bookmark is a verb here.
newtab-menu-bookmark = Xatcho‘p

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Havoladan nusxa olish
newtab-menu-go-to-download-page = Yuklab olish sahifasiga o‘tish
newtab-menu-remove-download = Tarixdan olib tashlash

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Topkichda ko‘rsatish
       *[other] Saqlangan jildni ochish
    }
newtab-menu-open-file = Faylni ochish

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Kirilgan
newtab-label-bookmarked = Xatcho‘pga qo‘shilgan
newtab-label-removed-bookmark = Xatchoʻp olib tashlandi
newtab-label-recommended = Trendda
newtab-label-saved = { -pocket-brand-name } xizmatiga saqlandi
newtab-label-download = Yuklab olindi
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Reklama huquqi asosida
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Homiydan ({ $sponsor })
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } daq

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Qismni olib tashlash
newtab-section-menu-collapse-section = Qismni yig‘ish
newtab-section-menu-expand-section = Qismni yoyish
newtab-section-menu-manage-section = Qismni boshqarish
newtab-section-menu-manage-webext = Kengaytmani boshqarish
newtab-section-menu-add-topsite = Ommabop saytga qo‘shish
newtab-section-menu-add-search-engine = Qidiruv tizimini qoʻshish
newtab-section-menu-move-up = Tepaga ko‘tarish
newtab-section-menu-move-down = Pastga tushirish
newtab-section-menu-privacy-notice = Maxfiylik eslatmalari

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Qismni yigʻish
newtab-section-expand-section-label =
    .aria-label = Qismni yoyish

## Section Headers.

newtab-section-header-topsites = Ommabop saytlar
newtab-section-header-recent-activity = Soʻnggi faoliyat
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } tomonidan tavsiya qilingan

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Saytlarni koʻrishni boshlashingiz bilan biz sizga ajoyib maqola, video va oxirgi kirilgan yoki xatchoʻplarga qoʻshilgan sahifalarni koʻrsatamiz.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Hammasini koʻrib chiqdingiz. { $provider }dan soʻnggi hikoyalarni o‘qish uchun keyinroq bu sahifaga qayting. Kuta olmaysizmi? Internetdan eng zoʻr hikoyalarni topish uchun ommabop mavzuni tanlang.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Hammasini oʻqib chiqdingiz!
newtab-discovery-empty-section-topstories-content = Yana boshqa maqolalarni oʻqish uchun keyinroq tashrif buyuring.
newtab-discovery-empty-section-topstories-try-again-button = Yana urining
newtab-discovery-empty-section-topstories-loading = Yuklanmoqda...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Obbo! Biz deyarli bu qismni yuklab boʻlgandik, lekin ulgurmabmiz.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Mashhur mavzular:
newtab-pocket-more-recommendations = Yana tavsiyalar
newtab-pocket-learn-more = Batafsil
newtab-pocket-cta-button = { -pocket-brand-name }ni yuklab olish
newtab-pocket-cta-text = Sizga yoqqan maqolalarni { -pocket-brand-name } xizmatiga saqlab qoʻying va maroqli oʻqib, tafakkuringizni rivojlantiring

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Kontent yuklanayotganda qandaydir xatolik yuz berdi.
newtab-error-fallback-refresh-link = Yana urinib ko‘rish uchun sahifani yangilang.

## Customization Menu

newtab-custom-settings = Boshqa sozlamalarni boshqarish

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

