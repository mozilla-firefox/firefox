# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

## Toolbar button tooltip reflects VPN state

ipprotection-button =
  .label = VPN
  .tooltiptext = VPN

ipprotection-button-error =
  .label = Turn VPN on
  .tooltiptext = Turn VPN on

##

# The word "Beta" is intended to be uppercase in the experiment label.
ipprotection-experiment-badge =
  .label = BETA

ipprotection-help-button =
  .tooltiptext = Open VPN support page

ipprotection-title = VPN

## Feature introduction callout

# Original strings

ipprotection-feature-introduction-title = Introducing VPN, now right inside your browser
ipprotection-feature-introduction-title-1 = Try { -brand-product-name }’s built-in VPN
ipprotection-feature-introduction-link-text-2 = Use our new <a data-l10n-name="learn-more-vpn">built-in VPN</a> to hide your location and protect your data.

# Unlimited bandwidth summer promotion

ipprotection-feature-introduction-text-summer-promo-1 = Turn it on to keep your browsing more private. <a data-l10n-name="summer-promo-link">Get unlimited bandwidth</a> and more locations to browse from. Now until August 31.
ipprotection-feature-introduction-title-summer-promo = Got travel plans? Take privacy with you.
ipprotection-feature-introduction-description-summer-promo = Go farther with { -brand-product-name }’s built-in VPN: more locations, unlimited bandwidth. Now until August 31.

## Unlimited bandwidth summer promotion offramp callouts

# Generic summer promo offramp message
ipprotection-summer-promo-offramp-generic-title = Your built-in VPN limits reset September 1
# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ipprotection-summer-promo-offramp-generic-description = Use your { $maxUsage } GB and 6 locations for extra privacy to make your browsing harder to trace back to you.

# Generic summer promo offramp message for users who cannot upgrade to Mozilla VPN due to locale and already have Firefox as their default browser
# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ipprotection-summer-promo-offramp-generic-description-default-browser-users-no-upgrade = Use your { $maxUsage } GB and more than 20 locations for extra privacy to make your browsing harder to trace back to you.

# Mozilla VPN subscriber summer promo offramp message
# Message shown to current subscribers of Mozilla VPN
# Refers to subscribers now receiving unlimited bandwidth and more locations in the Firefox built-in VPN
ipprotection-summer-promo-offramp-subscriber-title = Your built-in VPN just got better
ipprotection-summer-promo-offramp-subscriber-description = You now get unlimited bandwidth and more locations as a { -mozilla-vpn-brand-name } subscriber.

# Default browser incentive summer promo offramp message
# "Make Firefox your go-to browser" refers to setting Firefox to default.
# This appears in a promo message with a button labeled "Set to default"
ipprotection-summer-promo-offramp-default-browser-incentive-title = Keep your extra built-in VPN locations
ipprotection-summer-promo-offramp-default-browser-incentive-description = Make { -brand-product-name } your go-to browser and get more than 20 extra places to browse from after August 31.

# Mozilla VPN upsell summer promo offramp message
# "Level up" refers to enhancing VPN functionality from the Firefox built-in VPN
ipprotection-summer-promo-offramp-subscription-upsell-title = Level up with { -mozilla-vpn-brand-name }
ipprotection-summer-promo-offramp-subscription-upsell-description = Keep unlimited bandwidth after August 31, with 300+ locations on 5 devices to make your browsing harder to trace back to you.

## Summer promo offramp callout buttons

ipprotection-summer-promo-offramp-open-vpn-primary-button = Open VPN
ipprotection-summer-promo-offramp-set-to-default-primary-button = Set to default
ipprotection-summer-promo-offramp-get-subscription-button = Get { -mozilla-vpn-brand-name }
ipprotection-summer-promo-offramp-dismiss-secondary-button = Dismiss

## Callout shown when the user opens a private browsing window

