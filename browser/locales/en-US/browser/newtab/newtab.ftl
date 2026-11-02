# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = New Tab
#  (developer note): @nova-cleanup(remove-string): Remove newtab-customize-panel-icon-button once Nova lands, will be using newtab-customize-panel-label instead
newtab-customize-panel-icon-button =
    .title = Customize this page
#  (developer note): @nova-cleanup(remove-string): Remove newtab-customize-panel-icon-button-label once Nova lands, will be using newtab-customize-panel-label instead
newtab-customize-panel-icon-button-label = Customize
newtab-customize-panel-label =
    .label = Customize
newtab-settings-dialog-label =
    .aria-label = Settings
newtab-logo-and-wordmark =
    .aria-label = { -brand-full-name }

## Search box component.

# "Search" is a verb/action
newtab-search-box-search-button =
    .title = Search
    .aria-label = Search

# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-text = Search with { $engine } or enter address
newtab-search-box-handoff-text-no-engine = Search or enter address
# Variables:
#   $engine (string) - The name of the user's default search engine
newtab-search-box-handoff-input =
    .placeholder = Search with { $engine } or enter address
    .title = Search with { $engine } or enter address
    .aria-label = Search with { $engine } or enter address
newtab-search-box-handoff-input-no-engine =
    .placeholder = Search or enter address
    .title = Search or enter address
    .aria-label = Search or enter address

newtab-search-box-text = Search the web
newtab-search-box-input =
    .placeholder = Search the web
    .aria-label = Search the web

## Top Sites - General form dialog.

newtab-topsites-add-search-engine-header = Add Search Engine
newtab-topsites-add-shortcut-header = New Shortcut
newtab-topsites-edit-shortcut-header = Edit Shortcut
newtab-topsites-add-shortcut-title =
    .title = Add Shortcut
    .aria-label = Add Shortcut
newtab-shortcuts-pinned-area = Pinned Area
newtab-topsites-title-label = Title
newtab-topsites-title-input =
    .placeholder = Enter a title

newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Type or paste a URL
newtab-topsites-url-validation = Valid URL required

## Clear text button for the URL and image URL input fields in the Top Sites form.

newtab-topsites-clear-input =
    .aria-label = Clear text

newtab-topsites-image-url-label = Custom Image URL
newtab-topsites-use-custom-image-link = Use a custom image
newtab-topsites-image-validation = Image failed to load. Try a different URL.

## Top Sites - General form dialog buttons. These are verbs/actions.

newtab-topsites-cancel-button = Cancel
newtab-topsites-delete-history-button = Delete from History
newtab-topsites-save-button = Save
newtab-topsites-preview-button = Preview
newtab-topsites-add-button = Add

## Top Sites - Delete history confirmation dialog.

newtab-confirm-delete-history-p1 = Are you sure you want to delete every instance of this page from your history?
# "This action" refers to deleting a page from history.
newtab-confirm-delete-history-p2 = This action cannot be undone.

## Top Sites - Sponsored label

newtab-topsite-sponsored = Sponsored

## Label used by screen readers for pinned top sites

# Variables:
#   $title (string) - The label or hostname of the site.
topsite-label-pinned =
    .aria-label = { $title } (pinned)
    .title = { $title }

## Top Sites - Web notifications hover card

# Variables:
#   $site (string) - The label or hostname of the site the notifications are from.
newtab-topsites-hover-card-header = Notifications from { $site }

# Relative time shown for a notification posted less than a minute ago.
newtab-topsites-hover-card-just-now = Just now

newtab-topsites-hover-card-mark-all-read =
    .title = Mark all as read
    .aria-label = Mark all as read

newtab-topsites-hover-card-settings =
    .title = Notification settings
    .aria-label = Notification settings

newtab-topsites-hover-card-dismiss =
    .title = Dismiss
    .aria-label = Dismiss

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Open menu
    .aria-label = Open menu

# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Open menu
    .aria-label = Open context menu for { $title }

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edit
newtab-menu-add-topsite = Add New Shortcut
newtab-menu-open-new-window = Open in a New Window
newtab-menu-open-new-private-window = Open in a New Private Window
newtab-menu-dismiss = Dismiss
newtab-menu-pin = Pin
newtab-menu-unpin = Unpin
newtab-menu-delete-history = Delete from History
newtab-menu-show-privacy-info = Our sponsors & your privacy
# Report is a verb (i.e. report issue with the content).
newtab-menu-report = Report
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Block
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow-topic = Unfollow
# Context menu option to open a support page explaining the New Tab personalization features and privacy controls.
newtab-menu-section-learn-more = Learn more

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Manage sponsored content
newtab-menu-our-sponsors-and-your-privacy = Our sponsors and your privacy
newtab-menu-report-this-ad = Report this ad

##

# Bookmark is a noun in this case, "Remove bookmark".
newtab-menu-remove-bookmark = Remove Bookmark
# Bookmark is a verb here.
newtab-menu-bookmark = Bookmark

## Context Menu - Downloaded Menu. "Download" in these cases is not a verb,
## it is a noun. As in, "Copy the link that belongs to this downloaded item".

newtab-menu-copy-download-link = Copy Download Link
newtab-menu-go-to-download-page = Go to Download Page
newtab-menu-remove-download = Remove from History

## Context Menu - Download Menu: These are platform specific strings found in the context menu of an item that has
## been downloaded. The intention behind "this action" is that it will show where the downloaded file exists on the file
## system for each operating system.

newtab-menu-show-file =
    { PLATFORM() ->
        [macos] Show in Finder
       *[other] Open Containing Folder
    }
newtab-menu-open-file = Open File

## Card Labels: These labels are associated to pages to give
## context on how the element is related to the user, e.g. type indicates that
## the page is bookmarked, or is currently open on another device.

newtab-label-visited = Visited
newtab-label-bookmarked = Bookmarked
newtab-label-removed-bookmark = Bookmark removed
newtab-label-recommended = Trending
newtab-label-saved = Saved to { -pocket-brand-name }
newtab-label-download = Downloaded

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

# This string is used under fixed size ads to indicate sponsored content
newtab-label-sponsored-fixed = Sponsored

## Section Menu: These strings are displayed in the section context menu and are
## meant as a call to action for the given section.

newtab-section-menu-privacy-notice = Privacy Notice

## Section Headers.

newtab-section-header-topsites = Top Sites
newtab-section-header-recent-activity = Recent activity
newtab-section-header-stories = Thought-provoking stories
# "picks" refers to recommended articles
newtab-section-header-todays-picks = Today’s picks for you

## Empty Section States: These show when there are no more items in a section. Ex. When there are no more Pocket story recommendations, in the space where there would have been stories, this is shown instead.

newtab-empty-section-highlights = Start browsing, and we’ll show some of the great articles, videos, and other pages you’ve recently visited or bookmarked here.

# Ex. When there are no more story recommendations, in the space where there would have been stories, this is shown instead.
newtab-empty-section-topstories-generic = You’ve caught up. Check back later for more stories. Can’t wait? Select a popular topic to find more great stories from around the web.

## Empty Section (Content Discovery Experience). These show when there are no more stories or when some stories fail to load.

newtab-discovery-empty-section-topstories-header = You are caught up!
newtab-discovery-empty-section-topstories-content = Check back later for more stories.
newtab-discovery-empty-section-topstories-try-again-button = Try again
newtab-discovery-empty-section-topstories-loading = Loading…
# Headline and body shown when stories fail to load, for example when offline or the server can’t be reached.
newtab-discovery-empty-section-topstories-unavailable-header = Stories aren’t available right now
newtab-discovery-empty-section-topstories-unavailable-body = Check your connection or try again later.

## Strings for the story cards carousel

# Identifies the current carousel slide to screen reader users.
# Variables:
#   $index - the position of this slide
#   $total - how many slides there are
newtab-carousel-slide =
    .aria-label = { $index } of { $total }

# Button that goes to the previous carousel slide.
newtab-carousel-previous =
    .aria-label = Previous
# Button that goes to the next carousel slide.
newtab-carousel-next =
    .aria-label = Next
# Button that pauses autoplay on the carousel.
newtab-carousel-pause =
    .aria-label = Pause autoplay
# Button that resumes autoplay on the carousel.
newtab-carousel-play =
    .aria-label = Resume autoplay

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oops, something went wrong loading this content.
newtab-error-fallback-refresh-link = Refresh page to try again.

## Customization Menu

# An arrow button that goes back from a sub-panel in the customize panel, such as a wallpaper category, to the main Customize panel.
newtab-customize-panel-back-button =
    .aria-label = Back to Customize
    .title = Back to Customize

#  (developer note): @nova-cleanup(remove-string): Remove old string once Nova lands. The newtab-custom-shortcuts-nova string will take over
newtab-custom-shortcuts-toggle =
  .label = Shortcuts
  .description = Sites you save or visit

newtab-custom-shortcuts-nova =
  .label = Shortcuts

newtab-custom-web-notifications-toggle =
  .label = Web notifications
  .description = Show notifications from your sites on their shortcuts

newtab-custom-row-description =
  .description = Number of rows
# Variables
#   $num (number) - Number of rows to display
#  (developer note): @nova-cleanup(remove-string): Remove string once Nova lands. We won't be using "row"/"rows" anymore for the dropdown
newtab-custom-row-selector2 =
        .label = { $num ->
            [one] { $num } row
           *[other] { $num } rows
        }
