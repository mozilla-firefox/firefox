# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = নতুন ট্যাব
newtab-settings-button =
    .title = আপনার নতুন ট্যাবের পাতাটি কাস্টমাইজ করুন
newtab-personalize-icon-label =
    .title = নতুন ট্যাব ব্যক্তিগত করুন
    .aria-label = নতুন ট্যাব ব্যক্তিগত করুন
newtab-personalize-dialog-label =
    .aria-label = ব্যক্তিগতকরণ

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = অনুসন্ধান
    .aria-label = অনুসন্ধান
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = { $engine } দ্বারা অনুসন্ধান করুন অথবা ঠিকানা লিখুন
newtab-search-box-handoff-text-no-engine = অনুসন্ধান করুন বা ঠিকানা লিখুন
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = { $engine } দ্বারা অনুসন্ধান করুন অথবা ঠিকানা লিখুন
    .title = { $engine } দ্বারা অনুসন্ধান করুন অথবা ঠিকানা লিখুন
    .aria-label = { $engine } দ্বারা অনুসন্ধান করুন অথবা ঠিকানা লিখুন
newtab-search-box-handoff-input-no-engine =
    .placeholder = অনুসন্ধান করুন বা ঠিকানা লিখুন
    .title = অনুসন্ধান করুন বা ঠিকানা লিখুন
    .aria-label = অনুসন্ধান করুন বা ঠিকানা লিখুন
newtab-search-box-text = ওয়েবে অনুসন্ধান করুন
newtab-search-box-input =
    .placeholder = ওয়েবে অনুসন্ধান করুন
    .aria-label = ওয়েবে অনুসন্ধান করুন

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = অনুসন্ধান ইঞ্জিন যোগ করুন
newtab-topsites-add-shortcut-header = নতুন শর্টকাট
newtab-topsites-edit-topsites-header = শীর্ষ সাইট সম্পাদনা করুন
newtab-topsites-edit-shortcut-header = শর্টকাট সম্পাদনা করুন
newtab-topsites-title-label = শিরোনাম
newtab-topsites-title-input =
    .placeholder = শিরোনাম লিখুন
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = একটি URL লিখুন বা প্রতিলেপন করুন
newtab-topsites-url-validation = কার্যকর URL প্রয়োজন
newtab-topsites-image-url-label = কাস্টম ছবির URL
newtab-topsites-use-image-link = কাস্টম ছবি ব্যবহার করুন…
newtab-topsites-image-validation = ছবি লোড করতে ব্যর্থ। ভিন্ন URL এ চেস্টা করুন।

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = বাতিল
newtab-topsites-delete-history-button = ইতিহাস থেকে মুছে ফেলুন
newtab-topsites-save-button = সংরক্ষণ
newtab-topsites-preview-button = প্রাকদর্শন
newtab-topsites-add-button = যোগ করুন

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = আপনি কি নিশ্চিতভাবে আপনার ইতিহাস থেকে এই পাতার সকল কিছু মুছে ফেলতে চান?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = এই পরিবর্তনটি অপরিবর্তনীয়।

## Top Sites - Sponsored label

newtab-topsite-sponsored = স্পন্সরকৃত

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = মেনু খুলুন
    .aria-label = মেনু খুলুন
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = মুছে ফেলুন
    .aria-label = মুছে ফেলুন
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = মেনু খুলুন
    .aria-label = { $title } থেকে কনটেক্সট মেনু খুলুন
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = সাইটটি সম্পাদনা করুন
    .aria-label = সাইটটি সম্পাদনা করুন

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = সম্পাদন করুন
newtab-menu-open-new-window = নতুন উইন্ডোতে খুলুন
newtab-menu-open-new-private-window = নতুন ব্যক্তিগত উইন্ডোতে খুলুন
newtab-menu-dismiss = বাতিল
newtab-menu-pin = পিন করুন
newtab-menu-unpin = আনপিন করুন
newtab-menu-delete-history = ইতিহাস থেকে মুছে ফেলুন
newtab-menu-save-to-pocket = { -pocket-brand-name } এ সংরক্ষণ করুন
newtab-menu-delete-pocket = { -pocket-brand-name } থেকে মুছে দিন
newtab-menu-archive-pocket = { -pocket-brand-name } এ আর্কাইভ করুন
newtab-menu-show-privacy-info = আমাদের স্পন্সর ও আপনার গোপনীয়তা

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = সম্পন্ন
newtab-privacy-modal-button-manage = স্পনসর করা সামগ্রীর সেটিংস পরিচালনা করুন
newtab-privacy-modal-header = আপনার গোপনীয়তার বিষয়টি গুরুত্বপূর্ণ।
newtab-privacy-modal-paragraph-2 =
    মনোমুগ্ধকর গল্প পরিবেশন করার পাশাপাশি আমরা আপনাকে 
    নির্বাচিত স্পনসরদের প্রাসঙ্গিক ,
    উচ্চ-পরীক্ষিত বিষয়বস্তুও দেখাই। নিশ্চিত থাকুন, <strong>আপনার ব্রাউজিং
    তথ্য কখনই আপনার ব্যক্তিগত { -brand-product-name } এ থাকে না</strong> - আমরা তা দেখতে পাই না এবং আমাদের
    স্পনসরাও তা পায় না।
