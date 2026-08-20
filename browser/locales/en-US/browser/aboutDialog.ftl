# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

aboutDialog-title =
    .title = About { -brand-full-name }

releaseNotes-link = What’s new

update-checkForUpdatesButton =
    .label = Check for updates
    .accesskey = C

update-updateButton =
    .label = Restart to update { -brand-shorter-name }
    .accesskey = R

update-checkingForUpdates = Checking for updates…
settings-update-checking-for-updates =
    .label = Checking for updates…

## Variables:
##   $transfer (string) - Transfer progress.

settings-update-downloading-2 =
    .label = Downloading update — { $transfer }
aboutdialog-update-downloading = Downloading update — <label data-l10n-name="download-status">{ $transfer }</label>

##

update-applying = Applying update…
settings-update-applying =
    .label = Applying update…

update-failed = Update failed. <label data-l10n-name="failed-link">Download the latest version</label>
update-failed-main =
    Update failed. <a data-l10n-name="failed-link-main">Download the latest version</a>

update-policy-disabled = Updates disabled by your organization
settings-update-policy-disabled =
    .label = Updates disabled by your organization
update-noUpdatesFound = { -brand-short-name } is up to date
settings-update-no-updates-found =
    .label = { -brand-short-name } is up to date
aboutdialog-update-checking-failed = Failed to check for updates.
settings-update-checking-failed =
    .label = Failed to check for updates.
update-otherInstanceHandlingUpdates = { -brand-short-name } is being updated by another instance
settings-update-other-instance-handling-updates =
    .label = { -brand-short-name } is being updated by another instance

## Variables:
##   $displayUrl (String): URL to page with download instructions. Example: www.mozilla.org/firefox/nightly/

aboutdialog-update-manual-with-link = Updates available at <label data-l10n-name="manual-link">{ $displayUrl }</label>
settings-update-manual-with-link = Updates available at <a data-l10n-name="manual-link">{ $displayUrl }</a>

update-unsupported = You can not perform further updates on this system. <label data-l10n-name="unsupported-link">Learn more</label>
settings-update-unsupported = You can not perform further updates on this system. <a data-l10n-name="unsupported-link">Learn more</a>

update-restarting = Restarting…
settings-update-restarting =
    .label = Restarting…

update-internal-error2 = Unable to check for updates due to internal error. Updates available at <label data-l10n-name="manual-link">{ $displayUrl }</label>
settings-update-internal-error = Unable to check for updates due to internal error. Updates available at <a data-l10n-name="manual-link">{ $displayUrl }</a>

##

# Variables:
#   $channel (String): description of the update channel (e.g. "release", "beta", "nightly" etc.)
aboutdialog-channel-description = You are currently on the <label data-l10n-name="current-channel">{ $channel }</label> update channel.

warningDesc-version = { -brand-short-name } is experimental and may be unstable.

aboutdialog-help-user = { -brand-product-name } Help
aboutdialog-submit-feedback = Submit Feedback

community-exp = <label data-l10n-name="community-exp-mozillaLink">{ -vendor-short-name }</label> is a <label data-l10n-name="community-exp-creditsLink">global community</label> working together to keep the Web open, public and accessible to all.

community-2 = { -brand-short-name } is designed by <label data-l10n-name="community-mozillaLink">{ -vendor-short-name }</label>, a <label data-l10n-name="community-creditsLink">global community</label> working together to keep the Web open, public and accessible to all.

helpus = Want to help? <label data-l10n-name="helpus-donateLink">Make a donation</label> or <label data-l10n-name="helpus-getInvolvedLink">get involved!</label>

helpus-referrals2 = Want to help? <label data-l10n-name="helpus-donateLink">Make a donation</label>, <label data-l10n-name="helpus-shareFirefoxLink">share { -brand-product-name }</label>, or <label data-l10n-name="helpus-getInvolvedLink">get involved!</label>

bottomLinks-license = Licensing Information
bottom-links-terms = Terms of Use
bottom-links-privacy = Privacy Notice

# Example of resulting string: 66.0.1 (64-bit)
# Variables:
#   $version (String): version of Firefox, e.g. 66.0.1
#   $bits (Number): bits of the architecture (32 or 64)
aboutDialog-version = { $version } ({ $bits }-bit)

# Example of resulting string: 66.0a1 (2019-01-16) (64-bit)
# Variables:
#   $version (String): version of Firefox for Nightly builds, e.g. 66.0a1
#   $isodate (String): date in ISO format, e.g. 2019-01-16
#   $bits (Number): bits of the architecture (32 or 64)
aboutDialog-version-nightly = { $version } ({ $isodate }) ({ $bits }-bit)

# Example of resulting string: 131.0a1 (aarch64)
# Variables:
#   $version (String): version of Firefox, e.g. 66.0.1
#   $arch (String): name of the architecture (arm, aarch64, etc.)
aboutdialog-version-arch = { $version } ({ $arch })

# Example of resulting string: 131.0a1 (2024-08-27) (aarch64)
# Variables:
#   $version (String): version of Firefox for Nightly builds, e.g. 66.0a1
#   $isodate (String): date in ISO format, e.g. 2019-01-16
#   $arch (String): name of the architecture (arm, aarch64, etc.)
aboutdialog-version-arch-nightly = { $version } ({ $isodate }) ({ $arch })
