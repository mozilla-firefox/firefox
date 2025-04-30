# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = K'ak'a' Ruwi'
newtab-settings-button =
    .title = Tawichinaj ri ruxaq richin K'ak'a' Ruwi'
newtab-personalize-icon-label =
    .title = Tichinäx k'ak'a' ruwi'
    .aria-label = Tichinäx k'ak'a' ruwi'
newtab-personalize-dialog-label =
    .aria-label = Tichinäx

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Tikanöx
    .aria-label = Tikanöx
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Takanoj pa { $engine } o tatz'ib'aj ri rochochib'al
newtab-search-box-handoff-text-no-engine = Tikanöx chuqa' titz'ib'äx ri ochochib'äl
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Takanoj pa { $engine } o tatz'ib'aj ri rochochib'al
    .title = Takanoj pa { $engine } o tatz'ib'aj ri rochochib'al
    .aria-label = Takanoj pa { $engine } o tatz'ib'aj ri rochochib'al
newtab-search-box-handoff-input-no-engine =
    .placeholder = Tikanöx chuqa' titz'ib'äx ri ochochib'äl
    .title = Tikanöx chuqa' titz'ib'äx ri ochochib'äl
    .aria-label = Tikanöx chuqa' titz'ib'äx ri ochochib'äl
newtab-search-box-text = Tikanöx pan ajk'amaya'l
newtab-search-box-input =
    .placeholder = Tikanöx pan ajk'amaya'l
    .aria-label = Tikanöx pan ajk'amaya'l

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Titz'aqatisäx Kanob'äl
newtab-topsites-add-shortcut-header = K'ak'a' Chojokem
newtab-topsites-edit-topsites-header = Tinuk' re Utziläj Ruxaq K'amaya'l re'
newtab-topsites-edit-shortcut-header = Tinuk' Chojokem
newtab-topsites-title-label = B'i'aj
newtab-topsites-title-input =
    .placeholder = Tatz'ib'aj jun b'i'aj
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Tatz'ib'aj o tatz'ajb'a' jun URL
newtab-topsites-url-validation = Ütz URL k'atzinel
newtab-topsites-image-url-label = Ichinan Ruwachib'al URL
newtab-topsites-use-image-link = Tokisäx jun ichinan ruwachib'al…
newtab-topsites-image-validation = Man xsamajib'ëx ta ri wachib'äl. Titojtob'ëx rik'in jun chik URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Tiq'at
newtab-topsites-delete-history-button = Tiyuj el pa ri Natab'äl
newtab-topsites-save-button = Tiyak
newtab-topsites-preview-button = Nab'ey tzub'al
newtab-topsites-add-button = Titz'aqatisäx

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = ¿La kan nawajo ye'ayüj el ronojel ri kib'eyal re taq ruxaq re' chi kikojol ri anatab'al?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Man tikirel ta nitzolïx re b'anïk.

## Top Sites - Sponsored label

newtab-topsite-sponsored = To'on

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Tijaq k'utsamaj
    .aria-label = Tijaq k'utsamaj
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Tiyuj
    .aria-label = Tiyuj
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Tijaq k'utsamaj
    .aria-label = Tijar ri ruk'utsamaj k'ojlem richin { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Tinuk' re ruxaq k'amaya'l re'
    .aria-label = Tinuk' re ruxaq k'amaya'l re'

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Nuk'b'äl
newtab-menu-open-new-window = Tijaq pa jun K'ak'a' Tzuwäch
newtab-menu-open-new-private-window = Tijaq pa jun K'ak'a' Ichinan Tzuwäch
newtab-menu-dismiss = Tichup ruwäch
newtab-menu-pin = Ximoj
newtab-menu-unpin = Tosq'opïx
newtab-menu-delete-history = Tiyuj el pa ri Natab'äl
newtab-menu-save-to-pocket = Tiyak pa { -pocket-brand-name }
newtab-menu-delete-pocket = Tiyuj el pa { -pocket-brand-name }
newtab-menu-archive-pocket = Tiyak pa { -pocket-brand-name }
newtab-menu-show-privacy-info = Ri e qato'onela' & ri kichinanem

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Xk'is
newtab-privacy-modal-button-manage = Tinuk'samajïx runuk'ulem rupam to'on rub'anikil
newtab-privacy-modal-header = K'o rejqalem ri kichinanem.
newtab-privacy-modal-paragraph-2 =
    Man xa xe ta jeb'ël taq b'anob'äl yeqak'üt, chuqa' yeqak'üt nïm taq etamab'äl.
    nik'on ri rupam kuma ri yeto'on qichin. Man kamayon, <strong>ri rujikomal rutzij awokem pa k'amaya'l
     majub'ey nuya' kan jun ruwachib'al { -brand-product-name }</strong> — man niqatz'ët ta, nita ri yojto'o qichin.
newtab-privacy-modal-link = Tawetamaj rub'eyal nisamäj ri ichinanem pa ri k'ak'a' ruwi'

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Tiyuj el ri yaketal
# Bookmark is a verb here.
newtab-menu-bookmark = Yaketal

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Tiwachib'ëx Ruximonel Qasanïk
newtab-menu-go-to-download-page = Tib'e pa Ruxaq Qasanïk
newtab-menu-remove-download = Tiyuj pa Natab'äl

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Tik'ut pan Finder
       *[other] Tijaq K'wayöl Yakwuj
    }
