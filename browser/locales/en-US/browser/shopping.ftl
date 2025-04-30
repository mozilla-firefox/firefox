# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

shopping-page-title = { -brand-product-name } Shopping

# Title for page showing where a user can check the
# review quality of online shopping product reviews
shopping-main-container-title = Review Checker
shopping-beta-marker = Beta
# This string is for ensuring that screen reader technology
# can read out the "Beta" part of the shopping sidebar header.
# Any changes to shopping-main-container-title and
# shopping-beta-marker should also be reflected here.
shopping-a11y-header =
  .aria-label = Review Checker - beta
shopping-close-button =
  .title = Close
# This string is for notifying screen reader users that the
# sidebar is still loading data.
shopping-a11y-loading =
  .aria-label = Loading…

## Strings for the letter grade component.
## For now, we only support letter grades A, B, C, D and F.
## Letter A indicates the highest grade, and F indicates the lowest grade.
## Letters are hardcoded and cannot be localized.

shopping-letter-grade-description-ab = Reliable reviews
shopping-letter-grade-description-c = Mix of reliable and unreliable reviews
shopping-letter-grade-description-df = Unreliable reviews

# This string is displayed in a tooltip that appears when the user hovers
# over the letter grade component without a visible description.
# It is also used for screen readers.
#  $letter (String) - The letter grade as A, B, C, D or F (hardcoded).
#  $description (String) - The localized letter grade description. See shopping-letter-grade-description-* strings above.
shopping-letter-grade-tooltip =
  .title = { $letter } - { $description }

## Strings for the shopping message-bar

shopping-message-bar-warning-stale-analysis-message-2 = New info to check
shopping-message-bar-warning-stale-analysis-button = Check now

shopping-message-bar-generic-error =
  .heading = No info available right now
  .message = We’re working to resolve the issue. Please check back soon.

shopping-message-bar-warning-not-enough-reviews =
  .heading = Not enough reviews yet
  .message = When this product has more reviews, we’ll be able to check their quality.

shopping-message-bar-warning-product-not-available =
  .heading = Product is not available
  .message = If you see this product is back in stock, report it and we’ll work on checking the reviews.
shopping-message-bar-warning-product-not-available-button2 = Report product is in stock

shopping-message-bar-thanks-for-reporting =
  .heading = Thanks for reporting!
  .message = We should have info about this product’s reviews within 24 hours. Please check back.

shopping-message-bar-warning-product-not-available-reported =
  .heading = Info coming soon
  .message = We should have info about this product’s reviews within 24 hours. Please check back.

shopping-message-bar-analysis-in-progress-message2 = This could take about 60 seconds.

# Variables:
#  $percentage (Number) - The percentage complete that the analysis is, per our servers.
shopping-message-bar-analysis-in-progress-with-amount = Checking review quality ({ $percentage }%)

shopping-message-bar-page-not-supported =
  .heading = We can’t check these reviews
  .message = Unfortunately, we can’t check the review quality for certain types of products. For example, gift cards and streaming video, music, and games.

shopping-message-bar-keep-closed-header =
  .heading = Keep closed?
  .message = You can update your settings to keep Review Checker closed by default. Right now, it opens automatically.
shopping-message-bar-keep-closed-dismiss-button = No thanks
shopping-message-bar-keep-closed-accept-button = Yes, keep closed

## Strings for the product review snippets card

shopping-highlights-label =
  .label = Highlights from recent reviews

shopping-highlight-price = Price
shopping-highlight-quality = Quality
shopping-highlight-shipping = Shipping
shopping-highlight-competitiveness = Competitiveness
shopping-highlight-packaging = Packaging

## Strings for show more card

shopping-show-more-button = Show more
shopping-show-less-button = Show less

## Strings for the settings card

shopping-settings-label =
  .label = Settings
shopping-settings-recommendations-toggle2 =
  .label = Show recommendations and sponsored content
