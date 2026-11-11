/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.menu

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.feature.tabs.TabsUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to move the current private tab to a non-private tab. Only shown when
 * the current tab is private.
 *
 * @param browserStore The [BrowserStore] to get the current tab from.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param migratePrivateTab [TabsUseCases.MigratePrivateTabUseCase] for moving the current tab to a non-private tab.
 */
class MoveToNormalTabsMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val migratePrivateTab: TabsUseCases.MigratePrivateTabUseCase,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.MoveToNonPrivateTab)))
        } else {
            MutableStateFlow(
                listOfNotNull(
                    if (target.browserSessionFrom(browserStore.state)?.content?.private == true) {
                        DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.MoveToNonPrivateTab)
                    } else {
                        null
                    }
                )
            )
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.MoveToNonPrivateTab)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return

        menu.dismiss()
        migratePrivateTab(tabId)
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_move_to_non_private_tab)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_external_link_24),
            )
    }
}