newtab-menu-open-file = Tijaq Yakb'äl

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Tz'eton
newtab-label-bookmarked = Yaketan
newtab-label-removed-bookmark = Yaketal elesan el
newtab-label-recommended = Rujawaxik
newtab-label-saved = Yakon pa { -pocket-brand-name }
newtab-label-download = Xqasäx
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · To'on
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Xto' rub'anikil ruma { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } ch'uti ramaj

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Tiyuj Tanaj
newtab-section-menu-collapse-section = Tich'utinarisäx Peraj
newtab-section-menu-expand-section = Tirik' Peraj
newtab-section-menu-manage-section = Tinuk'samajïx Peraj
newtab-section-menu-manage-webext = Tinuk'samajïx K'amal
newtab-section-menu-add-topsite = Titz'aqatisäx K'ïy Ruwinaq Ruxaq K'amaya'l
newtab-section-menu-add-search-engine = Titz'aqatisäx Kanob'äl
newtab-section-menu-move-up = Tijotob'äx
newtab-section-menu-move-down = Tiqasäx qa
newtab-section-menu-privacy-notice = Ichinan Na'oj

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Tich'utinarisäx Peraj
newtab-section-expand-section-label =
    .aria-label = Tirik' Peraj

## Section Headers.

newtab-section-header-topsites = Jeb'ël Taq Ruxaq
newtab-section-header-recent-activity = K'ak'a' samaj
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Chilab'en ruma { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Katok pa k'amaya'l richin niqak'üt chawäch jeb'ël taq cholna'oj, taq silowachib'äl, chuqa' ch'aqa' chik taq ruxaq k'a b'a' ke'atz'ët o aya'on kan ketal wawe'.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Xaq'i'. Katzolin chik pe richin ye'ak'ül ri utziläj taq rub'anob'al { $provider }. ¿La man noyob'en ta? Tacha' jun ütz na'oj richin nawïl ch'aqa' chik taq b'anob'äl e k'o chi rij ri ajk'amaya'l.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = ¡Awojqan ronojel!
newtab-discovery-empty-section-topstories-content = K'a xkatzolin pe richin ch'aqa' chik taq b'anob'äl.
newtab-discovery-empty-section-topstories-try-again-button = Titojtob'ëx Chik
newtab-discovery-empty-section-topstories-loading = Nisamäj…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ¡Uy! B'ama nisamajïx re peraj re', jub'a' chik xrajo'.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Nima'q taq Na'oj:
newtab-pocket-new-topics-title = ¿La ye'awajo' ch'aqa' chik natab'äl? Ke'atz'eta' re taq ruwachinel { -pocket-brand-name }
newtab-pocket-more-recommendations = Ch'aqa' chik taq Chilab'enïk
newtab-pocket-learn-more = Tetamäx ch'aqa' chik
newtab-pocket-cta-button = Tik'ul { -pocket-brand-name }
newtab-pocket-cta-text = Ke'ayaka' ri taq b'anob'äl ye'awajo' pa { -pocket-brand-name }, chuqa' taya' ruchuq'a' ajolom kik'in jeb'ël taq sik'inïk.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } jun { -brand-product-name } qach'alal
newtab-pocket-save = Tiyak
newtab-pocket-saved = Xyak

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Tawila' ri rutzil ajk'amaya'l

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Uy, k'o jun itzel xel toq nisamajib'ëx re rupam re'.
newtab-error-fallback-refresh-link = Titzolïx ruxaq richin nitojtob'ëx chik.

## Customization Menu

newtab-custom-shortcuts-title = Chojmin Okem
newtab-custom-shortcuts-subtitle = Taq ruxaq xe'ayäk o xe'atz'ët
newtab-custom-shortcuts-toggle =
    .label = Chojmin Okem
    .description = Taq ruxaq xe'ayäk o xe'atz'ët
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } cholaj
       *[other] { $num } taq cholaj
    }
newtab-custom-sponsored-sites = Xto' chojmin okem
newtab-custom-pocket-title = Chilab'en ruma { -pocket-brand-name }
newtab-custom-pocket-subtitle = Cha'on rupam cha'on ruma { -pocket-brand-name }, rach'ala'ïl { -brand-product-name }
newtab-custom-pocket-sponsored = To'on taq b'anob'äl
newtab-custom-pocket-show-recent-saves = Kek'ut k'ab'a' eyakon
newtab-custom-recent-title = K'ak'a' samaj
newtab-custom-recent-subtitle = Jun cha'on taq ruxaq chuqa' k'ak'a' rupam
newtab-custom-recent-toggle =
    .label = K'ak'a' samaj
    .description = Jun cha'on taq ruxaq chuqa' k'ak'a' rupam
newtab-custom-close-button = Titz'apïx
newtab-custom-settings = Tinuk'samajïx ch'aqa' runuk'ulem

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

