# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = زبانهٔ جدید
newtab-settings-button =
    .title = صفحهٔ زبانهٔ جدید را سفارشی کنید
newtab-personalize-icon-label =
    .title = شحصی‌سازی زبانهٔ جدید
    .aria-label = شحصی‌سازی زبانهٔ جدید
newtab-personalize-dialog-label =
    .aria-label = شخصی‌سازی

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = جستجو
    .aria-label = جستجو
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = با { $engine } جستجو یا آدرسی وارد کنید
newtab-search-box-handoff-text-no-engine = عبارتی برای جستجو یا یک آدرس وارد کنید
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = با { $engine } جستجو یا یک آدرس وارد کنید
    .title = با { $engine } جستجو یا یک آدرس وارد کنید
    .aria-label = با { $engine } جستجو یا یک آدرس وارد کنید
newtab-search-box-handoff-input-no-engine =
    .placeholder = عبارتی برای جست‌وجو یا یک آدرس وارد کنید
    .title = عبارتی برای جست‌وجو یا یک آدرس وارد کنید
    .aria-label = عبارتی برای جست‌وجو یا یک آدرس وارد کنید
newtab-search-box-text = جست‌وجو در وب
newtab-search-box-input =
    .placeholder = جستجو در وب
    .aria-label = جستجو در وب

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = افزودن موتور جستجو
newtab-topsites-add-shortcut-header = میانبر جدید
newtab-topsites-edit-topsites-header = ویرایش سایت برتر
newtab-topsites-edit-shortcut-header = ویرایش میانبر
newtab-topsites-title-label = عنوان
newtab-topsites-title-input =
    .placeholder = عنوان را وارد کنید
newtab-topsites-url-label = نشانی
newtab-topsites-url-input =
    .placeholder = یک نشانی بنویسید یا بچسبانید
newtab-topsites-url-validation = نشانی معتبر الزامی است
newtab-topsites-image-url-label = نشانیِ سفارشی عکس
newtab-topsites-use-image-link = استفاده از یک عکس سفارشی…
newtab-topsites-image-validation = بارگیری عکس شکست خورد. آدرس دیگری امتحان کنید.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = انصراف
newtab-topsites-delete-history-button = حذف از تاریخچه
newtab-topsites-save-button = ذخیره
newtab-topsites-preview-button = پیش‌نمایش
newtab-topsites-add-button = افزودن

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = آیا از پاک کردن همه نمونه‌های این صفحه از تاریخ‌چه خود اطمینان دارید؟
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = این عمل قابل برگشت نیست.

## Top Sites - Sponsored label

newtab-topsite-sponsored = حمایت شده

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = باز کردن منو
    .aria-label = باز کردن منو
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = حذف
    .aria-label = حذف
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = باز کردن منو
    .aria-label = بازکردن فهرست زمینه برای { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = ویرایش این سایت
    .aria-label = ویرایش این سایت

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = ويرايش
newtab-menu-open-new-window = باز کردن در یک پنجره جدید
newtab-menu-open-new-private-window = بار کردن در یک پنجره ناشناس جدید
newtab-menu-dismiss = رد کردن
newtab-menu-pin = سنجاق کردن
newtab-menu-unpin = جدا کردن
newtab-menu-delete-history = حذف از تاریخچه
newtab-menu-save-to-pocket = ذخیره‌سازی در { -pocket-brand-name }
newtab-menu-delete-pocket = حذف از { -pocket-brand-name }
newtab-menu-archive-pocket = آرشیو در { -pocket-brand-name }
newtab-menu-show-privacy-info = حامیان ما و حریم خصوصی شما

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = انجام شد
newtab-privacy-modal-button-manage = مدیریتِ تنظیماتِ محتوای مورد حمایت
newtab-privacy-modal-header = حریم خصوصی شما اهمیت دارد.
newtab-privacy-modal-paragraph-2 =
    ما علاوه بر نمایش داستان‌های جذاب، محتوای کاملا تایید شده و مرتبط،
    از حامیان مالی خود را نمایش خواهیم داد. مطمئن باشید، <strong>داده‌های مرور شما
    هیچ‌وقت نسخهٔ شخصی { -brand-product-name } فایرفاکس شما را ترک نمی‌کنند</strong> — ما آن را نمی‌بینیم، و ما
    حامیان مالی‌مان هم نخواهند دید.
newtab-privacy-modal-link = در مورد حریم خصوصی در برگهٔ جدید بیاموزید

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = حذف نشانک
# Bookmark is a verb here.
newtab-menu-bookmark = نشانک

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = رونوشت از پیوندِ بارگیری
newtab-menu-go-to-download-page = رفتن به صفحهٔ بارگیری
newtab-menu-remove-download = حذف از تاریخچه

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] نمایش در Finder
       *[other] باز کردن پوشهٔ محتوی
    }
newtab-menu-open-file = باز کردن پرونده

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = مشاهده شده
newtab-label-bookmarked = نشانک شده
newtab-label-removed-bookmark = نشانک حذف شد
newtab-label-recommended = موضوعات داغ
newtab-label-saved = در { -pocket-brand-name } ذخیره شد
newtab-label-download = دریافت شد
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · حمایت مالی شده
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = حمایت شده توسط { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } دقیقه

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = حذف قسمت
newtab-section-menu-collapse-section = جمع کردن قسمت
newtab-section-menu-expand-section = باز کردن قسمت
newtab-section-menu-manage-section = مدیریت قسمت
newtab-section-menu-manage-webext = مدیریت افزودنی
newtab-section-menu-add-topsite = اضافه کردن سایت برتر
newtab-section-menu-add-search-engine = افزودن موتور جست‌وجو
newtab-section-menu-move-up = جابه‌جایی به بالا
newtab-section-menu-move-down = جابه‌جایی به پایین
newtab-section-menu-privacy-notice = نکات حریم‌خصوصی

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = جمع‌کردن بخش
newtab-section-expand-section-label =
    .aria-label = باز کردن بخش

