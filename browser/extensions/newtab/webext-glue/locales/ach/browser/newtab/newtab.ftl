# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Dirica matidi manyen
newtab-settings-button =
    .title = Yub potbuk me dirica matidi mamegi manyen

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Yeny
    .aria-label = Yeny

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Med ingin me yeny
newtab-topsites-edit-topsites-header = Yub Kakube maloyo
newtab-topsites-title-label = Wiye madit
newtab-topsites-title-input =
    .placeholder = Ket wiye
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Coo onyo mwon URL
newtab-topsites-url-validation = URL ma tye atir mite
newtab-topsites-image-url-label = URL me cal ma kiyubo
newtab-topsites-use-image-link = Tii ki cal ma kiyubo…
newtab-topsites-image-validation = Cano cal pe olare. Tem URL mukene.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Kwer
newtab-topsites-delete-history-button = Kwany ki ii gin mukato
newtab-topsites-save-button = Gwoki
newtab-topsites-preview-button = Nen
newtab-topsites-add-button = Medi

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Imoko ni imito kwanyo nyig jami weng me potbuk man ki i gin mukato mamegi?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Pe ki twero gonyo tic man.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Yab jami ayera
    .aria-label = Yab jami ayera
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Kwany
    .aria-label = Kwany
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Yab jami ayera
    .aria-label = Yab jami ayera pi { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Yub kakube man
    .aria-label = Yub kakube man

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Yubi
newtab-menu-open-new-window = Yab i dirica manyen
newtab-menu-open-new-private-window = Yab i dirica manyen me mung
newtab-menu-dismiss = Kwer
newtab-menu-pin = Mwon
newtab-menu-unpin = War
newtab-menu-delete-history = Kwany ki ii gin mukato
newtab-menu-save-to-pocket = Gwok i { -pocket-brand-name }
newtab-menu-delete-pocket = Kwany ki ii { -pocket-brand-name }
newtab-menu-archive-pocket = Kan i { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Otum
newtab-privacy-modal-link = Nong ngec ikit ma mung tiyo kwede i dirica matidi manyen

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Kwany alamabuk
# Bookmark is a verb here.
newtab-menu-bookmark = Alamabuk

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Lok Kakube me Gam
newtab-menu-go-to-download-page = Cit i Potbuk me Gam
newtab-menu-remove-download = Kwany ki i Gin mukato

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Nyut i Gin nongo
       *[other] Yab boc manonge iyie
    }
newtab-menu-open-file = Yab Pwail

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Kilimo
newtab-label-bookmarked = Kiketo alamabuk
newtab-label-removed-bookmark = Kikwanyo alamabuk
newtab-label-recommended = Ma cuke lamal
newtab-label-saved = Kigwoko i { -pocket-brand-name }
newtab-label-download = Ki gamo

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Kwany bute
newtab-section-menu-collapse-section = Kan bute
newtab-section-menu-expand-section = Yar bute
newtab-section-menu-manage-section = Lo bute
newtab-section-menu-manage-webext = Lo Lamed
newtab-section-menu-add-topsite = Med Kakube maloyo
newtab-section-menu-add-search-engine = Med ingin me yeny
newtab-section-menu-move-up = Kob Malo
newtab-section-menu-move-down = Kob Piny
newtab-section-menu-privacy-notice = Ngec me mung

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Kan Bute
newtab-section-expand-section-label =
    .aria-label = Yar Bute

## Section Headers.

newtab-section-header-topsites = Kakube maloyo
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Lami tam obedo { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Cak yeny, ka wa binyuto coc akwana mabeco, video, ki potbuk mukene ma ilimo cokcokki onyo ma kiketo alamabuk kany.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ityeko weng. Rot doki lacen pi lok madito mapol ki bot { $provider }. Pe itwero kuro? Yer lok macuke lamal me nongo lok mabeco mapol ki i but kakube.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Ityeko woko weng!
newtab-discovery-empty-section-topstories-content = Rot doki lacen pi lok mapol.
newtab-discovery-empty-section-topstories-try-again-button = Tem Doki
newtab-discovery-empty-section-topstories-loading = Tye ka cano…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oops! Manok kono onongo wa cano bute man, ento pe weng.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Lok macuk gi lamal:
newtab-pocket-learn-more = Nong ngec mapol
newtab-pocket-cta-button = Nong { -pocket-brand-name }
newtab-pocket-cta-text = Gwok lok ma imaro ii { -pocket-brand-name }, ka i pik wii ki jami me akwana ma mako wii.
newtab-pocket-save = Gwoki
newtab-pocket-saved = Kigwoko

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Aii, gin mo otime marac i cano jami man.
newtab-error-fallback-refresh-link = Nwo cano potbuk me temo odoco.

## Customization Menu

newtab-custom-close-button = Lor

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

