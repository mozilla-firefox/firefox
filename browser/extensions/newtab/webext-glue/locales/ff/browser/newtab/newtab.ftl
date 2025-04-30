# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Tabbere hesere
newtab-settings-button =
    .title = Neɗɗin tabbere maa hello hesere ndee

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Yiylo
    .aria-label = Yiylo

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Ɓeydu yiylorde
newtab-topsites-edit-topsites-header = Taƴto Lowre Dowrowre
newtab-topsites-title-label = Tiitoonde
newtab-topsites-title-input =
    .placeholder = Naatnu tiitoonde
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Tappu walla ɗakku URL
newtab-topsites-url-validation = URL Moƴƴo ina naamnaa
newtab-topsites-image-url-label = Neɗɗin ngal natal URL
newtab-topsites-use-image-link = Huutoro natal neɗɗinangal…
newtab-topsites-image-validation = loowgol natal gallii. Enndir URL goɗɗo.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Haaytu
newtab-topsites-delete-history-button = Momtu e daartol
newtab-topsites-save-button = Daɗndu
newtab-topsites-preview-button = Sooyno
newtab-topsites-add-button = Ɓeydu

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Aɗa yananaa yiɗde momtude kala cilol ngoo hello e to aslol maa?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ngal baɗal waawaa firteede.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Uddit cuɓirgol
    .aria-label = Uddit cuɓirgol
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Momtu
    .aria-label = Momtu
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Uddit cuɓirgol
    .aria-label = Uddit dosol ngonka wonande { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Taƴto ndee lowre
    .aria-label = Taƴto ndee lowre

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Taƴto
newtab-menu-open-new-window = Uddit e Henorde Hesere
newtab-menu-open-new-private-window = Uddit e Henorde Suturo Hesere
newtab-menu-dismiss = Salo
newtab-menu-pin = Ñippu
newtab-menu-unpin = Ñippit
newtab-menu-delete-history = Momtu e daartol
newtab-menu-save-to-pocket = Danndu e { -pocket-brand-name }
newtab-menu-delete-pocket = Momtu e { -pocket-brand-name }
newtab-menu-archive-pocket = Mooftu nder { -pocket-brand-name }
newtab-menu-show-privacy-info = Tammbiiɓe min & suturo mon

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Gasii
newtab-privacy-modal-header = Suturo maa ko ngoƴa amen.
newtab-privacy-modal-link = Humpito hol no suturo yahrata e tabbere hesere ndee

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Momtu maanto ɗerewol
# Bookmark is a verb here.
newtab-menu-bookmark = Maanto ɗerewol

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Natto jokkorde awtaande ndee
newtab-menu-go-to-download-page = Yah to hello awtaango ngoo
newtab-menu-remove-download = Momtu ɗum e kewol hee

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Hollit e Finder
       *[other] Uddit loowdi doosiyee
    }
newtab-menu-open-file = Uddit Fiilde

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Yillaama
newtab-label-bookmarked = Maantoraama
newtab-label-removed-bookmark = Maantorol motaama
newtab-label-recommended = Jolɗum
newtab-label-saved = Danndaama e { -pocket-brand-name }
newtab-label-download = Awtaama
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Tammbaama
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = E ballal { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } hoj

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Momtu taƴre
newtab-section-menu-collapse-section = Renndin taƴe
newtab-section-menu-expand-section = Yaajtin taƴre
newtab-section-menu-manage-section = Feewnu taƴre
newtab-section-menu-manage-webext = Yiil Timmitere
newtab-section-menu-add-topsite = Ɓeydu lowre rowrowre
newtab-section-menu-add-search-engine = Ɓeydu yiylorde
newtab-section-menu-move-up = Dirtin dow
newtab-section-menu-move-down = Dirtin les
newtab-section-menu-privacy-notice = Tintinal sirlu

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Famɗin taƴre
newtab-section-expand-section-label =
    .aria-label = Yaajtin taƴre

## Section Headers.

newtab-section-header-topsites = Lowe dowrowe
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Waggini ɗum ko { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Fuɗɗo wanngaade, min kolloymaa huunde e binndanɗe mawɗe ɗee, widewooji kañum e kelle goɗɗe ɗe njilliɗaa ko ɓooyaani walla maantoraaɗe ɗoo.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Rutto ɗoo goɗngol ngam daari goɗɗi { $provider }. A waawaa fadde ? Suvo tiitoonde lollunde ngam yiytude e geese hee daari goɗɗi.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = A heɓtaama!
newtab-discovery-empty-section-topstories-content = Ƴeewto so ɓooyii ngam daarti goɗɗi.
newtab-discovery-empty-section-topstories-try-again-button = Eto goɗngol
newtab-discovery-empty-section-topstories-loading = Nana loowa…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ndoo! Ina wayi no min loowi ndee yamre, kono yonaani!

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Loowdiiji lolluɗi:
newtab-pocket-more-recommendations = Wasiyaaji goɗɗi
newtab-pocket-learn-more = Ɓeydu humpito
newtab-pocket-cta-button = Heɓ { -pocket-brand-name }
newtab-pocket-cta-text = Hisnu daari njiɗ-ɗaa nder { -pocket-brand-name }, ñikliraa hakkille maa taro welngo.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Uf, saɗeende kewii e loowgol ngoo loowdi.
newtab-error-fallback-refresh-link = Wultin hello ngoo ngam ennditde.

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