#  (developer note): @nova-cleanup(remove-string): Remove string once Nova lands. We won't be having a description under "Recommended stories" anymore
newtab-custom-stories-toggle =
  .label = Recommended stories
  .description = Exceptional content curated by the { -brand-product-name } family
newtab-recommended-stories-toggle =
  .label = Recommended stories
newtab-custom-stories-personalized-toggle =
  .label = Stories
newtab-custom-stories-personalized-checkbox =
  .label = Personalized stories based on your activity
newtab-custom-weather-toggle =
  .label = Weather
  .description = Today’s forecast at a glance
newtab-custom-widget-weather-toggle =
  .label = Weather
newtab-custom-widget-lists-toggle =
  .label = Lists
newtab-custom-widget-timer-toggle =
  .label = Timer
newtab-custom-widget-clock-toggle =
  .label = Clock
newtab-custom-widget-privacy-toggle =
  .label = Privacy
# Crossword is a widget on New Tab that shows a daily crossword puzzle.
newtab-custom-widget-crossword-toggle =
  .label = Crossword
# Finance is a widget on New Tab that shows stock ticker prices.
newtab-custom-widget-stocks-toggle2 =
  .label = Finance
newtab-custom-widget-picture-toggle =
  .label = Picture of the day
newtab-custom-widget-search-toggle =
  .label = Search
newtab-custom-widget-horoscopes-toggle =
  .label = Horoscopes
# Travel (noun) is a widget on New Tab for searching hotels, flights
# and car rentals.
newtab-custom-widget-travel-toggle =
  .label = Travel
# Solitaire is a widget on New Tab where you can play solitaire, a card game
# for one player, also known as patience.
# Game displays within an iframe with name “Yahoo Solitaire”
newtab-custom-widget-solitaire-toggle =
  .label = Solitaire
newtab-custom-widget-section-title = Widgets
newtab-custom-widget-section-toggle =
    .label = Widgets
newtab-widget-manage-title = Widgets
newtab-widget-manage-widget-button =
    .label = Manage widgets

## New Tab Appearance (browser theme picker)

# Title of the browser theme ("Appearance") section in the customize panel.
newtab-custom-appearance-section-title = Appearance
# Button that opens the full browser theme selection view.
newtab-appearance-more-themes-button =
    .label = See more themes
# Title of the full theme selection sub-panel, also used as its back button label.
newtab-appearance-manage-title = { -brand-product-name } Themes
# Header for the list of the user's already-installed themes in the full theme panel.
newtab-appearance-your-themes-header = Your themes
# Button that enables an already-installed theme.
newtab-appearance-enable-theme-button =
    .label = Enable
# Button that disables the active theme and reverts to the default.
newtab-appearance-disable-theme-button =
    .label = Disable
# Button that installs a theme.
newtab-appearance-install-theme-button =
    .label = Install theme
# Button/link on the full theme panel that opens the complete list of available themes.
newtab-appearance-explore-more-themes-button = Explore more themes

# Tooltip for close button
newtab-custom-close-menu-button =
    .title = Close
    .aria-label = Close menu
newtab-custom-settings = Manage more settings

## New Tab Wallpapers

#  (developer note): @nova-cleanup(remove-string): Remove old "Wallpapers" heading string once Nova lands. The newtab-wallpaper-toggle-title string will take over
newtab-wallpaper-title = Wallpapers
#  (developer note): @nova-cleanup(remove-string): Remove string once Nova lands. Nova has no reset button; the wallpapers toggle handles reset
newtab-wallpaper-reset = Reset to default
#  (developer note): @nova-cleanup(remove-string): Remove old "Upload an image" string once Nova lands. The new "Add an image"  string will take over
newtab-wallpaper-upload-image = Upload an image
newtab-wallpaper-add-an-image = Add an image
newtab-wallpaper-custom-color = Choose a color
newtab-wallpaper-toggle-title =
    .label = Wallpapers
# Label for the grid of wallpaper categories in the customize panel
newtab-wallpaper-category-list =
    .aria-label = Wallpaper categories
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = The image exceeded the file size limit of { $file_size }MB. Please try uploading a smaller file.
newtab-wallpaper-error-upload-file-type = We couldn’t upload your file. Please try again with an image file.
newtab-wallpaper-light-red-panda = Red panda
newtab-wallpaper-light-mountain = White mountain
newtab-wallpaper-light-sky = Sky with purple and pink clouds
newtab-wallpaper-light-color = Blue, pink and yellow shapes
newtab-wallpaper-light-landscape = Blue mist mountain landscape
newtab-wallpaper-light-beach = Beach with palm tree
newtab-wallpaper-dark-aurora = Aurora Borealis
newtab-wallpaper-dark-color = Red and blue shapes
newtab-wallpaper-dark-panda = Red panda hidden in forest
newtab-wallpaper-dark-sky = City landscape with a night sky
newtab-wallpaper-dark-mountain = Landscape mountain
newtab-wallpaper-dark-city = Purple city landscape
newtab-wallpaper-dark-fox-anniversary = A fox on the pavement near a forest
newtab-wallpaper-light-fox-anniversary = A fox in a grassy field with a misty mountain landscape

## "Your images" is the folder of wallpapers someone has saved. A saved wallpaper
## can be a file they uploaded, a Picture of the Day they chose to keep, or a
## Firefox wallpaper kept for them when it was retired.

newtab-wallpaper-your-images = Your images
# Accessible name for the tile that opens the "Your images" folder in the
# wallpaper picker. The tile shows one of the saved images and has no text of
# its own, so this is all a screen reader has to go on.
newtab-wallpaper-your-images-folder =
    .aria-label = Your images, wallpapers that you have saved
# Read by screen readers for a saved image that has a name of its own: a kept
# Picture of the Day, or a Firefox wallpaper kept when it was retired. An image
# someone added themselves is numbered instead, see the string below.
# Variables:
#   $name (string) - The picture's own title, or the Firefox wallpaper's name
newtab-wallpaper-your-images-item = { $name }
# Read by screen readers for an image someone added themselves. Firefox counts
# these as they are saved rather than keeping the name of their file.
# Variables:
#   $number (number) - Which saved image this is, counting from one
newtab-wallpaper-your-images-item-numbered = Image { $number }
# Each saved image has its own remove button. The tooltip stays short because
# a name can be long and some locales put it before the verb, which would push
# "remove" out of view. The full name is on the label a screen reader reads.
# Variables:
#   $name (string) - The picture's own title, or the Firefox wallpaper's name
newtab-wallpaper-remove-image =
    .title = Remove image
    .aria-label = Remove { $name }
# The remove button for an image someone added themselves. .title is the
# tooltip and .aria-label is what a screen reader reads.
# Variables:
#   $number (number) - Which saved image this is, counting from one
newtab-wallpaper-remove-image-numbered =
    .title = Remove image { $number }
    .aria-label = Remove image { $number }
newtab-wallpaper-remove-image-title = Remove image?
# "This action" refers to removing a saved wallpaper image.
newtab-wallpaper-remove-image-body = This action cannot be undone.
newtab-wallpaper-remove-image-confirm = Remove
newtab-wallpaper-remove-image-cancel = Cancel

## Solid Colors

#  (developer note): @nova-cleanup(remove-string): Remove old "Solid colors" string once Nova lands. The simplified "Colors" string will take over
newtab-wallpaper-category-title-colors = Solid colors
newtab-wallpaper-colors = Colors
newtab-wallpaper-blue = Blue
newtab-wallpaper-light-blue = Light blue
newtab-wallpaper-light-purple = Light purple
newtab-wallpaper-light-green = Light green
newtab-wallpaper-green = Green
newtab-wallpaper-beige = Beige
newtab-wallpaper-yellow = Yellow
newtab-wallpaper-orange = Orange
newtab-wallpaper-pink = Pink
newtab-wallpaper-light-pink = Light pink
newtab-wallpaper-red = Red
newtab-wallpaper-dark-blue = Dark blue
newtab-wallpaper-dark-purple = Dark purple
newtab-wallpaper-dark-green = Dark green
newtab-wallpaper-brown = Brown

## Abstract

newtab-wallpaper-category-title-abstract = Abstract
newtab-wallpaper-abstract-green = Green shapes
newtab-wallpaper-abstract-blue = Blue shapes
newtab-wallpaper-abstract-purple = Purple shapes
newtab-wallpaper-abstract-orange = Orange shapes
newtab-wallpaper-gradient-orange = Gradient orange and pink
newtab-wallpaper-abstract-blue-purple = Blue and purple shapes
newtab-wallpaper-abstract-white-curves = White with shaded curves
newtab-wallpaper-abstract-purple-green = Purple and green light gradient
newtab-wallpaper-abstract-blue-purple-waves = Blue and purple wavy shapes
newtab-wallpaper-abstract-black-waves = Black wavy shapes

## Photographs