shopping-settings-recommendations-learn-more3 =
  { -brand-product-name } doesn’t share your personal data, so these recommendations won’t follow you around the internet. <a data-l10n-name="review-quality-url">Learn more</a>
shopping-settings-opt-out-button = Turn off Review Checker
powered-by-fakespot = Review Checker is powered by <a data-l10n-name="fakespot-link">{ -fakespot-brand-full-name }</a>.

shopping-settings-auto-open-toggle =
  .label = Automatically open Review Checker

# Description text for regions where we support three sites. Sites are limited to Amazon, Walmart and Best Buy.
# Variables:
#   $firstSite (String) - The first shopping page name
#   $secondSite (String) - The second shopping page name
#   $thirdSite (String) - The third shopping page name
shopping-settings-auto-open-description-three-sites = When you view products on { $firstSite }, { $secondSite }, and { $thirdSite }

# Description text for regions where we support only one site (e.g. currently used in FR/DE with Amazon).
# Variables:
#   $currentSite (String) - The current shopping page name
shopping-settings-auto-open-description-single-site = When you view products on { $currentSite }

## Strings for the adjusted rating component

# "Adjusted rating" means a star rating that has been adjusted to include only
# reliable reviews.
shopping-adjusted-rating-label =
  .label = Adjusted rating
shopping-adjusted-rating-based-reliable-reviews = Based on reliable reviews

## Strings for the review reliability component

shopping-review-reliability-label =
  .label = How reliable are these reviews?

## Strings for the analysis explainer component

shopping-analysis-explainer-label =
  .label = How we determine review quality
shopping-analysis-explainer-intro2 =
  We use AI technology from { -fakespot-brand-full-name } to check the reliability of product reviews. This will only help you assess review quality, not product quality.
shopping-analysis-explainer-grades-intro =
  We assign each product’s reviews a <strong>letter grade</strong> from A to F.
shopping-analysis-explainer-adjusted-rating-description =
  The <strong>adjusted rating</strong> is based only on reviews we believe to be reliable.
shopping-analysis-explainer-learn-more2 =
  Learn more about <a data-l10n-name="review-quality-url">how { -fakespot-brand-name } determines review quality</a>.

# This string includes the short brand name of one of the three supported
# websites, which will be inserted without being translated.
#  $retailer (String) - capitalized name of the shopping website, for example, "Amazon".
shopping-analysis-explainer-highlights-description =
  <strong>Highlights</strong> are from { $retailer } reviews within the last 80 days that we believe to be reliable.

# Fallback for analysis highlights explainer if the retailer is ever unknown
shopping-analysis-explainer-highlights-description-unknown-retailer =
  <strong>Highlights</strong> are from reviews within the last 80 days that we believe to be reliable.

shopping-analysis-explainer-review-grading-scale-reliable = Reliable reviews. We believe the reviews are likely from real customers who left honest, unbiased reviews.
shopping-analysis-explainer-review-grading-scale-mixed = We believe there’s a mix of reliable and unreliable reviews.
shopping-analysis-explainer-review-grading-scale-unreliable = Unreliable reviews. We believe the reviews are likely fake or from biased reviewers.

## Strings for UrlBar button

shopping-sidebar-open-button2 =
  .tooltiptext = Open Review Checker
shopping-sidebar-close-button2 =
  .tooltiptext = Close Review Checker

## Strings for the unanalyzed product card.
## The word 'analyzer' when used here reflects what this tool is called on
## fakespot.com. If possible, a different word should be used for the Fakespot
## tool (the Fakespot by Mozilla 'analyzer') other than 'checker', which is
## used in the name of the Firefox feature ('Review Checker'). If that is not
## possible - if these terms are not meaningfully different - that is OK.

shopping-unanalyzed-product-header-2 = No info about these reviews yet
shopping-unanalyzed-product-message-2 = To know whether this product’s reviews are reliable, check the review quality. It only takes about 60 seconds.
shopping-unanalyzed-product-analyze-button = Check review quality

