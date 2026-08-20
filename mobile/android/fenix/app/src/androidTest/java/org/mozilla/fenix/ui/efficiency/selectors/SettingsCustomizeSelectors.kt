/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsCustomizeSelectors {
    val SETTINGS_CUSTOMIZE_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Customize",
            description = "The Customize Settings title",
            groups = listOf("requiredForPage"),
        )

    val SHOW_TOOLBAR_TOGGLE =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_ID,
            value = "show_toolbar_toggle",
            description = "Show Toolbar Toggle",
            groups = listOf("customizeSettings"),
        )

    val SHOW_TAB_BAR_TOGGLE =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preference_tab_strip_show),
            description = "Show tab bar toggle",
            groups = listOf("requiresScroll"),
        )

    val SELECT_APP_ICON_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preference_select_app_icon_title),
            description = "Select App Icon title",
            groups = listOf("appIconDefault"),
        )

    val APP_ICON_DEFAULT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Default",
            description = "Default app icon option",
            groups = listOf("appIconDefault"),
        )

    val TOOLBAR_LAYOUT_SIMPLE =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preference_simple_toolbar),
            description = "Simple toolbar layout option",
            groups = listOf("toolbarLayout", "requiresScroll", "swipeDown"),
        )

    val TOOLBAR_LAYOUT_EXPANDED =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preference_expanded_toolbar),
            description = "Expanded toolbar layout option",
            groups = listOf("toolbarLayout", "requiresScroll"),
        )

    val TOOLBAR_POSITION_BOTTOM =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preference_bottom_toolbar),
            description = "Bottom toolbar position option",
            groups = listOf("requiresScroll"),
        )

    val NAVIGATE_BACK_TOOLBAR_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Navigate up",
            description = "Navigate back toolbar button",
            groups = listOf("requiredForPage"),
        )

    val all =
        listOf(
            SETTINGS_CUSTOMIZE_TITLE,
            SHOW_TOOLBAR_TOGGLE,
            SHOW_TAB_BAR_TOGGLE,
            SELECT_APP_ICON_TITLE,
            APP_ICON_DEFAULT,
            TOOLBAR_LAYOUT_SIMPLE,
            TOOLBAR_LAYOUT_EXPANDED,
            TOOLBAR_POSITION_BOTTOM,
            NAVIGATE_BACK_TOOLBAR_BUTTON,
        )
}
