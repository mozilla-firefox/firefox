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
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to refresh or stop loading the current page.
 *
 * @param browserStore [BrowserStore] used to know if the current page is loading.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param reload [SessionUseCases.ReloadUrlUseCase] for reloading the current page.
 * @param stopLoading [SessionUseCases.StopLoadingUseCase] for stopping loading the current page.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class RefreshMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val reload: SessionUseCases.ReloadUrlUseCase,
    private val stopLoading: SessionUseCases.StopLoadingUseCase,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM)).asStateFlow()
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(
                    listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.Reload(bypassCache = false)))
                )
                .asStateFlow()
        } else {
            browserStore.stateFlow
                .distinctUntilChangedBy { target.browserSessionFrom(it)?.content?.loading }
                .map { listOfNotNull(it.refreshItem()) }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue = listOfNotNull(browserStore.state.refreshItem()),
                )
        }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview && (event is MenuAction.Navigate.Reload || event == MenuAction.Navigate.Stop)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return

        when (event) {
            is MenuAction.Navigate.Reload -> {
                menu.dismiss()
                reload(
                    tabId = tabId,
                    flags =
                        when (event.bypassCache) {
                            true -> LoadUrlFlags.select(LoadUrlFlags.BYPASS_CACHE)
                            false -> LoadUrlFlags.none()
                        },
                )
            }

            MenuAction.Navigate.Stop -> {
                menu.dismiss()
                stopLoading(tabId = tabId)
            }

            else -> Unit
        }
    }

    private fun BrowserState.refreshItem(): MenuItem? =
        target.browserSessionFrom(this)?.content?.let { content ->
            if (content.loading) {
                StandardMenuItem(
                    title = MenuItemTitle(Text.Resource(R.string.browser_menu_stop)),
                    icon = MenuItemIconRes(iconsR.drawable.mozac_ic_cross_24),
                    onClickEvent = MenuAction.Navigate.Stop,
                )
            } else {
                DEFAULT_MENU_ITEM.copy(
                    onClickEvent = MenuAction.Navigate.Reload(bypassCache = false),
                    onLongClickEvent = MenuAction.Navigate.Reload(bypassCache = true),
                )
            }
        }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_refresh)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_arrow_clockwise_24),
            )
    }
}
