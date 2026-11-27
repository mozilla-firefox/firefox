/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.compose.menu.R as MozacMenuR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuTestTag
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.helpers.TestHelper.appName
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection

object MainMenuSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        HOME_PAGE_MAIN_MENU_ITEMS,
        BROWSER_VIEW_MAIN_MENU_ITEMS,
        BOOKMARK_ACTIONS,
        EDIT_BOOKMARK_ACTIONS,
        MORE_MAIN_MENU_SUB_LIST,
        EXPANDED_EXTENSIONS_MENU_ITEMS,
        HOME_BANNER,
    }

    val MAIN_MENU_ANCHOR =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = MenuTestTag.MAIN_MENU,
            description = "Main menu anchor",
            readiness = PageReadinessProfiles.IDENTITY_ANCHOR,
        )

    val EXTENSIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_extensions),
            description = "Main menu Extensions button",
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val BOOKMARKS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.library_bookmarks),
            description = "Main menu Bookmarks button",
            // Below the fold in the landscape browser main menu; a no-op in portrait.
            // where the item is already displayed (mozSwipeTo returns before swiping).
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val HISTORY_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.library_history),
            description = "Main menu History button",
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val DOWNLOADS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.library_downloads),
            description = "Main menu Downloads button",
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val PASSWORDS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_passwords),
            description = "Main menu Passwords button",
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val SIGN_IN_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_sign_in),
            description = "Main menu Sign in button",
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val SETTINGS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_settings),
            description = "Main menu Settings button",
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val BOOKMARK_THIS_PAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_bookmark_this_page_2),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Bookmark this page button",
            groups = setOf(Group.BOOKMARK_ACTIONS, Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val EDIT_BOOKMARK_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_edit_bookmark),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Edit bookmark button",
            groups = setOf(Group.EDIT_BOOKMARK_ACTIONS),
        )

    val FIND_IN_PAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_find_in_page),
            description = "Main menu Find in page button",
            groups = setOf(Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val BACK_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_back),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Main menu Back button",
            groups = setOf(Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
        )

    val FORWARD_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_forward),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Main menu Forward button",
            groups = setOf(Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
        )

    val REFRESH_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_refresh),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Main menu Refresh button",
            groups = setOf(Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
        )

    val SHARE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_UNDER_TAG,
            value = getStringResource(R.string.browser_menu_share),
            secondaryValue = MenuTestTag.MAIN_MENU,
            description = "Main menu Share button",
            groups = setOf(Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
        )

    val DESKTOP_SITE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = getStringResource(R.string.browser_menu_desktop_site),
            description = "Main menu Desktop site button",
            groups = setOf(Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val DESKTOP_SITE_ON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_AND_STATE_DESCRIPTION,
            value = getStringResource(R.string.browser_menu_desktop_site),
            secondaryValue = getStringResource(R.string.browser_feature_desktop_site_on),
            description = "Main menu Desktop site ON state",
        )

    val DESKTOP_SITE_OFF =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_AND_STATE_DESCRIPTION,
            value = getStringResource(R.string.browser_menu_desktop_site),
            secondaryValue = getStringResource(R.string.browser_feature_desktop_site_off),
            description = "Main menu Desktop site OFF state",
        )

    val MORE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_more_settings),
            description = "Main menu More button",
            groups = setOf(Group.BROWSER_VIEW_MAIN_MENU_ITEMS),
            scrollDirection = SwipeDirection.UP,
        )

    val REPORT_BROKEN_SITE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_webcompat_reporter_2),
            description = "Main menu Report broken site button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    val TRY_RECOMMENDED_EXTENSION_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_AND_TEXT_SUBSTRING,
            value = getStringResource(R.string.browser_menu_extensions),
            secondaryValue = getStringResource(R.string.browser_menu_try_a_recommended_extension_description),
            description = "Main menu Extensions - Try a recommended extension button",
        )

    // Shown on the Extensions row when extensions are installed but all disabled.
    val NO_EXTENSIONS_ENABLED_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_AND_TEXT_SUBSTRING,
            value = getStringResource(R.string.browser_menu_extensions),
            secondaryValue = getStringResource(R.string.browser_menu_no_extensions_installed_description),
            description = "Main menu Extensions - No extensions enabled button",
        )

    val DISCOVER_MORE_EXTENSIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_discover_more_extensions),
            description = "Main menu Discover more extensions button",
            groups = setOf(Group.EXPANDED_EXTENSIONS_MENU_ITEMS),
        )

    // Shown in the expanded Extensions submenu once at least one extension is installed; opens the
    // full add-ons manager. Mirrors the legacy clickManageExtensionsButtonFromRedesignedMainMenu.
    val MANAGE_EXTENSIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_manage_extensions),
            description = "Main menu Manage extensions button",
        )

    // The "Add <addon>" install icon on a recommended addon row in the expanded Extensions submenu.
    // Keyed on the addon name because there is no test tag on the install icon (see AddonMenuItem);
    // matched at the device level like the legacy installRecommendedAddon (itemWithDescription).
    @Suppress("FunctionName")
    fun RECOMMENDED_ADDON_INSTALL_BUTTON(addonTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = getStringResource(R.string.browser_menu_extension_plus_icon_content_description_2, addonTitle),
            description = "Install recommended addon '$addonTitle' button",
        )

    // Keyed on the install button every recommended addon row has rather than the addon name: which addons
    // AMO recommends is server-driven, so matching names would couple the test to remote data.
    val RECOMMENDED_ADDON_ITEM =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION_SUBSTRING,
            value = getStringResource(R.string.browser_menu_extension_plus_icon_content_description_2, ""),
            description = "Recommended addon row in the expanded Extensions submenu",
            groups = setOf(Group.EXPANDED_EXTENSIONS_MENU_ITEMS),
        )

    @Suppress("FunctionName")
    fun EXTENSIONS_BUTTON_WITH_INSTALLED_EXTENSION(addonTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_AND_TEXT_SUBSTRING,
            value = getStringResource(R.string.browser_menu_extensions),
            secondaryValue = addonTitle,
            description = "Extensions menu row advertising installed extension '$addonTitle'",
        )

    // The installed extension row in the expanded Extensions submenu. Excludes the Extensions row, whose
    // summary also lists the extension.
    @Suppress("FunctionName")
    fun INSTALLED_EXTENSION_ITEM(addonTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_CLICKABLE_BY_TEXT_SUBSTRING_WITHOUT_TEXT,
            value = addonTitle,
            secondaryValue = getStringResource(R.string.browser_menu_extensions),
            description = "Installed extension '$addonTitle' row in the expanded Extensions submenu",
        )

    // TODO (M. Barone 3/20/2026): add getting 'appName' to our base helpers
    val DEFAULT_BROWSER_BANNER_TITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_default_banner_title, appName),
            description = "Make Firefox your default banner title",
            groups = setOf(Group.HOME_BANNER, Group.HOME_PAGE_MAIN_MENU_ITEMS),
        )

    val DEFAULT_BROWSER_BANNER_SUBTITLE =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_default_banner_subtitle_2),
            description = "Make Firefox your default banner subtitle",
            groups = setOf(Group.HOME_BANNER, Group.HOME_PAGE_MAIN_MENU_ITEMS),
        )

    val DEFAULT_BROWSER_BANNER_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_default_banner_button),
            description = "Make Firefox your default banner button",
            groups = setOf(Group.HOME_BANNER, Group.HOME_PAGE_MAIN_MENU_ITEMS),
        )

    val DEFAULT_BROWSER_BANNER_DISMISS =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_CONTENT_DESCRIPTION,
            value = getStringResource(MozacMenuR.string.mozac_menu_banner_dismiss_description),
            description = "Make Firefox your default banner dismiss button",
            groups = setOf(Group.HOME_BANNER, Group.HOME_PAGE_MAIN_MENU_ITEMS),
        )

    // Quit is the last item in the scrollable main menu, so give it an explicit scroll direction: the framework
    // then polls/swipes it into view (ensureReachable -> mozSwipeTo) before clicking, instead of
    // asserting on it one-shot while the menu is still settling or the item is below the fold.
    val QUIT_FIREFOX_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_delete_browsing_data_on_quit, appName),
            description = "Quit Firefox button",
            scrollDirection = SwipeDirection.UP,
        )

    val CUSTOMIZE_HOMEPAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_customize_homepage),
            description = "Customize homepage Settings button",
            groups = setOf(Group.HOME_PAGE_MAIN_MENU_ITEMS),
        )

    val ADD_TO_SHORTCUTS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_add_to_shortcuts),
            description = "Main menu Add to shortcuts button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    val TRANSLATE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_translate_page_2),
            description = "Translate page button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    val TRANSLATED_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_translated),
            description = "Translate page button",
            scrollDirection = SwipeDirection.UP,
        )

    val SAVE_AS_PDF_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_save_as_pdf_2),
            description = "Main menu save as PDF button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    val SUMMARIZE_PAGE_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_summarize_page),
            description = "Main menu Summarize page button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    val PRINT_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_print_2),
            description = "Print page button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    val REMOVE_FROM_SHORTCUTS_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_remove_from_shortcuts),
            description = "Main menu remove from shortcuts button",
            scrollDirection = SwipeDirection.UP,
        )

    val ADD_TO_HOMESCREEN_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_add_to_homescreen),
            description = "Main menu add to homescreen button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    // Only present in the "More settings" submenu while a private tab is in the foreground; not part of the
    // MORE_MAIN_MENU_SUB_LIST group, which models the normal-browsing sub-list.
    val MOVE_TO_NON_PRIVATE_TAB_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_move_to_non_private_tab),
            description = "Main menu Move to non-private tab button",
            groups = setOf(),
        )

    val ADD_APP_TO_HOMESCREEN_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_add_app_to_homescreen),
            description = "Main menu add app (PWA) to homescreen button",
            scrollDirection = SwipeDirection.UP,
        )

    val OPEN_IN_APP_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_open_app_link),
            description = "Main menu Open in app button",
            groups = setOf(Group.MORE_MAIN_MENU_SUB_LIST),
            scrollDirection = SwipeDirection.UP,
        )

    @Suppress("FunctionName")
    fun OPEN_IN_APP_NAME_BUTTON(appName: String = "") =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT_MERGED,
            value = getStringResource(R.string.browser_menu_open_in_fenix, appName),
            description = "Main menu Open in $appName button",
            scrollDirection = SwipeDirection.UP,
        )

    // Only present when the browser is showing reader view.
    val CUSTOMIZE_READER_VIEW_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TEXT,
            value = getStringResource(R.string.browser_menu_customize_reader_view_2),
            description = "Main menu Customize Reader View button",
        )

    override val scrollTraversalOrder: Map<SelectorGroup, List<Selector>> =
        mapOf(
            Group.HOME_PAGE_MAIN_MENU_ITEMS to
                listOf(
                    EXTENSIONS_BUTTON,
                    HISTORY_BUTTON,
                    BOOKMARKS_BUTTON,
                    DOWNLOADS_BUTTON,
                    PASSWORDS_BUTTON,
                    SIGN_IN_BUTTON,
                    SETTINGS_BUTTON,
                ),
            Group.BROWSER_VIEW_MAIN_MENU_ITEMS to
                listOf(
                    BOOKMARK_THIS_PAGE_BUTTON,
                    FIND_IN_PAGE_BUTTON,
                    DESKTOP_SITE_BUTTON,
                    EXTENSIONS_BUTTON,
                    MORE_BUTTON,
                    HISTORY_BUTTON,
                    BOOKMARKS_BUTTON,
                    DOWNLOADS_BUTTON,
                    PASSWORDS_BUTTON,
                    SIGN_IN_BUTTON,
                    SETTINGS_BUTTON,
                ),
            Group.MORE_MAIN_MENU_SUB_LIST to
                listOf(
                    TRANSLATE_BUTTON,
                    SUMMARIZE_PAGE_BUTTON,
                    REPORT_BROKEN_SITE_BUTTON,
                    ADD_TO_SHORTCUTS_BUTTON,
                    ADD_TO_HOMESCREEN_BUTTON,
                    OPEN_IN_APP_BUTTON,
                    SAVE_AS_PDF_BUTTON,
                    PRINT_BUTTON,
                ),
        )
}