## Strings for the advertisement

more-to-consider-ad-label =
  .label = More to consider
shopping-sponsored-label = Sponsored

## Shopping survey strings.

shopping-survey-headline = Help improve { -brand-product-name }
shopping-survey-question-one = How satisfied are you with the Review Checker experience in { -brand-product-name }?

shopping-survey-q1-radio-1-label = Very satisfied
shopping-survey-q1-radio-2-label = Satisfied
shopping-survey-q1-radio-3-label = Neutral
shopping-survey-q1-radio-4-label = Dissatisfied
shopping-survey-q1-radio-5-label = Very dissatisfied

shopping-survey-question-two = Does the Review Checker make it easier for you to make purchase decisions?

shopping-survey-q2-radio-1-label = Yes
shopping-survey-q2-radio-2-label = No
shopping-survey-q2-radio-3-label = I don’t know

shopping-survey-next-button-label = Next
shopping-survey-submit-button-label = Submit
shopping-survey-terms-link = Terms of use
shopping-survey-thanks =
  .heading = Thanks for your feedback!

## Shopping opted-out survey strings
## Opt-out survey options are displayed as checkboxes and the user can select one or many.

shopping-survey-opted-out-multiselect-label = Please let us know why you turned off Review Checker. Select multiple if needed.
shopping-survey-thanks-title = Thanks for your feedback!

shopping-survey-opted-out-hard-to-understand = It’s hard to understand
shopping-survey-opted-out-too-slow = It’s too slow
shopping-survey-opted-out-not-accurate = It’s not accurate
shopping-survey-opted-out-not-helpful = It’s not helpful to me
shopping-survey-opted-out-check-myself = I’d rather check reviews myself
shopping-survey-opted-out-other = Other

## Shopping Feature Callout strings.
## "price tag" refers to the price tag icon displayed in the address bar to
## access the feature.

shopping-callout-closed-opted-in-subtitle = Get back to <strong>Review Checker</strong> whenever you see the price tag.

shopping-callout-pdp-opted-in-title = Are these reviews reliable? Find out fast.
shopping-callout-pdp-opted-in-subtitle = Open Review Checker to see an adjusted rating with unreliable reviews removed. Plus, see highlights from recent authentic reviews.

shopping-callout-closed-not-opted-in-title = One click to reliable reviews
shopping-callout-closed-not-opted-in-subtitle = Give Review Checker a try whenever you see the price tag. Get insights from real shoppers quickly — before you buy.

shopping-callout-closed-not-opted-in-revised-title = One click to trustworthy reviews
shopping-callout-closed-not-opted-in-revised-subtitle = Just click the price tag icon in the address bar to get back to Review Checker.
shopping-callout-closed-not-opted-in-revised-button = Got it

shopping-callout-not-opted-in-reminder-title = Shop with confidence
shopping-callout-not-opted-in-reminder-subtitle = Not sure if a product’s reviews are real or fake? Review Checker from { -brand-product-name } can help.
shopping-callout-not-opted-in-reminder-open-button = Open Review Checker
shopping-callout-not-opted-in-reminder-close-button = Dismiss
shopping-callout-not-opted-in-reminder-ignore-checkbox = Don’t show again
shopping-callout-not-opted-in-reminder-img-alt =
  .aria-label = Abstract illustration of three product reviews. One has a warning symbol indicating it may not be trustworthy.

shopping-callout-disabled-auto-open-title = Review Checker is now closed by default
shopping-callout-disabled-auto-open-subtitle = Click the price tag icon in the address bar whenever you want to see if you can trust a product’s reviews.
shopping-callout-disabled-auto-open-button = Got it

shopping-callout-opted-out-title = Review Checker is off
shopping-callout-opted-out-subtitle = To turn it back on, click the price tag icon in the address bar and follow the prompts.
shopping-callout-opted-out-button = Got it

