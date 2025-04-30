# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = نواں ٹیب
newtab-settings-button =
    .title = اپݨے نویں ٹیب دے صفحہ دی تخصیص کرو
newtab-settings-dialog-label =
    .aria-label = ترتیباں
newtab-personalize-icon-label =
    .title = نویں ٹیب کوں ذاتی بݨاؤ
    .aria-label = نویں ٹیب کوں ذاتی بݨاؤ
newtab-personalize-dialog-label =
    .aria-label = ‏‏تخصیص کرو
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = ڳولو
    .aria-label = ڳولو
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = ‏{ $engine } نال ڳولو یا پتہ درج کرو
newtab-search-box-handoff-text-no-engine = ڳولو یا پتہ درج کرو
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = { $engine } نال ڳولو یا پتہ درج کرو
    .title = { $engine } نال ڳولو یا پتہ درج کرو
    .aria-label = { $engine } نال ڳولو یا پتہ درج کرو
newtab-search-box-handoff-input-no-engine =
    .placeholder = ڳولو یا پتہ درج کرو
    .title = ڳولو یا پتہ درج کرو
    .aria-label = ڳولو یا پتہ درج کرو
newtab-search-box-text = ویب ڳولو
newtab-search-box-input =
    .placeholder = ویب تے ڳولو
    .aria-label = ویب تے ڳولو

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = ڳولݨ انجݨ دا اضافہ کرو
newtab-topsites-add-shortcut-header = نواں شارٹ کٹ
newtab-topsites-edit-topsites-header = بہترین سائٹ دی تدوین کرو
newtab-topsites-edit-shortcut-header = شارٹ کٹ وِچ ترمیم کرو
newtab-topsites-add-shortcut-label = شارٹ کٹ شامل کرو
newtab-topsites-title-label = عنوان
newtab-topsites-title-input =
    .placeholder = ہک عنوان درج کرو
newtab-topsites-url-label = یوآرایل
newtab-topsites-url-input =
    .placeholder = ٹائپ کرو یا ہک URL چسباں کرو
newtab-topsites-url-validation = جائز URL درکار ہے
newtab-topsites-image-url-label = مخصوص تصویر دا URL
newtab-topsites-use-image-link = ہک مخصوص تصویر استعمال کرو …
newtab-topsites-image-validation = تصویر لوڈ تھیوݨ وِچ ناکام رہی۔ براہ مہربانی ہک مختلف URL کوں آزماؤ۔

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = منسوخ کرو
newtab-topsites-delete-history-button = ہسٹری کنوں مٹاؤ
newtab-topsites-save-button = ہتھیکڑا کرو
newtab-topsites-preview-button = پیش منظر
newtab-topsites-add-button = شامل کرو

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = کیا تہاکوں یقین ہے جو تساں ایں صفحہ دا ہر نمونہ اپݨی ہسٹری کنوں میسݨ چاہندے او؟
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = اے عمل کلعدم نہیں تھی سڳدا۔

## Top Sites - Sponsored label

newtab-topsite-sponsored = سپانسر تھئے

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = مینیو کھولو
    .aria-label = مینیو کھولو
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = ہٹاؤ
    .aria-label = ہٹاؤ
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = مینیو کھولو
    .aria-label = { $title } کیتے کنٹیسکٹ مینیو کھولو
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = ایں سائٹ دی تدوین کرو
    .aria-label = ایں سائٹ دی تدوین کرو

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = تبدیلی کرو
newtab-menu-open-new-window = نویں ونڈو وچ کھولو
newtab-menu-open-new-private-window = نویں نجی ونڈو وِچ کھولو
newtab-menu-dismiss = فارغ کرو
newtab-menu-pin = پن
newtab-menu-unpin = ان پن
newtab-menu-delete-history = ہسٹری کنوں مٹاؤ
newtab-menu-save-to-pocket = { -pocket-brand-name } تے ہتھیکڑا کرو
newtab-menu-delete-pocket = { -pocket-brand-name } کنوں مٹاؤ
newtab-menu-archive-pocket = { -pocket-brand-name } وِچ سوگھا کرو
newtab-menu-show-privacy-info = ساݙے سپانسر تے تہاݙی رازداری
newtab-menu-about-fakespot = { -fakespot-brand-name } بارے

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = تھی ڳیا
newtab-privacy-modal-button-manage = سپانسر شدہ مواد دیاں ترتیباں دا بندوبست کرو
newtab-privacy-modal-header = تہاݙی رازداری دی اہمیت ہے۔
newtab-privacy-modal-paragraph-2 =
    دلفریب کہانیاں پیش کرݨ دے علاوہ، اساں تہاکوں متعلقہ وی ݙکھیندے ہیں،
    منتخب سپائسرز دی طرفوں انتہائی جانچ شدہ مواد۔ یقین رکھو، <strong> تہاݙی براؤزنگ
    ݙیٹا کݙاہیں وی { -brand-product-name }</strong> دی تہاݙی ذاتی کاپی نہیں چھوڑیندا — اساں اینکوں نہیں ݙیکھدے، تے اساݙے
    سپانسرز وی کائنی۔
