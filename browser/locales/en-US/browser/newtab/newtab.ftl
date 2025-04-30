# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### Firefox Home / New Tab strings for about:home / about:newtab.

newtab-page-title = New Tab
newtab-settings-button =
    .title = Customize your New Tab page
newtab-personalize-settings-icon-label =
    .title = Personalize New Tab
    .aria-label = Settings
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
newtab-topsites-add-shortcut-label = Add Shortcut
newtab-topsites-title-label = Title
newtab-topsites-title-input =
    .placeholder = Enter a title

newtab-topsites-url-label = URL
newtab-topsites-url-input =
    .placeholder = Type or paste a URL
newtab-topsites-url-validation = Valid URL required

newtab-topsites-image-url-label = Custom Image URL
newtab-topsites-use-image-link = Use a custom image…
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

## Context Menu - Action Tooltips.

# General tooltip for context menus.
newtab-menu-section-tooltip =
    .title = Open menu
    .aria-label = Open menu

# Tooltip for dismiss button
newtab-dismiss-button-tooltip =
    .title = Remove
    .aria-label = Remove

# This tooltip is for the context menu of Pocket cards or Topsites
# Variables:
#   $title (string) - The label or hostname of the site. This is for screen readers when the context menu button is focused/active.
newtab-menu-content-tooltip =
    .title = Open menu
    .aria-label = Open context menu for { $title }

## Context Menu: These strings are displayed in a context menu and are meant as a call to action for a given page.

newtab-menu-edit-topsites = Edit
newtab-menu-open-new-window = Open in a New Window
newtab-menu-open-new-private-window = Open in a New Private Window
newtab-menu-dismiss = Dismiss
newtab-menu-pin = Pin
newtab-menu-unpin = Unpin
newtab-menu-delete-history = Delete from History
newtab-menu-save-to-pocket = Save to { -pocket-brand-name }
newtab-menu-delete-pocket = Delete from { -pocket-brand-name }
newtab-menu-archive-pocket = Archive in { -pocket-brand-name }
newtab-menu-show-privacy-info = Our sponsors & your privacy
newtab-menu-about-fakespot = About { -fakespot-brand-name }
newtab-menu-report-content = Report this content
# Context menu option to personalize New Tab recommended stories by blocking a section of stories,
# e.g. "Sports". "Block" is a verb here.
newtab-menu-section-block = Block
# "Follow", "unfollow", and "following" are social media terms that refer to subscribing to or unsubscribing from a section of stories.
# e.g. Following the travel section of stories.
newtab-menu-section-unfollow = Unfollow topic

## Context menu options for sponsored stories and new ad formats on New Tab.

newtab-menu-manage-sponsored-content = Manage sponsored content
newtab-menu-our-sponsors-and-your-privacy = Our sponsors and your privacy
newtab-menu-report-this-ad = Report this ad

## Message displayed in a modal window to explain privacy and provide context for sponsored content.

newtab-privacy-modal-button-done = Done
newtab-privacy-modal-button-manage = Manage sponsored content settings
newtab-privacy-modal-header = Your privacy matters.
newtab-privacy-modal-paragraph-2 =
    In addition to dishing up captivating stories, we also show you relevant,
    highly-vetted content from select sponsors. Rest assured, <strong>your browsing
    data never leaves your personal copy of { -brand-product-name }</strong> — we don’t see it, and our
    sponsors don’t either.
newtab-privacy-modal-link = Learn how privacy works on the new tab

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
newtab-discovery-empty-section-topstories-try-again-button = Try Again
newtab-discovery-empty-section-topstories-loading = Loading…
# Displays when a layout in a section took too long to fetch articles.
newtab-discovery-empty-section-topstories-timed-out = Oops! We almost loaded this section, but not quite.

## Thumbs up and down buttons that shows over a newtab stories card thumbnail on hover.

# Clicking the thumbs up button for this story will result in more stories like this one being recommended
newtab-pocket-thumbs-up-tooltip =
    .title = More like this
# Clicking the thumbs down button for this story informs us that the user does not feel like the story is interesting for them
newtab-pocket-thumbs-down-tooltip =
    .title = Not for me
# Used to show the user a message upon clicking the thumbs up or down buttons
newtab-toast-thumbs-up-or-down2 =
    .message = Thanks. Your feedback will help us improve your feed.
