# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = ແທັບໃຫມ່
newtab-settings-button =
    .title = ປັບແຕ່ງຫນ້າແທັບໃຫມ່ຂອງທ່ານ
newtab-personalize-icon-label =
    .title = ປັບແຕ່ງແຖບໃໝ່
    .aria-label = ປັບແຕ່ງແຖບໃໝ່
newtab-personalize-dialog-label =
    .aria-label = ປັບແຕ່ງສ່ວນຕົວ
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = ຊອກ​ຫາ
    .aria-label = ຊອກ​ຫາ
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = ຊອກຫາດ້ວຍ { $engine } ຫຼື ໃສ່ທີ່ຢູ່
newtab-search-box-handoff-text-no-engine = ຊອກຫາ ຫລື ປ້ອນທີ່ຢູ່ໃສ່
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = ຊອກຫາດ້ວຍ { $engine } ຫຼືໃສ່ທີ່ຢູ່
    .title = ຊອກຫາດ້ວຍ { $engine } ຫຼືໃສ່ທີ່ຢູ່
    .aria-label = ຊອກຫາດ້ວຍ { $engine } ຫຼືໃສ່ທີ່ຢູ່
newtab-search-box-handoff-input-no-engine =
    .placeholder = ຄົ້ນຫາ ຫລື ປ້ອນທີ່ຢູ່ໃສ່
    .title = ຄົ້ນຫາ ຫລື ປ້ອນທີ່ຢູ່ໃສ່
    .aria-label = ຄົ້ນຫາ ຫລື ປ້ອນທີ່ຢູ່ໃສ່
newtab-search-box-text = ຄົ້ນຫາເວັບໄຊທ
newtab-search-box-input =
    .placeholder = ຄົ້ນຫາເວັບໄຊທ
    .aria-label = ຄົ້ນຫາເວັບໄຊທ

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = ເພີ່ມ Search Engine
newtab-topsites-add-shortcut-header = ທາງລັດໃໝ່
newtab-topsites-edit-topsites-header = ແກ້ໄຂເວັບໄຊທ໌ຍອດນິຍົມ
newtab-topsites-edit-shortcut-header = ແກ້ໄຂທາງລັດ
newtab-topsites-title-label = ຊື່ເລື່ອງ
newtab-topsites-title-input =
    .placeholder = ປ້ອນຊື່ເລື່ອງ
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = ພິມຫລືວາງ URL
newtab-topsites-url-validation = ຕ້ອງການ URL ທີ່ຖືກຕ້ອງ
newtab-topsites-image-url-label = URL ຮູບພາບທີ່ກຳນົດເອງ
newtab-topsites-use-image-link = ໃຊ້ຮູບພາບທີ່ກຳນົດເອງ…
newtab-topsites-image-validation = ການໂຫລດຮູບພາບລົ້ມເຫລວ. ລອງໃຊ້ URL ອື່ນ.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = ຍົກເລີກ
newtab-topsites-delete-history-button = ລຶບອອກຈາກປະຫວັດການນຳໃຊ້
newtab-topsites-save-button = ບັນທຶກ
newtab-topsites-preview-button = ສະແດງຕົວຢ່າງ
newtab-topsites-add-button = ເພີ່ມ

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ທ່ານແນ່ໃຈຫຼືບໍ່ວ່າຕ້ອງການລຶບທຸກ instance ຂອງຫນ້ານີ້ອອກຈາກປະຫວັດການໃຊ້ງານຂອງທ່ານ?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = ການກະທຳນີ້ບໍ່ສາມາດຍົກເລີກໄດ້.

## Top Sites - Sponsored label

