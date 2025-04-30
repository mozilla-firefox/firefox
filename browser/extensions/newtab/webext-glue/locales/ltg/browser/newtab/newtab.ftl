# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Jauna cilne
newtab-settings-button =
    .title = Īstateit sovu jaunas cilnes lopu

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Mekleit
    .aria-label = Mekleit

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Davīnōt mekleitōji
newtab-topsites-edit-topsites-header = Maineit lopu topā
newtab-topsites-title-label = Viersroksts
newtab-topsites-title-input =
    .placeholder = Īvodi viersrokstu
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Īroksti voi īleimej lopas URL
newtab-topsites-url-validation = Napīcīšams korekts URL
newtab-topsites-image-url-label = Jebkaidys biļdis URL
newtab-topsites-use-image-link = Izmontōt cytu biļdi…
newtab-topsites-image-validation = Naizadeve īlōdēt biļdi. Paraugi cytu URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Atceļt
newtab-topsites-delete-history-button = Nūteireit nu viestures
newtab-topsites-save-button = Saglobōt
newtab-topsites-preview-button = Prīkšskatejums
newtab-topsites-add-button = Pīvīnōt

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Voi gribi dzēst vysus itōs lopys īrokstus nu viestures?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Itei ir naatgrīzeniska darbeiba.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Attaiseit izvielni
    .aria-label = Attaiseit izvielni
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Attaiseit izvielni
    .aria-label = Attaiseit izvielni deļ { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Maineit lopu
    .aria-label = Maineit lopu

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Redigeit
newtab-menu-open-new-window = Attaiseit saiti jaunā lūgā
newtab-menu-open-new-private-window = Attaiseit saiti jaunā privātajā lūgā
newtab-menu-dismiss = Paslēpt
newtab-menu-pin = Daspraust
newtab-menu-unpin = Atbreivōt
newtab-menu-delete-history = Nūteireit nu viestures
newtab-menu-save-to-pocket = Saglobōt { -pocket-brand-name }
newtab-menu-delete-pocket = Dzēst nu { -pocket-brand-name }
newtab-menu-archive-pocket = Arhivēt { -pocket-brand-name }

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Izjimt grōmotzeimi
# Bookmark is a verb here.
newtab-menu-bookmark = Grōmotzeime

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Kopēt lejupīlōdes saiti
newtab-menu-go-to-download-page = Īt iz lejupīlōdes lopu
newtab-menu-remove-download = Nūteireit nu viestures

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Parōdēt Finder
       *[other] Attaiseit mapi
    }
newtab-menu-open-file = Attaiseit failu

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Apsavārtys lopys
newtab-label-bookmarked = Saglobōts grōmotzemēs
newtab-label-removed-bookmark = Grōmotzeime izdzāsta
newtab-label-recommended = Populars
newtab-label-saved = Saglobōts { -pocket-brand-name }
newtab-label-download = Nūlōdeits

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Aizvōkt sadaļu
newtab-section-menu-collapse-section = Sakļaut sadaļu
newtab-section-menu-expand-section = Izstīpt sadaļu
newtab-section-menu-manage-section = Porvaldēt sadaļu
newtab-section-menu-manage-webext = Porvaldēt paplašinōjumu
newtab-section-menu-add-topsite = Jauna lopa topā
newtab-section-menu-add-search-engine = Davīnōt mekleitōji
newtab-section-menu-move-up = Porvītōt iz augšu
newtab-section-menu-move-down = Porvītōt iz zamušku
newtab-section-menu-privacy-notice = Privatuma pīzeime

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Sakļaut sadaļu
newtab-section-expand-section-label =
    .aria-label = Izstīpt sadaļu

## Section Headers.

newtab-section-header-topsites = Popularōkōs lopys
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } īsaceitōs

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Sōc porlyukōšonu un mes tev parōdēsim dažus breineigus rokstus, video un cytys lopys, kuras tu naseņ esi skatiejs voi davīnōjs grōmotzeimem.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Esi vysu izlasiejs. Īej vāļōk, kab redzēt vaira ziņu nu { $provider }. Nagribi gaidēt? Izavielej popularu tēmu, kab atrostu vaira interesantu rokstu nu vysa interneta.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Tu vysu izlasieji!
newtab-discovery-empty-section-topstories-content = Pīkōp vāļōk, kab īraudzēt vaira rokstu.
newtab-discovery-empty-section-topstories-try-again-button = Raugi vēļreiz
newtab-discovery-empty-section-topstories-loading = Īlōdej…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Malns! Mes gondreiž īlōdēm itū sadaļu, bet na da gola.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Popularas tēmas:
newtab-pocket-more-recommendations = Vaira īsacejumu
newtab-pocket-cta-button = Paraugi { -pocket-brand-name }
newtab-pocket-cta-text = Sagloboj interesantus stōstus { -pocket-brand-name } un paboroj sovu prōtu ar interesantu losamvīlu.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Naz kas aizgōja škārsu, īlōdejut itū saturu.
newtab-error-fallback-refresh-link = Porlōdej lopu, kab paraudzēt par jaunu.

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