ipprotection-feature-introduction-title-private-browsing = Maximize privacy with { -brand-product-name }’s built-in VPN
ipprotection-feature-introduction-link-text-private-browsing-2 = Use our new <a data-l10n-name="learn-more-vpn">built-in VPN</a> to hide your location and protect your data, even when you’re in a Private Window.
ipprotection-feature-introduction-description-private-browsing = Browse with extra protection by hiding your location, even when you’re in a Private Window.
# Alternate version of the feature discovery callout copy used after we've released site inclusions for the VPN. Users have the option to turn the VPN on for specific sites only
ipprotection-feature-introduction-description-private-browsing-3 = Help hide your location as you browse. Set rules to turn on VPN when you want <a data-l10n-name="learn-more-vpn">extra privacy</a> or location-based browsing, and off when you don’t.

## Callout shown when the user logs in to a captive portal network

ipprotection-feature-introduction-title-captive-portal = On public Wi-Fi? Try { -brand-product-name }’s built-in VPN.
ipprotection-feature-introduction-description-captive-portal = Browse with extra protection by hiding your location, even on public Wi-Fi.

## Callout shown to users who have expressed an interest in privacy and security

ipprotection-feature-introduction-title-privacy = Add another layer of privacy
ipprotection-feature-introduction-title-privacy-1 = Add another layer of privacy with built-in VPN
ipprotection-feature-introduction-link-text-privacy-3 = Get <a data-l10n-name="learn-more-vpn">extra privacy</a> by choosing from multiple locations to hide where you browse.

# Used for multiple discovery callouts after release of the site inclusions feature
ipprotection-feature-introduction-description-inclusions = Help hide your location for <a data-l10n-name="learn-more-vpn">extra privacy</a> as you browse. Set the VPN on or off for certain sites.

# Used for discovery callouts for both captive portal login and private browsing
ipprotection-feature-introduction-link-text-captive-portal-1 = Get <a data-l10n-name="learn-more-vpn">extra privacy</a> by choosing from several locations to hide where you browse.

## Buttons used for all feature introduction callouts

ipprotection-feature-introduction-button-primary = Next
ipprotection-feature-introduction-button-secondary-not-now = Not now
ipprotection-feature-introduction-button-secondary-not-now-menuitem =
    .label = Not now
ipprotection-feature-introduction-button-secondary-no-thanks = No thanks
ipprotection-feature-introduction-button-secondary-no-thanks-menuitem =
    .label = No thanks
ipprotection-feature-introduction-button-secondary-remove = Remove VPN from toolbar
ipprotection-feature-introduction-button-secondary-remove-1 =
    .label = Remove VPN from toolbar
ipprotection-feature-introduction-button-open-vpn = Open VPN
ipprotection-feature-introduction-button-get-started = Get started

## Site settings callout

ipprotection-site-settings-callout-title = Choose where you use VPN
ipprotection-site-settings-callout-subtitle = Turn VPN off for a specific site and we’ll remember it next time you visit.
ipprotection-site-settings-callout-button = Got it

## Location selection callout

ipprotection-location-selection-callout-title = New: Switch up your location
ipprotection-location-selection-callout-description-1 = <a data-l10n-name="learn-more-vpn">{ -brand-product-name }’s built-in VPN</a> lets you choose from several browsing locations, or let us pick the fastest one for you.
ipprotection-location-selection-callout-primary-button = Try it
ipprotection-location-selection-callout-secondary-button = Dismiss

## VPN for Android promo callout

ipprotection-android-promo-callout-title = Take built-in VPN with you on Android
ipprotection-android-promo-callout-description = Scan the QR code to get { -brand-product-name } for Android and turn on built-in VPN for more privacy on the go. Coming soon to iOS.
ipprotection-android-promo-callout-primary-button = Got it

## Feature discovery callout message advertising a feature where the user can turn on the VPN for specific sites only

# Here 'browse on' means continue browsing
ipprotection-site-inclusions-callout-title-existing-users = Set built-in VPN rules, then browse on
ipprotection-site-inclusions-callout-title-lapsed-users = Try built-in VPN, now site by site
ipprotection-site-inclusions-callout-description-1 = Turn on VPN when you want extra privacy or location-based browsing, and off where you don’t.
ipprotection-site-inclusions-callout-description-lapsed-users = Set rules to turn on VPN when you want extra privacy or location-based browsing, and off when you don’t.
ipprotection-site-inclusions-callout-primary-button = Set rules
ipprotection-site-inclusions-callout-secondary-button-existing-users = Not now
ipprotection-site-inclusions-callout-secondary-button-lapsed-users = Dismiss