newtab-topsite-sponsored = ໄດ້ຮັບການສະຫນັບສະຫນູນ

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = ເປີດເມນູ
    .aria-label = ເປີດເມນູ
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = ລຶບ
    .aria-label = ລຶບ
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = ແກ້ໄຂເວັບໄຊທ໌ນີ້
    .aria-label = ແກ້ໄຂເວັບໄຊທ໌ນີ້

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = ແກ້ໄຂ
newtab-menu-open-new-window = ເປີດລີ້ງໃນວິນໂດໃຫມ່
newtab-menu-open-new-private-window = ເປີດໃນວິນໂດສ່ວນຕົວໃຫມ່
newtab-menu-dismiss = ຍົກເລີກ
newtab-menu-pin = ປັກໝຸດ
newtab-menu-unpin = ຖອນປັກໝຸດ
newtab-menu-delete-history = ລຶບອອກຈາກປະຫວັດການນຳໃຊ້
newtab-menu-save-to-pocket = ບັນທືກໄປທີ່ { -pocket-brand-name }
newtab-menu-delete-pocket = ລຶບອອກຈາກ { -pocket-brand-name }
newtab-menu-archive-pocket = ເກັບຖາວອນໃນ { -pocket-brand-name }
newtab-menu-show-privacy-info = ຜູ້ສະຫນັບສະຫນູນຂອງພວກເຮົາ & ຄວາມເປັນສ່ວນຕົວຂອງທ່ານ

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = ສຳເລັດ
newtab-privacy-modal-button-manage = ຈັດການການຕັ້ງຄ່າເນື້ອຫາທີ່ສະປອນເຊີ
newtab-privacy-modal-header = ຄວາມເປັນສ່ວນຕົວຂອງເຈົ້າສຳຄັນ.
newtab-privacy-modal-paragraph-2 =
    ນອກ​ເໜືອ​ໄປ​ຈາກ​ການ​ເລົ່າ​ເລື່ອງ​ທີ່​ໜ້າ​ຈັບ​ໃຈ​ແລ້ວ, ພວກ​ເຮົາ​ຍັງ​ສະ​ແດງ​ໃຫ້​ທ່ານ​ເຫັນ​ກ່ຽວ​ກັບ
    ເນື້ອຫາທີ່ໄດ້ຮັບການກວດສອບສູງຈາກຜູ້ສະຫນັບສະຫນູນທີ່ເລືອກ. ໝັ້ນໃຈໄດ້, <strong>ການທ່ອງເວັບຂອງເຈົ້າ
    ຂໍ້ມູນບໍ່ເຄີຍຖິ້ມສຳເນົາສ່ວນຕົວຂອງເຈົ້າຂອງ { -brand-product-name }</strong> — ພວກເຮົາບໍ່ເຫັນມັນ, ແລະຂອງພວກເຮົາ
    ຜູ້ສະຫນັບສະຫນູນກໍ່ບໍ່ໄດ້.
newtab-privacy-modal-link = ຮຽນຮູ້ວິທີຄວາມເປັນສ່ວນຕົວເຮັດວຽກຢູ່ໃນແຖບໃໝ່

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = ລຶບບຸກມາກອອກ
# Bookmark is a verb here.
newtab-menu-bookmark = ບຸກມາກ

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ສຳເນົາລີ້ງດາວໂຫລດ
newtab-menu-go-to-download-page = ໄປທີ່ຫນ້າດາວໂຫລດ
newtab-menu-remove-download = ລຶບອອກຈາກປະຫວັດ

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] ສະແດງໃນ Finder
       *[other] ເປີດໂຟນເດີທີ່ບັນຈຸ
    }
newtab-menu-open-file = ເປີດໄຟລ໌

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = ເຂົ້າໄປເບິ່ງມາແລ້ວ
newtab-label-bookmarked = ບຸກມາກໄວ້ແລ້ວ
newtab-label-removed-bookmark = ລຶບບຸກມາກອອກແລ້ວ
newtab-label-recommended = ກຳລັງນິຍົມ
newtab-label-saved = ບັນທຶກລົງໃນ { -pocket-brand-name } ແລ້ວ
newtab-label-download = ດາວໂຫຼດແລ້ວ
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · ສະປອນເຊີ
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = ສະໜັບສະໜູນໂດຍ { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } ນທ

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = ລຶບສ່ວນ
newtab-section-menu-collapse-section = ຍຸບສ່ວນ
newtab-section-menu-expand-section = ຂະຫຍາຍສ່ວນ
newtab-section-menu-manage-section = ຈັດການສ່ວນ
newtab-section-menu-manage-webext = ຈັດການສ່ວນເສີມ
newtab-section-menu-add-topsite = ເພີ່ມເວັບໄຊທ໌ຍອດນິຍົມ
newtab-section-menu-add-search-engine = ເພີ່ມ Search Engine
newtab-section-menu-move-up = ຍ້າຍຂື້ນ
newtab-section-menu-move-down = ຍ້າຍລົງ
newtab-section-menu-privacy-notice = ນະໂຍບາຍຄວາມເປັນສ່ວນຕົວ

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = ຍຸບສ່ວນ
newtab-section-expand-section-label =
    .aria-label = ຂະຫຍາຍສ່ວນ

## Section Headers.

