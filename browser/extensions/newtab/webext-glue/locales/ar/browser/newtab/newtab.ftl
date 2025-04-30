# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = لسان جديد
newtab-settings-button =
    .title = خصص صفحة اللسان الجديد
newtab-personalize-settings-icon-label =
    .title = خصّص صفحة اللسان الجديد
    .aria-label = إعدادات
newtab-settings-dialog-label =
    .aria-label = الإعدادات
newtab-personalize-icon-label =
    .title = خصّص صفحة اللسان الجديد
    .aria-label = خصّص صفحة اللسان الجديد
newtab-personalize-dialog-label =
    .aria-label = خصّص
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = ابحث
    .aria-label = ابحث
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = ‫ابحث ب { $engine } أو أدخِل عنوانا
newtab-search-box-handoff-text-no-engine = ابحث أو أدخِل عنوانا
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = ‫ابحث ب { $engine } أو أدخِل عنوانا
    .title = ‫ابحث ب { $engine } أو أدخِل عنوانا
    .aria-label = ‫ابحث ب { $engine } أو أدخِل عنوانا
newtab-search-box-handoff-input-no-engine =
    .placeholder = ابحث أو أدخِل عنوانا
    .title = ابحث أو أدخِل عنوانا
    .aria-label = ابحث أو أدخِل عنوانا
newtab-search-box-text = ابحث في الوِب
newtab-search-box-input =
    .placeholder = ابحث في الوِب
    .aria-label = ابحث في الوِب

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = أضِف محرك بحث
newtab-topsites-add-shortcut-header = اختصار جديد
newtab-topsites-edit-topsites-header = حرّر الموقع الشائع
newtab-topsites-edit-shortcut-header = حرّر الاختصار
newtab-topsites-add-shortcut-label = أضِف اختصارًا
newtab-topsites-title-label = العنوان
newtab-topsites-title-input =
    .placeholder = أدخل عنوانًا
newtab-topsites-url-label = المسار
newtab-topsites-url-input =
    .placeholder = اكتب أو ألصق مسارًا
newtab-topsites-url-validation = مطلوب مسار صالح
newtab-topsites-image-url-label = مسار الصورة المخصصة
newtab-topsites-use-image-link = استخدم صورة مخصصة…
newtab-topsites-image-validation = فشل تحميل الصورة. جرّب مسارا آخر.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = ألغِ
newtab-topsites-delete-history-button = احذف من التأريخ
newtab-topsites-save-button = احفظ
newtab-topsites-preview-button = عايِن
newtab-topsites-add-button = أضِفْ

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = هل أنت متأكد أنك تريد حذف كل وجود لهذه الصفحة من تأريخك؟
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = لا يمكن التراجع عن هذا الإجراء.

## Top Sites - Sponsored label

newtab-topsite-sponsored = نتيجة مموّلة

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = افتح القائمة
    .aria-label = افتح القائمة
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = أزِل
    .aria-label = أزِل
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = افتح القائمة
    .aria-label = افتح قائمة { $title } السياقية
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = حرّر هذا الموقع
    .aria-label = حرّر هذا الموقع

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = حرِّر
newtab-menu-open-new-window = افتح في نافذة جديدة
newtab-menu-open-new-private-window = افتح في نافذة خاصة جديدة
newtab-menu-dismiss = ألغِ
newtab-menu-pin = ثبّت
newtab-menu-unpin = أزل
newtab-menu-delete-history = احذف من التأريخ
newtab-menu-save-to-pocket = احفظ في { -pocket-brand-name }
newtab-menu-delete-pocket = احذف من { -pocket-brand-name }
newtab-menu-archive-pocket = أرشِف في { -pocket-brand-name }
newtab-menu-show-privacy-info = رُعاتنا الرسميّون وخصوصيّتك
newtab-menu-about-fakespot = عن { -fakespot-brand-name }
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = احجب
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = ألغِ متابعة الموضوع

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = تمّ
newtab-privacy-modal-button-manage = أدِر إعدادات المحتوى المرعيّ
newtab-privacy-modal-header = خصوصيتك فوق كل شيء.
newtab-privacy-modal-paragraph-2 = نعرض عليكم محتوى مفحوصًا بحذر من رُعاة مُختارين بعناية، بالإضافة للقصص الآسرة التي نقدّمها. اطمئن <strong>فبياناتك وأنت تتصفّح لا تخرج مطلقًا خارج نسختك من { -brand-product-name }</strong> — إذ لا نحن نراها، ولا رُعاتنا يرونَها.
newtab-privacy-modal-link = تعرّف على طريقة عمل الخصوصية في الألسنة الجديدة

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = أزل العلامة
# Bookmark is a verb here.
newtab-menu-bookmark = علّم

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = انسخ رابط التنزيل
newtab-menu-go-to-download-page = انتقل إلى صفحة التنزيل
newtab-menu-remove-download = احذف من التأريخ

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] أظهِر في فايندر
       *[other] افتح المجلد المحتوي
    }