newtab-wallpaper-category-title-photographs = Photographs
newtab-wallpaper-beach-at-sunrise = Beach at sunrise
newtab-wallpaper-beach-at-sunset = Beach at sunset
newtab-wallpaper-storm-sky = Storm sky
newtab-wallpaper-sky-with-pink-clouds = Sky with pink clouds
newtab-wallpaper-red-panda-yawns-in-a-tree = Red panda yawns in a tree
newtab-wallpaper-white-mountains = White mountains
newtab-wallpaper-hot-air-balloons = Assorted color of hot air balloons during daytime
newtab-wallpaper-starry-canyon = Blue starry night
newtab-wallpaper-suspension-bridge = Grey full-suspension bridge photography during daytime
newtab-wallpaper-sand-dunes = White sand dunes
newtab-wallpaper-palm-trees = Silhouette of coconut palm trees during golden hour
newtab-wallpaper-blue-flowers = Closeup photography of blue-petaled flowers in bloom

## Celestial

# “Celestial” referring to astronomy; positioned in or relating to the sky,
# or outer space as observed in astronomy.
# Not to be confused with religious definition of the word.
newtab-wallpaper-category-title-celestial = Celestial
newtab-wallpaper-celestial-lunar-eclipse = Lunar eclipse
newtab-wallpaper-celestial-earth-night = Night photo from low Earth orbit
newtab-wallpaper-celestial-starry-sky = Starry sky
newtab-wallpaper-celestial-eclipse-time-lapse = Lunar eclipse time lapse
newtab-wallpaper-celestial-black-hole = Black hole galaxy illustration
newtab-wallpaper-celestial-river = Satellite image of river

## Firefox

newtab-wallpaper-category-title-firefox = { -brand-product-name }

## Firefox wallpaper descriptions used for screen readers

newtab-wallpaper-firefox-orange = A fox at the right edge on an orange background
newtab-wallpaper-firefox-colorful-sky = Waves of orange across a purple night sky
newtab-wallpaper-firefox-desert-dark = A fox sitting in a dark purple desert
newtab-wallpaper-firefox-desert-light = A fox running across a light desert
newtab-wallpaper-firefox-hills-dark = A fox running over dark hills
newtab-wallpaper-firefox-hills-light = A fox running over light hills
newtab-wallpaper-firefox-tail-dark = A fox’s tail on a dark background
newtab-wallpaper-firefox-tail-light = A fox’s tail on a light background
newtab-wallpaper-firefox-side-kit-dark = A fox on the left side, on a dark background
newtab-wallpaper-firefox-side-kit-light = A fox on the left side, on a light background
newtab-wallpaper-firefox-sitting-hill-dark = A fox sitting on dark purple hills
newtab-wallpaper-firefox-sitting-hill-light = A fox sitting on light hills
newtab-wallpaper-firefox-peak-dark = A fox’s face at the left edge, on a dark background
newtab-wallpaper-firefox-peak-light = A fox’s face at the left edge, on a light background
newtab-wallpaper-firefox-sky-dark = Dark purple hills under a night sky
newtab-wallpaper-firefox-sky-light = Light hills under a soft sky
# “Wrexham AFC” is the name of a football club and should not be translated.
# "crest" refers to the football club's logo (team badge).
newtab-wallpaper-firefox-wrexham = The Wrexham AFC crest on a red background

# Variables
#   $author_string (String) - The name of the creator of the photo.
#   $webpage_string (String) - The name of the webpage where the photo is located.
newtab-wallpaper-attribution = Photo by <a data-l10n-name="name-link">{ $author_string }</a> on <a data-l10n-name="webpage-link">{ $webpage_string }</a>
newtab-wallpaper-feature-highlight-header = Try a splash of color
newtab-wallpaper-feature-highlight-content = Give your New Tab a fresh look with wallpapers.
newtab-wallpaper-feature-highlight-button = Got it
# Tooltip for dismiss button
feature-highlight-dismiss-button =
    .title = Dismiss
    .aria-label = Close popup
feature-highlight-wallpaper =
    .title = { -newtab-wallpaper-feature-highlight-header }
    .aria-label = { -newtab-wallpaper-feature-highlight-content }

## New Tab Weather

# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-see-forecast-description =
    .title = See forecast in { $provider }
    .aria-description = { $provider } ∙ Sponsored
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsored
# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-weather-widget-open-menu-button =
    .title = Open weather menu
    .aria-label = Open weather menu
newtab-weather-menu-change-location = Change location
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Search location
    .aria-label = Search location
# "Back" is an icon-only button next to the location search field that returns to the widget.
newtab-weather-search-back-button =
    .title = Back
    .aria-label = Back
# Screen reader label for the dropdown below the location search field that
# lists matching locations as the user types.
newtab-weather-location-search-results =
    .aria-label = Location suggestions
# "Current" refers to the user's physical/geographic location detected via geolocation.
newtab-weather-change-location-search-use-current =
    .label = Use current location
newtab-weather-menu-weather-display = Weather display
newtab-weather-todays-forecast = Today’s forecast
newtab-weather-see-full-forecast = See full forecast
# Display options are:
# - Simple: Displays a current weather condition icon and the current temperature
# - Detailed: Include simple information plus a short text summary: e.g. "Mostly cloudy"
newtab-weather-menu-weather-display-option-simple = Simple
newtab-weather-menu-change-weather-display-simple = Switch to simple view
newtab-weather-menu-weather-display-option-detailed = Detailed
newtab-weather-menu-change-weather-display-detailed = Switch to detailed view
newtab-weather-menu-temperature-units = Temperature units
newtab-weather-menu-temperature-option-fahrenheit = Fahrenheit
newtab-weather-menu-temperature-option-celsius = Celsius
newtab-weather-menu-change-temperature-units-fahrenheit = Switch to Fahrenheit
newtab-weather-menu-change-temperature-units-celsius = Switch to Celsius
newtab-weather-menu-learn-more = Learn more
newtab-weather-menu-detect-my-location = Detect my location
# This message is shown if user is working offline
newtab-weather-error-not-available = Weather data is not available right now.
newtab-weather-opt-in-see-weather = Do you want to see the weather for your location?
newtab-weather-opt-in-not-now =
    .label = Not now
newtab-weather-opt-in-yes =
    .label = Yes
newtab-weather-opt-in-headline = Get your local weather forecast
newtab-weather-opt-in-use-location =
    .label = Use location
newtab-weather-opt-in-choose-location = Choose location
# We'll be showing static (fake) weather data if the user has not opted in to using their location
newtab-weather-static-city = New York City
# "Highest" here refers to the highest temperature of the day
newtab-weather-high =
    .aria-label = High
# "Lowest" here refers to the lowest temperature of the day
newtab-weather-low =
    .aria-label = Low

## Topic Labels

newtab-topic-label-business = Business
newtab-topic-label-career = Career
newtab-topic-label-education = Education
newtab-topic-label-arts = Entertainment
newtab-topic-label-food = Food
newtab-topic-label-health = Health
newtab-topic-label-hobbies = Gaming
# ”Money” = “Personal Finance”, refers to articles and stories that help readers better manage
# and understand their personal finances – from saving money to buying a home. See the
# “Curated by our editors“ section at the top of https://getpocket.com/explore/personal-finance for more context
newtab-topic-label-finance = Money
newtab-topic-label-society-parenting = Parenting
newtab-topic-label-government = Politics
newtab-topic-label-education-science = Science
# ”Life Hacks” = “Self Improvement”, refers to articles and stories aimed at helping readers improve various
# aspects of their lives – from mental health to  productivity. See the “Curated by our editors“ section
# at the top of https://getpocket.com/explore/self-improvement for more context.
newtab-topic-label-society = Life Hacks
newtab-topic-label-sports = Sports
newtab-topic-label-tech = Tech
newtab-topic-label-travel = Travel
newtab-topic-label-home = Home & Garden

## Topic Selection Modal

# “fine-tune” refers to the process of making small adjustments to something to get
# the best or desired experience or performance.
newtab-topic-selection-title = Select topics to fine-tune your feed
# “tailored” refers to process of (a tailor) making (clothes) to fit individual customers.
# In other words, “Our expert curators prioritize stories to fit your selected interests”
newtab-topic-selection-subtitle = Choose two or more topics. Our expert curators prioritize stories tailored to your interests. Update anytime.
newtab-topic-selection-save-button = Save
newtab-topic-selection-cancel-button = Cancel
newtab-topic-selection-button-maybe-later = Maybe later
newtab-topic-selection-privacy-link = Learn how we protect and manage data
newtab-topic-selection-button-update-interests = Update your interests
newtab-topic-selection-button-pick-interests = Pick your interests

## Content Feed Sections
## "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
## e.g. Following the travel section of stories.

newtab-section-follow-button = Follow
# Variables:
#   $topic (string) - Topic that the user can follow
newtab-section-follow-button-label =
    .aria-label = Follow { $topic }
newtab-section-following-button = Following
newtab-section-unfollow-button = Unfollow
# Variables:
#   $topic (string) - Topic that the user is following and can unfollow
newtab-section-unfollow-button-label =
    .aria-label = Following: Unfollow { $topic }
# A modal may appear next to the Follow button, directing users to try out the feature
newtab-section-follow-highlight-title = Fine-tune your feed
newtab-section-follow-highlight-subtitle = Follow your interests to see more of what you like.

## Topic navigation
## A row of buttons above the stories, each scrolling the page to that topic's section.

newtab-topic-navigation-label =
    .aria-label = Topics
