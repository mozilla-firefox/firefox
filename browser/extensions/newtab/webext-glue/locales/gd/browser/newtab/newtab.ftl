# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = Taba ùr
newtab-settings-button =
    .title = Gnàthaich duilleag nan tabaichean ùra agad
newtab-personalize-icon-label =
    .title = Gnàthaich an taba ùr
    .aria-label = Gnàthaich an taba ùr
newtab-personalize-dialog-label =
    .aria-label = Gnàthaich

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Lorg
    .aria-label = Lorg
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Dèan lorg le { $engine } no cuir a-steach seòladh
newtab-search-box-handoff-text-no-engine = Dèan lorg no cuir a-steach seòladh
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Dèan lorg le { $engine } no cuir a-steach seòladh
    .title = Dèan lorg le { $engine } no cuir a-steach seòladh
    .aria-label = Dèan lorg le { $engine } no cuir a-steach seòladh
newtab-search-box-handoff-input-no-engine =
    .placeholder = Dèan lorg no cuir a-steach seòladh
    .title = Dèan lorg no cuir a-steach seòladh
    .aria-label = Dèan lorg no cuir a-steach seòladh
newtab-search-box-text = Lorg air an lìon
newtab-search-box-input =
    .placeholder = Lorg air an lìon
    .aria-label = Lorg air an lìon

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Cuir einnsean-luirg ris
newtab-topsites-add-shortcut-header = Ath-ghoirid ùr
newtab-topsites-edit-topsites-header = Deasaich am brod làraich
newtab-topsites-edit-shortcut-header = Deasaich an ath-ghoirid
newtab-topsites-title-label = Tiotal
newtab-topsites-title-input =
    .placeholder = Cuir ainm a-steach
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Sgrìobh URL no cuir fear ann
newtab-topsites-url-validation = Tha feum air URL dligheach
newtab-topsites-image-url-label = URL deilbh gnàthaichte
newtab-topsites-use-image-link = Cleachd dealbh gnàthaichte...
newtab-topsites-image-validation = Dh’fhàillig luchdadh an deilbh. Feuch URL eile.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Sguir dheth
newtab-topsites-delete-history-button = Sguab às an eachdraidh
newtab-topsites-save-button = Sàbhail
newtab-topsites-preview-button = Ro-shealladh
newtab-topsites-add-button = Cuir ris

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = A bheil thu cinnteach gu bheil thu airson gach ionstans na duilleige seo a sguabadh às an eachdraidh agad?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = Cha ghabh seo a neo-dhèanamh.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsairichte

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Fosgail an clàr-taice
    .aria-label = Fosgail an clàr-taice
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Thoir air falbh
    .aria-label = Thoir air falbh
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Fosgail an clàr-taice
    .aria-label = Fosgail an clàr-taice co-theacsail aig { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Deasaich an làrach seo
    .aria-label = Deasaich an làrach seo

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Deasaich
newtab-menu-open-new-window = Fosgail ann an uinneag ùr
newtab-menu-open-new-private-window = Fosgail ann an uinneag phrìobhaideach ùr
newtab-menu-dismiss = Leig seachad
newtab-menu-pin = Prìnich
newtab-menu-unpin = Dì-phrìnich
newtab-menu-delete-history = Sguab às an eachdraidh
newtab-menu-save-to-pocket = Sàbhail ann am { -pocket-brand-name }
newtab-menu-delete-pocket = Air a sguabadh à { -pocket-brand-name }
newtab-menu-archive-pocket = Tasglannaich ann am { -pocket-brand-name }
newtab-menu-show-privacy-info = Na sponsairean againn ⁊ do phrìobhaideachd

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Deiseil
newtab-privacy-modal-button-manage = Stiùirich roghainnean na susbaint sponsairichte
newtab-privacy-modal-header = Tha do phrìobhaideachd cudromach.
newtab-privacy-modal-paragraph-2 =
    A bharrachd air naidheachdan inntinneach, seallaidh sinn susbaint làn-
    sgrùdaichte o sponsairean àraidh dhut. Na gabh dragh, <strong>chan fhalbh an dàta
    brabhsaidh agad am { -brand-product-name } agad fhèin uair sam bith</strong> – chan fhaic sinn e
    agus chan fhaic na sponsairean againn e nas mò.
newtab-privacy-modal-link = Fiosraich mar a dh’obraicheas a’ phrìobhaideachd san taba ùr

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Thoir an comharra-lìn air falbh
# Bookmark is a verb here.
newtab-menu-bookmark = Comharra-lìn

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Dèan lethbhreac dhen cheangal luchdaidh
newtab-menu-go-to-download-page = Tadhail aor duilleag nan luchdaidhean
newtab-menu-remove-download = Thoir air falbh on eachdraidh

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Seall san lorgair
       *[other] Fosgail am pasgan far a bheil e
    }
