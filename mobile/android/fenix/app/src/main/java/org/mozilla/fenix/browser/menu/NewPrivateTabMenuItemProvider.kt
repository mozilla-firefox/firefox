/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.menu

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR
import mozilla.components.ui.tabcounter.R as tabcounterR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.browser.browsingmode.BrowsingMode
import org.mozilla.fenix.browser.browsingmode.BrowsingModeManager
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.components.usecases.FenixBrowserUseCases
import org.mozilla.fenix.utils.Settings

/**
 * [MenuItemProvider] for opening a new tab in private mode.
 *
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param fenixBrowserUseCases [FenixBrowserUseCases] used for adding new homepage tabs.
 * @param settings [Settings] used to check user preferences for homepage as new tab.
 * @param browsingModeManager [BrowsingModeManager] used to set private browsing mode.
 */
class NewPrivateTabMenuItemProvider(
    private val target: MenuTarget,
    private val fenixBrowserUseCases: FenixBrowserUseCases,
    private val settings: Settings,
    private val browsingModeManager: BrowsingModeManager,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.NewPrivateTab)))
        }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview && (event == MenuAction.Navigate.NewPrivateTab)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        browsingModeManager.mode = BrowsingMode.Private

        if (settings.enableHomepageAsNewTab) {
            fenixBrowserUseCases.addNewHomepageTab(private = true)
            menu.navigate(NavGraphDirections.actionGlobalHome())
        } else {
            val focusOnAddressBar = !settings.enableHomepageSearchBar && !settings.enableHomepageTrendingRecentSearch

            menu.navigate(NavGraphDirections.actionGlobalHome(focusOnAddressBar = focusOnAddressBar))
        }
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(tabcounterR.string.mozac_browser_menu_new_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_private_mode_fill_24),
            )
    }
}
