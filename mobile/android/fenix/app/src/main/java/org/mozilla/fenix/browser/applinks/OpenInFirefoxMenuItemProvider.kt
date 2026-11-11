/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.applinks

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to open the current custom tab in the main application.
 *
 * @param appName The name of the application, which the item offers to open in.
 * @param appStore [AppStore] used to request opening in the main application.
 * @param target [MenuTarget] for which this menu item would be shown for.
 */
class OpenInFirefoxMenuItemProvider(
    appName: String,
    private val appStore: AppStore,
    private val target: MenuTarget,
) : MenuItemProvider {
    private val isSandboxCustomTab = (target as? MenuTarget.CustomTab)?.isSandboxed == true
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(defaultMenuItem(appName)))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(defaultMenuItem(appName).copy(onClickEvent = MenuAction.OpenInFirefox)))
        } else {
            MutableStateFlow(
                listOf(
                    defaultMenuItem(appName)
                        .copy(
                            onClickEvent = MenuAction.OpenInFirefox,
                            state =
                                when {
                                    isSandboxCustomTab -> MenuItemState.DISABLED
                                    else -> MenuItemState.DEFAULT
                                },
                        )
                )
            )
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.OpenInFirefox)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (isSandboxCustomTab) return
        menu.dismiss()
        appStore.dispatch(AppAction.OpenInFirefoxStarted)
    }

    companion object {
        private fun defaultMenuItem(appName: String) =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_open_in_fenix, listOf(appName))),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_external_link_24),
            )
    }
}
