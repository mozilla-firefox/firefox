/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.topsites

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.compose.menu.ui.MenuItemState
import mozilla.components.feature.top.sites.PinnedSiteStorage
import mozilla.components.feature.top.sites.TopSitesUseCases
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.ShortcutAction
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.middleware.getTabUrl
import org.mozilla.fenix.components.menu.store.MenuAction

/**
 * [MenuItemProvider] for the menu item allowing to add or remove the current webpage from home shortcuts.
 *
 * @param browserStore [BrowserStore] allowing to integrate with the current open tabs.
 * @param target [MenuTarget] for which this menu item would be shown for.
 * @param pinnedSiteStorage [PinnedSiteStorage] used to check whether that page already is a shortcut.
 * @param areShortcutsEnabled Whether the user allows shortcuts to be shown at all.
 * @param topSitesUseCases [TopSitesUseCases] for adding or removing the current webpage from shortcuts.
 * @param appStore [AppStore] for informing the rest of the application about a new shortcut.
 * @param scope [CoroutineScope] tied to the lifetime of the menu, on which to query and change the shortcuts.
 * @param applicationScope [CoroutineScope] tied to the lifetime of the application, on which to remove a shortcut since
 *   that cannot be interrupted.
 */
class ShortcutMenuItemProvider(
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
    private val pinnedSiteStorage: PinnedSiteStorage,
    areShortcutsEnabled: Boolean,
    private val topSitesUseCases: TopSitesUseCases,
    private val appStore: AppStore,
    private val scope: CoroutineScope,
    private val applicationScope: CoroutineScope,
) : MenuItemProvider {
    // Without a page shown there is nothing to add to shortcuts. Knowing whether the shown page already is one means
    // reading from disk, which the menu should not wait for, so it starts out offered as not being a shortcut.
    private val mutableItems =
        MutableStateFlow<List<MenuItem>>(
            listOfNotNull(
                if (target == MenuTarget.CustomizeMenu) {
                    DEFAULT_MENU_ITEM
                } else {
                    DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.AddShortcut).takeIf {
                        areShortcutsEnabled && target.browserSessionFrom(browserStore.state) != null
                    }
                }
            )
        )

    override val itemsFlow: StateFlow<List<MenuItem>> = mutableItems.asStateFlow()

    init {
        if (target != MenuTarget.CustomizeMenu && mutableItems.value.isNotEmpty()) {
            scope.launch { resolveShortcut() }
        }
    }

    private suspend fun resolveShortcut() {
        val url = target.browserSessionFrom(browserStore.state)?.getTabUrl() ?: return
        pinnedSiteStorage.getPinnedSites().firstOrNull { it.url == url } ?: return

        mutableItems.value = listOf(REMOVE_SHORTCUT_ITEM)
    }

    override fun handles(event: MenuEvent) = event == MenuAction.AddShortcut || event == MenuAction.RemoveShortcut

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        when (event) {
            MenuAction.AddShortcut -> scope.launch { addShortcut(menu) }
            MenuAction.RemoveShortcut -> scope.launch { removeShortcut(menu) }
            else -> Unit
        }
    }

    private suspend fun addShortcut(menu: MenuHost) {
        val tab = target.browserSessionFrom(browserStore.state) ?: return
        val url = tab.getTabUrl() ?: return
        val title = tab.content.title

        val shortcuts = pinnedSiteStorage.getPinnedSites()
        // The menu item may have been shown before the page was known to already be a shortcut.
        if (shortcuts.any { it.url == url }) return

        topSitesUseCases.addPinnedSites(title = title, url = url)
        appStore.dispatch(
            ShortcutAction.ShortcutAdded(
                url = url,
                source = AddShortcutSource.MANUAL,
                entryPoint = AddShortcutEntryPoint.PAGE_MENU,
            )
        )
        menu.dismiss()
    }

    private suspend fun removeShortcut(menu: MenuHost) {
        val url = target.browserSessionFrom(browserStore.state)?.getTabUrl() ?: return
        val shortcut = pinnedSiteStorage.getPinnedSites().firstOrNull { it.url == url } ?: return

        // Removing a shortcut also deletes the history entries of that page, which will run until completion even if
        // the coroutine is canceled. As such the work outliving the menu must not reference this provider, which would
        // otherwise be leaked while waiting for it.
        val removeTopSite = topSitesUseCases.removeTopSites
        applicationScope.async { removeTopSite(topSite = shortcut) }.await()

        menu.dismiss()
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_add_to_shortcuts)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_pin_24),
            )

        private val REMOVE_SHORTCUT_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_remove_from_shortcuts)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_pin_fill_24),
                onClickEvent = MenuAction.RemoveShortcut,
                state = MenuItemState.ACTIVE,
            )
    }
}