## Section Headers.

newtab-section-header-topsites = سایت‌های برتر
newtab-section-header-recent-activity = فعالیت‌های اخیر
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = پیشنهاد شده توسط { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = مرور کردن را شروع کنید و شاهد تعداد زیادی مقاله، فیلم و صفحات خوبی باشید که اخیر مشاهده کرده اید یا نشانگ گذاری کرده اید.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = فعلاً تمام شد. بعداً دوباره سر بزن تا مطالب جدیدی از { $provider } ببینی. نمی‌توانی صبر کنی؟ یک موضوع محبوب را انتخاب کن تا مطالب جالب مرتبط از سراسر دنیا را پیدا کنی.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = تمام شد!
newtab-discovery-empty-section-topstories-content = بعداً سر بزن تا مطالب بیشتری ببینی.
newtab-discovery-empty-section-topstories-try-again-button = تلاش دوباره
newtab-discovery-empty-section-topstories-loading = در حال بارگذاری...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = آخ! ما تقریباً این بخش را بارگذاری کرده بودیم، اما کامل نیست.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = موضوع‌های محبوب:
newtab-pocket-new-topics-title = مطالب بیشتری می‌خواهید؟ موضوعات پرطرفدار را در { -pocket-brand-name } ببینید
newtab-pocket-more-recommendations = توصیه‌های بیشتر
newtab-pocket-learn-more = اطلاعات بیشتر
newtab-pocket-cta-button = دریافت { -pocket-brand-name }
newtab-pocket-cta-text = مطالبی که دوست دارید را در { -pocket-brand-name } ذخیره کنید، و به ذهن خود با مطالب فوق‌العاده انرژی بدهید.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } بخشی از خانوادهٔ { -brand-product-name } است
newtab-pocket-save = ذخیره
newtab-pocket-saved = ذخیره شد

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = بهترین‌های وب را کشف کنید

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = اوه، هنگام بارگیری این محتوا مشکلی پیش آمد.
newtab-error-fallback-refresh-link = برای تلاش مجدد صفحه را نوسازی کنید.

## Customization Menu

newtab-custom-shortcuts-title = میانبرها
newtab-custom-shortcuts-subtitle = وب‌سایت‌هایی که ذخیره یا بازدید می‌کنید
newtab-custom-shortcuts-toggle =
    .label = میانبرها
    .description = وب‌سایت‌هایی که ذخیره یا بازدید می‌کنید
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } ردیف
       *[other] { $num } ردیف
    }
newtab-custom-sponsored-sites = میانبرهای حمایت شده
newtab-custom-pocket-title = توصیه‌های { -pocket-brand-name }
newtab-custom-pocket-subtitle = محتوای استثنایی که توسط { -pocket-brand-name }، بخشی از خانواده { -brand-product-name } جمع‌آوری شده‌اند.
newtab-custom-pocket-sponsored = محتواهایی از حامیان مالی
newtab-custom-pocket-show-recent-saves = نمایش ذخیره‌های اخیر
newtab-custom-recent-title = فعالیت‌های اخیر
newtab-custom-recent-subtitle = منتخبی از سایت‌ها و مطالب اخیر
newtab-custom-recent-toggle =
    .label = فعالیت‌های اخیر
    .description = منتخبی از سایت‌ها و مطالب اخیر
newtab-custom-weather-toggle =
    .label = آب و هوا
    .description = پیش‌بینی آب و هوای امروز به طور خلاصه
newtab-custom-close-button = بستن
newtab-custom-settings = مدیریت تنظیمات بیشتر

## New Tab Wallpapers

newtab-wallpaper-title = کاغذدیواری‌ها
newtab-wallpaper-reset = بازگرداندن به تنظیمات اولیه
newtab-wallpaper-light-red-panda = پاندای قرمز
newtab-wallpaper-light-mountain = کوه سفید
newtab-wallpaper-light-sky = آسمانی با ابرهای بنفش و صورتی
newtab-wallpaper-light-color = اشکال آبی، صورتی و زرد

## Solid Colors

newtab-wallpaper-blue = آبی
newtab-wallpaper-light-blue = آبی روشن
newtab-wallpaper-light-purple = بنفش روشن
newtab-wallpaper-light-green = سبز روشن
newtab-wallpaper-green = سبز
newtab-wallpaper-beige = بژ
newtab-wallpaper-yellow = زرد
newtab-wallpaper-orange = نارنجی
newtab-wallpaper-pink = صورتی
newtab-wallpaper-light-pink = صورتی روشن
newtab-wallpaper-red = قرمز
newtab-wallpaper-dark-blue = آبی تیره
newtab-wallpaper-dark-purple = بنفش تیره
newtab-wallpaper-dark-green = سبز تیره
newtab-wallpaper-brown = قهوه‌ای

## Abstract


## Celestial

newtab-wallpaper-white-mountains = کوه‌های سفید

## Celestial


## New Tab Weather

# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = ساده
newtab-weather-menu-learn-more = بیشتر بدانید

## Topic Labels


## Topic Selection Modal

newtab-topic-selection-save-button = ذخیره
newtab-topic-selection-cancel-button = انصراف
newtab-topic-selection-button-maybe-later = شاید بعداً

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