## Holiday Promotion Callout

ipprotection-holiday-promo-title-1 = Get unlimited VPN until January 31
ipprotection-holiday-promo-description = Make your browsing private and harder to trace with built-in VPN.
ipprotection-holiday-promo-primary-button = Open VPN
ipprotection-holiday-promo-secondary-button = Dismiss

## Panel

# Also used for the callout shown in private browsing
unauthenticated-vpn-title = Try { -brand-product-name }’s built-in VPN

unauthenticated-hide-location-message-3 = <a data-l10n-name="learn-more-vpn">Hide your location</a> while browsing in { -brand-product-name }.
unauthenticated-private-location-message = Helps <a data-l10n-name="learn-more-vpn">keep your location private</a> in { -brand-product-name }.
unauthenticated-choose-location-message-1 = Choose from several locations or let { -brand-product-name } pick the fastest one.
unauthenticated-site-rules-message = Control which sites use VPN and which sites don’t.
unauthenticated-get-started = Get started
unauthenticated-terms-of-service-privacy-notice = By proceeding, you agree to the <a data-l10n-name="vpn-terms-of-service">Terms of Service</a> and <a data-l10n-name="vpn-privacy-notice">Privacy Notice</a>.

site-exclusion-toggle-enabled-1 =
  .label = Use VPN for this site
  .aria-label = VPN is on for this site
site-exclusion-toggle-disabled-1 =
  .label = Use VPN for this site
  .aria-label = VPN is off for this site
site-exclusion-toggle-description = Site not working? Try turning VPN off.

# Manages rules for VPN to turn on or off automatically for certain websites
site-rules-manage-rules-link-text = Manage VPN rules

# Heading for user defined rules on VPN usage for particular websites
site-rules-status-heading = Your rule
# Used in the panel when a user navigates to a site where the VPN is off due to a site exclusion
site-rules-description-exclusion = VPN is off for this site
# Used in the panel when a user navigates to a site where the VPN is on due to a site inclusion
site-rules-description-inclusion = VPN is on for this site

# Message shown in the panel the first time a user turns on the VPN, offering to always turn on VPN for the current website
# Variables
#   $website (string) - The host of the current website (e.g. "example.com")
site-rules-inclusion-prompt =
  .heading = Always turn on VPN for { $website }?
  .message = Update your rules anytime in settings.

ipprotection-settings-link =
  .label = Settings

## Status card

ipprotection-connection-status-connected-1 = VPN is on
  .aria-label = VPN is on
ipprotection-connection-status-disconnected-1 = VPN is off
  .aria-label = VPN is off
ipprotection-connection-status-excluded-1 = VPN is off for this site
  .aria-label = VPN is off for this site
ipprotection-connection-status-connecting-1 = VPN is connecting…
  .aria-label = VPN is connecting…

# Button to turn off the VPN
ipprotection-button-turn-vpn-off-1 = Turn off
# Button to turn off the VPN when the VPN panel is open while viewing
# a page from an excluded site.
ipprotection-button-turn-vpn-off-excluded-site = Turn off VPN everywhere
# Button to turn on the VPN
ipprotection-button-turn-vpn-on-1 = Turn on
# Button while VPN is connecting
ipprotection-button-connecting = Turning on…

## Location controls

# The button displays the selected VPN location.
# This shows the default selection, "Recommended" which is the recommended location as determined by Firefox.
ipprotection-recommended-location-button = Location: Recommended
ipprotection-recommended-location-description = { -brand-product-name } finds the fastest location

# Variables
#   $country (string) - The country selected for the VPN server location
ipprotection-location-country-button = Location: { $country }

ipprotection-locations-subview =
    .title = Choose location
ipprotection-locations-subview-description = Choose a different location to browse from.

ipprotecion-locations-subview-recommended-label = Recommended
ipprotection-locations-subview-recommended-description = Finds the fastest location