## Onboarding message strings.

shopping-onboarding-headline = Try our trusted guide to product reviews

# Dynamic subtitle. Sites are limited to Amazon, Walmart or Best Buy.
# Variables:
#   $currentSite (str) - The current shopping page name
#   $secondSite (str) - A second shopping page name
#   $thirdSite (str) - A third shopping page name
shopping-onboarding-dynamic-subtitle-1 = See how reliable product reviews are on <b>{ $currentSite }</b> before you buy. Review Checker, an experimental feature from { -brand-product-name }, is built right into the browser. It works on <b>{ $secondSite }</b> and <b>{ $thirdSite }</b>, too.

# Subtitle for countries where we only support one shopping website (e.g. currently used in FR/DE with Amazon)
# Variables:
#   $currentSite (str) - The current shopping page name
shopping-onboarding-single-subtitle = See how reliable product reviews are on <b>{ $currentSite }</b> before you buy. Review Checker, an experimental feature from { -brand-product-name }, is built right into the browser.

shopping-onboarding-body = Using the power of { -fakespot-brand-full-name }, we help you avoid biased and inauthentic reviews. Our AI model is always improving to protect you as you shop. <a data-l10n-name="learn_more">Learn more</a>
shopping-onboarding-opt-in-privacy-policy-and-terms-of-use3 = By selecting “{ shopping-onboarding-opt-in-button }” you agree to { -brand-product-name }’s <a data-l10n-name="privacy_policy">privacy policy</a> and { -fakespot-brand-name }’s <a data-l10n-name="terms_of_use">terms of use</a>.
shopping-onboarding-opt-in-button = Yes, try it
shopping-onboarding-not-now-button = Not now
shopping-onboarding-dialog-close-button =
    .title = Close
    .aria-label = Close

# Aria-label to make the "steps" of the shopping onboarding container visible to screen readers.
# Variables:
#   $current (Int) - Number of the current page
#   $total (Int) - Total number of pages
shopping-onboarding-welcome-steps-indicator-label =
    .aria-label = Progress: step { $current } of { $total }

## Review Checker in Integrated sidebar

# Opt-in message strings for Review Checker when it is integrated into the global sidebar.
shopping-opt-in-integrated-headline = Shop with confidence

# Description text for regions where we support three sites. Sites are limited to Amazon, Walmart and Best Buy.
# Variables:
#   $firstSite (String) - The first shopping page name
#   $secondSite (String) - The second shopping page name
#   $thirdSite (String) - The third shopping page name
shopping-opt-in-integrated-subtitle = Turn on Review Checker from { -brand-product-name } to see how reliable product reviews are, before you buy. It uses AI technology to analyze reviews and works when you shop on { $firstSite }, { $secondSite }, and { $thirdSite }. <a data-l10n-name="learn_more">Learn more</a>

# Description text for regions where we support three sites. Sites are limited to Amazon, Walmart and Best Buy.
# Variables:
#   $firstSite (String) - The first shopping page name
#   $secondSite (String) - The second shopping page name
#   $thirdSite (String) - The third shopping page name
shopping-opt-in-integrated-subtitle-unsupported-site = Review Checker from { -brand-product-name } helps you know how reliable a product’s reviews are, before you buy. It uses AI technology to analyze reviews and works when you shop on { $firstSite }, { $secondSite }, and { $thirdSite }. <a data-l10n-name="learn_more">Learn more</a>

## Messages for callout for users not opted into the sidebar integrated version of Review Checker.

shopping-callout-opt-in-integrated-headline = Can you trust these reviews?

