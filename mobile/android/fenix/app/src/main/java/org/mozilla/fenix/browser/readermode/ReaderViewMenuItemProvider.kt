/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.browser.readermode

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.browser.state.state.TabSessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ReaderViewAction
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to customize the reader view, shown only while reader view is active.
 *
 * @param browserStore [BrowserStore] used to know whether reader view is active for the current tab.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param appStore [AppStore] for showing the reader view controls.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class ReaderViewMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val appStore: AppStore,
    scope: CoroutineScope,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.CustomizeReaderView)))
        } else {
            browserStore.stateFlow
                .map { listOfNotNull(it.customizeReaderViewItem()) }
                .stateIn(
                    scope = scope,
                    started = SharingStarted.Eagerly,
                    initialValue = listOfNotNull(browserStore.state.customizeReaderViewItem()),
                )
        }

    override fun handles(event: MenuEvent) = target != MenuTarget.Preview && (event == MenuAction.CustomizeReaderView)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (target.browserSessionFrom(browserStore.state) == null) return

        menu.dismiss()
        appStore.dispatch(ReaderViewAction.ReaderViewControlsShown)
    }

    /** Customizing the reader view is only offered while the current page is shown in reader view. */
    private fun BrowserState.customizeReaderViewItem() =
        when ((target.browserSessionFrom(this) as? TabSessionState)?.readerState?.active) {
            true -> DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.CustomizeReaderView)
            else -> null
        }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_customize_reader_view_2)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tool_24),
            )
    }
}