# Label shown next to a VPN location that the user cannot select.
# The aria-label is for accessibility, and should communicate that the
# location is unavailable and the button is disabled.
ipprotection-locations-unavailable-label-1 = Unavailable
  .aria-label = Unavailable, disabled

ipprotection-locations-subview-promo =
  .heading = Take protection further with { -mozilla-vpn-brand-name }
  .message = Choose from 300+ locations and protect all your apps on up to 5 devices.
ipprotection-locations-subview-promo-button = Get { -mozilla-vpn-brand-name }

## VPN paused state

ipprotection-connection-status-paused-title-2 = VPN is paused
  .aria-label = VPN is paused
# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ipprotection-connection-status-paused-description-1 = You’ve used all { $maxUsage } GB of your VPN data. Access resets next month.
upgrade-vpn-title = Get extra protection beyond the browser
upgrade-vpn-description = Choose your VPN location, use VPN for all of your apps and up to 5 devices, and stay secure on any network — at home or on public Wi-Fi.
upgrade-vpn-button = Try { -mozilla-vpn-brand-name }

## Messages and errors

ipprotection-connection-status-generic-error-title-1 = Couldn’t connect to VPN
  .aria-label = Couldn’t connect to VPN
ipprotection-connection-status-generic-error-description = Try again in a few minutes.
ipprotection-connection-status-generic-error-try-again = Please try again later.

ipprotection-connection-status-network-error-title-1 = Check your internet connection
  .aria-label = Check your internet connection
ipprotection-connection-status-network-error-description = Connect to the internet, then try turning VPN on.

ipprotection-connection-status-blocked-error-title-1 = VPN is unavailable
  .aria-label = VPN is unavailable
# "Where" refers to the user's location. It does not refer to apps or devices because the built-in VPN only protects a user's browsing in Firefox
ipprotection-connection-status-blocked-error-description-1 = Local laws and restrictions limit where you can use VPN. <a data-l10n-name="learn-more-link">Learn more</a>

# Variables
#   $usageLeft (string) - The amount of data a user has left in a month (in GB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ipprotection-message-bandwidth-warning =
  .heading = Getting close to your VPN limit
  .message = You have { $usageLeft } GB of { $maxUsage } GB left this month.

# Variables
#   $usageLeft (number) - The amount of data a user has left in a month (in MB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ipprotection-message-bandwidth-warning-mb =
  .heading = Getting close to your VPN limit
  .message = You have { $usageLeft } MB of { $maxUsage } GB left this month.

ipprotection-message-continuous-onboarding-intro = Turn on VPN to hide your location and add extra encryption to your browsing.
ipprotection-message-continuous-onboarding-autostart = <a data-l10n-name="setting-link">Set VPN to turn on</a> every time you open { -brand-short-name } for an extra layer of protection.
ipprotection-message-continuous-onboarding-site-settings = { -brand-short-name } will remember which websites you’ve set to use VPN. Update these in <a data-l10n-name="setting-link">settings</a> anytime.

confirmation-hint-ipprotection-navigated-to-excluded-site = VPN is off for this site
confirmation-hint-ipprotection-navigated-to-included-site = VPN is on for this site

ipprotection-open-button = Open { -brand-product-name }
ipprotection-come-back-title = Come back to try built-in VPN
ipprotection-message-body-hide-location = Hide your location when you browse in { -brand-product-name } for more privacy and control.

## IP Protection bandwidth callouts

ipprotection-bandwidth-upgrade-title = Like built-in VPN? Get even more protection outside { -brand-product-name } with { -mozilla-vpn-brand-name }.
ipprotection-bandwidth-upgrade-text = Choose a VPN location and add protection to all your apps on up to 5 devices, whether you’re at home or on public Wi-Fi.

## IP Protection bandwidth warning infobar

# Variables
#   $usageLeft (string) - The amount of data a user has left in a month (in GB)
ip-protection-bandwidth-warning-infobar-message-75 = <strong>Getting close to your VPN limit.</strong> You have { $usageLeft } GB left. Your data will reset at the start of next month.

