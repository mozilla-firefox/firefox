# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.


### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = නව පටිත්ත
newtab-settings-button =
    .title = නව පටිත්ත පිටුව අභිරුචිකරණය
newtab-personalize-settings-icon-label =
    .title = නව පටිත්ත පුද්ගලීකරණය
    .aria-label = සැකසුම්
newtab-personalize-icon-label =
    .title = නව පටිත්ත පුද්ගලීකරණය
    .aria-label = නව පටිත්ත පුද්ගලීකරණය
newtab-personalize-dialog-label =
    .aria-label = පුද්ගලීකරණය
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = සොයන්න
    .aria-label = සොයන්න
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = { $engine } සමඟ සොයන්න හෝ ලිපිනය ලියන්න
newtab-search-box-handoff-text-no-engine = සොයන්න හෝ ලිපිනය ලියන්න
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = { $engine } සමඟ සොයන්න හෝ ලිපිනය ලියන්න
    .title = { $engine } සමඟ සොයන්න හෝ ලිපිනය ලියන්න
    .aria-label = { $engine } සමඟ සොයන්න හෝ ලිපිනය ලියන්න
newtab-search-box-handoff-input-no-engine =
    .placeholder = සොයන්න හෝ ලිපිනය ලියන්න
    .title = සොයන්න හෝ ලිපිනය ලියන්න
    .aria-label = සොයන්න හෝ ලිපිනය ලියන්න
newtab-search-box-text = සොයන්න
newtab-search-box-input =
    .placeholder = සොයන්න
    .aria-label = සොයන්න

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = සෙවුම් යන්ත්‍රයක් යොදන්න
newtab-topsites-add-shortcut-header = නව කෙටිමඟ
newtab-topsites-edit-topsites-header = ප්‍රචලිත අඩවිය සංස්කරණය
newtab-topsites-edit-shortcut-header = කෙටිමඟ සංස්කරණය
newtab-topsites-add-shortcut-label = කෙටිමඟක් යොදන්න
newtab-topsites-title-label = සිරැසිය
newtab-topsites-title-input =
    .placeholder = සිරැසියක් යොදන්න
newtab-topsites-url-label = ඒ.ස.නි.
newtab-topsites-url-input =
    .placeholder = ඒ.ස.නි. ලියන්න හෝ අලවන්න
newtab-topsites-url-validation = වලංගු ඒ.ස.නි. අවශ්‍ය වේ
newtab-topsites-image-url-label = අභිරුචි රූපයේ ඒ.ස.නි.
newtab-topsites-use-image-link = අභිරුචි රූපයක් යොදා ගන්න...
newtab-topsites-image-validation = රූපය පූරණයට අසමත් විය. අන් ඒ.ස.නි. බලන්න.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = අවලංගු කරන්න
newtab-topsites-delete-history-button = ඉතිහාසයෙන් මකන්න
newtab-topsites-save-button = සුරකින්න
newtab-topsites-preview-button = පෙරදසුන
newtab-topsites-add-button = එකතු

## Top Sites - Delete history confirmation dialog.

# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = මෙම ක්‍රියාමාර්ගය අප්‍රතිවර්ත්‍යයි.

## Top Sites - Sponsored label

newtab-topsite-sponsored = අනුග්‍රහය ලද

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = වට්ටෝරුව අරින්න
    .aria-label = වට්ටෝරුව අරින්න
# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = ඉවත් කරන්න
    .aria-label = ඉවත් කරන්න
# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = වට්ටෝරුව අරින්න
    .aria-label = { $title } සඳහා සන්දර්භය අරින්න
# Tooltip on an empty topsite box to open the New Top Site dialog.
newtab-menu-topsites-placeholder-tooltip =
    .title = අඩවිය සංස්කරණය
    .aria-label = අඩවිය සංස්කරණය

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = සංස්කරණය
newtab-menu-open-new-window = නව කවුළුවක අරින්න
newtab-menu-open-new-private-window = නව පෞද්. කවුළුවක අරින්න
newtab-menu-dismiss = ඉවතලන්න
newtab-menu-pin = අමුණන්න
newtab-menu-unpin = ගළවන්න
newtab-menu-delete-history = ඉතිහාසයෙන් මකන්න
newtab-menu-save-to-pocket = { -pocket-brand-name } හි සුරකින්න
newtab-menu-delete-pocket = { -pocket-brand-name } වෙතින් මකන්න
newtab-menu-archive-pocket = { -pocket-brand-name } හි සංරක්‍ෂණය
newtab-menu-show-privacy-info = අපගේ අනුග්‍රහකයින් හා ඔබගේ පෞද්ගලිකත්‍වය

