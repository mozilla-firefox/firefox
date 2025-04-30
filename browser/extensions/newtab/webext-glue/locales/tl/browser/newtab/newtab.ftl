# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Bagong Tab
newtab-settings-button =
    .title = I-customize ang iyong pahina ng Bagong Tab
newtab-personalize-icon-label =
    .title = i-Personalize ang bagong tab
    .aria-label = i-Personalize
newtab-personalize-dialog-label =
    .aria-label = i-Personalize

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Hanapin
    .aria-label = Hanapin
newtab-search-box-handoff-text-no-engine = Maghanap o ilagay ang address
newtab-search-box-handoff-input-no-engine =
    .placeholder = Hanapin o ilagay ang address
    .title = Hanapin o ilagay ang address
    .aria-label = Hanapin o ilagay ang address
newtab-search-box-input =
    .placeholder = Hanapin sa web
    .aria-label = Hanapin sa web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Magdagdag ng Search Engine
newtab-topsites-add-shortcut-header = Bagong Shortcut
newtab-topsites-edit-topsites-header = Baguhin ang Pangunahing Site
newtab-topsites-edit-shortcut-header = I-edit ang Shortcut
newtab-topsites-title-label = Pamagat
newtab-topsites-title-input =
    .placeholder = MAgbigay ng Pamagat
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = I-type o i-paste ang URL
newtab-topsites-url-validation = Wastong URL ang kinakailangan
newtab-topsites-image-url-label = URL ng Custom na Larawan
newtab-topsites-use-image-link = Gumamit ng custom na larawan...
newtab-topsites-image-validation = Nabigo ang pag-load ng larawan. Subukan ang ibang URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Kanselahin
newtab-topsites-delete-history-button = Burahin sa Kasaysayan
newtab-topsites-save-button = i-Save
newtab-topsites-preview-button = I-preview
newtab-topsites-add-button = Idagdag

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Sigurado ka bang gusto mong tanggalin ang bawat pagkakataon ng pahinang ito mula sa iyong kasaysayan?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ang aksyon na ito ay hindi na mababawi.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsored

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Buksan ang menu
    .aria-label = Buksan ang menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Tanggalin
    .aria-label = Tanggalin
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Buksan ang menu
    .aria-label = Buksan ang menu ng konteksto para sa { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Baguhin ang site na ito
    .aria-label = Baguhin ang site na ito

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = I-edit
newtab-menu-open-new-window = Buksan sa isang Bagong Window
newtab-menu-open-new-private-window = Buksan sa Bagong Private Window
newtab-menu-dismiss = Paalisin
newtab-menu-pin = i-Pin
newtab-menu-unpin = I-unpin
newtab-menu-delete-history = Burahin sa Kasaysayan
newtab-menu-save-to-pocket = I-save sa { -pocket-brand-name }
newtab-menu-delete-pocket = I-delete sa { -pocket-brand-name }
newtab-menu-archive-pocket = I-archive sa { -pocket-brand-name }
newtab-menu-show-privacy-info = Ang aming mga suki at iyong privacy

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Tapos na
newtab-privacy-modal-button-manage = I-manage ang mga setting sa sponsored content
newtab-privacy-modal-header = Mahalaga ang iyong privacy.
newtab-privacy-modal-paragraph-2 =
    Bukod sa paghahatid ng magagandang kwento, nagpapakita rin kami ng mga 
    nilalamang inirerekomenda ng piling mga suki. Sinisiguro namin na <strong>ang iyong
    browsing data ay hindi lumalabas mula sa kopya mo ng { -brand-product-name }</strong> — hindi namin ito nakikita,
    kahit mga suki namin.
newtab-privacy-modal-link = Alamin kung paano gumagana ang privacy sa bagong tab

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Alisin ang Bookmark
# Bookmark is a verb here.
newtab-menu-bookmark = Bookmark

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopyahin ang Download Link
newtab-menu-go-to-download-page = Magtungo sa Download Page
newtab-menu-remove-download = Burahin sa Kasaysayan

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Ipakita sa Finder
       *[other] Buksan ang Naglalaman na Folder
    }
