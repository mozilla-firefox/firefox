# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = புதிய கீற்று
newtab-settings-button =
    .title = உங்கள் புதிய கீற்றுப் பக்கத்தை விருப்பமை

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = தேடு
    .aria-label = தேடு

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = தேடுபொறியைச் சேர்
newtab-topsites-edit-topsites-header = முக்கிய தளத்தை தொகு
newtab-topsites-title-label = தலைப்பு
newtab-topsites-title-input =
    .placeholder = தலைப்பை இடு
newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = உள்ளிடு (அ) ஒரு URL ஒட்டு
newtab-topsites-url-validation = சரியான URL தேவை
newtab-topsites-image-url-label = தனிப்பயன் பட URL
newtab-topsites-use-image-link = தனிப்பயன் படத்தை பயன்படுத்தவும்…
newtab-topsites-image-validation = படத்தை ஏற்றுவதில் தோல்வி. வேறு URL ஐ முயற்சிக்கவும்.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = தவிர்
newtab-topsites-delete-history-button = வரலாற்றிலருந்து அழி
newtab-topsites-save-button = சேமி
newtab-topsites-preview-button = முன்தோற்றம்
newtab-topsites-add-button = சேர்

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = இப்பக்கத்தை உங்களின் வரலாற்றிலிருந்து முழுமையாக நீக்க விரும்புகிறீர்களா?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = இச்செயலை மீட்க முடியாது.

## Top Sites - Sponsored label


## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = பட்டியைத் திற
    .aria-label = பட்டியைத் திற
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = பட்டியைத் திற
    .aria-label = { $title } என்பதற்கான உள்ளடக்க பட்டியலைத் திற
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = இத்தளத்தை தொகு
    .aria-label = இத்தளத்தை தொகு

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = தொகு
newtab-menu-open-new-window = ஒரு புதிய சாளரத்தில் திற
newtab-menu-open-new-private-window = ஒரு புதிய கமுக்க சாளரத்தில் திற
newtab-menu-dismiss = வெளியேற்று
newtab-menu-pin = பொருத்து
newtab-menu-unpin = விடுவி
newtab-menu-delete-history = வரலாற்றிலருந்து அழி
newtab-menu-save-to-pocket = { -pocket-brand-name } ல் சேமி
newtab-menu-delete-pocket = { -pocket-brand-name } லிருந்து நீக்கு
newtab-menu-archive-pocket = { -pocket-brand-name } ல் காப்பெடு

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.


##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = புத்தகக்குறியை நீக்கு
# Bookmark is a verb here.
newtab-menu-bookmark = புத்தகக்குறி

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = பதிவிறக்க இணைப்பை நகலெடு
newtab-menu-go-to-download-page = பதிவிறக்க பக்கத்திற்கு செல்
newtab-menu-remove-download = வரலாற்றிலிருந்து நீக்கு

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] தேடலில் காண்பி
       *[other] கோப்பகத்திலிருந்து திற
    }
newtab-menu-open-file = கோப்பைத் திற

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = பார்த்தவை
newtab-label-bookmarked = புத்தகக்குறியிடப்பட்டது
newtab-label-recommended = பிரபலமான
newtab-label-saved = { -pocket-brand-name } ல் சேமிக்கப்பட்டது
newtab-label-download = பதிவிறக்கப்பட்டது

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = பகுதியை நீக்கவும்
newtab-section-menu-collapse-section = பகுதியைச் சுருக்கு
newtab-section-menu-expand-section = பகுதியை விரி
newtab-section-menu-manage-section = பகுதியை நிர்வகி
newtab-section-menu-manage-webext = நீட்சிகளை நிர்வகி
newtab-section-menu-add-topsite = முதன்மை தளத்தைச் சேர்
newtab-section-menu-add-search-engine = தேடுபொறியைச் சேர்
newtab-section-menu-move-up = மேலே நகர்த்து
newtab-section-menu-move-down = கீழே நகர்த்து
newtab-section-menu-privacy-notice = தனியுரிமை அறிவிப்பு

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = பகுதியைச் சுருக்கு
newtab-section-expand-section-label =
    .aria-label = பகுதியை விரி

## Section Headers.

newtab-section-header-topsites = சிறந்த தளங்கள்
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } என்பவரால் பரிந்துரைக்கப்பட்டது

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = உலாவலைத் தொடங்கவும், மேலும் நாங்கள் சில சிறந்த கட்டுரைகள், காணொளிகள், மற்றும் நீங்கள் சமீபத்தில் பார்த்த அல்லது புத்தகக்குறியிட்ட பக்கங்களை இங்கே காட்டுவோம்.
# Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.
# Variables:
#   $provider (string) - Name of the content provider for this section, e.g "Pocket".
newtab-empty-section-topstories = நீங்கள் முடித்துவிட்டீர்கள். { $provider } இலிருந்து கூடுதல் கதைகளுக்கு பின்னர் பாருங்கள். காத்திருக்க முடியவில்லையா? இணையத்திலிருந்து கூடுதலான கதைகளைக் கண்டுபிடிக்க பிரபலமான தலைப்பைத் தேர்ந்தெடுங்கள்.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = நீங்கள் பிடிபட்டீர்கள்!
newtab-discovery-empty-section-topstories-content = மேலும் கதைகளுக்குப் பின்னர் சரிபார்க்கவும்.
newtab-discovery-empty-section-topstories-try-again-button = மீண்டும் முயற்சிக்கவும்
newtab-discovery-empty-section-topstories-loading = ஏற்றுகிறது…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = அச்சச்சோ! நாங்கள் கிட்டத்தட்ட இந்தப் பகுதியை ஏற்றினோம், ஆனால் சரி இல்லை.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = பிரபலமான தலைப்புகள்:
newtab-pocket-more-recommendations = மேலும் பரிந்துரைகள்
newtab-pocket-cta-button = { -pocket-brand-name } ஐ பெறுக
newtab-pocket-cta-text = { -pocket-brand-name } நீங்கள் விரும்பும் கதையைச் சேமித்தால், அதுவே உங்கள் மனதை வெள்ளும் வாசித்தலைத் தரும்.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.


## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = அச்சச்சோ, இந்த உள்ளடக்கத்தை ஏற்றுவதில் ஏதோ தவறு ஏற்பட்டது.
newtab-error-fallback-refresh-link = மீண்டும் முயற்சிக்க பக்கத்தை புதுப்பி.

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