newtab-menu-open-file = Fosgail faidhle

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Na thadhail thu air
newtab-label-bookmarked = ’Nan comharran-lìn
newtab-label-removed-bookmark = Chaidh an comharra-lìn a thoirt air falbh
newtab-label-recommended = A’ treandadh
newtab-label-saved = Air a shàbhaladh ann am { -pocket-brand-name }
newtab-label-download = Air a luchdadh a-nuas
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsairichte
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = ’Ga sponsaireadh le { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } mion

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Thoir an earrann air falbh
newtab-section-menu-collapse-section = Co-theannaich an earrann
newtab-section-menu-expand-section = Leudaich an earrann
newtab-section-menu-manage-section = Stiùirich an earrann
newtab-section-menu-manage-webext = Stiùirich an leudachan
newtab-section-menu-add-topsite = Cuir ris brod làraich
newtab-section-menu-add-search-engine = Cuir einnsean-luirg ris
newtab-section-menu-move-up = Gluais suas
newtab-section-menu-move-down = Gluais sìos
newtab-section-menu-privacy-notice = Sanas prìobhaideachd

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = Co-theannaich an earrann
newtab-section-expand-section-label =
    .aria-label = Leudaich an earrann

## Section Headers.

newtab-section-header-topsites = Brod nan làrach
newtab-section-header-recent-activity = Gnìomhachd o chionn goirid
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = ’Ga mholadh le { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Tòisich air brabhsadh is seallaidh sinn dhut an-seo cuid dhe na h-artaigilean, videothan is duilleagan eile air an do thadhail thu no a chuir thu ris na comharran-lìn o chionn goirid.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Sin na naidheachdan uile o { $provider } an-dràsta ach bidh barrachd ann a dh’aithghearr. No thoir sùil air cuspair air a bheil fèill mhòr is leugh na tha a’ dol mun cuairt air an lìon an-dràsta.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Chan eil naidheachd eile ann!
newtab-discovery-empty-section-topstories-content = Till an-seo an ceann greis airson barrachd naidheachdan.
newtab-discovery-empty-section-topstories-try-again-button = Feuch ris a-rithist
newtab-discovery-empty-section-topstories-loading = ’Ga luchdadh…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Ìoc, tha sinn cha mhòr air an earrann seo a luchdadh ach chan ann buileach fhathast.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Cuspairean fèillmhor:
newtab-pocket-new-topics-title = A bheil thu ag iarraidh fiù barrachd? Thoir sùil air na cuspairean seo o { -pocket-brand-name } air a bheil fèill mhòr
newtab-pocket-more-recommendations = Barrachd mholaidhean
newtab-pocket-learn-more = Barrachd fiosrachaidh
newtab-pocket-cta-button = Faigh { -pocket-brand-name }
newtab-pocket-cta-text = Sàbhail na sgeulachdan as fhearr leat ann am { -pocket-brand-name } is faigh toileachas inntinn san leughadh.
newtab-pocket-pocket-firefox-family = Tha { -pocket-brand-name } ’na phàirt de theaghlach bathar { -brand-product-name }
newtab-pocket-save = Sàbhail
newtab-pocket-saved = Air a shàbhaladh

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Faigh lorg air brod an lìn
newtab-pocket-onboarding-cta = Rùraichidh { -pocket-brand-name } raon farsaing de dh’fhoillseachaidhean airson an t-susbaint as fiosrachaile, as brosnachaile agus as earbsaiche a chur ri do làimh an-seo ann am brabhsair { -brand-product-name } agad fhèin.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Ìoc, chaidh rudeigin cearr fhad ’s a bha sinn a’ luchdadh na susbaint seo.
newtab-error-fallback-refresh-link = Ath-nuadhaich an duilleag airson fheuchainn ris a-rithist.

## Customization Menu

newtab-custom-shortcuts-title = Ath-ghoiridean
newtab-custom-shortcuts-subtitle = Làraichean a shàbhail thu no a thadhail thu orra
newtab-custom-shortcuts-toggle =
    .label = Ath-ghoiridean
    .description = Làraichean a shàbhail thu no a thadhail thu orra
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } ràgh
        [two] { $num } ràgh
        [few] { $num } ràghan
       *[other] { $num } ràgh
    }
newtab-custom-sponsored-sites = Ath-ghoiridean sponsairichte
newtab-custom-pocket-title = ’Ga mholadh le { -pocket-brand-name }
newtab-custom-pocket-subtitle = Sàr-shusbaint ’ga thasgadh le { -pocket-brand-name } mar phàirt de theaghlach { -brand-product-name }
newtab-custom-pocket-sponsored = Sgeulachdan sponsairichte
newtab-custom-pocket-show-recent-saves = Seall na chaidh a shàbhaladh o chionn goird
newtab-custom-recent-title = Gnìomhachd o chionn goirid
newtab-custom-recent-subtitle = Roghainn de làraichean is susbaint faisg ort
newtab-custom-recent-toggle =
    .label = Gnìomhachd o chionn goirid
    .description = Roghainn de làraichean is susbaint faisg ort
newtab-custom-close-button = Dùin
newtab-custom-settings = Stiùirich barrachd roghainnean

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

