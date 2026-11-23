# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

# The title of the about:translations page.
about-translations-title = { -brand-short-name } translations

# The brief description of the Translations functionality on the page.
about-translations-description = Instant translations that respect your privacy.
about-translations-learn-more-link = Learn more

# This button closes the about:translations page's split-view tab, and leaves the adjacent tab open as a full tab.
about-translations-split-view-close-button =
  .title = Close { -brand-short-name } translations

# An info message displayed if the device's hardware is not compatible with the Translations feature requirements.
about-translations-unsupported-info-message-2 =
  .heading = Translation isn’t available on this device
  .message = Try switching to a different device.

# An info message displayed when translations are disabled and locked by enterprise policy.
about-translations-policy-disabled-info-message =
  .heading = Translations aren’t available for your organization
  .message = This is a policy set by your administrator.

# An info message displayed when translations are disabled but can still be unblocked.
about-translations-feature-blocked-info-message =
  .heading = To translate text, unblock this feature
  .message = It looks like you updated AI controls to block this feature. Unblock any time to start using it.
about-translations-feature-blocked-unblock-button = Unblock

# An error message displayed when the language list fails to load.
about-translations-language-load-error-message-2 =
  .heading = Couldn’t load languages
  .message = Check your internet connection and try again.
about-translations-language-load-error-button = Try again

# An error message displayed when a translation request fails.
about-translations-translation-error-heading = There was a problem translating
about-translations-translation-error-subtext = Please try again.
about-translations-translation-error-button = Try again

# An informational message displayed when the detected language is not supported.
# Variables:
#   $language (String) - The display name of the detected language.
about-translations-detected-language-unsupported-heading-2 = Sorry, we don’t support translations from { $language } yet
about-translations-detected-language-unsupported-heading-unknown-2 = Sorry, we don’t support translations from this language yet
about-translations-detected-language-unsupported-subtext = Please choose another language.

# Placeholder text shown in the source-language text area when the user has not typed any text.
about-translations-input-placeholder =
  .placeholder = Add text to translate

# Text displayed on the source-language selector when no explicit option is selected
# and no language has been identified from the content of the source-language text area.
about-translations-detect-default-label =
  .label = Detect language

# Text displayed on the source-language selector when no explicit option is selected
# and a valid language has been identified from the content of the source-language text area.
# Variables:
#   $language (string) - The localized display name of the detected language
about-translations-detect-language-label =
  .label = { $language } (detected)

# Placeholder text shown in the target-language output area when no translation has occurred.
about-translations-output-placeholder =
  .placeholder = Translation

# Button label for copying the translated output to the clipboard.
about-translations-copy-button-default =
  .label = Copy
  .title = Copy translation

# Button label shown after the translated output has been copied to the clipboard.
about-translations-copy-button-copied =
  .label = Copied
  .title = Copy translation

# Text displayed on target-language selector when no language option is selected.
about-translations-select-label =
  .label = Select language

# A message displayed in the target-language output area while waiting for the translation to complete.
about-translations-translating-message = Translating…

# The title attribute for the swap languages button, which swaps the selected
# source and target languages, reversing the direction of translation.
about-translations-swap-languages =
  .title = Swap languages

# The title attribute for the button that clears the source text area.
about-translations-clear-button =
  .title = Clear source text