newtab-section-header-topsites = ເວັບໄຊຕ໌ຍອດນິຍົມ
newtab-section-header-recent-activity = ກິດ​ຈະ​ກໍາ​ທີ່​ຜ່ານ​ມາ
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = ແນະນຳໂດຍ { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = ເລີ່ມການທ່ອງເວັບ ແລະ ພວກເຮົາຈະສະແດງເນື້ອຫາ, ວິດີໂອ ແລະ ຫນ້າອື່ນໆບາງສ່ວນທີ່ທ່ານຫາກໍເຂົ້າໄປເບິງມາ ຫລື ຫາກໍໄດ້ບຸກມາກໄວ້ທີ່ນີ້.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = ທ່ານໄດ້ອ່ານເລື່ອງລາວຄົບທັງຫມົດແລ້ວ. ທ່ານສາມາດກັບມາເບິ່ງເລື່ອງລາວເດັ່ນໄດ້ຈາກ { $provider } ໃນພາຍຫລັງ. ອົດໃຈຖ້າບໍ່ໄດ້ແມ່ນບໍ່? ເລືອກຫົວຂໍ້ຍອດນິຍົມເພື່ອຄົ້ນຫາເລື່ອງລາວທີ່ຍອດຢ້ຽມຈາກເວັບຕ່າງໆ.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ທ່ານໄດ້ອ່ານເລື່ອງລາວຄົບໝົດແລ້ວ
newtab-discovery-empty-section-topstories-content = ກວດເບິ່ງຄືນໃນພາຍຫຼັງສໍາລັບເລື່ອງເພີ່ມເຕີມ.
newtab-discovery-empty-section-topstories-try-again-button = ລອງໃຫມ່ອີກຄັ້ງ
newtab-discovery-empty-section-topstories-loading = ກຳລັງໂຫລດ…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ອຸຍ! ພວກເຮົາເກືອບຈະໂຫລດພາກສ່ວນນີ້, ແຕ່ບໍ່ແມ່ນຂ້ອນຂ້າງ.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = ຫົວຂໍ້ຍອດນິຍົມ:
newtab-pocket-new-topics-title = ຕ້ອງການເລື່ອງເພີ່ມເຕີມບໍ? ເບິ່ງຫົວຂໍ້ຍອດນິຍົມເຫຼົ່ານີ້ຈາກ { -pocket-brand-name }
newtab-pocket-more-recommendations = ຂໍ້ແນະນໍາເພີ່ມເຕີມ
newtab-pocket-learn-more = ຮຽນຮູ້ເພີ່ມເຕີມ
newtab-pocket-cta-button = ຮັບ { -pocket-brand-name }
newtab-pocket-cta-text = ຊ່ວຍບັນທຶກເລື່ອງທີ່ທ່ານຮັກໃນ { -pocket-brand-name }, ແລະນ້ໍາໃຈຂອງທ່ານກັບອ່ານທີ່ຫນ້າສົນໃຈ.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } ແມ່ນສ່ວນໜຶ່ງຂອງຄອບຄົວ { -brand-product-name }
newtab-pocket-save = ບັນທຶກ
newtab-pocket-saved = ບັນທຶກແລ້ວ

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = ຄົ້ນພົບສິ່ງທີ່ດີທີ່ສຸດຂອງເວັບ
newtab-pocket-onboarding-cta = { -pocket-brand-name } ສຳຫຼວດຄວາມຫຼາກຫຼາຍຂອງສິ່ງພິມຕ່າງໆເພື່ອນຳເອົາເນື້ອຫາໃຫ້ຂໍ້ມູນ, ເປັນແຮງບັນດານໃຈ, ແລະ ເຊື່ອຖືໄດ້ຫຼາຍທີ່ສຸດໃຫ້ກັບຕົວທ່ອງເວັບ { -brand-product-name } ຂອງທ່ານ.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = ໂອ້ຍ, ມີບາງສິ່ງບາງຢ່າງຜິດພາດໃນການໂຫລດເນື້ອຫານີ້.
newtab-error-fallback-refresh-link = ຟື້ນຟູໜ້າເພື່ອລອງອີກຄັ້ງ.

## Customization Menu

newtab-custom-shortcuts-title = ທາງລັດ
newtab-custom-shortcuts-subtitle = ເວັບໄຊທທີ່ທ່ານໄດ້ບັນທຶກໄວ້ ຫລື ເຂົ້າໄປເບິງມາ
newtab-custom-shortcuts-toggle =
    .label = ທາງລັດ
    .description = ເວັບໄຊທທີ່ທ່ານໄດ້ບັນທຶກໄວ້ ຫລື ເຂົ້າໄປເບິງມາ
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector = { $num } ແຖວ
newtab-custom-sponsored-sites = ທາງລັດສະປອນເຊີ
newtab-custom-pocket-title = ແນະນຳໂດຍ { -pocket-brand-name }
newtab-custom-pocket-subtitle = ເນື້ອຫາພິເສດທີ່ຄັດສັນໂດຍ { -pocket-brand-name }, ສ່ວນຫນຶ່ງຂອງຄອບຄົວ { -brand-product-name }
newtab-custom-pocket-sponsored = ເລື່ອງລາວທີ່ໄດ້ຮັບການສະຫນັບສະຫນູນ
newtab-custom-pocket-show-recent-saves = ສະແດງບັນທຶກຫຼ້າສຸດ
newtab-custom-recent-title = ກິດ​ຈະ​ກໍາ​ທີ່​ຜ່ານ​ມາ
newtab-custom-recent-subtitle = ການເລືອກເວັບໄຊ ແລະເນື້ອຫາຫຼ້າສຸດ
newtab-custom-recent-toggle =
    .label = ກິດ​ຈະ​ກໍາ​ທີ່​ຜ່ານ​ມາ
    .description = ການເລືອກເວັບໄຊ ແລະເນື້ອຫາຫຼ້າສຸດ
newtab-custom-close-button = ປິດ
newtab-custom-settings = ຈັດການການຕັ້ງຄ່າເພີ່ມເຕີມ

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

