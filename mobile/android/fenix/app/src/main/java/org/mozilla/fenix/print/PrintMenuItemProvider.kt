/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.print

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to print the current webpage
 *
 * @param isAndroidAutomotiveAvailable Whether the device is running on Android Automotive.
 * @param browserStore [BrowserStore] used to get the current page.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param printContent [SessionUseCases.PrintContentUseCase] for printing the current page.
 */
class PrintMenuItemProvider(
    isAndroidAutomotiveAvailable: Boolean,
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val printContent: SessionUseCases.PrintContentUseCase,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.PrintRequested)))
        } else {
            MutableStateFlow(
                listOfNotNull(
                    if (isAndroidAutomotiveAvailable) {
                        null
                    } else {
                        DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.PrintRequested)
                    }
                )
            )
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.PrintRequested)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return

        menu.dismiss()
        printContent(tabId = tabId)
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_print_2)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_print_24),
            )
    }
}
