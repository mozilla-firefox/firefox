# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = New Tab
newtab-settings-button =
    .title = Mak yer New Tab page yer ain
newtab-personalize-icon-label =
    .title = Personalise new tab
    .aria-label = Personalise new tab
newtab-personalize-dialog-label =
    .aria-label = Personalise

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Sairch
    .aria-label = Sairch
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Sairch wi { $engine } or inpit address
newtab-search-box-handoff-text-no-engine = Sairch or inpit address
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Sairch wi { $engine } or inpit address
    .title = Sairch wi { $engine } or inpit address
    .aria-label = Sairch wi { $engine } or inpit address
newtab-search-box-handoff-input-no-engine =
    .placeholder = Sairch or inpit address
    .title = Sairch or inpit address
    .aria-label = Sairch or inpit address
newtab-search-box-text = Sairch the wab
newtab-search-box-input =
    .placeholder = Sairch the wab
    .aria-label = Sairch the wab

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Eik On Airt-oot Engine
newtab-topsites-add-shortcut-header = New Shortcut
newtab-topsites-edit-topsites-header = Edit Tap Site
newtab-topsites-edit-shortcut-header = Edit Shortcut
newtab-topsites-title-label = Title
newtab-topsites-title-input =
    .placeholder = Inpit a title
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Type or paste a URL
newtab-topsites-url-validation = Suithfest URL needit
newtab-topsites-image-url-label = Custom Image URL
newtab-topsites-use-image-link = Yaise an image o yer ain...
newtab-topsites-image-validation = Image couldnae load. Try anither URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Stap
newtab-topsites-delete-history-button = Dicht fae Historie
newtab-topsites-save-button = Save
newtab-topsites-preview-button = Preview
newtab-topsites-add-button = Eik on

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Are ye shair ye're wantin tae dicht ilka instance o this page fae yer historie?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = This action cannae be unduin.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsored

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Open menu
    .aria-label = Open menu
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Remuive
    .aria-label = Remuive
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Open menu
    .aria-label = Open context menu fur { $title }
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = Edit this site
    .aria-label = Edit this site

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edit
newtab-menu-open-new-window = Open in a New Windae
newtab-menu-open-new-private-window = Open in a New Private Windae
newtab-menu-dismiss = Dismiss
newtab-menu-pin = Peen
newtab-menu-unpin = Remuive Peen
newtab-menu-delete-history = Dicht fae Historie
newtab-menu-save-to-pocket = Save tae { -pocket-brand-name }
newtab-menu-delete-pocket = Dicht fae { -pocket-brand-name }
newtab-menu-archive-pocket = Archive in { -pocket-brand-name }
newtab-menu-show-privacy-info = Oor sponsors & yer privacy

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Duin
newtab-privacy-modal-button-manage = Manage sponsored content settins
newtab-privacy-modal-header = Yer privacy maitters.
newtab-privacy-modal-paragraph-2 =
    Forby dishin oot the maist by-ordinar stories, we can shaw ye relevant,
    tentily checked-oot content fae selectit sponsors. Dinnae fash, <strong>yer stravaigin
    data nivver leaves yer ain copy o { -brand-product-name }</strong> — we dinnae see it, and oor
    sponsors dinnae either.
newtab-privacy-modal-link = Lairn how privacy wirks on the new tab

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Remuive Buikmerk
# Bookmark is a verb here.
newtab-menu-bookmark = Buikmerk

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copy Doonload Link
newtab-menu-go-to-download-page = Gang Tae Doonload Page
newtab-menu-remove-download = Remuive fae Historie

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Kythe in Finder
       *[other] Open Conteenin Folder
    }
newtab-menu-open-file = Open File

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Veesitit
newtab-label-bookmarked = Buikmerkt
newtab-label-removed-bookmark = Buikmerk remuived
newtab-label-recommended = Trendin
newtab-label-saved = Saved tae { -pocket-brand-name }
newtab-label-download = Doonloadit
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · Sponsored
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = Sponsored by { $sponsor }
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · { $timeToRead } min

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = Remuive Section
newtab-section-menu-collapse-section = In-fauld Section
newtab-section-menu-expand-section = Oot-fauld Section
newtab-section-menu-manage-section = Manage Section
newtab-section-menu-manage-webext = Manage Extension
newtab-section-menu-add-topsite = Eik On Tap Site
newtab-section-menu-add-search-engine = Eik On Airt-oot Engine
newtab-section-menu-move-up = Shift Up
newtab-section-menu-move-down = Shift Doon
newtab-section-menu-privacy-notice = Privacy Notice

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = In-fauld Section
newtab-section-expand-section-label =
    .aria-label = Oot-fauld Section

## Section Headers.

newtab-section-header-topsites = Tap Sites
newtab-section-header-recent-activity = Recent activity
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = Recommendit by { $provider }

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Stert stravaigin, and we'll shaw some o the smashin airticles, videos, and ither pages ye've recently veesitit or buikmerkt here.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = Ye're aw caught up. Check back efter fur mair tap stories fae { $provider }. Cannae wait? Wale a popular topic fur tae find mair smashin stories fae aroond the wab.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = Ye're aw caught up!
newtab-discovery-empty-section-topstories-content = Check back efter for mair stories.
newtab-discovery-empty-section-topstories-try-again-button = Try Aince Mair
newtab-discovery-empty-section-topstories-loading = Loadin…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Och! We'd nearly loadit this section, but it didnae quite happen.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = Popular Topics:
newtab-pocket-more-recommendations = Mair Recommendations
newtab-pocket-learn-more = Lairn mair
newtab-pocket-cta-button = Get { -pocket-brand-name }
newtab-pocket-cta-text = Save the stories ye're intae wi { -pocket-brand-name }, and nourish yer mind wi some wunnerfu reads.
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } is pairt o the { -brand-product-name } faimily

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Och, sowt went wrang when loadin this content.
newtab-error-fallback-refresh-link = Refresh page fur tae gie it anither shottie.

## Customization Menu

newtab-custom-shortcuts-title = Shortcuts
newtab-custom-shortcuts-subtitle = Sites ye save or veesit
newtab-custom-shortcuts-toggle =
    .label = Shortcuts
    .description = Sites ye save or veesit
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] { $num } row
       *[other] { $num } rows
    }
newtab-custom-sponsored-sites = Sponsored shortcuts
newtab-custom-pocket-title = Recommendit by { -pocket-brand-name }
newtab-custom-pocket-subtitle = By-ordinar content pit thegither by { -pocket-brand-name }, pairt o the { -brand-product-name } faimily.
newtab-custom-pocket-sponsored = Sponsored stories
newtab-custom-recent-title = Recent activity
newtab-custom-recent-subtitle = A walin o recent sites and content
newtab-custom-recent-toggle =
    .label = Recent activity
    .description = A walin o recent sites and content
newtab-custom-close-button = Sneck
newtab-custom-settings = Manage mair settins

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

