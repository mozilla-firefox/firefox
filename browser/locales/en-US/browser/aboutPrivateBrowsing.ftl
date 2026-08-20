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
about-private-browsing-nova-info-body = Closing all your private windows deletes your cookies, history and site data.
about-private-browsing-nova-info-link = Who might still be able to see my activity?
