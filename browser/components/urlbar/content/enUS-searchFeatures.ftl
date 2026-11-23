# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

### These strings are related to the Firefox Suggest feature. Firefox Suggest
### shows recommended and sponsored third-party results in the address bar
### panel. It also shows headings/labels above different groups of results. For
### example, a "Firefox Suggest" label is shown above bookmarks and history
### results, and an "{ $engine } Suggestions" label may be shown above search
### suggestion results.

## These terms are defined in this file because the feature is en-US only.
## They should be moved to toolkit/branding/brandings.ftl if the feature is
## exposed for localization.

-mdn-brand-short-name = MDN

## These strings are used for urlbar weather suggestions in the "simpler" and
## "full" weather UIs.

# This string is displayed above the current temperature
firefox-suggest-weather-currently = Currently

# This string displays the current temperature value and unit
# Variables:
#   $value (number) - The temperature value
#   $unit (String) - The unit for the temperature
firefox-suggest-weather-temperature = { $value }°{ $unit }

# This string is the title of the weather summary used for the "full" and
# "simpler" UI treatments.
# Variables:
#   $city (String) - The name of the city the weather data is for
#   $region (String) - The name of the region (e.g., U.S. state)
firefox-suggest-weather-title = Weather for { $city }, { $region }

# This string displays the weather summary
# Variables:
#   $currentConditions (String) - The current weather conditions summary
#   $forecast (String) - The forecast weather conditions summary
firefox-suggest-weather-summary-text = { $currentConditions }; { $forecast }

# This string displays the high and low temperatures
# Variables:
#   $high (number) - The number for the high temperature
#   $unit (String) - The unit for the temperature
#   $low (number) - The number for the low temperature
firefox-suggest-weather-high-low = High: { $high }°{ $unit } · Low: { $low }°{ $unit }

## These strings are used as labels of menu items in the result menu.

firefox-suggest-command-dont-show-mdn2 = Don’t show { -mdn-brand-short-name } suggestions
    .accesskey = n

firefox-suggest-command-dont-show-addons2 = Don’t show { -brand-product-name } extension suggestions
    .accesskey = n

## These strings are used for Yelp suggestions in the urlbar.

# This string is shown as the title in Yelp suggestions when the suggestion
# subject is a general service instead of a business name.
# Variables:
#   $service (string) - The title of the service, e.g., "coffee shops".
firefox-suggest-yelp-service-title = Top results for { $service }

## Used as title on the introduction pane. The text can be formatted to span
## multiple lines as needed (line breaks are significant).

firefox-suggest-onboarding-introduction-title-1 =
  Make sure you’ve got our latest
  search experience
firefox-suggest-onboarding-introduction-title-2 =
  We’re building a better search experience —
  one you can trust
firefox-suggest-onboarding-introduction-title-3 =
  We’re building a better way to find what
  you’re looking for on the web
firefox-suggest-onboarding-introduction-title-4 =
  A faster search experience is in the works
firefox-suggest-onboarding-introduction-title-5 =
  Together, we can create the kind of search
  experience the Internet deserves
firefox-suggest-onboarding-introduction-title-6 =
  Meet { -firefox-suggest-brand-name }, the next
  evolution in search
firefox-suggest-onboarding-introduction-title-7 =
  Find the best of the web, faster.

##

firefox-suggest-onboarding-introduction-close-button =
  .title = Close

firefox-suggest-onboarding-introduction-next-button-1 = Find out how
firefox-suggest-onboarding-introduction-next-button-2 = Find out more
firefox-suggest-onboarding-introduction-next-button-3 = Show me how

## Used as title on the main pane. The text can be formatted to span
## multiple lines as needed (line breaks are significant).

firefox-suggest-onboarding-main-title-1 =
  We’re building a richer search experience
firefox-suggest-onboarding-main-title-2 =
  Help us guide the way to the
  best of the Internet
firefox-suggest-onboarding-main-title-3 =
  A richer, smarter search experience
firefox-suggest-onboarding-main-title-4 =
  Finding the best of the web, faster
firefox-suggest-onboarding-main-title-5 =
  We’re building a better search experience —
  you can help
firefox-suggest-onboarding-main-title-6 =
  It’s time to think outside the search engine
firefox-suggest-onboarding-main-title-7 =
  We’re building a smarter search experience —
  one you can trust
firefox-suggest-onboarding-main-title-8 =
  Finding the best of the web should be
  simpler and more secure.
firefox-suggest-onboarding-main-title-9 =
  Find the best of the web, faster

##

