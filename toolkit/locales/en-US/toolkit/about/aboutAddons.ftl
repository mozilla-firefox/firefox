# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at http://mozilla.org/MPL/2.0/.

addons-page-title = Add-ons Manager

search-header =
    .placeholder = Search addons.mozilla.org
    .searchbuttonlabel = Search

## Variables
##   $domain - Domain name where add-ons are available (e.g. addons.mozilla.org)

list-empty-get-extensions-message =
    Get extensions and themes on <a data-l10n-name="get-extensions">{ $domain }</a>

list-empty-get-dictionaries-message =
    Get dictionaries on <a data-l10n-name="get-extensions">{ $domain }</a>

list-empty-get-language-packs-message =
    Get language packs on <a data-l10n-name="get-extensions">{ $domain }</a>

##

list-empty-installed =
    .value = You don’t have any add-ons of this type installed

list-empty-available-updates =
    .value = No updates found

list-empty-recent-updates =
    .value = You haven’t recently updated any add-ons

list-empty-find-updates =
    .label = Check For Updates

list-empty-button =
    .label = Learn more about add-ons

help-button = Add-ons Support
sidebar-help-button-title =
    .title = Add-ons Support

addons-settings-button = { -brand-short-name } Settings
sidebar-settings-button-title =
    .title = { -brand-short-name } Settings

show-unsigned-extensions-button =
    .label = Some extensions could not be verified

show-all-extensions-button =
    .label = Show all extensions

detail-version =
    .label = Version

detail-last-updated =
    .label = Last Updated

addon-detail-description-expand = Show more
addon-detail-description-collapse = Show less

detail-contributions-description = The developer of this add-on asks that you help support its continued development by making a small contribution.

detail-contributions-button = Contribute
    .title = Contribute to the development of this add-on
    .accesskey = C

detail-update-type =
    .value = Automatic Updates

detail-update-default =
    .label = Default
    .tooltiptext = Automatically install updates only if that’s the default

detail-update-automatic =
    .label = On
    .tooltiptext = Automatically install updates

detail-update-manual =
    .label = Off
    .tooltiptext = Don’t automatically install updates

# Used as a description for the option to allow or block an add-on in private windows.
detail-private-browsing-label = Run in Private Windows

# Some add-ons may elect to not run in private windows by setting incognito: not_allowed in the manifest.  This
# cannot be overridden by the user.
detail-private-disallowed-label = Not Allowed in Private Windows
detail-private-disallowed-description2 = This extension does not run while private browsing. <a data-l10n-name="learn-more">Learn more</a>

# Some special add-ons are privileged, run in private windows automatically, and this permission can't be revoked
detail-private-required-label = Requires Access to Private Windows
detail-private-required-description2 = This extension has access to your online activities while private browsing. <a data-l10n-name="learn-more">Learn more</a>

detail-private-browsing-on =
    .label = Allow
    .tooltiptext = Enable in Private Browsing

detail-private-browsing-off =
    .label = Don’t Allow
    .tooltiptext = Disable in Private Browsing

detail-home =
    .label = Homepage

detail-home-value =
    .value = { detail-home.label }

detail-repository =
    .label = Add-on Profile

detail-repository-value =
    .value = { detail-repository.label }

detail-check-for-updates =
    .label = Check for Updates
    .accesskey = U
    .tooltiptext = Check for updates for this add-on

detail-show-preferences =
    .label =
        { PLATFORM() ->
            [windows] Options
           *[other] Preferences
        }
    .accesskey =
        { PLATFORM() ->
            [windows] O
           *[other] P
        }
    .tooltiptext =
        { PLATFORM() ->
            [windows] Change this add-on’s options
           *[other] Change this add-on’s preferences
        }

detail-rating =
    .value = Rating

addon-restart-now =
    .label = Restart now

disabled-unsigned-heading =
    .value = Some add-ons have been disabled

disabled-unsigned-description =
    The following add-ons have not been verified for use in { -brand-short-name }. You can
    <label data-l10n-name="find-addons">find replacements</label> or ask the developer to get them verified.

disabled-unsigned-learn-more = Learn more about our efforts to help keep you safe online.

disabled-unsigned-devinfo =
    Developers interested in getting their add-ons verified can continue by reading our
    <label data-l10n-name="learn-more">manual</label>.

