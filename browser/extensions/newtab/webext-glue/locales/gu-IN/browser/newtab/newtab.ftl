# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = નવી ટૅબ
newtab-settings-button =
    .title = તમારા નવા ટૅબ પૃષ્ઠને કસ્ટમાઇઝ કરો

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = શોધો
    .aria-label = શોધો

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = શોધ એંજીન ઉમેરો
newtab-topsites-edit-topsites-header = ટોચની સાઇટ સંપાદિત કરો
newtab-topsites-title-label = શીર્ષક
newtab-topsites-title-input =
    .placeholder = શીર્ષક દાખલ કરો
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = URL ટાઇપ કરો અથવા પેસ્ટ કરો
newtab-topsites-url-validation = માન્ય URL આવશ્યક છે
newtab-topsites-image-url-label = વૈવિધ્યપૂર્ણ છબી URL
newtab-topsites-use-image-link = વૈવિધ્યપૂર્ણ છબીનો ઉપયોગ કરો…
newtab-topsites-image-validation = છબી લોડ થવામાં નિષ્ફળ. એક અલગ URL અજમાવી જુઓ.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = રદ કરો
newtab-topsites-delete-history-button = ઇતિહાસમાંથી દૂર કરો
newtab-topsites-save-button = સાચવો
newtab-topsites-preview-button = પૂર્વદર્શન
newtab-topsites-add-button = ઉમેરો

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = શું તમે ખરેખર તમારા ઇતિહાસમાંથી આ પૃષ્ઠનાં દરેક ઘટકને કાઢી નાખવા માંગો છો?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = આ ક્રિયા પૂર્વવત્ કરી શકાતી નથી.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = મેનૂ ખોલો
    .aria-label = મેનૂ ખોલો
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = મેનૂ ખોલો
    .aria-label = { $title } માટે સંદર્ભ મેનૂ ખોલો
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = આ સાઇટને સંપાદિત કરો
    .aria-label = આ સાઇટને સંપાદિત કરો

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = ફેરફાર કરો
newtab-menu-open-new-window = નવી વિન્ડોમાં ખોલો
newtab-menu-open-new-private-window = ખાનગી વિન્ડોમાં ખોલો
newtab-menu-dismiss = રદ કરો
newtab-menu-pin = પિન
newtab-menu-unpin = અનપિન
newtab-menu-delete-history = ઇતિહાસમાંથી દૂર કરો
newtab-menu-save-to-pocket = { -pocket-brand-name } માં સાચવો
newtab-menu-delete-pocket = { -pocket-brand-name } માંથી કાઢી નાંખો
newtab-menu-archive-pocket = { -pocket-brand-name } માં સંગ્રહ કરો

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = બુકમાર્ક કાઢો
# Bookmark is a verb here.
newtab-menu-bookmark = બુકમાર્ક

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = ડાઉનલોડ કડીની નકલ કરો
newtab-menu-go-to-download-page = ડાઉનલોડ પૃષ્ઠ પર જાઓ
newtab-menu-remove-download = ઇતિહાસમાંથી દૂર કરો

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] ફાઇન્ડર માં બતાવો
       *[other] સમાવેલ ફોલ્ડર ખોલો
    }
newtab-menu-open-file = ફાઇલ ખોલો

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = જોવામા આવેલ:
newtab-label-bookmarked = બુકમાર્ક્સ
newtab-label-recommended = વલણ
newtab-label-saved = { -pocket-brand-name } પર સાચવ્યું
newtab-label-download = ડાઉનલોડ કરેલું

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = વિભાગ દૂર કરો
newtab-section-menu-collapse-section = વિભાગ સંકુચિત કરો
newtab-section-menu-expand-section = વિભાગ વિસ્તૃત કરો
newtab-section-menu-manage-section = વિભાગ સંચાલિત કરો
newtab-section-menu-manage-webext = એક્સ્ટેંશનનો વહીવટ કરો
newtab-section-menu-add-topsite = ટોચની સાઇટ ઉમેરો
newtab-section-menu-add-search-engine = શોધ એંજીન ઉમેરો
newtab-section-menu-move-up = ઉપર કરો
newtab-section-menu-move-down = નીચે કરો
newtab-section-menu-privacy-notice = ખાનગી સૂચના

## Section aria-labels


## Section Headers.

newtab-section-header-topsites = ટોચની સાઇટ્સ
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } દ્વારા ભલામણ

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = બ્રાઉઝ કરવું પ્રારંભ કરો અને અમે અહીં કેટલાક સરસ લેખો, વિડિઓઝ અને અન્ય પૃષ્ઠો દર્શાવીશું જે તમે તાજેતરમાં મુલાકાત લીધાં છે અથવા બુકમાર્ક કર્યા છે.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = તમે પકડાઈ ગયા છો. { $provider } તરફથી વધુ ટોચની વાતો માટે પછીથી પાછા તપાસો. રાહ નથી જોઈ શકતા? સમગ્ર વેબ પરથી વધુ સુંદર વાર્તાઓ શોધવા માટે એક લોકપ્રિય વિષય પસંદ કરો.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = તમે પકડાયા છો!
newtab-discovery-empty-section-topstories-content = વધુ વાર્તાઓ માટે પાછળથી તપાસો.
newtab-discovery-empty-section-topstories-try-again-button = ફરીથી પ્રયત્ન કરો
newtab-discovery-empty-section-topstories-loading = લોડ કરી રહ્યું છે ...
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = અરે! અમે લગભગ આ વિભાગને લોડ કર્યો છે, પરંતુ તદ્દન નહીં.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = લોકપ્રિય વિષયો:
newtab-pocket-more-recommendations = વધુ ભલામણો
newtab-pocket-cta-button = { -pocket-brand-name } મેળવો
newtab-pocket-cta-text = { -pocket-brand-name } તમને જે કથાઓ ગમે છે તે સાચવો, અને તમારા મનને રસપ્રદ વાંચન સાથે ઉત્તેજિત કરો.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = ઊફ્ફ, આ સામગ્રીને લોડ કરવામાં કંઈક ખોટું થયું.
newtab-error-fallback-refresh-link = ફરી પ્રયાસ કરવા માટે પૃષ્ઠને તાજું કરો.

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

