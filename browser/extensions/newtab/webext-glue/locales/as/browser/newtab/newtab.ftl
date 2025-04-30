# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = নতুন টেব
newtab-settings-button =
    .title = আপোনাৰ নতুন টেবৰ পৃষ্ঠা কাষ্টমাইজ কৰক
newtab-personalize-icon-label =
    .title = নতুন টেব ব্যক্তিগতকৰণ কৰক
    .aria-label = নতুন টেব ব্যক্তিগতকৰণ কৰক।
newtab-personalize-dialog-label =
    .aria-label = ব্যক্তিগতকৰণ কৰক

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = সন্ধান কৰক
    .aria-label = সন্ধান কৰক
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = { $engine }-এৰে সন্ধান কৰক নাইবা ঠিকনা লিখক
newtab-search-box-handoff-text-no-engine = সন্ধান কৰক নাইবা ঠিকনা লিখক
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = { $engine }-এৰে সন্ধান কৰক নাইবা ঠিকনা লিখক
    .title = { $engine }-এৰে সন্ধান কৰক নাইবা ঠিকনা লিখক
    .aria-label = { $engine }-এৰে সন্ধান কৰক নাইবা ঠিকনা লিখক
newtab-search-box-handoff-input-no-engine =
    .placeholder = সন্ধান কৰক নাইবা ঠিকনা লিখক
    .title = সন্ধান কৰক নাইবা ঠিকনা লিখক
    .aria-label = সন্ধান কৰক নাইবা ঠিকনা লিখক
newtab-search-box-text = ৱেবত সন্ধান কৰক
newtab-search-box-input =
    .placeholder = ৱেবত সন্ধান কৰক
    .aria-label = ৱেবত সন্ধান কৰক

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = ছাৰ্চ ইঞ্জিন যোগ কৰক
newtab-topsites-edit-topsites-header = শীৰ্ষ ছাইট সম্পাদনা কৰক
newtab-topsites-title-label = শীৰ্ষক
newtab-topsites-title-input =
    .placeholder = শীৰ্ষক প্ৰবিষ্ট কৰক
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL টাইপ বা পে'ষ্ট কৰক
newtab-topsites-url-validation = বৈধ URLৰ প্ৰয়োজন
newtab-topsites-image-url-label = কাষ্টম ছবিৰ URL
newtab-topsites-use-image-link = কাষ্টম ছবি ব্যৱহাৰ কৰক…
newtab-topsites-image-validation = ছবি ল'ড হোৱা বিফল হ'ল। বেলেগ এটা URL পৰীক্ষা কৰক।

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = বাতিল কৰক
newtab-topsites-delete-history-button = ইতিহাসৰ পৰা মচক
newtab-topsites-save-button = সাঁচি থওক
newtab-topsites-preview-button = পূৰ্বলোকন
newtab-topsites-add-button = যোগ কৰক

## Top Sites - Delete history confirmation dialog.

# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = এই কাৰ্য্য পিছত পূৰ্বৰ দৰে কৰিব নোৱাৰি।

## Top Sites - Sponsored label