plugin-deprecation-description =
    Missing something? Some plugins are no longer supported by { -brand-short-name }. <label data-l10n-name="learn-more">Learn More.</label>

legacy-warning-show-legacy = Show legacy extensions

legacy-extensions =
    .value = Legacy Extensions

legacy-extensions-description =
    These extensions do not meet current { -brand-short-name } standards so they have been deactivated. <label data-l10n-name="legacy-learn-more">Learn about the changes to add-ons</label>

private-browsing-description2 =
    { -brand-short-name } is changing how extensions work in private browsing. Any new extensions you add to
    { -brand-short-name } won’t run by default in Private Windows. Unless you allow it in settings, the
    extension won’t work while private browsing, and won’t have access to your online activities
    there. We’ve made this change to keep your private browsing private.
    <label data-l10n-name="private-browsing-learn-more">Learn how to manage extension settings</label>

addon-category-discover = Recommendations
addon-category-discover-title =
    .title = Recommendations
addon-category-extension = Extensions
addon-category-extension-title =
    .title = Extensions
addon-category-theme = Themes
addon-category-theme-title =
    .title = Themes
addon-category-plugin = Plugins
addon-category-plugin-title =
    .title = Plugins
addon-category-dictionary = Dictionaries
addon-category-dictionary-title =
    .title = Dictionaries
addon-category-locale = Languages
addon-category-locale-title =
    .title = Languages
addon-category-available-updates = Available Updates
addon-category-available-updates-title =
    .title = Available Updates
addon-category-recent-updates = Recent Updates
addon-category-recent-updates-title =
    .title = Recent Updates
addon-category-sitepermission = Site Permissions
addon-category-sitepermission-title =
    .title = Site Permissions
# String displayed in about:addons in the Site Permissions section
# Variables:
#  $host (string) - DNS host name for which the webextension enables permissions
addon-sitepermission-host = Site Permissions for { $host }

## These are global warnings

extensions-warning-safe-mode2 =
    .message = All add-ons have been disabled by safe mode.
extensions-warning-check-compatibility2 =
    .message = Add-on compatibility checking is disabled. You may have incompatible add-ons.
extensions-warning-check-compatibility-button = Enable
    .title = Enable add-on compatibility checking
extensions-warning-update-security2 =
    .message = Add-on update security checking is disabled. You may be compromised by updates.
extensions-warning-update-security-button = Enable
    .title = Enable add-on update security checking
extensions-warning-imported-addons2 =
    .message = Please finalize the installation of extensions that were imported to { -brand-short-name }.
extensions-warning-imported-addons-button = Install Extensions

## Strings connected to add-on updates

addon-updates-check-for-updates = Check for Updates
    .accesskey = C
addon-updates-view-updates = View Recent Updates
    .accesskey = V

# This menu item is a checkbox that toggles the default global behavior for
# add-on update checking.

addon-updates-update-addons-automatically = Update Add-ons Automatically
    .accesskey = A

## Specific add-ons can have custom update checking behaviors ("Manually",
## "Automatically", "Use default global behavior"). These menu items reset the
## update checking behavior for all add-ons to the default global behavior
## (which itself is either "Automatically" or "Manually", controlled by the
## extensions-updates-update-addons-automatically.label menu item).

addon-updates-reset-updates-to-automatic = Reset All Add-ons to Update Automatically
    .accesskey = R
addon-updates-reset-updates-to-manual = Reset All Add-ons to Update Manually
    .accesskey = R

## Status messages displayed when updating add-ons

addon-updates-updating = Updating add-ons
addon-updates-installed = Your add-ons have been updated.
addon-updates-none-found = No updates found
addon-updates-manual-updates-found = View Available Updates

## Add-on install/debug strings for page options menu

addon-install-from-file = Install Add-on From File…
    .accesskey = I
# Like `addon-install-from-file` but used when the `extensions.webextensions.prefer-update-over-install-for-existing-addon`
# pref is set.
addon-install-or-update-from-file = Install or Update Add-on From File…
    .accesskey = I
addon-install-from-file-dialog-title = Select add-on to install
addon-install-from-file-filter-name = Add-ons
addon-open-about-debugging = Debug Add-ons
    .accesskey = b

## Extension shortcut management

