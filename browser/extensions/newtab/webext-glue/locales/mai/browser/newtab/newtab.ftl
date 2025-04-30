# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = नव टैब
newtab-settings-button =
    .title = अपन नव टैब पृष्ठ पसंदीदा बनाउ

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = खोज
    .aria-label = खोज

## Top Sites - General form dialog.

newtab-topsites-edit-topsites-header = टॉप साइट संपादित करू
newtab-topsites-title-label = शीर्षक
newtab-topsites-title-input =
    .placeholder = शीर्षक दर्ज करू
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = एकटा URL टाइप अथवा साटू
newtab-topsites-url-validation = मान्य URL जरूरी
newtab-topsites-image-url-label = कस्टम छवि URL
newtab-topsites-use-image-link = एकटा कस्टम छवि उपयोग करू...
newtab-topsites-image-validation = छवि लोड करए मे असफल. दोसर URL सँ कोसिस करू.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = कैंसिल करू
newtab-topsites-delete-history-button = इतिहास सँ मेटाबू
newtab-topsites-save-button = सहेजू
newtab-topsites-preview-button = पूर्वावलोकन
newtab-topsites-add-button = जोड़ू

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = की अहाँ वाकई ई पृष्ठक हर उदाहरण क अपन इतिहास सँ हटाबै चाहैत छी?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = ई क्रिया केँ पहिले जहिना नहि कएल जाए सकैत अछि.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = ई साइट केँ संपादित करू
    .aria-label = ई साइट केँ संपादित करू

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = संपादन
newtab-menu-open-new-window = नव विंडो मे खोलू
newtab-menu-open-new-private-window = नव निजी विंडो मे खोलू
newtab-menu-dismiss = खारिज करू
newtab-menu-pin = पिन करू
newtab-menu-unpin = पिन हटाबू
newtab-menu-delete-history = इतिहास सँ मेटाबू
newtab-menu-save-to-pocket = { -pocket-brand-name } मे सहेजू

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = पुस्तचिह्न हटाबू
# Bookmark is a verb here.
newtab-menu-bookmark = पुस्तचिह्न

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = डाउनलोड लिंक कॉपी करू
newtab-menu-go-to-download-page = डाउनलोड पृष्ठ पर जाउ
newtab-menu-remove-download = इतिहास सँ हटाउ

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] फाइंडरमे देखाउ
       *[other] संग्राहक फोल्डर खोलू
    }
newtab-menu-open-file = फाइल खोलू

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = देखल गेल
newtab-label-bookmarked = बुकमार्क करल
newtab-label-recommended = ट्रेंडिंग
newtab-label-saved = { -pocket-brand-name } मे सहेजल
newtab-label-download = डाउनलोड करल

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = खंड हटाबू
newtab-section-menu-collapse-section = खंड संक्षिप्त करू
newtab-section-menu-expand-section = खंड पसारू
newtab-section-menu-manage-section = खंड प्रबंधित करू
newtab-section-menu-add-topsite = टॉप साइट जोड़ू
newtab-section-menu-move-up = उप्पर जाउ
newtab-section-menu-move-down = नीच्चाँ जाउ
newtab-section-menu-privacy-notice = गोपनीयता सूचना

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = टॉप साइट
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } द्वारा अनुशंसित

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ब्राउजिंग आरंभ करू, आओर हम अहाँक किछु बढियाँ नवीन आर्टिकिल, वीडियो, आओर आन पेज देखाएब, जकरा अङाँ हाले में विजिट कएलहुँ अथवा एतय बुकमार्क कएलहुँ.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = अहाँ आखिर धरि पहुँचि गेलहु, { $provider } सँ बाद में फिनु टॉप स्टोरी देखू. इंतजार नहि कए सकब? अधिक बढिया स्टोरी वेब सँ पाबै लेल एकटा लेकप्रिय टॉपिक चुनू.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.


## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = लोकप्रिय विषय:

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = ओह, बुझाय अछि जे कंटेंट लोड हए मे किछु गलत भ गेल.
newtab-error-fallback-refresh-link = फेनु प्रयास करए लेल पेज रीफ्रेश करू.

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