newtab-topsite-sponsored = পৃষ্ঠপোষকতা কৰা

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = মেন্যু খোলক
    .aria-label = মেন্যু খোলক
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = আঁতৰাওক
    .aria-label = আঁতৰাওক
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = মেন্যু খোলক
    .aria-label = { $title }-ৰ কণ্টেক্স্‌ট মেন্যু খোলক
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = এই ছাইট সম্পাদনা কৰক
    .aria-label = এই ছাইট সম্পাদনা কৰক

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = সম্পাদন কৰক
newtab-menu-open-new-window = নতুন উইণ্ড' এটাত খোলক
newtab-menu-open-new-private-window = নতুন ব্যক্তিগত উইণ্ড' এটাত খোলক
newtab-menu-dismiss = খাৰিজ কৰক
newtab-menu-pin = পিন কৰক
newtab-menu-unpin = আনপিন কৰক
newtab-menu-delete-history = ইতিহাসৰ পৰা মচি পেলাওক
newtab-menu-save-to-pocket = { -pocket-brand-name }-ত সাঁচি থওক
newtab-menu-delete-pocket = { -pocket-brand-name }-ৰ পৰা মচি পেলাওক
newtab-menu-archive-pocket = { -pocket-brand-name }-ত আৰ্কাইভ কৰক
newtab-menu-show-privacy-info = আমাৰ স্পঞ্চৰ আৰু আপোনাৰ গোপনিয়তা

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = হৈ গ'ল
newtab-privacy-modal-header = আপোনাৰ গোপনিয়তাৰ গুৰুত্ব আছে।

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = বুকমাৰ্ক আঁতৰাওক
# Bookmark is a verb here.
newtab-menu-bookmark = বুকমাৰ্ক কৰক

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ডাউনল'ড প্ৰতিলিপি কৰক
newtab-menu-go-to-download-page = ডাউনল'ড পৃষ্ঠালৈ যাওক
newtab-menu-remove-download = ইতিহাসৰ পৰা আঁতৰাওক

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-open-file = ফাইল খোলক

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = পৰিদৰ্শিত
newtab-label-bookmarked = বুকমাৰ্ক কৰা হ'ল
newtab-label-removed-bookmark = বুকমাৰ্ক আঁতৰোৱা হ'ল
newtab-label-recommended = ট্ৰেণ্ডিং
newtab-label-saved = { -pocket-brand-name }-ত সাঁচি থোৱা হ'ল
newtab-label-download = ডাউনল'ড কৰিছে
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · পৃষ্ঠপোষকতা কৰা
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = { $sponsor }দ্বাৰা পৃষ্ঠপোষকতা কৰা হৈছে

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = শাখা আঁতৰাওক
newtab-section-menu-collapse-section = শাখা জপাওক
newtab-section-menu-expand-section = শাখা বহলাওক
newtab-section-menu-manage-section = শাখা পৰিচালনা কৰক
newtab-section-menu-manage-webext = এক্সটেনশ্যন পৰিচালনা
newtab-section-menu-add-topsite = শীৰ্ষ ছাইট যোগ কৰক
newtab-section-menu-add-search-engine = ছাৰ্চ ইঞ্জিন যোগ কৰক
newtab-section-menu-move-up = ওপৰলৈ নিয়ক
newtab-section-menu-move-down = তললৈ নিয়ক
newtab-section-menu-privacy-notice = গোপনিয়তা জাননী

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = শাখা জপাওক
newtab-section-expand-section-label =
    .aria-label = শাখা বহলাওক

## Section Headers.

newtab-section-header-topsites = শীৰ্ষ ছাইটসমূহ
newtab-section-header-recent-activity = শেহতীয়া কাৰ্যকলাপ
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider }-ৰ দ্বাৰা পৰামৰ্শিত

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.


## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = আপুনি সকলো চালে!
newtab-discovery-empty-section-topstories-content = আৰু অধিক কাহিনীৰ বাবে পিছত আকৌ চাব।
newtab-discovery-empty-section-topstories-try-again-button = পুনৰ চেষ্টা কৰক
newtab-discovery-empty-section-topstories-loading = ল'ড হৈ আছে…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = উস্! আমি এই শাখাটো ল'ড কৰিছিলোঁৱেই প্ৰায়, কিন্তু পূৰা নহ'ল।

## Pocket Content Section.

newtab-pocket-learn-more = অধিক জানক
newtab-pocket-cta-button = { -pocket-brand-name } পাওক

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-refresh-link = পুনৰ চেষ্টা কৰিবলৈ পৃষ্ঠা সতেজ কৰক।

## Customization Menu

newtab-custom-shortcuts-subtitle = আপুনি সাঁচি থোৱা বা দৰ্শন কৰা ছাইটসমূহ
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num }টা শাৰী
       *[other] { $num }টা শাৰী
    }
newtab-custom-close-button = বন্ধ কৰক

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

