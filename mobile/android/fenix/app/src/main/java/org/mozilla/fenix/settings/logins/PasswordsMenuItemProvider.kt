/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins

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
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to open the saved passwords screen.
 *
 * @param isAutofillSupported Whether saving and autofilling passwords is supported on this device. The item is hidden
 *   otherwise.
 * @param target [MenuTarget] for which this menu item would be shown for.
 */
class PasswordsMenuItemProvider(
    isAutofillSupported: Boolean,
    private val target: MenuTarget,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Passwords)))
        } else {
            MutableStateFlow(
                listOfNotNull(
                    when (isAutofillSupported) {
                        true -> DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Passwords)
                        false -> null
                    }
                )
            )
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.Navigate.Passwords)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        menu.navigate(MenuFragmentDirections.actionMenuFragmentToLoginsListFragment())
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_passwords)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_login_24),
            )
    }
}