newtab-menu-open-file = افتح الملف

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = مُزارة
newtab-label-bookmarked = معلّمة
newtab-label-removed-bookmark = أُزيلت العلامة
newtab-label-recommended = مُتداول
newtab-label-saved = حُفِظت في { -pocket-brand-name }
newtab-label-download = نُزّل
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = برعاية · { $sponsorOrSource }
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = برعاية { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } دقيقة

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = أزِل القسم
newtab-section-menu-collapse-section = اطوِ القسم
newtab-section-menu-expand-section = وسّع القسم
newtab-section-menu-manage-section = أدِر القسم
newtab-section-menu-manage-webext = أدِر الامتداد
newtab-section-menu-add-topsite = أضف موقعًا شائعًا
newtab-section-menu-add-search-engine = أضِف محرك بحث
newtab-section-menu-move-up = انقل لأعلى
newtab-section-menu-move-down = انقل لأسفل
newtab-section-menu-privacy-notice = تنويه الخصوصية

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = اطوِ القسم
newtab-section-expand-section-label =
    .aria-label = وسّع القسم

## Section Headers.

newtab-section-header-topsites = المواقع الأكثر زيارة
newtab-section-header-recent-activity = أحدث الأنشطة
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = ينصح به { $provider }
newtab-section-header-stories = قصص تدعو للتأمل
# "picks" refers to recommended articles
newtab-section-header-todays-picks = اختياراتنا لك اليوم

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ابدأ التصفح وسنعرض أمامك بعض المقالات والفيديوهات والمواقع الأخرى التي زرتها حديثا أو أضفتها إلى العلامات هنا.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = لا جديد. تحقق لاحقًا للحصول على مزيد من أهم الأخبار من { $provider }. لا يمكنك الانتظار؟ اختر موضوعًا شائعًا للعثور على المزيد من القصص الرائعة من جميع أنحاء الوِب.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = بِتّ تعلم كل شيء!
newtab-discovery-empty-section-topstories-content = عُد فيما بعد لترى قصص أخرى.
newtab-discovery-empty-section-topstories-try-again-button = أعِد المحاولة
newtab-discovery-empty-section-topstories-loading = يحمّل…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = آخ. أوشكنا على تحميل هذا القسم، لكن للأسف لم يكتمل.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = المواضيع الشائعة:
newtab-pocket-new-topics-title = أتريد المزيد من القصص؟ اطلع على هذه المواضيع الشائعة من { -pocket-brand-name }
newtab-pocket-more-recommendations = مقترحات أخرى
newtab-pocket-learn-more = اطّلع على المزيد
newtab-pocket-cta-button = نزِّل { -pocket-brand-name }
newtab-pocket-cta-text = احفظ القصص التي تحبّها في { -pocket-brand-name }، وزوّد عقلك بمقالات رائعة.
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = احفظ
newtab-pocket-saved = حُفظت

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = المزيد من هذا القبيل
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = ليس لي
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = شكرًا لك. ستساعدنا تعليقاتك في تحسين خلاصتك.
newtab-toast-dismiss-button =
    .title = أهمِل
    .aria-label = أهمِل

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = اكتشف أفضل ما في الويب
newtab-pocket-onboarding-cta = يستكشف { -pocket-brand-name } مجموعة متنوعة من المنشورات لتجد المحتوى الأكثر إطلاعا وإلهاما وموثوقا به في متصفحك { -brand-product-name }.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = أخ! حدث خطب ما أثناء تحميل المحتوى.
newtab-error-fallback-refresh-link = أنعِش الصفحة لإعادة المحاولة.

## Customization Menu

newtab-custom-shortcuts-title = الاختصارات
newtab-custom-shortcuts-subtitle = المواقع التي حفظتها أو زرتها
newtab-custom-shortcuts-toggle =
    .label = الاختصارات
    .description = المواقع التي حفظتها أو زرتها
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [zero] ما من صفوف
        [one] صف واحد
        [two] صفّان اثنان
        [few] { $num } صفوف
        [many] { $num } صفًا
       *[other] { $num } صف
    }
