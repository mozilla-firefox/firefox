# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Ak' ika'ye'
newtab-settings-button =
    .title = B'an tuch ak' xaj u'uje' tu k'uchb'al tetze'

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Chuka
    .aria-label = Chuka

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Aq'ku' uma'l chukb'al tetz
newtab-topsites-edit-topsites-header = B'an tuch u atimb'ale'  ve nim atje'.
newtab-topsites-title-label = Ib'ii
newtab-topsites-title-input =
    .placeholder = Aq'ku' ib'ii
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Tz'ib'a o lak' u URL
newtab-topsites-url-validation = Ni sab'el uma'l u b'anla URL
newtab-topsites-image-url-label = Eetz u vatzib'ale'  URL
newtab-topsites-use-image-link = B'anbe' va vatzib'ale'.
newtab-topsites-image-validation = Ye'  ni toleb' ti ijajat u vatzib'ale', b'anb'e kato txumb'al sti.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Ya'sakan
newtab-topsites-delete-history-button = Sojsa unq'a aq'one ve kat a pichu.
newtab-topsites-save-button = Kola
newtab-topsites-preview-button = Il B'axa
newtab-topsites-add-button = Aq'o'ke'

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Yol chit see uva' tuk asojsa el jununil unq'a vee' tu u'uje' vaa' tu vee' chukeltu ve't'e?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Ye' la uch iq'ab'isal u aq'one' vaa.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Jaj u tachul tatine'
    .aria-label = Jaj u tachul tatine'
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Jaj u tachul tatine'
    .aria-label = Jaj u tachul tatine' tetz{ $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = B'an tuch u atimb'ale' vaa.
    .aria-label = B'an tuch u atimb'ale' vaa.

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = B'an tuche'
newtab-menu-open-new-window = Jaj ma't ak tzikab'al.
newtab-menu-open-new-private-window = La jaj tu uma'l ak'a ilb'al
newtab-menu-dismiss = Eesa kan
newtab-menu-pin = PIN
newtab-menu-unpin = Chajpu
newtab-menu-delete-history = Sojsa unq'a vee pich'umal s-an.
newtab-menu-save-to-pocket = kolkan tu{ -pocket-brand-name }
newtab-menu-delete-pocket = Sojsa tetz{ -pocket-brand-name }
newtab-menu-archive-pocket = Kol  u aq'one' tu{ -pocket-brand-name }
newtab-menu-show-privacy-info = Unq'a xa'ole'  ve ni aq'on kulochb'al as tuk  va tiichajile'

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Tzojpisamal
newtab-privacy-modal-header = Meresteel va tiichajile' sqe.
newtab-privacy-modal-link = Chusa' kam la olb'i  a xo'nit  unq'a kame tetz unq'a xo'le'.

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Sojsa u taq'il u aq'one'.
# Bookmark is a verb here.
newtab-menu-bookmark = K'uchb'al tetz

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Eesa ivatzib'al uve ni eq'on kutzan u   aq'one'
newtab-menu-go-to-download-page = Kuch tu atinb'ale' uve' ni teq'ol ku'tzan
newtab-menu-remove-download = Sojsa unq'a vee pichumal s-an

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] K'ucha kat na lej kat
       *[other] Jaj u atinb'ale' uve' at kat
    }
newtab-menu-open-file = Jaj u  aq'one'

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Kat pichuli
newtab-label-bookmarked = Tulaj unq'a texhlale'
newtab-label-removed-bookmark = Sojsamal ve't el unq'a texhlale'
newtab-label-recommended = Achite' ni b'anb'el cheel.
newtab-label-saved = Kat kulpu kan  tu{ -pocket-brand-name }
newtab-label-download = Kat eq'ol kutzan.

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Sojsa el u tanule'
newtab-section-menu-collapse-section = La eeq'otzan tanul
newtab-section-menu-expand-section = Inimb'isal Tanul
newtab-section-menu-manage-section = Il isuuchil tanul
newtab-section-menu-manage-webext = Il isuuchil taq'il
newtab-section-menu-add-topsite = Aq'ku' u pal chit ib'anb'ele'
newtab-section-menu-add-search-engine = Aq'ku' taq'onb'al ti' ichukpe'
newtab-section-menu-move-up = Al ije'e'
newtab-section-menu-move-down = Ok'utzan
newtab-section-menu-privacy-notice = Yol ti' uva' eetz kuxhtu'

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Eq'otzan tanul
newtab-section-expand-section-label =
    .aria-label = Inimb'isal Tanul

## Section Headers.

newtab-section-header-topsites = Uve' pal chit tilpe'
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = A' u { $provider } ni alon uva' la b'anb'eli

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Xe'ten ti' axaane' as la kuk'uch see unq'a b'anla yole', unq'a video as ka'taj u'uj uva' a'n-nal kuxh asajijta as at ve't ku' ti taq'ax texhlal.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = At ku' chit axh sti' junq'ii. la q'aav axh unpajte as la chuk unq'a vee' ilel kan ta'n u { $provider }. Ye' la uch itx'eb'one'? Txaa uma'l txumb'al uva' pal chit tab'ile' as la lej ka'taj yol uva' achveb'al chit tilpe' tu web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Txiyel axh.
newtab-discovery-empty-section-topstories-content = La pich'u xamtel ti' ootzit ka'taj txumb'al
newtab-discovery-empty-section-topstories-try-again-button = B'an unpajte
newtab-discovery-empty-section-topstories-loading = Ile' ni jaje'
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = ups! b'iit kuxh ye' kat oleb' o' ti' ijajpe', as ye' kat oleb' o'

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Unq'a txumb'al uva' pal chit talpe':
newtab-pocket-more-recommendations = Ka'taj txumb'al
newtab-pocket-learn-more = Ootzi ka'te
newtab-pocket-cta-button = La k'ul u { -pocket-brand-name }
newtab-pocket-cta-text = Kol unq'a vee' chukeltu ve't tu { -pocket-brand-name } as aq' chit te va txumb'ale' ti' asik'let unq'a vee' achveb'al chitu'.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = La kuy kupaav, at uma'l kam ye' kat el vitoj tul uva' ni'k teq'o ku'tzan u aq'one'.
newtab-error-fallback-refresh-link = B'an tuch u u'uje' as la q'aavisa unpajte.

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