newtab-privacy-modal-link = কীভাবে নতুন ট্যাবে গোপনীয়তা কাজ করে তা জানুন

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = বুকমার্ক মুছে দিন
# Bookmark is a verb here.
newtab-menu-bookmark = বুকমার্ক

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ডাউনলোডের লিঙ্ক অনুলিপি করুন
newtab-menu-go-to-download-page = ডাউনলোড পাতায় যান
newtab-menu-remove-download = ইতিহাস থেকে মুছে ফেলুন

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] ফাইন্ডারে প্রদর্শন করুন
       *[other] ধারণকারী ফোল্ডার খুলুন
    }
newtab-menu-open-file = ফাইল খুলুন

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = পরিদর্শিত
newtab-label-bookmarked = বুকমার্ক করা হয়েছে
newtab-label-removed-bookmark = বুকমার্ক মুছে ফেলা হয়েছে
newtab-label-recommended = প্রবণতা
newtab-label-saved = { -pocket-brand-name } এ সংরক্ষণ হয়েছে
newtab-label-download = ডাউনলোড হয়েছে
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } । প্রযোজিত
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = { $sponsor } দ্বারা স্পনসরকৃত
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } মিনিট

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = সেকশনটি সরান
newtab-section-menu-collapse-section = সেকশনটি সংকোচন করুন
newtab-section-menu-expand-section = সেকশনটি প্রসারিত করুন
newtab-section-menu-manage-section = সেকশনটি পরিচালনা করুন
newtab-section-menu-manage-webext = এক্সটেনসন ব্যবহার করুন
newtab-section-menu-add-topsite = শীর্ষ সাইট যোগ করুন
newtab-section-menu-add-search-engine = অনুসন্ধান ইঞ্জিন যোগ করুন
newtab-section-menu-move-up = উপরে উঠান
newtab-section-menu-move-down = নিচে নামান
newtab-section-menu-privacy-notice = গোপনীয়তা নীতি

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = বিভাগটি সংকুচিত করুন
newtab-section-expand-section-label =
    .aria-label = বিভাগটি প্রসারিত করুন

## Section Headers.

newtab-section-header-topsites = শীর্ঘ সাইট
newtab-section-header-recent-activity = সাম্প্রতিক কার্যকলাপ
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } দ্বারা সুপারিশকৃত

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ব্রাউজি করা শুরু করুন, এবং কিছু গুরুত্বপূর্ণ নিবন্ধ, ভিডিও, এবং আপনি সম্প্রতি পরিদর্শন বা বুকমার্ক করেছেন এমন কিছু পৃষ্ঠা আমরা এখানে প্রদর্শন করব।
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = কিছু একটা ঠিক নেই। { $provider } এর শীর্ষ গল্পগুলো পেতে কিছুক্ষণ পর আবার দেখুন। অপেক্ষা করতে চান না? বিশ্বের সেরা গল্পগুলো পেতে কোন জনপ্রিয় বিষয় নির্বাচন করুন।

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = আর কিছু নেই!
newtab-discovery-empty-section-topstories-content = আরোও গল্পের জন্য পরে আবার দেখুন।
newtab-discovery-empty-section-topstories-try-again-button = আবার চেষ্টা করুন
newtab-discovery-empty-section-topstories-loading = লোড করা হচ্ছে…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ওহো! আমরা এই অনুচ্ছেদ প্রায় লোড করেছিলাম, কিন্তু শেষ করতে পারিনি।

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = জনপ্রিয় বিষয়:
newtab-pocket-more-recommendations = আরও সুপারিশ
newtab-pocket-learn-more = আরও জানুন
newtab-pocket-cta-button = { -pocket-brand-name } ব্যবহার করুন
newtab-pocket-cta-text = { -pocket-brand-name } এ আপনার পছন্দের গল্পগুলো সংরক্ষণ করুন, এবং চমৎকার সব লেখা পড়ে আপনার মনের ইন্ধন যোগান।

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = ওহো, কনটেন্টটি লোড করতে কিছু ভুল হয়েছে।
newtab-error-fallback-refresh-link = আবার চেষ্টা করতে পাতাটি পুনঃসতেজ করুন।

## Customization Menu

newtab-custom-shortcuts-title = শর্টকাট
newtab-custom-shortcuts-subtitle = আপনার সংরক্ষণ বা পরিদর্শন করা সাইট
newtab-custom-shortcuts-toggle =
    .label = শর্টকাট
    .description = আপনার সংরক্ষণ বা পরিদর্শন করা সাইট
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num }টি সারি
       *[other] { $num }টি সারি
    }
newtab-custom-sponsored-sites = স্পনসরকৃত শর্টকাট
newtab-custom-pocket-title = { -pocket-brand-name } দ্বারা সুপারিশকৃত
newtab-custom-recent-title = সাম্প্রতিক কার্যকলাপ
newtab-custom-close-button = বন্ধ করুন
newtab-custom-settings = আরও সেটিং পরিচালনা করুন

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

