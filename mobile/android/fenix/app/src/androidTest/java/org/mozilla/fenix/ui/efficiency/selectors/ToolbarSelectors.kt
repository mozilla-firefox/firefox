/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.compose.browser.toolbar.R as browserToolbarR
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_URL_BOX
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.TABS_COUNTER
import mozilla.components.feature.summarize.R as summarizeR
import mozilla.components.ui.tabcounter.R as tabcounterR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.toolbar.BrowserToolbarTestTags.SITE_INFO_INSECURE_CONNECTION
import org.mozilla.fenix.components.toolbar.BrowserToolbarTestTags.SITE_INFO_SECURE
import org.mozilla.fenix.components.toolbar.BrowserToolbarTestTags.SITE_INFO_TRACKING_PROTECTION_OFF
import org.mozilla.fenix.components.toolbar.BrowserToolbarTestTags.SITE_INFO_UNKNOWN
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object ToolbarSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        EXPANDED_TOOLBAR_ITEM,
        EXPANDED_TOOLBAR_IN_LANDSCAPE_ITEM,
        HOME_SCREEN_TOOLBAR,
        BROWSER_VIEW_TOOLBAR_ITEMS,
        TAB_COUNTER_SHORTCUT_MENU,
        SIMPLE_TOOLBAR_NONE_SHORTCUT_ITEMS,
    }

    val TOOLBAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "composable_toolbar",
            description = "Toolbar",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val TAB_COUNTER =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = TABS_COUNTER,
            description = "Tab counter button",
            readiness = PageReadinessProfiles.READY_CONTENT,
            groups = setOf(Group.SIMPLE_TOOLBAR_NONE_SHORTCUT_ITEMS),
        )

    val MAIN_MENU_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.content_description_menu),
            description = "Three dot menu button",
            groups = setOf(Group.SIMPLE_TOOLBAR_NONE_SHORTCUT_ITEMS),
        )

    val TOOLBAR_URL_BOX =
        Selector(
            strategy = SelectorStrategy.COMPOSE_ON_ALL_NODES_BY_TAG_ON_FIRST,
            value = ADDRESSBAR_URL_BOX,
            description = "URL box",
            readiness = PageReadinessProfiles.READY_CONTENT,
        )

    // Use UIAutomator when navigating from BrowserPage — avoids Compose sync hanging when GeckoView is active.
    val TOOLBAR_URL_BOX_UIAUTOMATOR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = ADDRESSBAR_URL_BOX,
            description = "URL box",
        )

    val TOOLBAR_URL_BOX_UIAUTOMATOR2 =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RAW_RES,
            value = ADDRESSBAR_URL_BOX,
            description = "URL box",
        )

    val TAB_COUNTER_UIAUTOMATOR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_COMPOSE_TAG,
            value = TABS_COUNTER,
            description = "Tab counter button",
        )

    // Layout-agnostic tab counter. The TABS_COUNTER testTag only exists on the address-bar counter: with
    // shouldUseExpandedToolbar the counter moves into the bottom navigation bar and exposes no tag at all,
    // only this content-description. The description is present in BOTH layouts, so navigation edges that
    // must work either way should use this rather than the tag variants. Matching on the "Tabs Open:"
    // fragment covers the normal ("Non-private Tabs Open: N") and private ("Private Tabs Open: N") forms
    // and is independent of the tab count.
    val TAB_COUNTER_ANY_LAYOUT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Tabs Open:",
            description = "Tab counter button (either toolbar layout)",
        )

    val NEW_TAB_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "New tab",
            description = "New tab button",
        )

    val NEW_PRIVATE_TAB_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "New private tab",
            description = "New private tab button",
        )

    // Items in the shortcut menu raised by long-pressing the tab counter: Compose text nodes, distinct
    // from the NEW_TAB_BUTTON / NEW_PRIVATE_TAB_BUTTON toolbar buttons matched by content-description.
    val TAB_COUNTER_SHORTCUT_NEW_TAB =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(tabcounterR.string.mozac_browser_menu_new_tab),
            description = "Tab counter shortcut menu 'New tab' item",
            groups = setOf(Group.TAB_COUNTER_SHORTCUT_MENU),
        )

    val TAB_COUNTER_SHORTCUT_NEW_PRIVATE_TAB =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(tabcounterR.string.mozac_browser_menu_new_private_tab),
            description = "Tab counter shortcut menu 'New private tab' item",
            groups = setOf(Group.TAB_COUNTER_SHORTCUT_MENU),
        )

    val TAB_COUNTER_SHORTCUT_CLOSE_TAB =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(tabcounterR.string.mozac_close_tab),
            description = "Tab counter shortcut menu 'Close tab' item",
            groups = setOf(Group.TAB_COUNTER_SHORTCUT_MENU),
        )

    val EXPANDED_TOOLBAR_HOMEPAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Homepage",
            description = "Simple toolbar homepage button",
        )

    val EXPANDED_TOOLBAR_ADD_BOOKMARK_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.browser_menu_bookmark_this_page_2),
            description = "Expanded toolbar bookmark page button",
            groups = setOf(Group.EXPANDED_TOOLBAR_ITEM),
        )

    val EXPANDED_TOOLBAR_EDIT_BOOKMARK_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.browser_menu_edit_bookmark),
            description = "Expanded toolbar edit bookmark button",
            groups = setOf(Group.EXPANDED_TOOLBAR_ITEM),
        )

    val EXPANDED_TOOLBAR_SHARE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.browser_menu_share),
            description = "Expanded toolbar share button",
            groups = setOf(Group.EXPANDED_TOOLBAR_ITEM),
        )

    val EXPANDED_TOOLBAR_BACK_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.browser_menu_back),
            description = "Expanded toolbar back button",
            groups = setOf(Group.EXPANDED_TOOLBAR_IN_LANDSCAPE_ITEM),
        )

    val EXPANDED_TOOLBAR_FORWARD_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.browser_menu_forward),
            description = "Expanded toolbar forward button",
            groups = setOf(Group.EXPANDED_TOOLBAR_IN_LANDSCAPE_ITEM),
        )

    val EXPANDED_TOOLBAR_REFRESH_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.browser_menu_refresh),
            description = "Expanded toolbar refresh button",
            groups = setOf(Group.EXPANDED_TOOLBAR_IN_LANDSCAPE_ITEM),
        )

    val SIMPLE_TOOLBAR_SHARE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Share",
            description = "Simple toolbar share button",
        )

    val SIMPLE_TOOLBAR_HOMEPAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Homepage",
            description = "Simple toolbar homepage button",
        )

    val SIMPLE_TOOLBAR_BACK_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Back",
            description = "Simple toolbar back button",
        )

    val SIMPLE_TOOLBAR_SUMMARIZE_PAGES_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(summarizeR.string.mozac_summarize_settings_summarize_pages),
            description = "Simple toolbar summarize pages button",
        )

    val SIMPLE_TOOLBAR_NEW_TAB_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.home_screen_shortcut_open_new_tab_2),
            description = "Simple toolbar new tab button",
        )

    val EDIT_SHORTCUT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.toolbar_shortcut_menu_edit_option),
            description = "Edit shortcut button",
        )

    val SITE_INFO_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Site information",
            description = "Site information button",
        )

    // Items in the context menu shown after long-pressing the display-mode (browser) URL box, matched by content
    // description on the Compose toolbar. Strings come from the browser-toolbar component (see
    // PageOriginContextualMenuBuilder); Paste/Paste & Go only appear when the clipboard holds text/a URL.
    val DISPLAY_MODE_TOOLBAR_COPY =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(browserToolbarR.string.mozac_browser_toolbar_long_press_popup_copy),
            description = "Display-mode toolbar 'Copy' menu item",
        )

    val DISPLAY_MODE_TOOLBAR_PASTE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(browserToolbarR.string.mozac_browser_toolbar_long_press_popup_paste),
            description = "Display-mode toolbar 'Paste' menu item",
        )

    val DISPLAY_MODE_TOOLBAR_PASTE_AND_GO =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(browserToolbarR.string.mozac_browser_toolbar_long_press_popup_paste_and_go),
            description = "Display-mode toolbar 'Paste & Go' menu item",
        )

    @Suppress("FunctionName")
    fun SEARCH_ENGINE_SELECTOR_ICON(searchEngineName: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.search_engine_selector_content_description, searchEngineName),
            description = "Search engine selector icon",
            groups = setOf(Group.HOME_SCREEN_TOOLBAR),
        )

    // Substring to support selecting Reader View whether "Listen to Page" is enabled or disabled.
    val READER_VIEW_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = getStringResource(R.string.browser_menu_read),
            description = "Reader view toolbar button",
        )

    val READER_VIEW_CLOSE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(R.string.browser_menu_read_close),
            description = "Close reader view toolbar button",
        )

    // UIAutomator rather than Compose: this is asserted on BrowserPage with GeckoView active, where
    // Compose sync can hang (same reason TAB_COUNTER_UIAUTOMATOR exists).
    @Suppress("FunctionName")
    fun TAB_COUNTER_WITH_COUNT(openTabs: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Non-private Tabs Open: $openTabs",
            description = "Tab counter showing $openTabs open tab(s)",
        )

    // UIAutomator rather than Compose: asserted on BrowserPage with GeckoView active (see
    // TAB_COUNTER_WITH_COUNT). The capitalized "Private Tabs Open:" is distinct from the normal
    // counter's "Non-private Tabs Open:" fragment, so a description-contains match won't cross over.
    @Suppress("FunctionName")
    fun PRIVATE_TAB_COUNTER_WITH_COUNT(openTabs: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Private Tabs Open: $openTabs",
            description = "Private tab counter showing $openTabs open tab(s)",
        )

    @Suppress("FunctionName")
    fun TAB_STRIP_TAB_COUNTER_WITH_COUNT(openTabs: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Non-private Tabs Open: $openTabs. Tap to switch tabs.",
            description = "Tab strip tab counter showing $openTabs open tab(s)",
        )

    // Compose (content-description) variants of the toolbar tab counter. Prefer these over the
    // UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS forms above when asserting a count that changes as a result
    // of the action under test (e.g. a new tab opened by a text-selection Search): the UIAutomator
    // accessibility tree lags the Compose tree by seconds for the counter, so a UIAutomator check can
    // still read the old count while Compose already shows the new one. Mirrors the legacy
    // BrowserRobot.verifyTabCounter, which read the counter via composeTestRule.onNodeWithContentDescription.
    @Suppress("FunctionName")
    fun TAB_COUNTER_COMPOSE_WITH_COUNT(openTabs: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Non-private Tabs Open: $openTabs. Tap to switch tabs.",
            description = "Tab counter showing $openTabs open tab(s)",
        )

    @Suppress("FunctionName")
    fun PRIVATE_TAB_COUNTER_COMPOSE_WITH_COUNT(openTabs: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = "Private Tabs Open: $openTabs. Tap to switch tabs.",
            description = "Private tab counter showing $openTabs open tab(s)",
        )

    @Suppress("FunctionName")
    fun TAB_STRIP_TAB(tabTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = tabTitle,
            description = "Tab strip tab '$tabTitle'",
        )

    @Suppress("FunctionName")
    fun TAB_STRIP_CLOSE_TAB_BUTTON(tabTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Close tab $tabTitle",
            description = "Tab strip close button for '$tabTitle'",
        )

    val INSECURE_CONNECTION_INFORMATION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SITE_INFO_INSECURE_CONNECTION,
            description = "Insecure connection information button",
            groups = setOf(Group.BROWSER_VIEW_TOOLBAR_ITEMS),
        )

    val SECURE_SITE_INFORMATION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SITE_INFO_SECURE,
            description = "Secure site information button",
            groups = setOf(Group.BROWSER_VIEW_TOOLBAR_ITEMS),
        )

    val TRACKING_PROTECTION_OFF_INFORMATION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SITE_INFO_TRACKING_PROTECTION_OFF,
            description = "Tracking protection off information button",
            groups = setOf(Group.BROWSER_VIEW_TOOLBAR_ITEMS),
        )

    val UNKNOWN_SITE_INFORMATION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = SITE_INFO_UNKNOWN,
            description = "Unknown-state site information button",
            groups = setOf(Group.BROWSER_VIEW_TOOLBAR_ITEMS),
        )
}