# This is displayed in the page options menu
addon-manage-extensions-shortcuts = Manage Extension Shortcuts
    .accesskey = S

shortcuts-no-addons = You don’t have any extensions enabled.
shortcuts-no-commands = The following extensions do not have shortcuts:
shortcuts-input =
  .placeholder = Type a shortcut
# Accessible name for a trashcan icon button that removes an existent shortcut
shortcuts-remove-button =
  .aria-label = Remove shortcut

shortcuts-browserAction2 = Activate toolbar button
shortcuts-pageAction = Activate page action
shortcuts-sidebarAction = Toggle the sidebar

shortcuts-modifier-mac = Include Ctrl, Alt, or ⌘
shortcuts-modifier-other = Include Ctrl or Alt
shortcuts-invalid = Invalid combination
shortcuts-letter = Type a letter
shortcuts-system = Can’t override a { -brand-short-name } shortcut

# String displayed in warning label when there is a duplicate shortcut
shortcuts-duplicate = Duplicate shortcut

# String displayed when a keyboard shortcut is already assigned to more than one add-on
# Variables:
#   $shortcut (string) - Shortcut string for the add-on
shortcuts-duplicate-warning-message2 =
    .message = { $shortcut } is being used as a shortcut in more than one case. Duplicate shortcuts may cause unexpected behavior.

# String displayed when a keyboard shortcut is already used by another add-on
# Variables:
#   $addon (string) - Name of the add-on
shortcuts-exists = Already in use by { $addon }

# Variables:
#   $numberToShow (number) - Number of other elements available to show
shortcuts-card-expand-button =
    { $numberToShow ->
        *[other] Show { $numberToShow } More
    }

shortcuts-card-collapse-button = Show Less

header-back-button =
    .title = Go back

## Recommended add-ons page

# Explanatory introduction to the list of recommended add-ons. The action word
# ("recommends") in the final sentence is a link to external documentation.
# We hard code "Firefox" because we do not want to imply that a Firefox fork is
# making this recommendation.
discopane-intro3 =
    Extensions and themes let you customize { -brand-product-name }. They can boost privacy,
    enhance productivity, improve media, change the way { -brand-product-name } looks, and
    so much more. These small software programs are often developed by a third party. Here’s
    a selection Firefox <a data-l10n-name="learn-more-trigger">recommends</a> for
    exceptional security, performance, and functionality.

# Notice to make user aware that the recommendations are personalized.
discopane-notice-recommendations2 =
    .message =
        Some of these recommendations are personalized. They are based on other
        extensions you’ve installed, profile preferences, and usage statistics.
discopane-notice-learn-more = Learn more

# Notice for the colorway theme removal
colorway-removal-notice-message =
    .heading = Your colorway theme(s) were removed.
    .message =
        { -brand-product-name } updated its colorways collection. We removed
        the old version(s) from your “Saved Themes” list. Get new versions on
        the add-ons site.
colorway-removal-notice-learn-more = Learn more
colorway-removal-notice-button = Get updated colorways themes

privacy-policy = Privacy Policy

# Refers to the author of an add-on, shown below the name of the add-on.
# Variables:
#   $author (string) - The name of the add-on developer.
created-by-author = by <a data-l10n-name="author">{ $author }</a>
# Shows the number of daily users of the add-on.
# Variables:
#   $dailyUsers (number) - The number of daily users.
user-count = Users: { $dailyUsers }
install-extension-button = Add to { -brand-product-name }
install-theme-button = Install Theme
# The label of the button that appears after installing an add-on. Upon click,
# the detailed add-on view is opened, from where the add-on can be managed.
manage-addon-button = Manage
find-more-addons = Find more add-ons
find-more-themes = Find more themes

# This is a label for the button to open the "more options" menu, it is only
# used for screen readers.
addon-options-button =
    .aria-label = More Options

## Add-on actions

report-addon-button = Report
remove-addon-button = Remove
# The link will always be shown after the other text.
remove-addon-disabled-button = Can’t Be Removed <a data-l10n-name="link">Why?</a>
disable-addon-button = Disable
enable-addon-button = Enable
# This is used for the toggle on the extension card, it's a checkbox and this
# is always its label.
extension-enable-addon-button-label =
    .aria-label = Enable