# Opens a menu listing the topics that did not fit in the row.
newtab-topic-navigation-more-button = More

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Block
newtab-section-blocked-button = Blocked
newtab-section-unblock-button = Unblock

# Variables:
#   $topic (string) - Name of topic that user is following
newtab-section-follow-topic =
    .aria-label = Follow { $topic }
# Variables:
#   $topic (string) - Name of topic that user is unfollowing
newtab-section-unfollow-topic =
    .aria-label = Unfollow { $topic }
# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic =
    .aria-label = Block { $topic }
# Variables:
#   $topic (string) - Name of topic that user is unblocking
newtab-section-unblock-topic =
    .aria-label = Unblock { $topic }

## Confirmation modal for blocking a section

newtab-section-confirm-block-topic-p1 = Are you sure you want to block this topic?
newtab-section-confirm-block-topic-p2 = Blocked topics will no longer appear in your feed.

# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Block { $topic }
newtab-section-block-cancel-button = Cancel

## Panel in the Customize menu section to manage followed and blocked topics

newtab-section-mangage-topics-title = Topics
newtab-section-manage-topics-button-v2 =
    .label = Manage topics
newtab-section-mangage-topics-followed-topics = Followed
newtab-section-mangage-topics-followed-topics-empty-state = You have not followed any topics yet.
newtab-section-mangage-topics-blocked-topics = Blocked
newtab-section-mangage-topics-blocked-topics-empty-state = You have not blocked any topics yet.

## Strings for custom wallpaper highlight

newtab-custom-wallpaper-title = Custom wallpapers are here
# 'Make firefox yours" means to customize or personalize
newtab-custom-wallpaper-subtitle = Upload your own wallpaper or pick a custom color to make { -brand-product-name } yours.
newtab-custom-wallpaper-cta = Try it

## Strings for new user activation custom wallpaper highlight

newtab-new-user-custom-wallpaper-title = Choose a wallpaper to make { -brand-product-name } yours
newtab-new-user-custom-wallpaper-subtitle = Make every new tab feel like home with custom wallpapers and colors.
newtab-new-user-custom-wallpaper-cta = Try it now

## Strings for Nova wallpaper feature highlight

newtab-wallpaper-feature-highlight-title = Fresh new wallpapers just landed
newtab-wallpaper-feature-highlight-subtitle = Choose your favorite and make every new tab feel like home.
newtab-wallpaper-feature-highlight-cta = Choose wallpaper

## Strings for download mobile highlight

newtab-download-mobile-highlight-title = Download { -brand-product-name } for mobile
# "Scan the code" refers to scanning the QR code that appears above the body text that leads to Firefox for mobile download.
newtab-download-mobile-highlight-body-variant-a = Scan the code to securely browse on the go.
newtab-download-mobile-highlight-body-variant-b = Pick up where you left off when you sync your tabs, passwords, and more.
newtab-download-mobile-highlight-body-variant-c = Did you know you can take { -brand-product-name } on the go? Same browser. In your pocket.
newtab-download-mobile-highlight-image =
    .aria-label = QR code to download { -brand-product-name } for mobile

## Strings for reporting issues with ads and content

newtab-report-content-why-reporting-this =
  .label = Why are you reporting this?
newtab-report-ads-reason-not-interested =
  .label = I’m not interested
newtab-report-ads-reason-inappropriate =
  .label = It’s inappropriate
newtab-report-ads-reason-seen-it-too-many-times =
  .label = I’ve seen it too many times
newtab-report-content-wrong-category =
  .label = Wrong category
newtab-report-content-outdated =
  .label = Outdated
newtab-report-content-inappropriate-offensive =
  .label = Inappropriate or offensive
newtab-report-content-spam-misleading =
  .label = Spam or misleading
newtab-report-content-requires-payment-subscription =
  .label = Requires payment or subscription
newtab-report-content-requires-payment-subscription-learn-more = Learn more
newtab-report-cancel = Cancel
newtab-report-submit = Submit
newtab-toast-thanks-for-reporting =
    .message = Thank you for reporting this.
newtab-toast-widgets-hidden =
    .message = Select the pencil icon to add widgets back anytime.
# Variables:
#   $topic (string) - Topic that the user has followed
newtab-section-toast-follow =
    .message = You’re now following { $topic }.
# Variables:
#   $topic (string) - Topic that the user has unfollowed
newtab-section-toast-unfollow =
    .message = You’re no longer following { $topic }.
# Variables:
#   $topic (string) - Topic that the user has blocked
newtab-section-toast-block =
    .message = You won’t see stories about { $topic } anymore.

## Strings for task / to-do list productivity widget

newtab-widget-section-title = Widgets
newtab-widget-menu-hide = Hide widget
newtab-widget-menu-change-size = Change size
# Parent label for a submenu in the widget menu that reorders the widget
# among its siblings. "Left" and "Right" appear as items inside this submenu.
newtab-widget-menu-move = Move
# Submenu item under "Move"; moves the widget one position to the left.
# RTL locales should translate this as "Right".
newtab-widget-menu-move-left = Left
# Submenu item under "Move"; moves the widget one position to the right.
# RTL locales should translate this as "Left".
newtab-widget-menu-move-right = Right
# Submenu item under "Move"; pins the widget to Home, the New Tab space the
# page opens on.
newtab-widget-menu-move-to-home = Home
# Submenu item under "Move"; moves the widget from Home back to the Widgets
# space, the New Tab space that holds every widget not pinned to Home.
newtab-widget-menu-move-to-widgets = Widgets
newtab-widget-size-small = Small
newtab-widget-size-medium = Medium
newtab-widget-size-large = Large
# Tooltip for hide all widgets button
newtab-widget-section-hide-all-button =
    .title = Hide widgets
    .aria-label = Hide all widgets
newtab-widget-section-maximize =
    .title = Expand widgets
    .aria-label = Expand all widgets to full size
newtab-widget-section-minimize =
    .title = Minimize widgets
    .aria-label = Collapse all widgets to compact size
# Shown on the widgets section header button while the section is
# auto-minimized to its title row, to open the section back up.
newtab-widget-section-show-widgets =
    .title = Show widgets
    .aria-label = Show the widgets section
newtab-widget-section-menu-button =
    .title = Widgets menu
    .aria-label = Open widgets menu
newtab-widget-add-widgets-button =
    .aria-label = Add widget
    .title = Add widget
newtab-widget-section-menu-manage = Manage widgets
newtab-widget-section-menu-hide-all = Hide widgets
newtab-widget-section-menu-learn-more = Learn more
newtab-widget-section-feedback = Tell us what you think
# Button shown when additional widgets are hidden beyond the
# first row, allowing users to show them.
newtab-widget-section-show-more =
    .label = Show more widgets
# Button shown when the widgets row is expanded to multiple rows,
# allowing users to collapse it back to one row.
newtab-widget-section-show-less =
    .label = Show fewer widgets
newtab-widget-lists-name-default = Checklist
newtab-widget-lists-name-label-default =
    .label = Task list
newtab-widget-lists-name-label-checklist =
    .label = Checklist
newtab-widget-lists-name-placeholder-default =
    .placeholder = Task list
newtab-widget-lists-name-placeholder-checklist2 =
    .placeholder = Checklist
    .aria-label = Edit list name
# The placeholder value of the name field for a newly created list
newtab-widget-lists-name-placeholder-new2 =
    .placeholder = New list
    .aria-label = Edit list name

# "Add one" means adding a new task to the list (e.g., "Walk the dog")
newtab-widget-lists-empty-cta = The possibilities are endless. Add one.
# A simple label next to the default list name letting users know this is a new / beta feature
newtab-widget-lists-label-new =
    .label = New
newtab-widget-lists-label-beta =
    .label = Beta
# When tasks have been previous marked as complete, they will appear in their own separate list beneath incomplete items
# Variables:
#   $number (number) - Amount of list items marked complete
newtab-widget-lists-completed-list = Completed ({ $number })
newtab-widget-lists-celebration-headline = Good work
newtab-widget-lists-celebration-subhead = All clear
newtab-widget-task-list-menu-copy = Copy
newtab-widget-lists-menu-edit = Edit list name
newtab-widget-lists-menu-edit2 =
    .aria-label = Edit list name
newtab-widget-lists-menu-create = Create a new list
newtab-widget-lists-menu-delete = Delete this list
newtab-widget-lists-menu-copy = Copy list to clipboard
newtab-widget-lists-menu-learn-more = Learn more
newtab-widget-lists-button-add-item = Add an item
newtab-widget-lists-input-add-an-item2 =
    .placeholder = Add an item
    .aria-label = Add an item
newtab-widget-lists-input-error = Please include text to add an item.
newtab-widget-lists-input-menu-open-link = Open link
newtab-widget-lists-input-menu-move-up = Move up
newtab-widget-lists-input-menu-move-down = Move down
newtab-widget-lists-input-menu-delete = Delete
newtab-widget-lists-input-menu-edit = Edit
newtab-widget-lists-input-menu-edit2 =
    .aria-label = Edit item
newtab-widget-lists-edit-clear =
    .aria-label = Cancel
    .title = Cancel
# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
# Lists is a noun, as in "the menu for your lists".
newtab-widget-lists-open-menu-button =
    .title = Open lists menu
    .aria-label = Open lists menu