## Context menu options for sponsored stories and new ad formats on New Tab.


## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = අහවරයි
newtab-privacy-modal-button-manage = අනුග්‍රහය ලද අන්තර්ගත සැකසුම් කළමනාකරණය
newtab-privacy-modal-header = ඔබගේ පෞද්ගලිකත්‍වය වැදගත්ය.
newtab-privacy-modal-link = රහස්‍යතාව වැඩ කරන අයුරු නව පටිත්තකින් දැනගන්න

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = පොත්යොමුව ඉවත් කරන්න
# Bookmark is a verb here.
newtab-menu-bookmark = පොත්යොමුව

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = බාගැනීමේ සබැඳියේ පිටපතක්
newtab-menu-go-to-download-page = බාගැනීමේ පිටුවට යන්න
newtab-menu-remove-download = ඉතිහාසයෙන් ඉවත් කරන්න

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-open-file = ගොනුව අරින්න

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = ගොඩවැදුණු
newtab-label-bookmarked = පොත්යොමුවකි
newtab-label-removed-bookmark = පොත්යොමුව ඉවත් කළා
newtab-label-recommended = නැඟී එන
newtab-label-saved = { -pocket-brand-name } හි සුරැකිණි
newtab-label-download = බාගත විය
# This string is used in the story cards to indicate sponsored content
# Variables:
#   $sponsorOrSource (string) - The name of a company or their domain
newtab-label-sponsored = { $sponsorOrSource } · අනුග්‍රහය ලද
# This string is used at the bottom of story cards to indicate sponsored content
# Variables:
#   $sponsor (string) - The name of a sponsor
newtab-label-sponsored-by = { $sponsor } මගින් අනුග්‍රහය ලද
# This string is used under the image of story cards to indicate source and time to read
# Variables:
#   $source (string) - The name of a company or their domain
#   $timeToRead (number) - The estimated number of minutes to read this story
newtab-label-source-read-time = { $source } · විනාඩි { $timeToRead }

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-remove-section = කොටස ඉවතලන්න
newtab-section-menu-collapse-section = කොටස හකුළන්න
newtab-section-menu-expand-section = කොටස දිගහරින්න
newtab-section-menu-manage-section = කොටස කළමනාකරණය
newtab-section-menu-manage-webext = දිගුව කළමනාකරණය
newtab-section-menu-add-search-engine = සෙවුම් යන්ත්‍රයක් යොදන්න
newtab-section-menu-move-up = ඉහළට ගෙනයන්න
newtab-section-menu-move-down = පහළට ගෙනයන්න
newtab-section-menu-privacy-notice = රහස්‍යතා දැන්වීම

## Section aria-labels

newtab-section-collapse-section-label =
    .aria-label = කොටස හකුළන්න
newtab-section-expand-section-label =
    .aria-label = කොටස විහිදන්න

## Section Headers.

newtab-section-header-topsites = ප්‍රචලිත අඩවි
newtab-section-header-recent-activity = මෑත ක්‍රියාකාරකම
# Variables:
#   $provider (string) - Name of the corresponding content provider.
newtab-section-header-pocket = { $provider } විසින් නිර්දේශිතයි

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = පිරික්සීම අරඹන්න, ඔබ මෑත දී ගොඩවැදුණු හෝ පොත්යොමු යෙදූ වැදගත් ලිපි, දෘශ්‍යක සහ වෙනත් පිටු කිහිපයක් මෙහි පෙන්වනු ඇත.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-content = තවත් කතා සඳහා පසුව බලන්න.
newtab-discovery-empty-section-topstories-try-again-button = නැවත
newtab-discovery-empty-section-topstories-loading = පූරණය වෙමින්…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = අපොයි! මෙම කොටස මුළුමනින්ම පාහේ පූරණය වී ඇත, නමුත් හරියටම නොවේ.

## Pocket Content Section.