# Variables
#   $usageLeft (string) - The amount of data a user has left in a month (in GB)
ip-protection-bandwidth-warning-infobar-message-90 = <strong>You’re almost out of VPN data.</strong> You have { $usageLeft } GB left. Once you use it all, your VPN will pause until your data resets on the first of next month.

# Variables
#   $usageLeft (number) - The amount of data a user has left in a month (in MB)
ip-protection-bandwidth-warning-infobar-message-90-mb = <strong>You’re almost out of VPN data.</strong> You have { $usageLeft } MB left. Once you use it all, your VPN will pause until your data resets on the first of next month.

## IP Protection Settings

ip-protection-description =
  .label = VPN
  .description = Built-in VPN to enhance your privacy while browsing on { -brand-short-name }.
ip-protection-description-1 =
  .label = Built-in VPN
  .description = Get extra privacy by hiding your location while browsing.
ip-protection-learn-more = Learn more

ip-protection-not-opted-in-4 =
  .heading = Try { -brand-short-name }’s built-in VPN
  .message = Browse with extra protection by hiding your location.
# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-not-opted-in-inclusions-enabled =
  .heading = Ready to try it?
  .message = Get { $maxUsage } GB free each month and choose which sites use VPN.
ip-protection-not-opted-in-button = Get started

# Variables:
#   $count (number) - The number of sites saved as VPN exclusions.
ip-protection-site-exceptions-all-sites-button =
  .label = Manage website settings
  .description =
    { $count ->
        [one] { $count } website
       *[other] { $count } websites
    }

ip-protection-site-rules-header-1 =
  .heading = Manage VPN rules
ip-protection-site-rules-button-1 =
  .label = Manage VPN rules
  .description = Set rules for sites that need extra privacy or VPN turned off.

ip-protection-site-rules-list-section-1 =
  .label = Website rules
  .description = Set rules to turn VPN on or off automatically.

ip-protection-site-rules-add-button =
  .label = Set rule
ip-protection-site-rules-delete-all-button =
  .label = Delete all rules
ip-protection-site-rules-empty = Rules you set will appear here.

# Shown on a website whose rule turns the VPN on for it
ip-protection-site-rules-rule-included = VPN always on
# Shown on a website whose rule turns the VPN off for it
ip-protection-site-rules-rule-excluded = VPN always off

# Variables:
#   $website (string) - The website the rule applies to, e.g. https://example.com
ip-protection-site-rules-edit-button =
  .title = Edit
  .aria-label = Edit the rule for { $website }
# Variables:
#   $website (string) - The website the rule applies to, e.g. https://example.com
ip-protection-site-rules-delete-button =
  .title = Delete
  .aria-label = Delete the rule for { $website }

ip-protection-autostart =
  .label = Turn on VPN automatically
ip-protection-autostart-checkbox =
  .label = When I open { -brand-short-name }
ip-protection-autostart-private-checkbox =
  .label = In private windows

ip-protection-vpn-upgrade-link =
  .label = Get even more protection outside { -brand-short-name } with { -mozilla-vpn-brand-name }
  .description = Choose custom VPN locations and add protection to all your apps on up to five devices, whether you’re at home or on public Wi-Fi.
ip-protection-vpn-upgrade-link-1 =
  .label = Take protection further with { -mozilla-vpn-brand-name }
  .description = Choose from 300+ locations and protect all your apps on up to 5 devices.

## IP Protection dialogs

ip-protection-exceptions-dialog-window =
  .title = Manage website settings
ip-protection-exclusions-desc = Use VPN for all websites except ones on this list. Add a website here or by opening VPN.

ip-protection-site-rule-window =
  .title = Set rule
ip-protection-edit-site-rule-window =
  .title = Edit rule
ip-protection-site-rule-dialog =
  .buttonlabelaccept = Set
  .buttonaccesskeyaccept = S
ip-protection-edit-site-rule-dialog =
  .buttonlabelaccept = Save
  .buttonaccesskeyaccept = S
# VPN status refers to a state of 'always on' or 'always off' applied to a site
ip-protection-site-rule-intro = Enter a site, then select its VPN status.
# "ex:" is short for "example".
ip-protection-site-rule-website-field =
  .label = Website
  .placeholder = ex: acme.com