# the + symbol emphasises the functionality of adding a new list
newtab-widget-lists-dropdown-create =
    .label = + Create a new list

## Strings for timer productivity widget
## When the timer ends, a system notification may be shown. Depending on which mode the timer is in, that message would be shown

newtab-widget-timer-notification-title = Timer
newtab-widget-timer-notification-focus = Focus time is up. Nice work. Need a break?
newtab-widget-timer-notification-break = Your break is over. Ready to focus?
newtab-widget-timer-notification-warning = Notifications are off
newtab-widget-timer-mode-focus =
    .label = Focus
newtab-widget-timer-mode-break =
    .label = Break
newtab-widget-timer-label-play =
    .label = Play
newtab-widget-timer-label-pause =
    .label = Pause
newtab-widget-timer-reset =
    .title = Reset
newtab-widget-timer-menu-notifications = Turn off notifications
newtab-widget-timer-menu-notifications-on = Turn on notifications
newtab-widget-timer-menu-learn-more = Learn more
# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-widget-timer-open-menu-button =
    .title = Open timer menu
    .aria-label = Open timer menu

## Crossword widget

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-crossword-widget-open-menu-button =
    .title = Open crossword menu
    .aria-label = Open crossword menu

# Context menu item linking to the Particle website, the provider of the
# crossword. “Particle” is a brand name and should not be translated.
newtab-crossword-menu-powered-by-particle = Powered by Particle

## Strings introduced by the Nova redesign of the Timer widget

# Variables:
#   $minutes (number) - The currently selected timer duration in minutes
newtab-widget-timer-start-aria =
    .aria-label =
        { $minutes ->
            *[other] Start { $minutes }-minute timer
        }
newtab-widget-timer-pause-aria =
    .aria-label = Pause timer
# Variables:
#   $minutes (number) - The currently selected timer duration in minutes
newtab-widget-timer-spinbutton-name =
    .aria-label =
        { $minutes ->
            [one] { $minutes } minute
            *[other] { $minutes } minutes
        }
newtab-widget-timer-decrease-min =
    .title = Decrease 1 minute
newtab-widget-timer-increase-min =
    .title = Increase 1 minute
newtab-widget-timer-mode-group =
    .aria-label = Timer mode
# Small label shown beneath the live time while the focus timer is running or paused.
newtab-widget-timer-running-focus = Focus
# Small label shown beneath the live time while the break timer is running or paused.
newtab-widget-timer-running-break = Break
# Heading shown inside the Timer widget after a focus session ends.
newtab-widget-timer-celebration-heading-focus = Nice work
# Heading shown inside the Timer widget after a break session ends.
newtab-widget-timer-celebration-heading-break = Your break is over
# Message shown inside the Timer widget after a focus session ends.
newtab-widget-timer-celebration-message-focus = Need a break?
# Message shown inside the Timer widget after a break session ends.
newtab-widget-timer-celebration-message-break = Ready to focus?

newtab-widget-message-title = Stay focused with lists and a built-in timer
# to-dos stands for "things to do".
newtab-widget-message-copy = From quick reminders to daily to-dos, focus sessions to stretch breaks — stay on task and on time.
# One spot refers to a dedicated section on new tab to manage and use widgets
newtab-widget-message-focus-forecasts-title = One spot for focus, forecasts, and more
newtab-widget-message-focus-forecasts-body = Keep your day flowing with { -brand-product-name } widgets. Check the forecast, stay on task, or track time across the globe.

# Strings for the default promo card

newtab-promo-card-title = Support { -brand-product-name }
newtab-promo-card-body = Our sponsors support our mission to build a better web
newtab-promo-card-cta = Learn more

# "Make Firefox yours" refers to about:newtab. The call to action here ("Try it now")
# is to customize the new tab page with a background image or color from
# the built-in wallpaper collection or uploading your own image.
newtab-promo-card-title-addons = Make { -brand-product-name } yours
newtab-promo-card-body-addons = Pick a wallpaper from our collection, or create your own.
newtab-promo-card-cta-addons = Try it now
newtab-promo-card-dismiss-button =
    .title = Dismiss
    .aria-label = Dismiss

## Strings for activation window message variants. In certain experiment configurations,
## the strings from these variants may be displayed in a message below the search input
## for the first 48 hours of a new profile's lifetime. Some messages include buttons with
## labels, but not all.

newtab-activation-window-message-dismiss-button =
    .title = Dismiss
    .aria-label = Dismiss

# "This space" refers to about:newtab. The call to action here ("make it your own")
# is to customize newtab with a background image or colour, or by tweaking the
# existing widgetry that appears on it.
newtab-activation-window-message-customization-focus-header = Make this space your own
newtab-activation-window-message-customization-focus-message = Choose a fresh wallpaper, add shortcuts to your favorite sites, and stay up-to-date on stories that interest you.
newtab-activation-window-message-customization-focus-primary-button =
    .label = Start customizing

# "This space" refers to about:newtab. The sentiment of "plays by your rules" is
# meant to evoke the idea that newtab is malleable and customizable. The call to
# action is to customize newtab with a background image or colour, or by tweaking
# the existing widgetry that appears on it.
newtab-activation-window-message-values-focus-header = This space plays by your rules
newtab-activation-window-message-values-focus-message = { -brand-product-name } lets you browse the way you like, with a more personal way to start your day online. Make { -brand-product-name } your own.

## Strings for the New Tab customization callout shown at the Nova launch.

# "Put your finishing touch on" means to add the last, personal detail that
# makes something yours. The call to action is to open the New Tab
# customization panel and pick a theme or wallpaper.
newtab-nova-customization-callout-header = Put your finishing touch on { -brand-product-name }
newtab-nova-customization-callout-message = Explore light or dark themes and wallpapers that make the new { -brand-product-name } feel more like yours.
newtab-nova-customization-callout-primary-button =
    .label = Choose your look

## Strings for the Clock widget

newtab-clock-widget-menu-learn-more = Learn more
newtab-clock-widget-menu-edit = Edit clocks
newtab-clock-widget-menu-switch-to-12h = Switch to 12-hour format
newtab-clock-widget-menu-switch-to-24h = Switch to 24-hour format
newtab-clock-widget-label-your-clocks = Your clocks
# "Back" is an icon-only button next to the city search field form that returns to the list of clocks.
newtab-clock-widget-search-back-button =
    .title = Back
    .aria-label = Back
newtab-clock-widget-search-location-input2 =
    .placeholder = Search for a city
    .aria-label = Search for a city
# "Nickname (optional)" refers to a custom, user-defined label for a saved location
# (e.g., "Home", "Office", or "School") to make it easier to recognize.
# Not to be translated as a legal name, username, or alias used for identity verification.
newtab-clock-widget-input-nickname =
    .label = Nickname (optional)
    .placeholder = Add a nickname
    .aria-label = Nickname (optional)
# "Add new clock" is an icon-only button in the widget toolbar — the
# attributes are consumed as tooltip/screen-reader label only. The button
# never renders visible text.
newtab-clock-widget-button-add =
    .title = Add new clock
    .aria-label = Add new clock
newtab-clock-widget-button-add-clock = Add
newtab-clock-widget-button-cancel = Cancel
newtab-clock-widget-button-back =
    .title = Back
    .aria-label = Back
newtab-clock-widget-button-edit-clock =
    .title = Edit clock
    .aria-label = Edit clock
newtab-clock-widget-button-save = Save
newtab-clock-widget-button-remove-clock =
    .title = Remove clock
    .aria-label = Remove clock
# Accessible name for a clock row in the "Your clocks" management panel
# when the row has no user-provided nickname. Read aloud by screen
# readers when focus lands on the row.
# Variables:
#   $city (string) - The city name displayed in the row.
newtab-clock-widget-edit-item =
    .aria-label = { $city }
# Accessible name for a clock row when a user nickname has been set.
# Variables:
#   $city (string) - The city name displayed in the row.
#   $nickname (string) - The user-provided nickname for the row.
newtab-clock-widget-edit-item-with-nickname =
    .aria-label = { $city }, nickname: { $nickname }
newtab-clock-widget-add-clock-form =
    .aria-label = Add clock
newtab-clock-widget-edit-clock-form =
    .aria-label = Edit clock
# "Search results" is the accessible label for the listbox dropdown that appears
# below the location search field, listing matching cities as the user types.
# It means "results of the search", not "search within the results".
newtab-clock-widget-search-results =
    .aria-label = Search results
# Fallback row in the search results that lets the user add a city that is
# not in the list. $city (String) is the text the user has typed.
newtab-clock-widget-add-custom = Add “{ $city }” as a custom clock
# Text field for the display name of a user-added custom clock.
newtab-clock-widget-custom-city-input =
    .label = City name
    .placeholder = Name this clock
    .aria-label = City name
# Searchable time-zone field shown when adding a custom clock. The user
# types a city they know and picks it to set the clock's time zone.
newtab-clock-widget-custom-timezone-input =
    .label = Time zone
    .placeholder = Search by city, time zone, or UTC offset
    .aria-label = Time zone
newtab-clock-widget-custom-zone-results =
    .aria-label = Time zone results
# Shown in the time-zone picker when the search matches no time zones.
newtab-clock-widget-custom-zone-no-results = No matching time zones
# Returns from the custom clock form back to the city search.
newtab-clock-widget-custom-back = Back
# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-clock-widget-open-menu-button =
    .title = Open clock menu
    .aria-label = Open clock menu
