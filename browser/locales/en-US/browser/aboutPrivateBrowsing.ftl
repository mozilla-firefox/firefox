# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

privatebrowsingpage-open-private-window-label = Open a Private Window
    .accesskey = P
about-private-browsing-search-placeholder = Search the web
about-private-browsing-search-btn =
    .title = Search the web
# Variables
#  $engine (String): the name of the user's default search engine
about-private-browsing-handoff =
    .title = Search with { $engine } or enter address
about-private-browsing-handoff-no-engine =
    .title = Search or enter address
# Variables
#  $engine (String): the name of the user's default search engine
about-private-browsing-handoff-text = Search with { $engine } or enter address
about-private-browsing-handoff-text-no-engine = Search or enter address
about-private-browsing-not-private = You are currently not in a private window.

about-private-browsing-hide-activity = Hide your activity and location, everywhere you browse
about-private-browsing-get-privacy = Get privacy protections everywhere you browse
about-private-browsing-hide-activity-1 = Hide browsing activity and location with { -mozilla-vpn-brand-name }. One click creates a secure connection, even on public Wi-Fi.
about-private-browsing-prominent-cta = Stay private with { -mozilla-vpn-brand-name }

# This string is the title for the banner for search engine selection
# in a private window.
# Variables:
#   $engineName (String) - The engine name that will currently be used for the private window.
about-private-browsing-search-banner-title = { $engineName } is your default search engine in Private Windows
about-private-browsing-search-banner-description = {
  PLATFORM() ->
     [windows] To select a different search engine go to <a data-l10n-name="link-options">Options</a>
    *[other] To select a different search engine go to <a data-l10n-name="link-options">Preferences</a>
  }
about-private-browsing-search-banner-close-button =
    .aria-label = Close

about-private-browsing-promo-close-button =
  .title = Close

## Strings used in a “pin promotion” message, which prompts users to pin a private window

about-private-browsing-pin-promo-header = Private browsing freedom in one click
about-private-browsing-pin-promo-link-text = { PLATFORM() ->
    [macos] Keep in Dock
   *[other] Pin to taskbar
}
about-private-browsing-pin-promo-title = No saved cookies or history, right from your desktop. Browse like no one’s watching.

## Strings used in a promotion message for Firefox Relay

about-private-browsing-relay-promo-header = Help prevent inbox spam with email masks
about-private-browsing-relay-promo-title = Hide your real address with an email mask when you sign up, shop, or share it online.
about-private-browsing-relay-promo-link-text = Try email masks

## Strings for the info section of about:privatebrowsing

about-private-browsing-felt-privacy-v1-info-header = Leave no traces on this device
about-private-browsing-felt-privacy-v1-info-body = { -brand-short-name} deletes your cookies, history, and site data when you close all your private windows.
about-private-browsing-felt-privacy-v1-info-link = Who might be able to see my activity?

## Strings for the Nova redesign of about:privatebrowsing

# "You're off the record" is an English idiom meant to communicate that you
# are not being recorded. If there is not a comparable phrase in the locale,
# fall back to "Your browsing will be deleted"
about-private-browsing-nova-info-header = You’re off the record
about-private-browsing-nova-info-subheader2 = We’ll erase every search and sign-in when you close all your Private Windows. { -brand-short-name }’s built-in protections are on here too, like blocking trackers.
about-private-browsing-nova-info-body = Closing all your Private Windows deletes your cookies, history, and site data.
about-private-browsing-nova-info-link = Who might still be able to see my activity?
about-private-browsing-private-window-basics-link = Private Window basics
about-private-browsing-private-window-redesign-subheader = { -brand-short-name } is designed to protect your privacy as you browse, with built-in tracking protections. Closing this window erases its history, cookies, and site data to keep your browsing private from others who use this device.

## Strings for the Private Window basics spotlight

about-private-browsing-spotlight-basics-title = Private Window basics
about-private-browsing-spotlight-basics-subtitle = Private Windows help keep your browsing private from others on this device. They don’t make you anonymous or clear all of your data.
# This is a section header in the Private Window basics spotlight dialog,
# introducing information about what users should know about Private Windows.
about-private-browsing-spotlight-basics-what-to-know = What to know
about-private-browsing-spotlight-basics-activity-seen = Some activity may still be seen by sites, search engines, internet providers, or your employer.
about-private-browsing-spotlight-basics-bookmarks-downloads = Bookmarks and downloads stay on your device and may appear in the address bar.
# This is a section header in the Private Window basics spotlight dialog,
# introducing additional privacy protection features available in { -brand-short-name }.
about-private-browsing-spotlight-basics-more-privacy = More privacy protections
about-private-browsing-spotlight-basics-malware-alerts = { -brand-short-name } automatically alerts you about malware and deceptive sites.
# "Participating sites" refers to websites that honor Global Privacy Control (GPC) signals, either voluntarily or where legally required.
about-private-browsing-spotlight-basics-no-sell-data = { -brand-short-name } automatically asks participating sites not to sell or share your personal data.
about-private-browsing-spotlight-basics-vpn = Use built-in VPN to make your location harder to trace.
# "Strict" refers to the Strict level of Enhanced Tracking Protection settings.
# Translations should be consistent with the existing "Strict" string in about:preferences.
about-private-browsing-spotlight-basics-strict-tracking = Switch to Strict in settings for stronger tracking protections.
about-private-browsing-spotlight-basics-learn-more = Learn more