newtab-privacy-modal-link = سکھو جو نویں ٹیب تے رازداری کیویں کم کریندی ہے

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = نشانی ہٹاؤ
# Bookmark is a verb here.
newtab-menu-bookmark = بک مارک

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ڈاؤن لوڈ ربط نقل کرو
newtab-menu-go-to-download-page = ڈاؤن لوڈ صفحہ تے ونڄو
newtab-menu-remove-download = تاریخ کنوں ہٹاؤ

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] فولڈر وِچ ݙکھاؤ
       *[other] حامل فولڈر کھولو
    }
newtab-menu-open-file = فائل کھولو

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = دورہ شدہ
newtab-label-bookmarked = نشان شدہ
newtab-label-removed-bookmark = نشانی ہٹا ݙتی ڳئی اے
newtab-label-recommended = رجحان سازی
newtab-label-saved = { -pocket-brand-name } وِچ محفوظ شدہ
newtab-label-download = ڈاؤن لوڈ شدہ
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } - تعاون شدہ
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = { $sponsor } توں تعاون شدہ
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = سیکشن ہٹاؤ
newtab-section-menu-collapse-section = سیکشن کوں سنگوڑو
newtab-section-menu-expand-section = سیکشن کوں پھیلاؤ
newtab-section-menu-manage-section = سیکشن دا بندوبست کرو
newtab-section-menu-manage-webext = ایکسٹینشن دا بندوبست کرو
newtab-section-menu-add-topsite = بہترین سائٹ شامل کرو
newtab-section-menu-add-search-engine = ڳولݨ انجݨ شامل کرو
newtab-section-menu-move-up = اُتے کرو
newtab-section-menu-move-down = تلے کرو
newtab-section-menu-privacy-notice = رازداری نوٹس

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = سیکش کوں سنگوڑو
newtab-section-expand-section-label =
    .aria-label = سیکش کوں پھیلاؤ

## Section Headers.

newtab-section-header-topsites = بہترین سائٹس
newtab-section-header-recent-activity = حالیہ سرگرمی
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } دی طرفوں تجویز کردہ
newtab-section-header-stories = فکر انگیز کہاݨیاں
# "picks" refers to recommended articles
newtab-section-header-todays-picks = تہاݙے کیتے اڄ دیاں چوݨاں

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = برائوزنگ شروع کرو، تے اساں تہاکوں کجھ بہترین عبارتاں، وڈیوز تے حالیہ دورہ شددہ ٻئے صفحات یا بک مارک ݙکھیسوں۔
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = تساں وٹھی گھدا ہے۔ { $provider } کنوں ودھیک اہم خبراں کیتے بعد اِچ دوبارہ چیک کرو۔ انتظا نہیں سڳدے؟ ویب دے چودھاروں ودھیک عمدہ کہانیاں لبھݨ کیتے ہک مقبول موضوع منتخب کرو۔
# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = تساں وٹھی ڳئے ہو۔ ٻیاں کہاݨیاں کیتے بعد اِچ دوبارہ چیک کرو۔ انتظار نہیں سڳدے؟ ویب دے چودھاروں ودھیک عمدہ کہانیاں لبھݨ کیتے ہک مقبول موضوع منتخب کرو۔

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = تساں وٹھیج پئے او!
newtab-discovery-empty-section-topstories-content = ودھیک کہانیاں کیتے بعد اِچ دوبارہ پڑتال کریجو ۔
newtab-discovery-empty-section-topstories-try-again-button = ولدا کوشش کرو
newtab-discovery-empty-section-topstories-loading = لوڈ تھیندا پئے۔۔۔
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = اوہو! اساں ایں حصے کوں لڳ بھڳ لوڈ کر ݙتا ہے، پر سالم کینا۔

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = مشہور مضامین:
newtab-pocket-new-topics-title = ودھیک کہانیاں چاہندے او؟ { -pocket-brand-name } کنوں ایہ مقبول موضوعات ݙیکھو
newtab-pocket-more-recommendations = ودھیک سفارشاں
newtab-pocket-learn-more = ٻیا سِکھو
newtab-pocket-cta-button = { -pocket-brand-name } گھنو
newtab-pocket-cta-text = اپݨیاں من بھاندیاں کہانیاں { -pocket-brand-name } اِچ ہتھیکڑیاں کرو، تے شاندار پڑھݨ نال اپݨے چیتے کوں تکڑا کرو۔
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } حصہ ہے { -brand-product-name } ٹٻر دا
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = محفوظ
newtab-pocket-saved = محفوظ تھیا

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = ایہو جیہے ٻئے
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = میݙے کیتے کائنی
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = شکریہ،  تہاݙی فیڈبیک تہاݙی فیڈ کوں چنگا بݨاوݨ کیتے ساݙی مدد کریسی۔
newtab-toast-dismiss-button =
    .title = فارغ کرو
    .aria-label = فارغ کرو

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = ویب وچوں بہترین دریافت کرو
newtab-pocket-onboarding-cta = { -pocket-brand-name } تہاݙے { -brand-product-name } براؤزر تے سب کنوں ودھ معلوماتی، متاثر کن، تے قابل اعتماد مواد گھن آوݨ کیتے اشاعتاں دی متنوع رینج کوں پھلوریندا ہے۔

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = اوہو، ایں مواد کوں لوڈ کرݨ وِچ کجھ خراب تھی ڳئے۔
newtab-error-fallback-refresh-link = ولدا کوشش کرݨ کیتے ورقے کوں ریفریش کرو۔