firefox-suggest-onboarding-main-description-1 = Allowing { -vendor-short-name } to process your search queries means you’re helping us create smarter, more relevant search suggestions. And, as always, we’ll keep your privacy top of mind.
firefox-suggest-onboarding-main-description-2 = When you allow { -vendor-short-name } to process your search queries, you’re helping build a better { -firefox-suggest-brand-name } for everyone. And, as always, we’ll keep your privacy top of mind.
firefox-suggest-onboarding-main-description-3 = What if your browser helped you zero in on what you’re actually looking for? Allowing { -vendor-short-name } to process your search queries helps us create more relevant search suggestions that still keep your privacy top of mind.
firefox-suggest-onboarding-main-description-4 = You’re trying to get where you’re going on the web and get on with it. When you allow { -vendor-short-name } to process your search queries, we can help you get there faster—while keeping your privacy top of mind.
firefox-suggest-onboarding-main-description-5 = Allowing { -vendor-short-name } to process your search queries will help us create more relevant suggestions for everyone. And, as always, we’ll keep your privacy top of mind.
firefox-suggest-onboarding-main-description-6 = Allowing { -vendor-short-name } to process your search queries will help us create more relevant search suggestions. We’re building { -firefox-suggest-brand-name } to help you get where you’re going on the Internet while keeping your privacy in mind.
firefox-suggest-onboarding-main-description-7 = Allowing { -vendor-short-name } to process your search queries helps us create more relevant search suggestions.
firefox-suggest-onboarding-main-description-8 = Allowing { -vendor-short-name } to process your search queries helps us provide more relevant search suggestions. We don’t use this data to profile you on the web.
firefox-suggest-onboarding-main-description-9 =
  We’re building a better search experience. When you allow { -vendor-short-name } to process your search queries, we can create more relevant search suggestions for you.
  <a data-l10n-name="learn-more-link">Learn more</a>

firefox-suggest-onboarding-main-privacy-first = No user profiling. Privacy-first, always.

firefox-suggest-onboarding-main-accept-option-label = Allow. <a data-l10n-name="learn-more-link">Learn more</a>
firefox-suggest-onboarding-main-accept-option-label-2 = Enable

firefox-suggest-onboarding-main-accept-option-description-1 = Help improve the { -firefox-suggest-brand-name } feature with more relevant suggestions. Your search queries will be processed.
firefox-suggest-onboarding-main-accept-option-description-2 = Recommended for people who support improving the { -firefox-suggest-brand-name } feature. Your search queries will be processed.
firefox-suggest-onboarding-main-accept-option-description-3 = Help improve the { -firefox-suggest-brand-name } experience. Your search queries will be processed.

firefox-suggest-onboarding-main-reject-option-label = Don’t allow.
firefox-suggest-onboarding-main-reject-option-label-2 = Keep disabled

firefox-suggest-onboarding-main-reject-option-description-1 = Keep the default { -firefox-suggest-brand-name } experience with the strictest data-sharing controls.
firefox-suggest-onboarding-main-reject-option-description-2 = Recommended for people who prefer the strictest data-sharing controls. Keep the default experience.
firefox-suggest-onboarding-main-reject-option-description-3 = Leave the default { -firefox-suggest-brand-name } experience with the strictest data-sharing controls.

firefox-suggest-onboarding-main-submit-button = Save preferences
firefox-suggest-onboarding-main-skip-link = Not now

## Local search mode indicator labels in the urlbar

urlbar-search-mode-bookmarks-en = Bookmarks
urlbar-search-mode-tabs-en = Tabs
urlbar-search-mode-history-en = History
urlbar-search-mode-actions-en = Actions

## These strings are used for Yelp realtime suggestions in the urlbar.
## Yelp realtime suggestions shows shops, places information etc nearby.

# This string is shown as title when Yelp realtime suggestion are disabled.
urlbar-result-yelp-realtime-opt-in-title = Find great places nearby and more

# This string is shown as description when Yelp realtime suggestion are disabled.
urlbar-result-yelp-realtime-opt-in-description = Get suggestions for nearby places and services — plus updates on stocks, sports scores, and more from our partners by sharing search query data with { -vendor-short-name }. <a data-l10n-name="learn-more-link">Learn more</a>

# This string is shown in the result menu.
urlbar-result-menu-dont-show-yelp-realtime = Don’t show { -yelp-brand-name } suggestions
    .accesskey = n

# This string is shown as group label for Yelp realtime suggestions.
urlbar-result-yelp-realtime-group-label =
  .label = { -yelp-brand-name } · Sponsored

# This string is shown as the business hours information in cases where the shop
# is opening.
# e.g. <span>Open</span> until 3pm.
# The <span> is needed to change the text color by the status (open/closed).
# Variables:
#   $timeUntil (string) - The time that this state is kept.
urlbar-result-yelp-realtime-business-hours-open =
    <span>Open</span> until { $timeUntil }

# This string is shown as the business hours information in cases where the shop
# is closed.
# closed.
# e.g. <span>Closed</span> until 3pm.
# The <span> is needed to change the text color by the status (open/closed).
# Variables:
#   $timeUntil (string) - The time that this state is kept.
urlbar-result-yelp-realtime-business-hours-closed =
    <span>Closed</span> until { $timeUntil }

# This string is shown as popularity by the rating and the review count.
# Variables:
#   $rating (float) - The rating of this.
#   $review_count (integer) - The review count of this.
urlbar-result-yelp-realtime-popularity = { $rating } ({ $review_count })

# This a11y label is read by screen readers when an item in the row is selected.
urlbar-result-aria-group-yelp-realtime =
  .aria-label = { -yelp-brand-name } suggestions

## These strings are used for temporal search suggestions in the urlbar.
## Temporal search suggestions are curated searches related to the time of year,
## like a holiday, shown when the urlbar is focused.

# A label shown above temporal search suggestions.
urlbar-group-temporal-seasonal =
  .label = Seasonal Searches

# This string is shown in the result menu.
urlbar-result-menu-dont-show-temporal-seasonal = Don’t show seasonal searches
  .accesskey = n

# A checkbox in the address bar settings that controls whether seasonal search
# suggestions are shown.
addressbar-locbar-suggest-temporal-seasonal-option =
  .label = Seasonal search suggestions