# VPN status refers to a state of 'always on' or 'always off' applied to a site
ip-protection-site-rule-status-field =
  .label = VPN status
ip-protection-site-rule-status-on =
  .label = Always on
ip-protection-site-rule-status-off =
  .label = Always off
# Shown below the website field when what was typed is not a website address.
ip-protection-site-rule-invalid-error = Enter a valid website address.
# Shown below the website field when the typed website already has a rule.
ip-protection-site-rule-duplicate-error = This site already has a rule.
# Shown at the top of the dialog when the rule could not be saved.
ip-protection-site-rule-save-error =
  .message = Error creating rule. Please try again.
# Variables:
#   $website (string) - The website the rule applies to, e.g. https://example.com
ip-protection-delete-site-rule-message = Delete rule for { $website }?
ip-protection-delete-site-rule-cancel =
  .label = Cancel
ip-protection-delete-site-rule-confirm =
  .label = Delete
ip-protection-delete-all-site-rules-message = Delete all website rules?
ip-protection-delete-all-site-rules-confirm =
  .label = Delete all rules
# Shown in place of the delete confirmation when deleting a rule fails.
ip-protection-delete-site-rule-error = Couldn’t delete rule
# Shown in place of the delete confirmation when deleting all rules fails.
ip-protection-delete-all-site-rules-error = Couldn’t delete rules
ip-protection-delete-site-rule-retry =
  .label = Try again

## IP Protection Bandwidth

ip-protection-bandwidth-header-1 = Monthly data limit

# Variables
#   $usageLeft (string) - The amount of data a user has left in a month (in GB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-this-month-gb = { $usageLeft } GB of { $maxUsage } GB left this month

# The text inside the <span> is emphasized to highlight the amount of data left.
# Variables
#   $usageLeft (string) - The amount of data a user has left in a month (in GB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-gb-1 = <span data-l10n-name="usage">{ $usageLeft } GB</span> of { $maxUsage } GB left

# Variables
#   $usageLeft (number) - The amount of data a user has left in a month (in MB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-this-month-mb = { $usageLeft } MB of { $maxUsage } GB left this month

# The text inside the <span> is emphasized to highlight the amount of data left.
# Variables
#   $usageLeft (number) - The amount of data a user has left in a month (in MB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-mb-1 = <span data-l10n-name="usage">{ $usageLeft } MB</span> of { $maxUsage } GB left

# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-hit-for-the-month = You’ve used all { $maxUsage } GB of your VPN data. Access will reset next month.

# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-help-text = Resets to { $maxUsage } GB on the first of every month.

## IP Protection bandwidth reset callout

# Variables
#  $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ipprotection-bandwidth-reset-title = { $maxUsage } GB of VPN, refreshed and ready to go
ipprotection-bandwidth-reset-text = Turn on VPN for an extra privacy boost, free every month.
ipprotection-bandwidth-reset-button = Got it

## IP Protection add-on breakage warnings

ipp-activator-breakage-sign-in-warning = <strong>This website may not work with a VPN.</strong> Try signing in or turning VPN off while you use this website.
ipp-activator-breakage-turn-off-warning = <strong>This website may not work with a VPN.</strong> Try turning VPN off while you use this website.

## IP Protection alerts

vpn-paused-alert-title = VPN paused

# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
vpn-paused-alert-body = You’ve used all { $maxUsage } GB of your VPN data. VPN access will reset next month.

# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
vpn-error-page-paused-description = You’ve used all { $maxUsage } GB of your VPN data. Access resets next month.
vpn-error-page-continue-description = Choose how to continue without VPN
vpn-error-page-keep-browsing = Keep browsing in this session
vpn-error-page-new-session = Start a new session

vpn-paused-alert-close-tabs-button = Close all tabs
vpn-paused-alert-continue-wo-vpn-button = Continue without VPN

vpn-error-alert-title-1 = VPN and rules aren’t working right now.
vpn-error-alert-body-1 = Turning VPN on and off may fix the issue.

##
