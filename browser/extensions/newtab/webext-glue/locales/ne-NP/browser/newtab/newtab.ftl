# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = नयाँ ट्याब
newtab-settings-button =
    .title = तपाईंको नयाँ ट्याब पृष्ठ अनुकूलन गर्नुहोस्

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = खोजी गर्नुहोस्
    .aria-label = खोजी गर्नुहोस्

## Top Sites - General form dialog.

newtab-topsites-edit-topsites-header = शीर्ष साइट सम्पादन गर्नुहोस्
newtab-topsites-title-label = शीर्षक
newtab-topsites-title-input =
    .placeholder = शीर्षक प्रविष्ट गर्नुहोस्
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL लेख्नुहोस् वा पेस्ट गर्नुहोस्
newtab-topsites-url-validation = मान्य URL चाहिन्छ
newtab-topsites-image-url-label = अनुकूल तस्बिर URL
newtab-topsites-use-image-link = अनुकूल तस्बिर प्रयोग गर्नुहोस्…
newtab-topsites-image-validation = तस्बिर लोड गर्न असफल भयो । फरक URL प्रयास गर्नुहोस् ।

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = रद्द गर्नुहोस्
newtab-topsites-delete-history-button = इतिहासबाट मेट्नुहोस्
newtab-topsites-save-button = सङ्ग्रह गर्नुहोस्
newtab-topsites-preview-button = पूर्ववलोकन
newtab-topsites-add-button = थप्नुहोस्

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = के तपाईं पक्का हुनुहुन्छ कि तपाइँ यस पृष्ठको हरेक उदाहरण तपाइँको इतिहासबाट हटाउन चाहनुहुन्छ ?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = यो कार्य पूर्ववत गर्न सकिँदैन ।

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = यस साइटलाई सम्पादन गर्नुहोस्
    .aria-label = यस साइटलाई सम्पादन गर्नुहोस्

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = सम्पादन गर्नुहोस्
newtab-menu-open-new-window = नयाँ सञ्झ्यालमा खोल्नुहोस्
newtab-menu-open-new-private-window = नयाँ निजी सञ्झ्यालमा खोल्नुहोस्
newtab-menu-dismiss = खारेज गर्नुहोस्
newtab-menu-pin = पिन गर्नुहोस्
newtab-menu-unpin = अन पिन गर्नुहोस्
newtab-menu-delete-history = इतिहासबाट मेट्नुहोस्
newtab-menu-save-to-pocket = { -pocket-brand-name }मा बचत गर्नुहोस्
newtab-menu-delete-pocket = { -pocket-brand-name } बाट मेट्नुहोस्
newtab-menu-archive-pocket = { -pocket-brand-name } मा संग्रह गर्नुहोस्

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = पुस्तकचिनो हटाउनुहोस्
# Bookmark is a verb here.
newtab-menu-bookmark = पुस्तकचिनो

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = डाउनलोड लिङ्क प्रतिलिपि गर्नुहोस्
newtab-menu-go-to-download-page = डाउनलोड पेजमा जानुहोस्
newtab-menu-remove-download = इतिहासबाट हटाउनुहोस्

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Finder मा देखाउनुहोस्
       *[other] समाविष्ट भएको फोल्डर खोल्नुहोस्
    }
newtab-menu-open-file = फाइल खोल्नुहोस्

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = भ्रमण गरिएको
newtab-label-bookmarked = पुस्तकचिनो लागाइएको
newtab-label-recommended = प्रचलनमा
newtab-label-saved = { -pocket-brand-name } मा सङ्ग्रह गरियो
newtab-label-download = डाउनलोड भयो

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = खण्ड हटाउनुहोस्
newtab-section-menu-collapse-section = खण्ड संक्षिप्त गर्नुहोस्
newtab-section-menu-expand-section = खण्ड विस्तार गर्नुहोस्
newtab-section-menu-manage-section = खण्ड प्रबन्ध गर्नुहोस्
newtab-section-menu-add-topsite = शीर्ष साइट थप्नुहोस्
newtab-section-menu-move-up = माथि सार्नुहोस्
newtab-section-menu-move-down = तल सार्नुहोस्
newtab-section-menu-privacy-notice = गोपनीयता नीति

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = शीर्ष साइटहरु
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } द्वारा सिफारिस गरिएको

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ब्राउज गर्न सुरू गर्नुहोस्, र हामी केहि उत्कृष्ट लेखहरू, भिडियोहरू, र अन्य पृष्ठहरू जुन तपाईंले भर्खरै भ्रमण गर्नुभएको वा पुस्तकचिनो राख्नुभएको छ यहाँ देखाउँछौ ।
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = अहिले यति नै । { $provider } बाट थप शीर्ष कथाहरूको हेर्नका लागि पछि फेरि जाँच गर्नुहोस् । अाफुलाई रोक्न सक्नुहुदैन ? वेबभरिका राम्रा कथाहरु भेटाउन कुनै एउटा लोकप्रिय विषय छान्नुहोस् ।

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.


## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = लोकप्रिय शीर्षकहरू:

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = उफ्, सामाग्री लोड गर्न खोजदा केहि गलत भयो ।
newtab-error-fallback-refresh-link = पुनः प्रयास गर्न पृष्ठ ताजा गर्नुहोस् ।

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