# $nickname (String) - The user-defined nickname for a saved clock location (e.g., "Home", "Office").
newtab-clock-widget-label-nickname-with-value = Nickname: { $nickname }

# Curated World Clock city names. The value is the city name shown on the
# clock; translate to your locale's usual spelling for the city.
newtab-clock-city-us-new-york = New York
newtab-clock-city-us-los-angeles = Los Angeles
newtab-clock-city-us-chicago = Chicago
newtab-clock-city-us-san-francisco = San Francisco
newtab-clock-city-us-san-diego = San Diego
newtab-clock-city-us-dallas = Dallas
newtab-clock-city-us-houston = Houston
newtab-clock-city-us-philadelphia = Philadelphia
newtab-clock-city-us-atlanta = Atlanta
newtab-clock-city-us-washington-dc = Washington, D.C.
newtab-clock-city-us-boston = Boston
newtab-clock-city-us-miami = Miami
newtab-clock-city-us-seattle = Seattle
newtab-clock-city-us-denver = Denver
newtab-clock-city-us-honolulu = Honolulu
newtab-clock-city-us-anchorage = Anchorage
newtab-clock-city-de-berlin = Berlin
newtab-clock-city-de-munich = Munich
newtab-clock-city-de-frankfurt = Frankfurt
newtab-clock-city-de-hamburg = Hamburg
newtab-clock-city-fr-paris = Paris
newtab-clock-city-fr-lyon = Lyon
newtab-clock-city-fr-marseille = Marseille
newtab-clock-city-fr-toulouse = Toulouse
newtab-clock-city-in-kolkata = Kolkata
newtab-clock-city-in-mumbai = Mumbai
newtab-clock-city-in-delhi = Delhi
newtab-clock-city-in-bangalore = Bangalore
newtab-clock-city-cn-shanghai = Shanghai
newtab-clock-city-cn-beijing = Beijing
newtab-clock-city-cn-shenzhen = Shenzhen
newtab-clock-city-br-sao-paulo = São Paulo
newtab-clock-city-br-rio-de-janeiro = Rio de Janeiro
newtab-clock-city-br-brasilia = Brasília
newtab-clock-city-id-jakarta = Jakarta
newtab-clock-city-id-surabaya = Surabaya
newtab-clock-city-id-makassar = Makassar
newtab-clock-city-ca-toronto = Toronto
newtab-clock-city-ca-montreal = Montreal
newtab-clock-city-ca-vancouver = Vancouver
newtab-clock-city-au-sydney = Sydney
newtab-clock-city-au-perth = Perth
newtab-clock-city-au-adelaide = Adelaide
newtab-clock-city-pl-warsaw = Warsaw
newtab-clock-city-pl-krakow = Kraków
newtab-clock-city-jp-tokyo = Tokyo
newtab-clock-city-jp-osaka = Osaka
newtab-clock-city-mx-mexico-city = Mexico City
newtab-clock-city-mx-guadalajara = Guadalajara
newtab-clock-city-it-rome = Rome
newtab-clock-city-it-milan = Milan
newtab-clock-city-ru-moscow = Moscow
newtab-clock-city-ru-saint-petersburg = Saint Petersburg
newtab-clock-city-gb-london = London
newtab-clock-city-gb-birmingham = Birmingham
newtab-clock-city-es-madrid = Madrid
newtab-clock-city-es-barcelona = Barcelona
newtab-clock-city-nl-amsterdam = Amsterdam
newtab-clock-city-ch-zurich = Zurich
newtab-clock-city-at-vienna = Vienna
newtab-clock-city-cz-prague = Prague
newtab-clock-city-ar-buenos-aires = Buenos Aires
newtab-clock-city-gr-athens = Athens
newtab-clock-city-hu-budapest = Budapest
newtab-clock-city-be-brussels = Brussels
newtab-clock-city-ua-kyiv = Kyiv
newtab-clock-city-fi-helsinki = Helsinki
newtab-clock-city-co-bogota = Bogotá
newtab-clock-city-ph-manila = Manila
newtab-clock-city-tr-istanbul = Istanbul
newtab-clock-city-my-kuala-lumpur = Kuala Lumpur
newtab-clock-city-eg-cairo = Cairo
newtab-clock-city-se-stockholm = Stockholm
newtab-clock-city-ro-bucharest = Bucharest
newtab-clock-city-th-bangkok = Bangkok
newtab-clock-city-ng-lagos = Lagos
newtab-clock-city-tw-taipei = Taipei
newtab-clock-city-za-johannesburg = Johannesburg
newtab-clock-city-cl-santiago = Santiago
newtab-clock-city-pk-karachi = Karachi
newtab-clock-city-bg-sofia = Sofia
newtab-clock-city-sg-singapore = Singapore
newtab-clock-city-hk-hong-kong = Hong Kong
newtab-clock-city-sa-riyadh = Riyadh
newtab-clock-city-dk-copenhagen = Copenhagen
newtab-clock-city-pe-lima = Lima
newtab-clock-city-ke-nairobi = Nairobi
newtab-clock-city-nz-auckland = Auckland
newtab-clock-city-kr-seoul = Seoul
newtab-clock-city-lt-vilnius = Vilnius
newtab-clock-city-ie-dublin = Dublin
newtab-clock-city-ae-dubai = Dubai
newtab-clock-city-lv-riga = Riga
newtab-clock-city-pt-lisbon = Lisbon
newtab-clock-city-ir-tehran = Tehran
newtab-clock-city-bd-dhaka = Dhaka
newtab-clock-city-ec-guayaquil = Guayaquil
newtab-clock-city-vn-ho-chi-minh-city = Ho Chi Minh City
newtab-clock-city-np-kathmandu = Kathmandu
newtab-clock-city-mm-yangon = Yangon

##

newtab-card-dismiss-button =
    .title = Dismiss
    .aria-label = Dismiss

## Firefox Home content

home-prefs-content-header =
    .label = { -firefox-home-brand-name }

home-prefs-search-header2 =
    .label = Search

home-prefs-stories-header2 =
    .label = Stories
    .description = Exceptional content curated by the { -brand-product-name } family

home-prefs-widgets-header =
    .label = Widgets

home-prefs-mission-message2 =
    .message = Our sponsors support our mission to build a better web.

home-prefs-manage-topics-link2 =
    .label = Manage topics

home-prefs-manage-widgets-link =
    .label = Manage widgets

home-prefs-choose-wallpaper-link2 =
    .label = Choose a wallpaper

home-prefs-firefox-logo-header =
    .label = { -brand-short-name } logo

# Informational message bar that appears in the Firefox Home section when the options are disabled.
# The user must select Firefox Home as their homepage for either new tabs or new windows to enable
# the features in settings.
home-prefs-firefox-home-disabled-notice =
    .message = To use these features, set new tabs or new windows to { -firefox-home-brand-name }.

# Variables:
#   $num (number) - Number of rows displayed
home-prefs-sections-rows-option-srd =
    .label =
        { $num ->
            [one] { $num } row
           *[other] { $num } rows
        }

home-prefs-shortcuts-header-srd =
    .label = Shortcuts

home-prefs-shortcuts-select =
    .aria-label = Shortcuts

home-prefs-shortcuts-by-option-sponsored-srd =
    .label = Sponsored shortcuts

home-prefs-recommended-by-option-sponsored-stories-srd =
    .label = Sponsored stories

home-prefs-highlights-option-visited-pages-srd =
    .label = Visited pages

home-prefs-highlights-options-bookmarks-srd =
    .label = Bookmarks

home-prefs-highlights-option-most-recent-download-srd =
    .label = Most recent download

home-prefs-recent-activity-header-srd =
    .label = Recent activity

home-prefs-recent-activity-select =
    .aria-label = Recent activity

home-prefs-weather-header-srd =
    .label = Weather

home-prefs-support-firefox-header-srd =
    .label = Support { -brand-product-name }

home-prefs-mission-message-learn-more-link-srd = Find out how

## Strings for the Privacy widget

# Accessible name for the Privacy widget container. The widget shows no visible
# title, so screen readers rely on this label to identify it.
newtab-privacy-widget-label =
    .aria-label = Privacy

# Context menu item linking to more information about the Privacy widget.
newtab-privacy-menu-learn-more = Learn more

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-privacy-widget-open-menu-button =
    .title = Open privacy menu
    .aria-label = Open privacy menu

## Privacy widget — count readout

# Label shown under the large tracker-count number, appearing like:
# Line 1: 86
# Line 2: Trackers blocked today
# Translations can be adjusted to "Today's blocked trackers" or something
# similar if this fixed order causes issues.
# Variables:
#   $count (number) - Number of trackers blocked today
newtab-privacy-trackers-blocked-today =
    { $count ->
        [one] Tracker blocked today
       *[other] Trackers blocked today
    }

# Second line of the readout, under the large number and "Trackers blocked
# today". Counts the sites those trackers were blocked on. The English is
# shortened from "Blocked across { $count } sites" — translate it that fuller way
# if the short fragment doesn't work in your language.
# Variables:
#   $count (number) - Number of sites where trackers were blocked
newtab-privacy-across-sites =
    { $count ->
        [one] Across { $count } site
       *[other] Across { $count } sites
    }