## Customization Menu

newtab-custom-shortcuts-title = شارٹ کٹ
newtab-custom-shortcuts-subtitle = سائٹاں جہڑیاں تساں محفوظ کریندے یا ݙیہدے ہو
newtab-custom-shortcuts-toggle =
    .label = شارٹ کٹ
    .description = سائٹاں جہڑیاں تساں محفوظ کریندے یا ݙیہدے ہو
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } قطار
       *[other] { $num } قطاراں
    }
newtab-custom-sponsored-sites = سپانسر تھئے شارٹ کٹ
newtab-custom-pocket-title = { -pocket-brand-name } دی طرفوں سفارش کیتے ڳئے
newtab-custom-pocket-subtitle = { -pocket-brand-name } دے ذریعے تیار تھئے غیر معمولی مواد، { -brand-product-name } ٹٻر دا حصہ
newtab-custom-stories-toggle =
    .label = تجویز کردہ کہاݨیاں
    .description = { -brand-product-name }ٹَٻَّر دے ذریعے تیار کردہ غیر معمولی مواد
newtab-custom-pocket-sponsored = سپانسر تھیاں کہاݨیاں
newtab-custom-pocket-show-recent-saves = حالیہ ہتھیکڑیاں ظاہر کرو
newtab-custom-recent-title = حالیہ سرگرمی
newtab-custom-recent-subtitle = حالیہ سائٹاں تے مواد دی ہک چوݨ
newtab-custom-recent-toggle =
    .label = حالیہ سرگرمی
    .description = حالیہ سائٹاں تے مواد دی ہک چوݨ
newtab-custom-weather-toggle =
    .label = موسم
    .description = اڄ دی پیش گوئی تے ہک نظر
newtab-custom-close-button = بند کرو
newtab-custom-settings = ودھیک ترتیباں دا بندوبست کرو

## New Tab Wallpapers

newtab-wallpaper-title = وال پیپرز
newtab-wallpaper-reset = ڈیفالٹ تے مقرر کرو
newtab-wallpaper-light-red-panda = رتا پانڈا
newtab-wallpaper-light-mountain = چٹی پہاڑی
newtab-wallpaper-light-sky = ڄُمُّوں اَتے غُلابی بَدلاں دے نال اَسمان
newtab-wallpaper-light-color = نیلے، غُلابی اَتے پیلے رنگ دیاں شکلاں
newtab-wallpaper-light-landscape = نیلے دُھندلے پہاڑی منظر
newtab-wallpaper-light-beach = کھڄّی دے وݨ نال مَݨ
newtab-wallpaper-dark-aurora = ارورہ بوریلس
newtab-wallpaper-dark-color = رَتّے اَتے نیلے شکلاں
newtab-wallpaper-dark-panda = جَھر وِچ لُکّیا ہوئیا رَتّا پانڈا
newtab-wallpaper-dark-sky = رات دے اَسمان دے نال شہر دا منظر
newtab-wallpaper-dark-mountain = پہاڑ دا منظر
newtab-wallpaper-dark-city = ڄَمُّوں رنگ دے شہر د امنظر

## Solid Colors

newtab-wallpaper-category-title-colors = ٹھوس رنگ
newtab-wallpaper-blue = نیلا
newtab-wallpaper-light-blue = پھکا نیلا
newtab-wallpaper-light-purple = پھکا بادامى
newtab-wallpaper-light-green = پھکا ساوا
newtab-wallpaper-green = ساوا
newtab-wallpaper-beige = مٹیالا
newtab-wallpaper-yellow = پیلا
newtab-wallpaper-orange = نارنجی
newtab-wallpaper-pink = گلابی
newtab-wallpaper-light-pink = پھکا گلابی
newtab-wallpaper-red = لال
newtab-wallpaper-dark-blue = شوخ نیلا
newtab-wallpaper-dark-purple = شوخ جامنی
newtab-wallpaper-dark-green = شوخ ساوا
newtab-wallpaper-brown = بھورا

