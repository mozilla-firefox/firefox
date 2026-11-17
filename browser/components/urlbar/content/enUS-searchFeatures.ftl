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

## These strings are used in the urlbar panel.

# A label shown above the Shortcuts aka Top Sites group in the urlbar results
# if there's another result before that group. This should be consistent with
# addressbar-locbar-shortcuts-option.
urlbar-group-shortcuts =
  .label = Shortcuts

# A label shown above the top pick group in the urlbar results.
urlbar-group-best-match =
  .label = Top pick

# A message that replaces a result when the user dismisses a single suggestion.
firefox-suggest-dismissal-acknowledgment-one = Thanks for your feedback. You won’t see this suggestion again.

# A message that replaces a result when the user dismisses a single MDN
# suggestion.
firefox-suggest-dismissal-acknowledgment-one-mdn = Thanks for your feedback. You won’t see this { -mdn-brand-short-name } suggestion again.

# A message that replaces a result when the user dismisses all MDN suggestions.
firefox-suggest-dismissal-acknowledgment-all-mdn = Thanks for your feedback. You won’t see { -mdn-brand-short-name } suggestions anymore.

# A message that replaces a result when the user dismisses a single Yelp
# suggestion.
firefox-suggest-dismissal-acknowledgment-one-yelp = Thanks for your feedback. You won’t see this { -yelp-brand-name } suggestion again.

# A message that replaces a result when the user dismisses all Yelp suggestions.
firefox-suggest-dismissal-acknowledgment-all-yelp = Thanks for your feedback. You won’t see { -yelp-brand-name } suggestions anymore.

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

firefox-suggest-command-dont-show-this2 = Don’t show this
firefox-suggest-command-dont-show-mdn2 = Don’t show { -mdn-brand-short-name } suggestions
firefox-suggest-command-not-interested2 = Not interested
firefox-suggest-command-dont-show-this-suggestion2 = Don’t show this suggestion
firefox-suggest-command-dont-show-any-suggestions2 = Don’t show any suggestions
firefox-suggest-command-dont-show-addons2 = Don’t show { -brand-product-name } extension suggestions

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
urlbar-result-menu-dont-show-yelp-realtime2 = Don’t show { -yelp-brand-name } suggestions

# A message that replaces a result when the user dismisses Yelp realtime
# suggestions.
urlbar-result-dismissal-acknowledgment-yelp-realtime = Thanks for your feedback. You won’t see { -yelp-brand-name } suggestions anymore.

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

## These strings are used for flight status suggestions in the urlbar.
## The flight status suggestions shows the flight time, origin and destination
## and the status like delayed, etc.

# This string is shown in the result menu.
urlbar-result-menu-dont-show-flight-status2 = Don’t show flight status suggestions

# A message that replaces a result when the user dismisses flight status
# suggestions.
urlbar-result-dismissal-acknowledgment-flight-status = Thanks for your feedback. You won’t see flight status suggestions anymore.

# This string is shown as the statis of 'On time'.
urlbar-result-flight-status-status-ontime = On time

# This string is shown as the statis of 'In flight'.
urlbar-result-flight-status-status-inflight = In flight

# This string is shown as the statis of 'Arrived'.
urlbar-result-flight-status-status-arrived = Arrived

# This string is shown as the statis of 'Cancelled'.
urlbar-result-flight-status-status-cancelled = Cancelled

# This string is shown as the statis of 'Delayed'.
# This label needs to show the estimated departure time too.
# e.g. Delayed until 5:50pm
# Variables:
#   $departureEstimatedTime (string) - The estimated departure time.
urlbar-result-flight-status-status-delayed =
    Delayed until { $departureEstimatedTime }

# This string is shown as the time remaining in an in-progress flight.
# e.g. 30 min left
# Variables:
#   $timeLeft (string) - Localized duration string, e.g., "1 hr, 30 min"
urlbar-result-flight-status-time-left = { $timeLeft } left

# This string is shown as the airport.
# e.g. Los Angeles (LAX) to New York (JFK)
# Variables:
#   $city (string) - The city of the airport.
#   $code (string) - The code of the airport.
urlbar-result-flight-status-airport = { $city } ({ $code })

# This string is shown as the flight number with the airline name.
# e.g. AC 8170, (Air Canada)
# Variables:
#   $flightNumber (string) - The flight number.
#   $airlineName (string) - The airline name.
urlbar-result-flight-status-flight-number-with-airline = { $flightNumber }, { $airlineName }

# This a11y label is read by screen readers when an item in the row is selected.
urlbar-result-aria-group-flight-status =
  .aria-label = Flight status suggestions

## These strings are used for sports suggestions in the urlbar. Sports
## suggestions show team names, scores, game times, etc.

# This string is shown for a scheduled future game. In English, "Team 1 at Team
# 2" means the game is taking place at Team 2's home venue, and we say Team 1 is
# the "away" team and Team 2 is the "home" team. If your language doesn't have a
# similar phrase, use your equivalent of "vs." or even just "and".

# This string is shown for a scheduled future game. Please list $homeTeam before
# $awayTeam so that the ordering of teams in the string matches the ordering of
# the team icons in the suggestion UI. Without violating the team ordering,
# please use your language's equivalent of "vs". "and" is another option.
# Variables:
#   $homeTeam (string) - Name of the home team.
#   $awayTeam (string) - Name of the visiting team.
urlbar-result-sports-team-names = { $homeTeam } vs { $awayTeam }

# This string is shown when the game is today, in the near future, or in the
# recent past.
# Variables:
#   $date (string) - Localized date string, e.g., "Today", "Oct 31"
#   $time (string) - Localized time
urlbar-result-sports-game-date-with-time = { $date } at { $time }

# This status is shown when the game is in progress.
urlbar-result-sports-status-live = Live

# This status is shown when the game is over.
urlbar-result-sports-status-final = Final

# This string is shown in the result menu.
urlbar-result-menu-dont-show-sports2 = Don’t show sports suggestions

# A message that replaces a result when the user dismisses sports suggestions.
urlbar-result-dismissal-acknowledgment-sports = Thanks for your feedback. You won’t see sports suggestions anymore.

# This a11y label is read by screen readers when an item in the row is selected.
urlbar-result-aria-group-sports =
  .aria-label = Sports suggestions

## These strings are used for temporal search suggestions in the urlbar.
## Temporal search suggestions are curated searches related to the time of year,
## like a holiday, shown when the urlbar is focused.

# A label shown above temporal search suggestions.
urlbar-group-temporal-seasonal =
  .label = Seasonal searches

# This string is shown in the result menu.
urlbar-result-menu-dont-show-temporal-seasonal = Don’t show seasonal searches
  .accesskey = D

# A message that replaces a result when the user dismisses temporal search
# suggestions.
urlbar-temporal-seasonal-dismissal-acknowledgment = Thanks for your feedback. You won’t see seasonal searches anymore.

# A checkbox in the address bar settings that controls whether seasonal search
# suggestions are shown.
addressbar-locbar-suggest-temporal-seasonal-option =
  .label = Seasonal search suggestions
