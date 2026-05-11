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
ipprotection-feature-introduction-link-text-2 = Use our new <a data-l10n-name="learn-more-vpn">built-in VPN</a> to hide your location and protect your data.

# Used for callout for users who expressed interest in privacy in onboarding
ipprotection-feature-introduction-title-privacy = Add another layer of privacy
ipprotection-feature-introduction-link-text-privacy = <a data-l10n-name="learn-more-vpn">{ -brand-product-name }’s built-in VPN</a> helps protect your browsing. Choose from 5 locations to keep where you browse more private.

# Original strings for private browsing callout

ipprotection-feature-introduction-link-text-private-browsing-2 = Use our new <a data-l10n-name="learn-more-vpn">built-in VPN</a> to hide your location and protect your data, even when you’re in a Private Window.
ipprotection-feature-introduction-description-private-browsing = Browse with extra protection by hiding your location, even when you’re in a Private Window.

# Used for callout shown on login to public wi-fi through a captive portal
ipprotection-feature-introduction-title-captive-portal = On public Wi-Fi? Try { -brand-product-name }’s built-in VPN.
ipprotection-feature-introduction-description-captive-portal = Browse with extra protection by hiding your location, even on public Wi-Fi.

# Used for discovery callouts for both captive portal login and private browsing
ipprotection-feature-introduction-link-text-captive-portal = Get <a data-l10n-name="learn-more-vpn">extra privacy</a> by choosing from 5 locations to hide where you browse.

## Buttons used for all feature introduction callouts

ipprotection-feature-introduction-button-primary = Next
ipprotection-feature-introduction-button-secondary-not-now = Not now
ipprotection-feature-introduction-button-secondary-no-thanks = No thanks
ipprotection-feature-introduction-button-secondary-remove = Remove VPN from toolbar

## Site settings callout

ipprotection-site-settings-callout-title = Choose where you use VPN
ipprotection-site-settings-callout-subtitle = Turn VPN off for a specific site and we’ll remember it next time you visit.
ipprotection-site-settings-callout-button = Got it

## Location selection callout

ipprotection-location-selection-callout-title = New: Switch up your location
ipprotection-location-selection-callout-description = <a data-l10n-name="learn-more-vpn">{ -brand-product-name }’s built-in VPN</a> lets you choose from 5 browsing locations, or let us pick the fastest one for you.
ipprotection-location-selection-callout-primary-button = Try it
ipprotection-location-selection-callout-secondary-button = Dismiss

## Panel

# Also used for the callout shown in private browsing
unauthenticated-vpn-title = Try { -brand-product-name }’s built-in VPN

unauthenticated-hide-location-message-3 = <a data-l10n-name="learn-more-vpn">Hide your location</a> while browsing in { -brand-product-name }.
unauthenticated-private-location-message = Helps <a data-l10n-name="learn-more-vpn">keep your location private</a> in { -brand-product-name }.
unauthenticated-choose-location-message = Choose from 5 locations or let { -brand-product-name } pick the fastest one.
# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
unauthenticated-bandwidth-limit-message = Get { $maxUsage } GB of free VPN data every month.
unauthenticated-get-started = Get started
unauthenticated-terms-of-service-privacy-notice = By proceeding, you agree to the <a data-l10n-name="vpn-terms-of-service">Terms of Service</a> and <a data-l10n-name="vpn-privacy-notice">Privacy Notice</a>.

site-exclusion-toggle-enabled-1 =
  .label = Use VPN for this site
  .aria-label = VPN is on for this site
site-exclusion-toggle-disabled-1 =
  .label = Use VPN for this site
  .aria-label = VPN is off for this site
site-exclusion-toggle-description = Site not working? Try turning VPN off.

ipprotection-settings-link =
  .label = Settings

## Status card

ipprotection-connection-status-connected = VPN is on
ipprotection-connection-status-disconnected = VPN is off
ipprotection-connection-status-excluded = VPN is off for this site
ipprotection-connection-status-connecting = VPN is connecting…

