/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.settings.account.AccountSettingsTestTag
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection

object SettingsSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        GENERAL_SETTINGS_SECTION,
        GENERAL_SETTINGS_DEFAULT_VALUES,
        SETTINGS_VIEW,
        ABOUT_SETTINGS_SECTION,
        PRIVACY_AND_SECURITY_SETTINGS_SECTION,
        ADVANCED_SETTINGS_SECTION,
        SYNC,
        EXPERIMENTS,
        ABOUT_SECTION,
        GOOGLE_PLAY,
        ABOUT_FIREFOX,
        DEFAULT_VALUES,
        SETTINGS_OPTION_SUMMARY,
    }

    // Present on Settings and its sub-screens; absent once back on Home/Browser. Used as the
    // anchor for backing out of nested Settings via PressBackUntilGone.
    val NAVIGATION_TOOLBAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "navigationToolbar",
            description = "the settings navigation toolbar",
        )

    val GO_BACK_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_CONTENT_DESC,
            value = "Navigate up",
            description = "the Back Arrow button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val GENERAL_HEADING =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_category_general),
            description = "the General heading",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION, Group.SETTINGS_VIEW),
        )

    // The "Privacy and security" preference category heading sits below the fold
    // on a phone. Mirrors the legacy verifySettingsView (scrollToElementByText + onView(withText(...))).
    val PRIVACY_AND_SECURITY_HEADING =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_category_privacy_security),
            description = "the Privacy and security heading",
            groups = setOf(Group.SETTINGS_VIEW),
            scrollDirection = SwipeDirection.UP,
        )

    // The "Extensions" settings entry is far down the settings list and needs the same scroll direction.
    // Mirrors the legacy verifySettingsView (RecyclerView scrollTo preferences_extensions).
    val EXTENSIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_extensions),
            description = "the Extensions button",
            groups = setOf(Group.SETTINGS_VIEW),
            scrollDirection = SwipeDirection.UP,
        )

    val SETTINGS_TITLE =
        navigationToolbarTitle(
            title = getStringResource(R.string.settings_title),
            description = "Settings screen title",
        )

    val SYNC_DEBUG_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_sync_debug),
            description = "the Sync Debug button",
        )

    val SEARCH_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_search),
            description = "the Search button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val SEARCH_DEFAULT_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Google",
            description = "the Search default summary",
            groups = setOf(Group.DEFAULT_VALUES, Group.GENERAL_SETTINGS_DEFAULT_VALUES),
        )

    // Espresso variant of the Search row, used to assert its summary (the default engine name) via a
    // sibling-text check - the UiAutomator SEARCH_BUTTON cannot express hasSibling.
    val SEARCH_SETTING_ROW =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Search",
            description = "the Search settings row",
        )

    val TABS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_tabs),
            description = "the Tabs button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val TABS_DEFAULT_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.close_tabs_manually_summary),
            description = "the Tabs default summary",
            groups = setOf(Group.DEFAULT_VALUES, Group.GENERAL_SETTINGS_DEFAULT_VALUES),
        )

    val HOMEPAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Homepage",
            description = "the Homepage button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val HOMEPAGE_BUTTON_DEFAULT_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.opening_screen_after_four_hours_of_inactivity_summary),
            description = "the Homepage default summary",
            groups = setOf(Group.DEFAULT_VALUES, Group.GENERAL_SETTINGS_DEFAULT_VALUES),
        )

    val CUSTOMIZE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_customize),
            description = "the Customize button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val PASSWORDS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_passwords_logins_and_passwords_2),
            description = "the Passwords button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    // UiAutomator, not Espresso: the "Autofill" label is applied at runtime (the credit_cards row retitled in
    // SettingsFragment) and the list is still settling right after the Settings transition. An Espresso text click
    // captures the row's coordinates then taps, so a shift mid-settle lands the tap on the adjacent "Passwords" row;
    // UiAutomator waits for the "Autofill" text to exist and clicks the node by identity. Exact match (not contains)
    // so it ignores the "Autofill in..." rows on the Passwords screen. Mirrors PASSWORDS_BUTTON.
    val AUTOFILL_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_autofill),
            description = "the Autofill button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val ACCESSIBILITY_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_accessibility),
            description = "the Accessibility button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val LANGUAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_language),
            description = "the Language button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    // Not part of GENERAL_SETTINGS_SECTION: the Translations row is only rendered when the translations
    // Nimbus feature is enabled and the translation engine is supported (SettingsFragment), which is not
    // the case on the test emulator. The legacy verifyGeneralSettingsItemsTest never asserted it either.
    val TRANSLATIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_translations),
            description = "the Translations button",
        )

    val PAGE_SUMMARIES_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_page_summaries),
            description = "the Page summaries button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
        )

    val AI_CONTROLS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_ai_controls),
            description = "the AI Controls button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
            scrollDirection = SwipeDirection.UP,
        )

    val SET_AS_DEFAULT_BROWSER_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_set_as_default_browser),
            description = "the Set as default browser button",
            groups = setOf(Group.GENERAL_SETTINGS_SECTION),
            scrollDirection = SwipeDirection.UP,
        )

    val ABOUT_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "About $appName",
            description = "the About button",
            groups = setOf(Group.ABOUT_SETTINGS_SECTION),
        )

    val DATA_COLLECTION_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Data collection",
            description = "the Data Collection button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val DELETE_BROWSING_DATA_ON_QUIT_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Delete browsing data on quit",
            description = "the Delete browsing data on quit button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val DELETE_BROWSING_DATA_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Delete browsing data",
            description = "the Delete browsing data button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val ENHANCED_TRACKING_PROTECTION_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Enhanced Tracking Protection",
            description = "the Enhanced tracking protection button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val HTTPS_ONLY_MODE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_https_only_title),
            description = "the HTTPS only mode button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val OPEN_LINKS_IN_APPS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Open links in apps",
            description = "the Open links in apps button",
            groups = setOf(Group.ADVANCED_SETTINGS_SECTION),
        )

    val PRIVATE_BROWSING_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Private browsing",
            description = "the Private browsing button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val SIGN_IN_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Sign in",
            description = "the Sign in button",
            groups = setOf(Group.SYNC),
        )

    val SIGN_IN_PROMO_CARD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = AccountSettingsTestTag.SIGN_IN_PROMO_CARD,
            description = "the Sign in promo card",
        )

    val NOTIFICATIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Notifications",
            description = "the Notifications button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val EXPERIMENTS_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_nimbus_experiments),
            description = "the Experiments button",
            groups = setOf(Group.EXPERIMENTS),
        )

    val SITE_SETTINGS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Site settings",
            description = "the Site settings button",
            groups = setOf(Group.PRIVACY_AND_SECURITY_SETTINGS_SECTION),
        )

    val ABOUT_SECTION_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "About",
            description = "The About Section Title",
            groups = setOf(Group.ABOUT_SECTION),
            scrollDirection = SwipeDirection.UP,
        )

    val RATE_ON_GOOGLE_PLAY_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "Google Play",
            description = "The Rate on Google Play Button",
            groups = setOf(Group.ABOUT_SECTION, Group.GOOGLE_PLAY),
            scrollDirection = SwipeDirection.UP,
        )

    val ABOUT_FIREFOX_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "About Firefox",
            description = "The About Firefox Title",
            groups = setOf(Group.ABOUT_SECTION, Group.ABOUT_FIREFOX),
            scrollDirection = SwipeDirection.UP,
        )

    val ETP_DEFAULT_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Standard",
            description = "the Enhanced Tracking Protection default summary",
            groups = setOf(Group.DEFAULT_VALUES),
        )

    val NOTIFICATIONS_DEFAULT_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Allowed",
            description = "the Notifications default summary",
            groups = setOf(Group.DEFAULT_VALUES),
        )

    val DOWNLOAD_SETTINGS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_downloads_2),
            description = "the Download settings button",
            groups = setOf(Group.ADVANCED_SETTINGS_SECTION, Group.DEFAULT_VALUES),
        )

    val REMOTE_IMPROVEMENTS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preferences_remote_improvements),
            description = "the Remote improvements button",
            groups = setOf(Group.ADVANCED_SETTINGS_SECTION, Group.DEFAULT_VALUES),
        )

    val OPEN_LINKS_IN_APPS_DEFAULT_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Ask before opening",
            description = "the Open links in apps default summary",
            groups = setOf(Group.DEFAULT_VALUES),
        )

    val HTTPS_ONLY_MODE_ON_ALL_TABS_SUMMARY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "On in all tabs",
            description = "HTTPS-Only Mode summary - On in all tabs",
        )

    @Suppress("FunctionName")
    fun SETTING_OPTION_SUMMARY(settingName: String = "", settingSummary: String = "") =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT_WITH_SIBLING_TEXT,
            value = settingName,
            secondaryValue = settingSummary,
            description = "Setting: $settingName with summary: $settingSummary",
            groups = setOf(Group.SETTINGS_OPTION_SUMMARY),
        )

    override val scrollTraversalOrder: Map<SelectorGroup, List<Selector>> =
        mapOf(
            Group.GENERAL_SETTINGS_SECTION to listOf(AI_CONTROLS_BUTTON, SET_AS_DEFAULT_BROWSER_BUTTON),
            Group.SETTINGS_VIEW to listOf(PRIVACY_AND_SECURITY_HEADING, EXTENSIONS_BUTTON),
            Group.ABOUT_SECTION to listOf(ABOUT_SECTION_TITLE, RATE_ON_GOOGLE_PLAY_BUTTON, ABOUT_FIREFOX_BUTTON),
        )
}