# This is shown at the bottom of the trending stories section and precedes a list of links to popular topics.
newtab-pocket-read-more = ජනප්‍රිය මාතෘකා:
newtab-pocket-more-recommendations = තවත් නිර්දේශ
newtab-pocket-learn-more = තව දැනගන්න
newtab-pocket-cta-button = { -pocket-brand-name } ගන්න
newtab-pocket-pocket-firefox-family = { -pocket-brand-name } යනු { -brand-product-name } පවුලේ කොටසකි
# A save to Pocket button that shows over the card thumbnail on hover.
newtab-pocket-image =
    .aria-label = { -pocket-brand-name }
newtab-pocket-save = සුරකින්න
newtab-pocket-saved = සුරැකිණි

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

newtab-toast-dismiss-button =
    .title = ඉවතලන්න
    .aria-label = ඉවතලන්න

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.


## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = අහෝ, මෙම අන්තර්ගතය පූර්ණයෙදී යම් වරදක් සිදුවිය.
newtab-error-fallback-refresh-link = පිටුව නැවුම් කර බලන්න.

## Customization Menu

newtab-custom-shortcuts-title = කෙටිමං
newtab-custom-shortcuts-subtitle = ඔබ සුරකින හෝ ගොඩවදින අඩවි
newtab-custom-shortcuts-toggle =
    .label = කෙටිමං
    .description = ඔබ සුරකින හෝ ගොඩවදින අඩවි
# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
    { $num ->
        [one] පේළි { $num }
       *[other] පේළි { $num }
    }
newtab-custom-sponsored-sites = අනුග්‍රහය ලද කෙටිමං
newtab-custom-pocket-title = { -pocket-brand-name } වෙතින් නිර්දේශිත
newtab-custom-pocket-sponsored = අනුග්‍රහය ලද කතා
newtab-custom-pocket-show-recent-saves = මෑත සුරැකීම් පෙන්වන්න
newtab-custom-recent-title = මෑත ක්‍රියාකාරකම
newtab-custom-recent-subtitle = මෑත අඩවි සහ අන්තර්ගතවල තේරීමකි
newtab-custom-recent-toggle =
    .label = මෑත ක්‍රියාකාරකම
    .description = මෑත අඩවි සහ අන්තර්ගතවල තේරීමකි
newtab-custom-close-button = වසන්න
newtab-custom-settings = වෙනත් සැකසුම් කළමනාකරණය

## New Tab Wallpapers

newtab-wallpaper-title = බිතුපත්

## Solid Colors

newtab-wallpaper-blue = නිල්
newtab-wallpaper-light-blue = ලා නිල්
newtab-wallpaper-light-purple = ලා දම්
newtab-wallpaper-light-green = ලා කොළ
newtab-wallpaper-green = කොළ
newtab-wallpaper-yellow = කහ
newtab-wallpaper-orange = තැඹිලි
newtab-wallpaper-pink = රෝස
newtab-wallpaper-light-pink = ලා රෝස
newtab-wallpaper-red = රතු

## Abstract


## Celestial

newtab-wallpaper-feature-highlight-button = තේරුණා
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = ඉවතලන්න
    .aria-label = උත්පතනය වසන්න
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## Celestial


## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ අනුග්‍රහය
newtab-weather-menu-change-location = ස්ථානය වෙනස් කරන්න
newtab-weather-change-location-search-input-placeholder =
    .placeholder = ස්ථානයක් සොයන්න
    .aria-label = ස්ථානයක් සොයන්න
newtab-weather-change-location-search-input = ස්ථානයක් සොයන්න
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = සරල
newtab-weather-menu-change-weather-display-simple = සරල දැක්මට මාරු වන්න
newtab-weather-menu-temperature-option-fahrenheit = ෆැරන්හයිට්
newtab-weather-menu-temperature-option-celsius = සෙල්සියස්
newtab-weather-menu-change-temperature-units-fahrenheit = ෆැරන්හයිට් වෙත මාරු වන්න
newtab-weather-menu-change-temperature-units-celsius = සෙල්සියස් වෙත මාරු වන්න
newtab-weather-menu-hide-weather = නව පටිති වල කාලගුණය සඟවන්න
newtab-weather-menu-learn-more = තව දැනගන්න
# This message is shown if user is working offline
newtab-weather-error-not-available = කාලගුණ දත්ත දැනට නොතිබේ.

## Topic Labels


## Topic Selection Modal


## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.


## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-blocked-button = අවහිරයි

## Confirmation modal for blocking a section


## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-blocked-topics = අවහිරයි
