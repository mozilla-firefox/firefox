/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.downloads

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to open the downloads screen.
 *
 * @param appStore [AppStore] to observe the downloads notification status.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class DownloadsMenuItemProvider(
    appStore: AppStore,
    private val target: MenuTarget,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM)).asStateFlow()
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Downloads))).asStateFlow()
        } else {
            appStore.stateFlow
                .map { it.supportedMenuNotifications.contains(SupportedMenuNotifications.Downloads) }
                .distinctUntilChanged()
                .map { isHighlighted -> listOf(createMenuItem(isHighlighted)) }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue =
                        listOf(
                            createMenuItem(
                                appStore.state.supportedMenuNotifications.contains(SupportedMenuNotifications.Downloads)
                            )
                        ),
                )
        }

    private fun createMenuItem(isHighlighted: Boolean) =
        DEFAULT_MENU_ITEM.copy(
            icon =
                MenuItemIconRes(
                    iconsR.drawable.mozac_ic_download_24,
                    isHighlighted = isHighlighted,
                ),
            onClickEvent = MenuAction.Navigate.Downloads,
        )

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.Navigate.Downloads)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        menu.navigate(NavGraphDirections.actionGlobalDownloadsFragment())
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.library_downloads)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_download_24),
            )
    }
}
