/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.collections

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.NavGraphDirections
import org.mozilla.fenix.R
import org.mozilla.fenix.components.TabCollectionStorage
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.utils.Settings

/**
 * [MenuItemProvider] for the menu item allowing to add the current webpage to a collection.
 *
 * @param settings [Settings] used to check whether the collections feature is enabled or not.
 * @param tabCollectionStorage [TabCollectionStorage] allowing to check what collections currently exist.
 * @param browserStore [BrowserStore] used to get the current tab to add to a collection.
 * @param target [MenuTarget] for which this menu item would be shown for.
 */
class SaveToCollectionMenuItemProvider(
    settings: Settings,
    tabCollectionStorage: TabCollectionStorage,
    private val browserStore: BrowserStore,
    private val target: MenuTarget,
) : MenuItemProvider {
    override val itemsFlow: StateFlow<List<MenuItem>> =
        if (target == MenuTarget.CustomizeMenu) {
            MutableStateFlow(listOf(DEFAULT_MENU_ITEM))
        } else if (target == MenuTarget.Preview) {
            MutableStateFlow(
                listOf(
                    DEFAULT_MENU_ITEM.copy(onClickEvent = MenuAction.Navigate.SaveToCollection(hasCollection = false))
                )
            )
        } else {
            MutableStateFlow(
                listOfNotNull(
                    if (settings.collections) {
                        DEFAULT_MENU_ITEM.copy(
                            onClickEvent =
                                MenuAction.Navigate.SaveToCollection(
                                    hasCollection = tabCollectionStorage.cachedTabCollections.isNotEmpty()
                                )
                        )
                    } else {
                        null
                    }
                )
            )
        }

    override fun handles(event: MenuEvent) =
        target != MenuTarget.Preview && (event is MenuAction.Navigate.SaveToCollection)

    override fun onEvent(event: MenuEvent, menu: MenuHost) {
        if (event !is MenuAction.Navigate.SaveToCollection) return
        val tabId = target.browserSessionFrom(browserStore.state)?.id ?: return

        menu.navigate(
            NavGraphDirections.actionGlobalCollectionCreationFragment(
                tabIds = arrayOf(tabId),
                selectedTabIds = arrayOf(tabId),
                saveCollectionStep =
                    when (event.hasCollection) {
                        true -> SaveCollectionStep.SelectCollection
                        false -> SaveCollectionStep.NameCollection
                    },
            )
        )
    }

    companion object {
        private val DEFAULT_MENU_ITEM =
            StandardMenuItem(
                title = MenuItemTitle(Text.Resource(R.string.browser_menu_save_to_collection_2)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_collection_24),
            )
    }
}