newtab-menu-open-file = Buksan ang File

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Binisita
newtab-label-bookmarked = Naka-bookmark
newtab-label-removed-bookmark = Natanggal na ang bookmark
newtab-label-recommended = Trending
newtab-label-saved = Nai-save na sa { -pocket-brand-name }
newtab-label-download = Nai-download na
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsored
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Suportado ng { $sponsor }

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Alisin ang Seksyon
newtab-section-menu-collapse-section = I-collapse ang Seksyon
newtab-section-menu-expand-section = Palawakin ang Seksyon
newtab-section-menu-manage-section = Pamahalaan ang Seksyon
newtab-section-menu-manage-webext = Pamahalaan ang Ekstensyon
newtab-section-menu-add-topsite = Magdagdag ng Nangungunang Site
newtab-section-menu-add-search-engine = Magdagdag ng Search Engine
newtab-section-menu-move-up = Ilipat Pataas
newtab-section-menu-move-down = Ilipat Pababa
newtab-section-menu-privacy-notice = Paunawa sa Pribasiya

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Paliitin ang Bahagi
newtab-section-expand-section-label =
    .aria-label = Palawakin ang Bahagi

## Section Headers.

newtab-section-header-topsites = Mga Pangunahing Site
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Inirekomenda ni { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Magsimulang mag-browse, at ipapakita namin ang ilan sa mga magagandang artikulo, video, at iba pang mga pahina na kamakailan mong binisita o na-bookmark dito.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Nakahabol ka na. Bumalik sa ibang pagkakataon para sa higit pang mga nangungunang kuwento mula sa { $provider }. Hindi makapaghintay? Pumili ng isang tanyag na paksa upang makahanap ng higit pang mahusay na mga kuwento mula sa buong web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Nakapaghabol ka na!
newtab-discovery-empty-section-topstories-content = Tingnan mo uli mamaya baka sakaling may bago.
newtab-discovery-empty-section-topstories-try-again-button = Subukan mo uli
newtab-discovery-empty-section-topstories-loading = Nagloload...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ops! Halos na-load na yung section na to pero parang hindi pa talaga.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Tanyag na mga paksa:
newtab-pocket-more-recommendations = Karagdagang Rekomendasyon
newtab-pocket-learn-more = Alamin
newtab-pocket-cta-button = Kunin ang { -pocket-brand-name }
newtab-pocket-cta-text = I-save sa { -pocket-brand-name } ang mga kwentong iyong nagustuhan, at palawigin ang iyong pagiisip sa mga nakabibighaning babasahin.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oops, may naganap na mali sa paglo-load ng nilalamang ito.
newtab-error-fallback-refresh-link = I-refresh ang pahina para masubukan uli.

## Customization Menu

newtab-custom-shortcuts-title = Mga shortcut
newtab-custom-shortcuts-subtitle = Mga site na nai-save o binibisita mo
newtab-custom-shortcuts-toggle =
    .label = Mga shortcut
    .description = Mga site na nai-save o binibisita mo
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } row
       *[other] { $num } rows
    }
newtab-custom-sponsored-sites = Mga naka-sponsor na shortcut
newtab-custom-pocket-title = Inirerekomenda ng { -pocket-brand-name }
newtab-custom-pocket-subtitle = Natatanging nilalaman na inorganisa ng { -pocket-brand-name }, bahagi ng pamilyang { -brand-product-name }
newtab-custom-recent-title = Kamakailang aktibidad
newtab-custom-recent-subtitle = Isang pagpipilian ng mga kamakailang site at nilalaman
newtab-custom-recent-toggle =
    .label = Kamakailang aktibidad
    .description = Isang pagpipilian ng mga kamakailang site at nilalaman
newtab-custom-close-button = Isara
newtab-custom-settings = Mamahala ng higit pang mga setting

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