## Abstract

newtab-wallpaper-category-title-abstract = خلاصہ
newtab-wallpaper-abstract-green = ساویاں شکلاں
newtab-wallpaper-abstract-blue = نیلیاں شکلاں
newtab-wallpaper-abstract-purple = جامنی شکلاں
newtab-wallpaper-abstract-orange = مالٹا شکلاں
newtab-wallpaper-gradient-orange = میلان نارنجی تے غلابی
newtab-wallpaper-abstract-blue-purple = نیلے تے جامنی رنگ دیاں شکلاں

## Celestial

newtab-wallpaper-category-title-photographs = فوٹو
newtab-wallpaper-beach-at-sunrise = ݙین٘ہ ابھرݨ ویلے ساحل
newtab-wallpaper-beach-at-sunset = ݙین٘ہ لہݨ ویلے ساحل
newtab-wallpaper-storm-sky = طوفانی آسمان
newtab-wallpaper-sky-with-pink-clouds = اسمان غلابی بدلاں نال
newtab-wallpaper-red-panda-yawns-in-a-tree = لال پانڈا ہک درخت تے اُٻاسی گھندے
newtab-wallpaper-white-mountains = چٹی پہاڑیاں
# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = <a data-l10n-name="webpage-link">{ $webpage_string }</a> تے <a data-l10n-name="name-link">{ $author_string }</a> ولوں فوٹو
newtab-wallpaper-feature-highlight-header = رنگ دا تُرکا ازماؤ
newtab-wallpaper-feature-highlight-content = وال پیپراں نال آپݨی نویں ٹیب کوں تازہ شکل ݙیوو۔
newtab-wallpaper-feature-highlight-button = سمجھ گھدے
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = فارغ کرو
    .aria-label = پوپ اپ بند کرو
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = { $provider } وچ پیش گوئی ݙیکھو
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙سپانسر تھیا
newtab-weather-menu-change-location = مقام وٹاؤ
newtab-weather-change-location-search-input-placeholder =
    .placeholder = مقام ڳولو
    .aria-label = مقام ڳولو
newtab-weather-change-location-search-input = مقام ڳولو
newtab-weather-menu-weather-display = موسم دا ڈسپلے
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = سادہ
newtab-weather-menu-change-weather-display-simple = سادہ منظر تے سوئچ کرو
newtab-weather-menu-weather-display-option-detailed = تفصیلی
newtab-weather-menu-change-weather-display-detailed = تفصیلی منظر تے سوئچ کرو
newtab-weather-menu-temperature-units = درجہ حرارت دے یونٹ
newtab-weather-menu-temperature-option-fahrenheit = فارن ہائیٹ
newtab-weather-menu-temperature-option-celsius = سیلسیس
newtab-weather-menu-change-temperature-units-fahrenheit = فارن ہائٹ  تے سوئچ کرو
newtab-weather-menu-change-temperature-units-celsius = سینٹی گریڈ  تے سوئچ کرو
newtab-weather-menu-hide-weather = نویں ٹیب تے موسم لکاؤ
newtab-weather-menu-learn-more = ٻیا سِکھو
# This message is shown if user is working offline
newtab-weather-error-not-available = عیں ایں ویلے موسم ڈیٹا دستیاب کائنی۔

## Topic Labels

newtab-topic-label-business = کاروبار
newtab-topic-label-career = روزگار تے کم
newtab-topic-label-education = تعلیم
newtab-topic-label-arts = تفریح
newtab-topic-label-food = کھاݨا
newtab-topic-label-health = صحت
newtab-topic-label-hobbies = کھیݙݨ
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = رقم
newtab-topic-label-society-parenting = پرورش کرݨ
newtab-topic-label-government = سیاست
newtab-topic-label-education-science = سائنس
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = لائف ہیکس
newtab-topic-label-sports = کھیݙاں
newtab-topic-label-tech = ٹیکنالوجی
newtab-topic-label-travel = پندھ
newtab-topic-label-home = گھر تے باغ

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = آپݨی فیڈ کوں چنگا بݨاوݨ کیتے موضوعات چݨو
newtab-topic-selection-save-button = محفوظ
newtab-topic-selection-cancel-button = منسوخ
newtab-topic-selection-button-maybe-later = شاید بعد وچ
newtab-topic-selection-privacy-link = سکھو جو اساں ڈیٹا دی حفاظت تے منیج کین٘ویں کریندے ہیں۔
newtab-topic-selection-button-update-interests = آپݨیاں دلچسپیاں اپ ڈیٹ کرو
newtab-topic-selection-button-pick-interests = آپݨیاں دلچسپیاں چݨو

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