preferences-addon-button =
    { PLATFORM() ->
        [windows] Options
       *[other] Preferences
    }
details-addon-button = Details
release-notes-addon-button = Release Notes
permissions-addon-button = Permissions

extension-enabled-heading = Enabled
extension-disabled-heading = Disabled

theme-enabled-heading = Enabled
theme-disabled-heading2 = Saved Themes

plugin-enabled-heading = Enabled
plugin-disabled-heading = Disabled

dictionary-enabled-heading = Enabled
dictionary-disabled-heading = Disabled

locale-enabled-heading = Enabled
locale-disabled-heading = Disabled

sitepermission-enabled-heading = Enabled
sitepermission-disabled-heading = Disabled

always-activate-button = Always Activate
never-activate-button = Never Activate

addon-detail-author-label = Author
addon-detail-version-label = Version
addon-detail-last-updated-label = Last Updated
addon-detail-homepage-label = Homepage
addon-detail-rating-label = Rating

# Message for add-ons with a staged pending update.
install-postponed-message2 =
    .message = This extension will be updated when { -brand-short-name } restarts.
install-postponed-button = Update Now

# This string is used to show that an add-on is disabled.
# Variables:
#   $name (string) - The name of the add-on
addon-name-disabled = { $name } (disabled)

# The number of reviews that an add-on has received on AMO.
# Variables:
#   $numberOfReviews (number) - The number of reviews received
addon-detail-reviews-link =
    { $numberOfReviews ->
        [one] { $numberOfReviews } review
       *[other] { $numberOfReviews } reviews
    }

## Pending uninstall message bar

# Variables:
#   $addon (string) - Name of the add-on
pending-uninstall-description2 =
    .message = { $addon } has been removed.
pending-uninstall-undo-button = Undo

addon-detail-updates-label = Allow automatic updates
addon-detail-updates-radio-default = Default
addon-detail-updates-radio-on = On
addon-detail-updates-radio-off = Off
addon-detail-update-check-label = Check for Updates
install-update-button = Update
# aria-label associated to the updates row to help screen readers to announce the group
# of input controls being entered.
addon-detail-group-label-updates =
    .aria-label = { addon-detail-updates-label }

# This is the tooltip text for the private browsing badge in about:addons. The
# badge is the private browsing icon included next to the extension's name.
addon-badge-private-browsing-allowed3 =
    .title = Allowed in private windows
addon-detail-private-browsing-help = When allowed, the extension will have access to your online activities while private browsing. <a data-l10n-name="learn-more">Learn more</a>
addon-detail-private-browsing-allow = Allow
addon-detail-private-browsing-disallow = Don’t Allow
# aria-label associated to the private browsing row to help screen readers to announce the group
# of input controls being entered.
addon-detail-group-label-private-browsing =
    .aria-label = { detail-private-browsing-label }

## "sites with restrictions" (internally called "quarantined") are special domains
## where add-ons are normally blocked for security reasons.

# Used as a description for the option to allow or block an add-on on quarantined domains.
addon-detail-quarantined-domains-label = Run on sites with restrictions
# Used as help text part of the quarantined domains UI controls row.
addon-detail-quarantined-domains-help = When allowed, the extension will have access to sites restricted by { -vendor-short-name }. Allow only if you trust this extension.
# Used as label and tooltip text on the radio inputs associated to the quarantined domains UI controls.
addon-detail-quarantined-domains-allow = Allow
addon-detail-quarantined-domains-disallow = Don’t Allow
# aria-label associated to the quarantined domains exempt row to help screen readers to announce the group.
addon-detail-group-label-quarantined-domains =
    .aria-label = { addon-detail-quarantined-domains-label }

## This is the tooltip text for the recommended badges for an extension in about:addons. The
## badge is a small icon displayed next to an extension when it is recommended on AMO.

# This string needs to work in the context of other forks that are not Firefox
# or built by Mozilla. In particular, we do not want to imply that an
# organisation other than Mozilla or the Firefox team are making the
# recommendation. As such, we hard code "Firefox" and avoid personalising
# language like the words "our" or "we".
addon-badge-recommended4 =
  .title = Firefox only recommends extensions that meet standards for security and performance
# We hard code "Mozilla" in the string below because the extensions are built
# by Mozilla and we don't want forks to display "by Fork".
addon-badge-line4 =
  .title = Official extension built by Mozilla. Meets security and performance standards