# Appears underneath shopping-opt-in-integrated-headline to answer the question 'Can you trust these reviews?'
shopping-callout-not-opted-in-integrated-paragraph1 = Turn on Review Checker from { -brand-product-name } to find out. It’s powered by { -fakespot-brand-full-name } and uses AI technology to analyze reviews.
shopping-callout-not-opted-in-integrated-paragraph2 = By selecting “{ shopping-opt-in-integrated-button }” you agree to { -brand-product-name }’s <a data-l10n-name="privacy_policy">privacy notice</a> and { -fakespot-brand-full-name }’s <a data-l10n-name="terms_of_use">terms of use</a>.
shopping-callout-not-opted-in-integrated-reminder-dismiss-button = Dismiss
shopping-callout-not-opted-in-integrated-reminder-accept-button = Turn on Review Checker

shopping-opt-in-integrated-privacy-policy-and-terms-of-use = Review Checker is powered by { -fakespot-brand-full-name }. By selecting “{ shopping-opt-in-integrated-button }” you agree to { -brand-product-name }’s <a data-l10n-name="privacy_policy">privacy notice</a> and { -fakespot-brand-name }’s <a data-l10n-name="terms_of_use">terms of use</a>.
shopping-opt-in-integrated-button = Try Review Checker

## Message strings for Review Checker's empty states.

shopping-empty-state-header = Ready to check reviews
shopping-empty-state-supported-site = View a product and { -brand-product-name } will check if the reviews are reliable.

# We show a list of sites supported by Review Checker whenever a user opens the feature in an unsupported site.
# This string will be displayed above the list of sites. The list will be hardcoded and does not require localization.
shopping-empty-state-non-supported-site = Review Checker works when you shop on:

## Confirm disabling Review Checker for newly opted out users

shopping-integrated-callout-opted-out-title = Review Checker is off
shopping-integrated-callout-opted-out-subtitle = To turn it back on, select the price tag in the sidebar and turn on Review Checker.

## Callout for where to find Review Checker when the sidebar closes

shopping-integrated-callout-sidebar-closed-title = Get back to Review Checker
shopping-integrated-callout-sidebar-closed-subtitle = Select the price tag in the sidebar to see if you can trust a product’s reviews.
shopping-integrated-callout-no-logo-sidebar-closed-subtitle = Select the sidebar button to see if you can trust a product’s reviews.

## Strings for a notification card about Review Checker's new position in the sidebar.
## The card will only appear for users that have the default sidebar position, which is on the left side for non RTL locales.
## Review Checker in the sidebar is only available to US users at this time, so we can assume that the default position is on the left side.

shopping-integrated-new-position-notification-title = Same Review Checker, new spot
shopping-integrated-new-position-notification-move-right-subtitle = Keep Review Checker and the rest of the { -brand-product-name } sidebar here — or move them to the right. Switch now or anytime in <a data-l10n-name="sidebar_settings">sidebar settings</a>.
shopping-integrated-new-position-notification-move-left-subtitle = Keep Review Checker and the rest of the { -brand-product-name } sidebar here — or move them to the left. Switch now or anytime in <a data-l10n-name="sidebar_settings">sidebar settings</a>.
shopping-integrated-new-position-notification-move-right-button = Move right
shopping-integrated-new-position-notification-move-left-button = Move left
shopping-integrated-new-position-notification-dismiss-button = Got it

## Combined setting for auto-open and auto-close.

shopping-settings-auto-open-and-close-toggle =
  .label = Automatically open and close Review Checker

# Description text for regions where we support three sites. Sites are limited to Amazon, Walmart and Best Buy.
# Variables:
#   $firstSite (String) - The first shopping page name
#   $secondSite (String) - The second shopping page name
#   $thirdSite (String) - The third shopping page name
shopping-settings-auto-open-and-close-description-three-sites = Opens when you view products on { $firstSite }, { $secondSite }, and { $thirdSite } and closes when you leave

# Description text for regions where we support only one site (e.g. currently used in FR/DE with Amazon).
# Variables:
#   $currentSite (String) - The current shopping page name
shopping-settings-auto-open-and-close-description-single-site = Opens when you view products on { $currentSite } and closes when you leave