## Privacy widget — empty state

# Shown when no trackers have been blocked yet today.
newtab-privacy-empty-state = { -brand-short-name } automatically blocks trackers, keeping more of your activity private.
# "A running tally" is an informal way to say a total that keeps updating as it goes.
# Here we are referring to the number of trackers blocked, which increases as the user browses.
# An alternative can be "See a running total here"
newtab-privacy-empty-state-tally = See a running tally here.
# Shorter empty-state copy for the small card, shown as two separate lines.
newtab-privacy-empty-state-small-title = Fewer trackers, faster browsing.
newtab-privacy-empty-state-small-message = Browse to start blocking.

## Privacy widget — Enhanced Tracking Protection off state

# Shown when the user has turned off the Enhanced Tracking Protection setting.
newtab-privacy-etp-off-faster-browsing = Faster browsing. Fewer trackers.
newtab-privacy-etp-off-turn-on-tracking = Turn on tracking protections in settings to start blocking.

## Privacy widget — informational messages
##
## Rotating "info" secondary messages. Each message has a body plus a matched
## "-cta" button label sharing the same id stem (the CTA button isn't rendered
## yet; the labels are authored so the pairing is ready).

newtab-privacy-message-info-1 = { -brand-short-name } blocks trackers automatically as you browse.
newtab-privacy-message-info-1-cta = View protections
newtab-privacy-message-info-2 = Tracker blocking helps stop companies from following you online.
newtab-privacy-message-info-2-cta = View protections
newtab-privacy-message-info-3 = Many sites have trackers so companies you’ve never visited may follow you online.
newtab-privacy-message-info-3-cta = View protections
newtab-privacy-message-info-4 = Choosing { -brand-short-name } means choosing protection by default.
newtab-privacy-message-info-4-cta = View protections
newtab-privacy-message-info-5 = Blocked trackers means fewer companies can follow you across sites.
newtab-privacy-message-info-5-cta = View protections
newtab-privacy-message-info-6 = Keep your data with { -brand-short-name }. We never sell it, but other browsers might.
newtab-privacy-message-info-6-cta = Learn more
newtab-privacy-message-info-7 = See which trackers { -brand-short-name } blocked.
newtab-privacy-message-info-7-cta = View protections
newtab-privacy-message-info-8 = Browsing with { -brand-short-name } supports { -vendor-short-name }’s mission to build a better web.
newtab-privacy-message-info-8-cta = Learn more
newtab-privacy-message-info-9 = Make { -brand-short-name } your go-to browser for built-in privacy.
newtab-privacy-message-info-9-cta = Make default
newtab-privacy-message-info-10 = Save passwords in { -brand-short-name } to use strong, unique logins everywhere.
newtab-privacy-message-info-10-cta = Go to passwords
newtab-privacy-message-info-11 = Find out how { -brand-short-name } helps keep your browsing more private.
newtab-privacy-message-info-11-cta = Learn more
newtab-privacy-message-info-12 = Blocking trackers can help save bandwidth on limited data plans.
newtab-privacy-message-info-12-cta = View protections
newtab-privacy-message-info-13 = { -brand-short-name } blocks trackers, freeing up bandwidth for smoother streaming.
newtab-privacy-message-info-13-cta = View protections

## Privacy widget — promotional messages
##
## Rotating "promo" secondary messages suggesting another Firefox feature. Each
## has a body plus a matched "-cta" button label.

newtab-privacy-message-promo-monitor-1 = Find out if your personal info shows up in a data breach.
newtab-privacy-message-promo-monitor-1-cta = Learn more
newtab-privacy-message-promo-monitor-2 = Protect your info with free data breach monitoring, for up to 20 emails.
newtab-privacy-message-promo-monitor-2-cta = Learn more
newtab-privacy-message-promo-signin-1 = Keep bookmarks, passwords, and tabs encrypted across devices with your { -vendor-short-name } account.
newtab-privacy-message-promo-signin-1-cta = Sign in
newtab-privacy-message-promo-vpn-1 = Shopping on public Wi-Fi? Turn on built-in VPN for extra protection.
newtab-privacy-message-promo-vpn-1-cta = Open VPN
newtab-privacy-message-promo-vpn-2 = Using airport Wi-Fi? Protect your browsing by turning on built-in VPN.
newtab-privacy-message-promo-vpn-2-cta = Open VPN
newtab-privacy-message-promo-vpn-3 = Turn on built-in VPN to help keep your location more private.
newtab-privacy-message-promo-vpn-3-cta = Open VPN
newtab-privacy-message-promo-private-window-1 = Try a private window to browse more privately when using a shared computer.
newtab-privacy-message-promo-private-window-1-cta = Open private window
newtab-privacy-message-promo-relay-1 = Save your real email for people you trust; use an email mask for sign-ups.
newtab-privacy-message-promo-relay-1-cta = Get masks
newtab-privacy-message-promo-relay-2 = Protect your inbox from spam with free email masking.
newtab-privacy-message-promo-relay-2-cta = Get masks
newtab-privacy-message-promo-relay-3 = Get 50 free email masks to help keep your real email private.
newtab-privacy-message-promo-relay-3-cta = Get masks

## Privacy widget — celebration messages
##
## Earned "celebration" moments (milestones, daily cap, streak, first
## protection). Count-bearing ones interpolate { $count }.

# Variables:
#   $count (number) - Trackers blocked this week
newtab-privacy-message-milestone-week =
    { $count ->
        [one] { $count } tracker blocked this week. See what { -brand-short-name } is keeping out of your way
       *[other] { $count } trackers blocked this week. See what { -brand-short-name } is keeping out of your way
    }
newtab-privacy-message-milestone-week-cta = View protections

# Variables:
#   $count (number) - Trackers blocked this month
newtab-privacy-message-milestone-month =
    { $count ->
        [one] { $count } tracker blocked this month. One small step for privacy. One big step for peace of mind.
       *[other] { $count } trackers blocked this month. One small step for privacy. One big step for peace of mind.
    }
newtab-privacy-message-milestone-month-cta = View protections

# Variables:
#   $count (number) - Trackers blocked this year
newtab-privacy-message-milestone-year =
    { $count ->
        [one] { $count } tracker blocked this year. That’s a powerful year of protecting your privacy.
       *[other] { $count } trackers blocked this year. That’s a powerful year of protecting your privacy.
    }
newtab-privacy-message-milestone-year-cta = View protections

# Variables:
#   $count (number) - Trackers blocked all-time
newtab-privacy-message-milestone-total =
    { $count ->
        [one] { $count } tracker blocked. That’s major progress towards privacy on your terms.
       *[other] { $count } trackers blocked. That’s major progress towards privacy on your terms.
    }
newtab-privacy-message-milestone-total-cta = View protections

# Shown when today's blocked-tracker count reaches the display cap ("100+").
newtab-privacy-message-daily-cap = (100+ trackers blocked today.) Fewer trackers means more privacy.
newtab-privacy-message-daily-cap-cta = View protections

# Variables:
#   $count (number) - Consecutive days the user has had trackers blocked
newtab-privacy-message-streak =
    { $count ->
        [one] You’ve been protected { $count } day in a row.
       *[other] You’ve been protected { $count } days in a row.
    }
newtab-privacy-message-streak-cta = View protections

# Shown the first time the tracker count goes above zero.
newtab-privacy-message-first-protection = Keep browsing, { -brand-short-name } will keep blocking.
newtab-privacy-message-first-protection-cta = View protections

## Strings for the Finance widget

# Accessible name for the Finance widget. It is usually not shown, since the
# list dropdown or the chosen ticker symbol takes the title's place.
newtab-stocks-widget-title2 = Finance

# "Markets" is the default list of market ETFs. The value is shown in the menu,
# and .label is shown on the button that opens it.
newtab-stocks-list-markets = Markets
    .label = Markets
# "Watchlist" is the user's list of stocks to follow. The value is shown in the
# menu, and .label is shown on the button that opens it.
newtab-stocks-list-watchlist = Watchlist
    .label = Watchlist

# Context menu item that opens the stock search (by company name or ticker symbol).
newtab-stocks-menu-search-stocks = Search by name or symbol

# Context menu item linking to more information about the Finance widget.
newtab-stocks-menu-learn-more = Learn more

# Shown in the Finance widget when its data could not be loaded.
newtab-stocks-error-not-available = Stock data is not available.

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-stocks-widget-open-menu-button2 =
    .title = Open finance menu
    .aria-label = Open finance menu

# Toolbar button that opens the stock search. It shows only the icon until it is
# hovered or focused, then the .label as well; "Search" is a verb. .title and
# .aria-label name it for tooltips and screen readers.
newtab-stocks-search-button =
    .label = Search
    .title = Search by name or symbol
    .aria-label = Search by name or symbol

# Shown on the Watchlist while the user has no stocks in it, above a button that
# opens the stock search.
newtab-stocks-watchlist-empty = Start watching stocks that matter to you

# Button under the empty-watchlist message that opens the stock search. Shown
# with a magnifying-glass icon and the .label, where "Search" is a verb; .title
# and .aria-label name it the same way as the toolbar search button.
newtab-stocks-watchlist-empty-search =
    .label = Search
    .title = Search by name or symbol
    .aria-label = Search by name or symbol

