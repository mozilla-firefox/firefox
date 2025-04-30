# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = بازدەری نوێ
newtab-settings-button =
    .title = پەڕەی بازدەری نوێ بە دڵی خۆت لێبکە

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = گەڕان
    .aria-label = گەڕان

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = بزوێنەری گەڕان زیادبکە
newtab-topsites-add-shortcut-header = قەدبڕی نوێ
newtab-topsites-edit-topsites-header = ماڵپەڕی سەرەکی دەستکاریبکە
newtab-topsites-edit-shortcut-header = قەدبڕ دەستکاریبکە
newtab-topsites-title-label = سەردێڕ
newtab-topsites-title-input =
    .placeholder = سەردێڕێک بنووسە
newtab-topsites-url-label = بەستەر
newtab-topsites-url-input =
    .placeholder = بینووسە یان بەستەر بلکێنە
newtab-topsites-url-validation = بەستەری گونجاو پێویستە
newtab-topsites-image-url-label = بەستەری وێنەی خوازراو
newtab-topsites-use-image-link = بەستەری خوازراو بەکاربێنە...
newtab-topsites-image-validation = نەتوانرا وێنە باربکرێت. بەستەرێکی تر تاقیبکەرەوە.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = پاشگەزبوونەوە
newtab-topsites-delete-history-button = سڕینەوە لە مێژووی کار
newtab-topsites-save-button = پاشەکەوتکردن
newtab-topsites-preview-button = پێشبینین
newtab-topsites-add-button = زیادکردن

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = تۆ دڵنیای کە هەموو شتێکی ئەم پەڕەیە بسڕیتەوە لە مێژووی کار؟
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = ئەم کارە پاشگەزبوونەوەی نیە.

## Top Sites - Sponsored label

newtab-topsite-sponsored = پاڵپشتیکراو

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = پێڕست بکەرەوە
    .aria-label = پێڕست بکەرەوە
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = بیسڕەوە
    .aria-label = بیسڕەوە
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = پێڕست بکەرەوە
    .aria-label = کردنەوەی پێکهاتەی پێڕست بۆ { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = ئەم ماڵپەڕە دەستکاری بکە
    .aria-label = ئەم ماڵپەڕە دەستکاری بکە

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = دەستکاریکردن
newtab-menu-open-new-window = لە پەنجەرەیەکی نوێ بیکەرەوە
newtab-menu-open-new-private-window = لە پەنجەرەیەکی نویی تایبەت بیکەرەوە
newtab-menu-dismiss = پشتگوێخستن
newtab-menu-pin = هەڵواسین
newtab-menu-unpin = لابردن
newtab-menu-delete-history = سڕینەوە لە مێژووی کار
newtab-menu-save-to-pocket = پاشەکەوتکردن لە { -pocket-brand-name }
newtab-menu-delete-pocket = سڕینەوە لە { -pocket-brand-name }
newtab-menu-archive-pocket = ئەرشیف کردن لە { -pocket-brand-name }
newtab-menu-show-privacy-info = سپۆنسەرەکانمان و تایبەتێتی تۆ

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = تەواو
newtab-privacy-modal-header = تایبەتێتی تۆ گرنگە
newtab-privacy-modal-link = فێربە چۆن تایبەتێتی کاردەکات لە بازدەرێکی نوێ

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = دڵخواز بسڕەوە
# Bookmark is a verb here.
newtab-menu-bookmark = دڵخواز

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = بەستەری داگرتن لەبەربگرەوە
newtab-menu-go-to-download-page = بڕۆ بۆ پەڕەی داگرتن
newtab-menu-remove-download = سڕینەوە لە مێژووی کار

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] کردنەوەی لە بوخچەدا
       *[other] بوخچەی لەخۆگری بکەرەوە
    }
newtab-menu-open-file = پەڕگە بکەرەوە

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = سەردانیکراو
newtab-label-bookmarked = دڵخوازکراو
newtab-label-removed-bookmark = دڵخواز سڕایەوە
newtab-label-recommended = باوە
newtab-label-saved = پاشەکەوتکردن لە { -pocket-brand-name }
newtab-label-download = داگیراو
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · سپۆنسەرکراو

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = بەش بسڕەوە
newtab-section-menu-collapse-section = داخستنەوەی بەش
newtab-section-menu-expand-section = فراوانکردنی بەش
newtab-section-menu-manage-section = ڕێکخستنی بەش
newtab-section-menu-manage-webext = ڕێکخستنی پێوەکراو
newtab-section-menu-add-topsite = ماڵپەڕی سەرەکی زیادبکە
newtab-section-menu-add-search-engine = بزوێنەری گەڕان زیادبکە
newtab-section-menu-move-up = بیبە سەرەوە
newtab-section-menu-move-down = بیبە خوارەوە
newtab-section-menu-privacy-notice = تێبینی لەسەر تایبەتێتی

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = داخستنەوەی بەش
newtab-section-expand-section-label =
    .aria-label = فراوانکردنی بەش

## Section Headers.

newtab-section-header-topsites = ماڵپەڕە سەرەکییەکان
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = پێشنیازکراوە لە لایەن { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = دەست بکە بە گەڕان، ئێمەش چەن بابەتێک باشت پیشان دەدەین، ڤیدیۆ، چەند پەرەیەکی تر کە پێشتر سەردانت کردووە یان دڵخوازت کردووە.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = هەموویت ئەنجام دا. کاتێکی تر وەرەوە بۆ چیرۆکی زیاتر لە { $provider }وە. ناتوانیت چاوەڕی بکەیت؟ بابەتێکی بەناوبانگ هەڵبژێرە بۆ ئەوەی چیرۆکی نایاب بدۆزیتەوە لە هەموو وێب.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = هەموویت ئەنجام دا!
newtab-discovery-empty-section-topstories-content = جارێکی تر بگەرێوە بۆ چیرۆکی تر
newtab-discovery-empty-section-topstories-try-again-button = دووبارە هەوڵ بدەرەوە
newtab-discovery-empty-section-topstories-loading = باردەکرێت...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ئوپس! وا هەموو بەشەکە باردەکەین، بەڵام بە تەواوی نا.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = بابەتی بەناوبانگ:
newtab-pocket-more-recommendations = پێشنیازکراوی زیاتر
newtab-pocket-learn-more = زیاتر بزانە
newtab-pocket-cta-button = بەدەستهێنانی { -pocket-brand-name }
newtab-pocket-cta-text = چیرۆکە دڵخوازەکانت پاشەکەوت بکە لە { -pocket-brand-name }، مێشکت پڕ بکە لە خوێندنەوەی دڵڕفێن.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = ئوپس! هەڵەیەک ڕوویدا لە کاتی بارکردنی ئەم ناوەڕۆکە.
newtab-error-fallback-refresh-link = پەڕە نوێبکەرەوە بۆ هەوڵدانێکی تر.

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

