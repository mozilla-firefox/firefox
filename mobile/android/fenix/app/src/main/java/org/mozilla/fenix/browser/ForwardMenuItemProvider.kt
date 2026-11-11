/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to navigate forward.
 *
 * @param browserStore [BrowserStore] used to know if the current page can navigate forward.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param goForward [SessionUseCases.GoForwardUseCase] for navigating forward in the current tab.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class ForwardMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val goForward: SessionUseCases.GoForwardUseCase,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM)).asStateFlow()
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(
                    listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Forward(viewHistory = false)))
                )
                .asStateFlow()
        } else {
            browserStore.stateFlow
                .distinctUntilChangedBy { target.browserSessionFrom(it)?.content?.canGoForward }
                .map { listOf(target.browserSessionFrom(it).forwardItem()) }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue = listOf(target.browserSessionFrom(browserStore.state).forwardItem()),
                )
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event is MenuAction.Navigate.Forward)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (event !is MenuAction.Navigate.Forward) return
        val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return

        if (event.viewHistory) {
            menu.navigate(
                NavGraphDirections.actionGlobalTabHistoryDialogFragment(
                    activeSessionId = (target as? MenuTarget.CustomTab)?.customTabId
                )
            )
        } else {
            menu.dismiss()
            goForward(tabId = tabId)
        }
    }

    private fun SessionState?.forwardItem(): MenuItem {
        val canGoForward = this?.content?.canGoForward ?: false
        val state = if (canGoForward) MenuItemState.DEFAULT else MenuItemState.DISABLED

        return DEFAULT_MENU_ITEM.copy(
            onClickEvent = MenuAction.Navigate.Forward(viewHistory = false),
            onLongClickEvent = MenuAction.Navigate.Forward(viewHistory = true),
            state = state,
        )
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_forward)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_forward_24),
            )
    }
}