newtab-custom-sponsored-sites = الاختصارات الممولة
newtab-custom-pocket-title = مُقترح من { -pocket-brand-name }
newtab-custom-pocket-subtitle = محتوى مميّز جمعه لك { -pocket-brand-name }، وهو جزء من عائلة { -brand-product-name }
newtab-custom-pocket-sponsored = قصص مموّلة
newtab-custom-pocket-show-recent-saves = أظهِر عمليات الحفظ الأخيرة
newtab-custom-recent-title = أحدث الأنشطة
newtab-custom-recent-subtitle = مختارات من المواقع والمحتويات الحديثة
newtab-custom-recent-toggle =
    .label = أحدث الأنشطة
    .description = مختارات من المواقع والمحتويات الحديثة
newtab-custom-close-button = أغلِق
newtab-custom-settings = أدِر المزيد من الإعدادات

## New Tab Wallpapers

newtab-wallpaper-title = الخلفيات
newtab-wallpaper-reset = صفّر إلى المبدئي
newtab-wallpaper-light-red-panda = باندا أحمر
newtab-wallpaper-light-mountain = جبل ابيض
newtab-wallpaper-light-sky = سماء مع غيوم أرجوانية ووردية
newtab-wallpaper-light-color = الأشكال الزرقاء والوردية والصفراء
newtab-wallpaper-light-landscape = منظر جبلي ضبابي أزرق
newtab-wallpaper-light-beach = شاطئ مع شجرة نخيل
newtab-wallpaper-dark-aurora = شفق قطبي
newtab-wallpaper-dark-color = أشكال حمراء وزرقاء
newtab-wallpaper-dark-panda = باندا حمراء مختبئة في الغابة
newtab-wallpaper-dark-sky = منظر المدينة مع سماء الليل
newtab-wallpaper-dark-mountain = منظر جبلي
newtab-wallpaper-dark-city = منظر المدينة الأرجواني
newtab-wallpaper-dark-fox-anniversary = ثعلب على الرصيف بالقرب من الغابة
newtab-wallpaper-light-fox-anniversary = ثعلب في حقل عشبي مع منظر جبلي ضبابي

## Solid Colors

newtab-wallpaper-category-title-colors = الألوان الصلبة
newtab-wallpaper-blue = أزرق
newtab-wallpaper-light-blue = أزرق فاتح
newtab-wallpaper-light-purple = ارجواني فاتح
newtab-wallpaper-light-green = اخضر فاتح
newtab-wallpaper-green = أخضر
newtab-wallpaper-beige = بيج
newtab-wallpaper-yellow = أصفر
newtab-wallpaper-orange = برتقالي
newtab-wallpaper-pink = وردي
newtab-wallpaper-light-pink = وردي فاتح
newtab-wallpaper-red = أحمر
newtab-wallpaper-dark-blue = أزرق غامق
newtab-wallpaper-dark-purple = أرجواني داكن
newtab-wallpaper-dark-green = أخضر غامق
newtab-wallpaper-brown = بني

## Abstract

newtab-wallpaper-category-title-abstract = مجرّدة
newtab-wallpaper-abstract-green = أشكال خضراء
newtab-wallpaper-abstract-blue = أشكال زرقاء
newtab-wallpaper-abstract-purple = أشكال أرجوانية
newtab-wallpaper-abstract-orange = أشكال برتقالية
newtab-wallpaper-gradient-orange = تدرج اللون البرتقالي والوردي
newtab-wallpaper-abstract-blue-purple = الأشكال الزرقاء والأرجوانية

## Celestial

newtab-wallpaper-white-mountains = جبال بيضاء
newtab-wallpaper-feature-highlight-header = جرب دفقة من الألوان
newtab-wallpaper-feature-highlight-button = فهمت
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast =
    .title = أظهِر التوقعات في { $provider }
newtab-weather-menu-change-location = غيّر المكان
newtab-weather-menu-learn-more = اطّلع على المزيد

## Topic Labels


## Topic Selection Modal

newtab-topic-selection-save-button = احفظ
newtab-topic-selection-cancel-button = ألغِ
newtab-topic-selection-button-maybe-later = ربما لاحقا

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-unfollow-button = ألغِ المتابعة

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.


## Confirmation modal for blocking a section

newtab-section-cancel-button = ليس الآن

## Panel in the Customize menu section to manage followed and blocked topics

newtab-custom-wallpaper-cta = جربه
