/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsHomepageSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        HOMEPAGE_SETTINGS
    }

    val SETTINGS_HOMEPAGE_TITLE =
        navigationToolbarTitle(
            title = getStringResource(R.string.preferences_home_2),
            description = "Homepage toolbar title",
        )

    val SHOW_TOP_SITES_TOGGLE =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_ID,
            value = "show_top_sites_toggle",
            description = "Show Top Sites Toggle",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val SHORTCUTS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Shortcuts",
            description = "the Shortcuts button",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
            readiness = PageReadinessProfiles.READY_CONTENT,
        )

    val CONTINUE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Continue",
            description = "the Continue button",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val RECENT_BOOKMARKS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.customize_toggle_bookmarks),
            description = "the Recent bookmarks button",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val RECENTLY_VISITED_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Recently visited",
            description = "the Recently visited button",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val POCKET_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Pocket",
            description = "the Pocket button",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val WALLPAPERS_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_wallpapers),
            description = "the Wallpapers button",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    // Espresso title anchors for the toggle/checkbox rows. Their on/off state is a View-hierarchy question
    // (the switch/checkbox is a relative of the title, addressed via hasCousinSwitch / hasSiblingCheckBox),
    // so these must resolve through Espresso rather than the UiAutomator "*_BUTTON" variants above.
    val SHORTCUTS_SWITCH =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.top_sites_toggle_top_recent_sites_4),
            description = "the Shortcuts switch row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val SPONSORED_SHORTCUTS_CHECKBOX =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_toggle_contile),
            description = "the Sponsored shortcuts checkbox row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val PRIVACY_REPORT_SWITCH =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_toggle_privacy_report),
            description = "the Privacy report switch row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val CONTINUE_SWITCH =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_toggle_continue),
            description = "the Continue switch row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val BOOKMARKS_SWITCH =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_toggle_bookmarks),
            description = "the Bookmarks switch row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val RECENTLY_VISITED_SWITCH =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_toggle_recently_visited),
            description = "the Recently visited switch row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val STORIES_SWITCH =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_toggle_pocket_3),
            description = "the Stories switch row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    val SPONSORED_STORIES_CHECKBOX =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.customize_toggle_pocket_sponsored),
            description = "the Sponsored stories checkbox row",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )

    @Suppress("FunctionName")
    fun OPENING_SCREEN_OPTION(openingScreenOption: String = "") =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_ID_WITH_SIBLING_TEXT,
            value = "radio_button",
            secondaryValue = openingScreenOption,
            description = "Opening screen option: $openingScreenOption",
            groups = setOf(Group.HOMEPAGE_SETTINGS),
        )
}
