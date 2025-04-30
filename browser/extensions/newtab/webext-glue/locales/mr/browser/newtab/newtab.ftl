# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = नवीन टॅब
newtab-settings-button =
    .title = आपले नवीन टॅब पृष्ठ सानुकूलित करा

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = शोधा
    .aria-label = शोधा

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = शोध इंजीन जोडा
newtab-topsites-edit-topsites-header = खास साईट संपादित करा
newtab-topsites-title-label = शिर्षक
newtab-topsites-title-input =
    .placeholder = शिर्षक प्रविष्ट करा
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL चिकटवा किंवा टाईप करा
newtab-topsites-url-validation = वैध URL आवश्यक
newtab-topsites-image-url-label = सानुकूल प्रतिमा URL
newtab-topsites-use-image-link = सानुकूल प्रतिमा वापरा…
newtab-topsites-image-validation = प्रतिमा लोड झाली नाही. वेगळी URL वापरून पहा.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = रद्द करा
newtab-topsites-delete-history-button = इतिहासातून नष्ट करा
newtab-topsites-save-button = जतन करा
newtab-topsites-preview-button = पूर्वावलोकन
newtab-topsites-add-button = समाविष्ट करा

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = आपल्या इतिहासामधून या पृष्ठातील प्रत्येक उदाहरण खात्रीने हटवू इच्छिता?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = ही क्रिया पूर्ववत केली जाऊ शकत नाही.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = मेनु उघडा
    .aria-label = मेनु उघडा
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = मेनु उघडा
    .aria-label = { $title } साठी संदर्भ मेनू उघडा
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = ही साइट संपादित करा
    .aria-label = ही साइट संपादित करा

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = संपादित करा
newtab-menu-open-new-window = नवीन पटलात उघडा
newtab-menu-open-new-private-window = नवीन खाजगी पटलात उघडा
newtab-menu-dismiss = रद्द करा
newtab-menu-pin = पिन लावा
newtab-menu-unpin = पिन काढा
newtab-menu-delete-history = इतिहासातून नष्ट करा
newtab-menu-save-to-pocket = { -pocket-brand-name } मध्ये जतन करा
newtab-menu-delete-pocket = { -pocket-brand-name } मधून हटवा
newtab-menu-archive-pocket = { -pocket-brand-name } मध्ये संग्रहित करा
newtab-menu-show-privacy-info = आमचे प्रायोजक आणि आपली गोपनीयता

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = झाले
newtab-privacy-modal-header = आपली गोपनीयता महत्वाची आहे.
newtab-privacy-modal-link = नवीन टॅबवर गोपनीयता कसे कार्य करते ते जाणून घ्या

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = वाचनखुण काढा
# Bookmark is a verb here.
newtab-menu-bookmark = वाचनखुण

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = डाउनलोड दुव्याची प्रत बनवा
newtab-menu-go-to-download-page = डाउनलोड पृष्ठावर जा
newtab-menu-remove-download = इतिहासातून काढून टाका

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Finder मध्ये दर्शवा
       *[other] समाविष्ट करणारे फोल्डर उघडा
    }
newtab-menu-open-file = फाइल उघडा

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = भेट दिलेले
newtab-label-bookmarked = वाचनखुण लावले
newtab-label-removed-bookmark = वाचनखूण काढली
newtab-label-recommended = प्रचलित
newtab-label-saved = { -pocket-brand-name } मध्ये जतन झाले
newtab-label-download = डाउनलोड केलेले

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = विभाग काढा
newtab-section-menu-collapse-section = विभाग ढासळा
newtab-section-menu-expand-section = विभाग वाढवा
newtab-section-menu-manage-section = विभाग व्यवस्थापित करा
newtab-section-menu-manage-webext = एक्सटेन्शन व्यवस्थापित करा
newtab-section-menu-add-topsite = खास साईट्स जोडा
newtab-section-menu-add-search-engine = शोध इंजीन जोडा
newtab-section-menu-move-up = वर जा
newtab-section-menu-move-down = खाली जा
newtab-section-menu-privacy-notice = गोपनीयता सूचना

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = विभाग संकुचित करा
newtab-section-expand-section-label =
    .aria-label = विभाग विस्तृत करा

## Section Headers.

newtab-section-header-topsites = खास साईट
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } तर्फे शिफारस

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ब्राउझिंग सुरू करा, आणि आम्ही आपल्याला इथे आपण अलीकडील भेट दिलेले किंवा वाचनखूण लावलेले उत्कृष्ठ लेख, व्हिडिओ, आणि इतर पृष्ठांपैकी काही दाखवू.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = तुम्ही सर्व बघितले. { $provider } कडून आणखी महत्वाच्या गोष्टी बघण्यासाठी नंतर परत तपासा. प्रतीक्षा करू शकत नाही? वेबवरील छान गोष्टी शोधण्यासाठी लोकप्रिय विषय निवडा.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-content = अधिक कथांसाठी नंतर पुन्हा तपासा.
newtab-discovery-empty-section-topstories-try-again-button = पुन्हा प्रयत्न करा
newtab-discovery-empty-section-topstories-loading = लोड करत आहे…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = अरेरे! आम्ही हा विभाग जवळजवळ लोड केला आहे, परंतु बर्‍यापैकी नाही.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = लोकप्रिय विषय:
newtab-pocket-more-recommendations = अधिक शिफारसी
newtab-pocket-cta-button = { -pocket-brand-name } मिळवा

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = अरेरे, हा मजकूर लोड करताना काहीतरी गोंधळ झाला.
newtab-error-fallback-refresh-link = पुन्हा प्रयत्न करण्यासाठी पृष्ठ रिफ्रेश करा.

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