# Button to turn off the VPN
ipprotection-button-turn-vpn-off = Turn off VPN
# Button to turn off the VPN when the VPN panel is open while viewing
# a page from an excluded site.
ipprotection-button-turn-vpn-off-excluded-site = Turn off VPN everywhere
# Button to turn on the VPN
ipprotection-button-turn-vpn-on = Turn on VPN
# Button while VPN is connecting
ipprotection-button-connecting = Turning on…

## Location controls

# The button displays the selected VPN location.
# This shows the default selection, "Recommended" which is the recommended location as determined by Firefox.
ipprotection-recommended-location-button = Location: Recommended
ipprotection-recommended-location-description = { -brand-product-name } finds the fastest location
ipprotection-recommended-location-badge = NEW

# Variables
#   $country (string) - The country selected for the VPN server location
ipprotection-location-country-button = Location: { $country }

ipprotection-locations-subview =
    .title = Choose location
ipprotection-locations-subview-description = Choose a different location to browse from.

ipprotecion-locations-subview-recommended-label = Recommended
ipprotection-locations-subview-recommended-description = Finds the fastest location

ipprotection-locations-subview-promo =
  .heading = Take protection further with { -mozilla-vpn-brand-name }
  .message = Choose from 300+ locations and protect all your apps on up to 5 devices.
ipprotection-locations-subview-promo-button = Get { -mozilla-vpn-brand-name }

## VPN paused state

ipprotection-connection-status-paused-title-1 = VPN is paused
# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ipprotection-connection-status-paused-description-1 = You’ve used all { $maxUsage } GB of your VPN data. Access resets next month.
upgrade-vpn-title = Get extra protection beyond the browser
upgrade-vpn-description = Choose your VPN location, use VPN for all of your apps and up to 5 devices, and stay secure on any network — at home or on public Wi-Fi.
upgrade-vpn-button = Try { -mozilla-vpn-brand-name }

## Messages and errors

ipprotection-connection-status-generic-error-title = Couldn’t connect to VPN
ipprotection-connection-status-generic-error-description = Try again in a few minutes.
ipprotection-connection-status-generic-error-try-again = Please try again later.

ipprotection-connection-status-network-error-title = Check your internet connection
ipprotection-connection-status-network-error-description = Connect to the internet, then try turning VPN on.

ipprotection-connection-status-blocked-error-title = VPN is unavailable
ipprotection-connection-status-blocked-error-description = Local laws prevent us from providing VPN service in this region. <a data-l10n-name="learn-more-link">Learn more</a>

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

# Variables:
#   $maxUsage (number) - The bandwidth limit of free VPN, in GB
ip-protection-not-opted-in-2 =
  .heading = Try { -brand-short-name }’s built-in VPN
  .message = Hide your location while browsing in { -brand-short-name }. Get { $maxUsage } GB of free VPN data every month.
# Variables
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-not-opted-in-3 =
  .heading = Try { -brand-short-name }’s built-in VPN
  .message = Browse with extra protection by hiding your location. Get { $maxUsage } GB of free VPN data every month.
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

## IP Protection Bandwidth

ip-protection-bandwidth-header-1 = Monthly data limit

# Variables
#   $usageLeft (string) - The amount of data a user has left in a month (in GB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-this-month-gb = { $usageLeft } GB of { $maxUsage } GB left this month

# Variables
#   $usageLeft (string) - The amount of data a user has left in a month (in GB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-gb = { $usageLeft } GB of { $maxUsage } GB left

# Variables
#   $usageLeft (number) - The amount of data a user has left in a month (in MB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-this-month-mb = { $usageLeft } MB of { $maxUsage } GB left this month

# Variables
#   $usageLeft (number) - The amount of data a user has left in a month (in MB)
#   $maxUsage (number) - The maximum amount of data a user can use in a month (in GB)
ip-protection-bandwidth-left-mb = { $usageLeft } MB of { $maxUsage } GB left

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

vpn-error-alert-title = VPN isn’t working right now.
vpn-error-alert-body = Try again later.

##
