/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser

import androidx.compose.ui.semantics.Role
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemBadge
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to switch the current page between its desktop and mobile versions.
 *
 * @param browserStore [BrowserStore] allowing to integrate with the current open tabs.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param requestDesktopSite [SessionUseCases.RequestDesktopSiteUseCase] for switching between the two versions.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class DesktopSiteMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val requestDesktopSite: SessionUseCases.RequestDesktopSiteUseCase,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM)).asStateFlow()
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.RequestDesktopSite))).asStateFlow()
        } else {
            browserStore.stateFlow
                .map { listOfNotNull(it.desktopSiteItem()) }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue = listOfNotNull(browserStore.state.desktopSiteItem()),
                )
        }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview &&
            (event == MenuAction.RequestDesktopSite || event == MenuAction.RequestMobileSite)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return

        menu.dismiss()
        requestDesktopSite(enable = event == MenuAction.RequestDesktopSite, tabId = tabId)
    }

    /** Switching versions is only offered while a page is shown. */
    private fun BrowserState.desktopSiteItem() =
        target.browserSessionFrom(this)?.content?.let {
            DesktopSiteStatus(isDesktopMode = it.desktopMode, isPdf = it.isPdf).toMenuItem()
        }

    private fun DesktopSiteStatus.toMenuItem(): MenuItem {
        val state =
            when {
                isPdf -> MenuItemState.DISABLED
                isDesktopMode -> MenuItemState.ACTIVE
                else -> MenuItemState.DEFAULT
            }

        return DEFAULT_MENU_ITEM.copy(
            onClickEvent =
                when (isDesktopMode) {
                    true -> MenuAction.RequestMobileSite
                    else -> MenuAction.RequestDesktopSite
                },
            badge =
                when (state) {
                    MenuItemState.DISABLED -> null
                    else ->
                        MenuItemBadge(
                            text =
                                Text.Resource(
                                    when (isDesktopMode) {
                                        true -> R.string.browser_feature_desktop_site_on
                                        else -> R.string.browser_feature_desktop_site_off
                                    }
                                ),
                            state = state,
                        )
                },
            state = state,
        )
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_desktop_site)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_device_desktop_24),
                role = Role.Switch,
            )
    }
}

/**
 * Everything this menu item needs to know about the current page.
 *
 * @property isDesktopMode Whether the desktop version of the page is currently shown.
 * @property isPdf Whether the page is a PDF, for which switching versions is not supported.
 */
private data class DesktopSiteStatus(
    val isDesktopMode: Boolean,
    val isPdf: Boolean,
)