## Screen-reader summary of a stock ticker.
## Variables:
##   $name (String) - the full fund/ETF name, e.g. "SPDR S&P 500 ETF Trust".
##   $change (String) - the day's percentage change, e.g. "+2.1%".
##   $price (String) - the last price, e.g. "$559.44".

# Stock increased (went up) during the day
newtab-stocks-ticker-status-up = { $name }, up { $change }, { $price }

# Stock decreased (went down) during the day
newtab-stocks-ticker-status-down = { $name }, down { $change }, { $price }

# Stock didn't change during the day
newtab-stocks-ticker-status-flat = { $name }, no change, { $change }, { $price }

## Finance widget watchlist add and remove controls

# Tooltip and screen-reader label for the button that adds a stock to the watchlist.
# The button shows only an icon and never renders visible text.
# Variables:
#   $name (String) - the fund/ETF name, e.g. "SPDR S&P 500 ETF Trust".
newtab-stocks-add-to-watchlist =
    .title = Add { $name } to watchlist
    .aria-label = Add { $name } to watchlist

# Tooltip and screen-reader label for the button that removes a stock from the watchlist.
# The button shows only an icon and never renders visible text.
# Variables:
#   $name (String) - the fund/ETF name.
newtab-stocks-remove-from-watchlist =
    .title = Remove { $name } from watchlist
    .aria-label = Remove { $name } from watchlist

# Visually hidden text on a Markets row whose stock is already in the watchlist, so
# screen readers announce that it is saved. Removal happens on the Watchlist tab.
# Variables:
#   $name (String) - the fund/ETF name.
newtab-stocks-in-watchlist = { $name } is in your watchlist

# Announced to screen readers after a stock is added to the watchlist.
# Variables:
#   $name (String) - the fund/ETF name.
newtab-stocks-added-to-watchlist = Added { $name } to watchlist

# Announced to screen readers after a stock is removed from the watchlist.
# Variables:
#   $name (String) - the fund/ETF name.
newtab-stocks-removed-from-watchlist = Removed { $name } from watchlist

## Finance widget ticker search

# Placeholder and screen-reader label for the ticker search input.
newtab-stocks-search-input =
    .placeholder = Search by name or symbol
    .aria-label = Search by name or symbol

# "Search results" is the accessible label for the list of tickers matching the
# search. It means "results of the search", not "search within the results".
newtab-stocks-search-results =
    .aria-label = Search results

# Shown in the search panel, where the results will appear, until the user
# has searched.
newtab-stocks-search-hint = Search for symbols or companies to add to your watchlist

# "Back" is an icon-only button in the search panel header that returns to the
# widget — the attributes are consumed as tooltip/screen-reader label only. The
# button never renders visible text.
newtab-stocks-search-back-button =
    .title = Back
    .aria-label = Back

# Shown when a ticker search returns no matching symbols.
# Variables:
#   $query (String) - the text the user searched for.
newtab-stocks-search-no-results = No results for “{ $query }”

# Shown while a ticker search is running; also announced to screen readers.
newtab-stocks-search-loading = Loading…

# Shown when a ticker search fails to reach the service.
newtab-stocks-search-error = Couldn’t search right now. Try again later.

# Shown below successful search results when the watchlist is already full.
# Variables:
#   $limit (Number) - the maximum number of stocks the watchlist can hold.
newtab-stocks-watchlist-full =
    { $limit ->
        [one] You can add up to { $limit } stock. Remove one to add another.
        *[other] You can add up to { $limit } stocks. Remove one to add another.
    }

## Strings for the Picture of the Day widget

# Title shown at the top of the widget, with the source name appended.
# "Wikimedia Commons" is a brand name and should not be translated.
newtab-picture-header = Picture of the day · Wikimedia Commons

# Shorter title shown at the top of the widget, without the source name.
newtab-picture-header-main = Picture of the day

# Title shown at the top of the widget, with the date of the picture shown.
# $date (Date) - the day Wikimedia Commons featured the picture as its picture
# of the day, shown in numbers (for example 10/8/2026).
newtab-picture-header-date = Picture of the day: { DATETIME($date, year: "numeric", month: "numeric", day: "numeric") }

# Attribution line shown under the title once a picture loads: an author
# credit, a link to the picture's source page, and a link to its license.
# "©" is the copyright symbol.
# $author (string) - the name of the image's author.
newtab-picture-attribution-author = © { $author }

# Link to the picture's source page (its Wikimedia Commons file page).
# "Wikimedia Commons" is a brand name and should not be translated.
newtab-picture-attribution-source-link = Wikimedia Commons

# Screen-reader label for the license link; the visible text is the license
# name (for example "CC BY-SA 4.0") provided with the picture.
# $license (string) - the name of the license.
newtab-picture-attribution-license =
    .aria-label = View the { $license } license

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-picture-widget-open-menu-button =
    .title = Open picture of the day menu
    .aria-label = Open picture of the day menu

# Button that sets the picture shown in the widget, which may be today’s or the
# previous day’s, as the New Tab background wallpaper. The button collapses to an
# icon when not hovered/focused, so .title is its tooltip.
newtab-picture-set-wallpaper2 =
    .label = Set wallpaper
    .title = Set wallpaper
    .aria-label = Set this picture as your wallpaper

# Context menu item that opens the New Tab customization panel.
newtab-picture-menu-manage-wallpaper = Manage wallpaper

# Context menu item that hides the picture shown in the widget.
newtab-picture-menu-hide-photo2 = Hide this picture

# Context menu item that restores the picture after it has been hidden.
newtab-picture-menu-show-photo2 = Show picture

# Context menu item linking to more information about the widget.
newtab-picture-menu-learn-more = Learn more

# Icon button shown on the widget once the picture is hidden; restores it.
newtab-picture-show-button2 =
    .title = Show picture
    .aria-label = Show picture

# Shown when there is no new picture to display yet.
newtab-picture-check-back = Check back tomorrow for a new picture

# Screen-reader text alternative for the picture; fallback used when the source
# provides no localized description.
newtab-picture-image-alt = Wikimedia Commons picture of the day

## Strings for the Search widget

# Widget heading; also the widget's accessible name.
newtab-search-widget-title = Search

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-search-widget-open-menu-button =
    .title = Open search menu
    .aria-label = Open search menu

# Context menu item linking to more information about the widget.
newtab-recent-searches-menu-learn-more = Learn more

# Tab listing the searches the user has made recently.
newtab-recent-searches-tab-recent = Recent searches

# Tab listing what is trending with the user's search engine.
newtab-recent-searches-tab-trending = Trending

# Informs the user which engine the trending results come from.
# Variables:
#   $engine (string) - Name of the default search engine, e.g. "Google".
newtab-recent-searches-trending-attribution = via { $engine }

# Relative time shown for a search made less than a minute ago.
newtab-recent-searches-just-now = Just now

# Screen reader label for the button that forgets one listed search.
# Variables:
#   $search (string) - The search the row shows, e.g. "puffin colonies".
newtab-recent-searches-row-remove =
    .aria-label = Remove “{ $search }” from recent searches

## Recent searches widget — empty states

# Shown in place of the list when the user has made no searches recently.
newtab-recent-searches-empty-recent = Recent searches will show here so you can pick them up again anytime.

# Shown in place of the list when there are no trending search results.
newtab-recent-searches-empty-trending = Trending searches are not available right now.

## Strings for the Horoscopes widget

# Widget heading; also the widget's accessible name.
newtab-horoscopes-widget-title = Horoscopes

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-horoscopes-widget-open-menu-button =
    .title = Open horoscopes menu
    .aria-label = Open horoscopes menu

# Context menu item linking to more information about the widget.
newtab-horoscopes-menu-learn-more = Learn more

# Context menu item linking to the Yahoo website, the provider of the
# horoscopes. “Yahoo” is a brand name and should not be translated.
newtab-horoscopes-menu-powered-by-yahoo = Powered by Yahoo

## Strings for the Travel widget

# Widget heading; also the widget's accessible name. Travel (noun) is a
# widget on New Tab for searching hotels, flights and car rentals.
newtab-travel-widget-title = Travel

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-travel-widget-open-menu-button =
    .title = Open travel menu
    .aria-label = Open travel menu

# Context menu item linking to more information about the widget.
newtab-travel-menu-learn-more = Learn more

# Context menu item linking to the Kayak website, the provider of the
# travel search. “Kayak” is a brand name and should not be translated.
newtab-travel-menu-powered-by-kayak = Powered by Kayak

## Strings for the Solitaire widget. Solitaire is a widget on New Tab where you
## can play solitaire, a card game for one player, also known as patience.
## Game displays within an iframe with name “Yahoo Solitaire”

# Widget heading; also the widget's accessible name.
newtab-solitaire-widget-title = Solitaire

# Tooltip and screen reader label for the icon-only button that opens the
# widget's menu.
newtab-solitaire-widget-open-menu-button =
    .title = Open solitaire menu
    .aria-label = Open solitaire menu

# Context menu item linking to more information about the widget.
newtab-solitaire-menu-learn-more = Learn more

## Strings for the navigable panels that new tab content area can be
## split into.

newtab-spaces-tab-stories = Stories
newtab-spaces-tab-widgets = Widgets
newtab-spaces-tab-activity = Activity