newtab-toast-dismiss-button =
    .title = Dismiss
    .aria-label = Dismiss

## Pocket content onboarding experience dialog and modal for new users seeing the Pocket section for the first time, shown as the first item in the Pocket section.

newtab-pocket-onboarding-discover = Discover the best of the web
newtab-pocket-onboarding-cta = { -pocket-brand-name } explores a diverse range of publications to bring the most informative, inspirational, and trustworthy content right to your { -brand-product-name } browser.

## Error Fallback Content.
## This message and suggested action link are shown in each section of UI that fails to render.

newtab-error-fallback-info = Oops, something went wrong loading this content.
newtab-error-fallback-refresh-link = Refresh page to try again.

## Customization Menu

newtab-custom-shortcuts-toggle =
  .label = Shortcuts
  .description = Sites you save or visit

# Variables
#   $num (number) - Number of rows to display
newtab-custom-row-selector =
        { $num ->
            [one] { $num } row
           *[other] { $num } rows
        }
newtab-custom-stories-toggle =
  .label = Recommended stories
  .description = Exceptional content curated by the { -brand-product-name } family
newtab-custom-pocket-show-recent-saves = Show recent saves
newtab-custom-weather-toggle =
  .label = Weather
  .description = Today’s forecast at a glance
newtab-custom-close-button = Close
newtab-custom-settings = Manage more settings

## New Tab Wallpapers

newtab-wallpaper-title = Wallpapers
newtab-wallpaper-reset = Reset to default
newtab-wallpaper-upload-image = Upload an image
newtab-wallpaper-custom-color = Choose a color
# Variables
#   $file_size (number) - The number of the maximum image file size (in MB) that may be uploaded
newtab-wallpaper-error-max-file-size = The image exceeded the file size limit of { $file_size }MB. Please try uploading a smaller file.
newtab-wallpaper-error-file-type = We couldn’t upload your file. Please try again with different file type.
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

## Solid Colors

newtab-wallpaper-category-title-colors = Solid colors
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
newtab-weather-see-forecast =
    .title = See forecast in { $provider }
# Variables:
#   $provider (string) - Service provider for weather data
newtab-weather-sponsored = { $provider } ∙ Sponsored
newtab-weather-menu-change-location = Change location
newtab-weather-change-location-search-input-placeholder =
    .placeholder = Search location
    .aria-label = Search location
newtab-weather-menu-weather-display = Weather display
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
newtab-weather-menu-hide-weather = Hide weather on New Tab
newtab-weather-menu-learn-more = Learn more
# This message is shown if user is working offline
newtab-weather-error-not-available = Weather data is not available right now.

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
newtab-section-following-button = Following
newtab-section-unfollow-button = Unfollow

## Button to block/unblock listed topics
## "Block", "unblocked", and "blocked" are social media terms that refer to hiding a section of stories.
## e.g. Blocked the politics section of stories.

newtab-section-block-button = Block
newtab-section-blocked-button = Blocked
newtab-section-unblock-button = Unblock

## Confirmation modal for blocking a section

newtab-section-confirm-block-topic-p1 = Are you sure you want to block this topic?
newtab-section-confirm-block-topic-p2 = Blocked topics will no longer appear in your feed.

# Variables:
#   $topic (string) - Name of topic that user is blocking
newtab-section-block-topic-button = Block { $topic }
newtab-section-cancel-button = Not now

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

## Strings for download mobile highlight

newtab-download-mobile-highlight-title = Download { -brand-product-name } for mobile
# "Scan the code" refers to scanning the QR code that appears above the body text that leads to Firefox for mobile download.
newtab-download-mobile-highlight-body-variant-a = Scan the code to securely browse on the go.
newtab-download-mobile-highlight-body-variant-b = Pick up where you left off when you sync your tabs, passwords, and more.
newtab-download-mobile-highlight-body-variant-c = Did you know you can take { -brand-product-name } on the go? Same browser. In your pocket.
newtab-download-mobile-highlight-image =
    .aria-label = QR code to download { -brand-product-name } for mobile

## Strings for reporting ads and content

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
newtab-report-cancel = Cancel
newtab-report-submit = Submit
newtab-toast-thanks-for-reporting =
    .message = Thank you for reporting this.
