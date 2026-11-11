/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.AppAction.FindInPageAction
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to search the current page.
 *
 * Always shown, and always the same - searching a page is possible whatever else is going on.
 *
 * @param appStore [AppStore] for starting to search the current page.
 * @param target [MenuTarget] for which this menu item would be shown for.
 */
class FindInPageMenuItemProvider(
    private val appStore: AppStore,
    private val target: MenuTarget,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.FindInPage)))
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.FindInPage)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        menu.dismiss()
        appStore.dispatch(FindInPageAction.FindInPageStarted)
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_find_in_page)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_search_24),
            )
    }
}