# This string needs to work in the context of other forks that are not Firefox
# or built by Mozilla. In particular, we do not want to imply that an
# organisation other than Mozilla or the Firefox team are performing the
# security or performance reviews. As such, we avoid personalising language
# like the words "our" or "we".
addon-badge-verified4 =
  .title = This extension has been reviewed to meet standards for security and performance

##

available-updates-heading = Available Updates
recent-updates-heading = Recent Updates

release-notes-loading = Loading…
release-notes-error = Sorry, but there was an error loading the release notes.

addon-permissions-empty2 = This extension doesn’t require any permissions.
addon-permissions-required = Required permissions for core functionality:
addon-permissions-optional = Optional permissions for added functionality:
addon-permissions-learnmore = Learn more about permissions

recommended-extensions-heading = Recommended Extensions
recommended-themes-heading = Recommended Themes

# Variables:
#   $hostname (string) - Host where the permissions are granted
addon-sitepermissions-required = Grants the following capabilities to <span data-l10n-name="hostname">{ $hostname }</span>:

# A recommendation for the Firefox Color theme shown at the bottom of the theme
# list view. The "Firefox Color" name itself should not be translated.
recommended-theme-1 = Feeling creative? <a data-l10n-name="link">Build your own theme with Firefox Color.</a>

## Page headings

extension-heading = Manage Your Extensions
theme-heading = Manage Your Themes
plugin-heading = Manage Your Plugins
dictionary-heading = Manage Your Dictionaries
locale-heading = Manage Your Languages
updates-heading = Manage Your Updates
sitepermission-heading = Manage Your Site Permissions
discover-heading = Personalize Your { -brand-short-name }
shortcuts-heading = Manage Extension Shortcuts

default-heading-search-label = Find more add-ons
addons-heading-search-input =
    .placeholder = Search addons.mozilla.org

addon-page-options-button =
    .title = Tools for all add-ons

## Detail notifications
## Variables:
##   $name (string) - Name of the add-on.

# Variables:
#   $version (string) - Application version.
details-notification-incompatible2 =
    .message = { $name } is incompatible with { -brand-short-name } { $version }.

details-notification-unsigned-and-disabled2 =
    .message = { $name } could not be verified for use in { -brand-short-name } and has been disabled.

details-notification-unsigned2 =
    .message = { $name } could not be verified for use in { -brand-short-name }. Proceed with caution.

details-notification-hard-blocked-extension =
    .message = This extension is blocked for violating Mozilla’s policies and has been disabled.
details-notification-hard-blocked-other =
    .message = This add-on is blocked for violating Mozilla’s policies and has been disabled.
details-notification-blocked-link2 = See Details

details-notification-soft-blocked-extension-disabled =
    .message = This extension is restricted for violating Mozilla’s policies and has been disabled. You can enable it, but this may be risky.
details-notification-soft-blocked-extension-enabled =
    .message = This extension violates Mozilla’s policies. Using it may be risky.
details-notification-soft-blocked-other-disabled =
    .message = This add-on is restricted for violating Mozilla’s policies and has been disabled. You can enable it, but this may be risky.
details-notification-soft-blocked-other-enabled =
    .message = This add-on violates Mozilla’s policies. Using it may be risky.
details-notification-softblocked-link2 = See Details

details-notification-gmp-pending2 =
    .message = { $name } will be installed shortly.

## Gecko Media Plugins (GMPs)

plugins-gmp-license-info = License information
plugins-gmp-privacy-info = Privacy Information

plugins-openh264-name = OpenH264 Video Codec provided by Cisco Systems, Inc.
plugins-openh264-description = This plugin is automatically installed by Mozilla to comply with the WebRTC specification and to enable WebRTC calls with devices that require the H.264 video codec. Visit https://www.openh264.org/ to view the codec source code and learn more about the implementation.

plugins-widevine-name = Widevine Content Decryption Module provided by Google Inc.
plugins-widevine-description = This plugin enables playback of encrypted media in compliance with the Encrypted Media Extensions specification. Encrypted media is typically used by sites to protect against copying of premium media content. Visit https://www.w3.org/TR/encrypted-media/ for more information on Encrypted Media Extensions.
