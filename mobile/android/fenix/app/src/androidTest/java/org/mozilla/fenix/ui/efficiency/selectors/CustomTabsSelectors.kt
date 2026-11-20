/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.compose.browser.toolbar.R as composeBrowserToolbarR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.feature.customtabs.R as customtabsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuTestTag
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object CustomTabsSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        CUSTOM_TAB_TOOLBAR,
        CUSTOM_TAB_MAIN_MENU_ITEMS,
    }

    // Custom tabs run in their own activity; all locators are device-level (UIAutomator) so they resolve
    // regardless of which activity is foreground.
    // Redesigned toolbar: the menu button is a content-description ("More options"), not the old view res-id.
    val MAIN_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.content_description_menu),
            description = "Custom tabs main menu button",
        )

    val CLOSE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(customtabsR.string.mozac_feature_customtabs_exit_button),
            description = "Custom tabs close button",
            groups = setOf(Group.CUSTOM_TAB_TOOLBAR),
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val SITE_INFO_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Site information",
            description = "Custom tab site information button (opens the unified trust panel)",
            groups = setOf(Group.CUSTOM_TAB_TOOLBAR),
        )

    // Main-menu items (visible after opening the custom-tab menu)
    val MENU_BACK =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_back),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Custom tab menu: Back",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    val MENU_FORWARD =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_forward),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Custom tab menu: Forward",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    val MENU_REFRESH =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_refresh),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Custom tab menu: Refresh",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    val MENU_SHARE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_share),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Custom tab menu: Share",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    val MENU_OPEN_IN_APP =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.browser_menu_open_in_fenix, appName),
            description = "Custom tab menu: Open in app",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    val MENU_FIND_IN_PAGE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.browser_menu_find_in_page),
            description = "Custom tab menu: Find in page",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    val MENU_DESKTOP_SITE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.browser_menu_desktop_site),
            description = "Custom tab menu: Desktop site",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    val MENU_POWERED_BY =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "Powered by $appName",
            description = "Custom tab menu: Powered-by branding",
            groups = setOf(Group.CUSTOM_TAB_MAIN_MENU_ITEMS),
        )

    // Compose twin of MENU_BACK, required for long-press. mozLongClick dispatches on the resolved
    // element type: a UIAutomator UiObject gets UiObject.longClick() (the ~500ms system long-press
    // timeout), which this Compose button does not register as a long press — it lands as a plain
    // click and just navigates back. The Compose path instead does composeLongClick(5000ms), matching
    // the legacy robot's backButton() + LONG_CLICK_DURATION.
    val MENU_BACK_COMPOSE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_back),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Custom tab menu: Back (Compose, for long-press)",
        )

    @Suppress("FunctionName")
    fun MENU_CUSTOM_ITEM(label: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = label,
            description = "Custom tab menu: custom item '$label'",
        )

    // The custom-tab toolbar renders the URL (or its host) as a Compose text node. Matched as a substring so
    // the caller can assert on the host or any leading portion, mirroring the legacy verifyCustomTabUrl.
    @Suppress("FunctionName")
    fun TOOLBAR_URL(url: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_SUBSTRING,
            value = url,
            description = "Custom tab toolbar URL '$url'",
            groups = setOf(Group.CUSTOM_TAB_TOOLBAR),
        )

    // The toolbar URL box itself (Compose). Long-pressing it raises the copy/paste contextual menu. Matched
    // on the first node of the tag, mirroring the legacy robot's onAllNodes(hasTestTag(...)).onFirst().
    val TOOLBAR_URL_BOX =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST,
            value = ADDRESSBAR_URL_BOX,
            description = "Custom tab toolbar URL box",
            groups = setOf(Group.CUSTOM_TAB_TOOLBAR),
        )

    // "Copy" on the toolbar URL long-press contextual menu (Compose). Keyed off the string resource so it
    // survives localization; the legacy robot hardcoded the English "Copy".
    val LONG_PRESS_COPY =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(composeBrowserToolbarR.string.mozac_browser_toolbar_long_press_popup_copy),
            description = "Custom tab toolbar long-press menu: Copy",
        )
}
