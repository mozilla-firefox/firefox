/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.MenuItemIconRes
import mozilla.components.feature.tabdata.coordinator.TabDataCoordinator
import mozilla.components.feature.tabdata.coordinator.data.TabsSnapshot
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.R
import org.mozilla.fenix.components.menu.MenuFragmentDirections
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuTarget
import org.mozilla.fenix.components.menu.store.MenuAction
import org.mozilla.fenix.tabgroups.flow.TabGroupFlowEntryPoint

/**
 * [MenuItemProvider] for TabGroup menu items. Allows adding a tab to a new or existing tab group, or moving a tab
 * already in a tab group to a new or existing tab group.
 *
 * @param tabDataCoordinator [TabDataCoordinator] determines whether the selected tab is private or already in a group.
 * @param discoveryConfiguration [TabGroupsFeatureDiscoveryConfiguration] recording when the item is shown and used.
 * @param target [MenuTarget] target this menu item acts on.
 * @param isVisible Whether the item can be shown in the menu.
 * @param scope [CoroutineScope] used to keep the item up to date for as long as it can be shown.
 */
class TabGroupMenuItemProvider(
    tabDataCoordinator: TabDataCoordinator,
    private val discoveryConfiguration: TabGroupsFeatureDiscoveryConfiguration,
    target: MenuTarget,
    isVisible: Boolean,
    scope: CoroutineScope,
) : MenuItemProvider {

    companion object {
        @VisibleForTesting
        internal val ADD_ITEM =
            tabGroupItem(
                title = R.string.browser_menu_add_to_tab_group,
                entryPoint = null,
            )

        private fun tabGroupItem(
            @StringRes title: Int,
            entryPoint: TabGroupFlowEntryPoint?,
            isHighlighted: Boolean = false,
        ): StandardMenuItem {
            return StandardMenuItem(
                title = MenuItemTitle(Text.Resource(title)),
                icon = MenuItemIconRes(iconsR.drawable.mozac_ic_tab_group_24, isHighlighted = isHighlighted),
                showNewIndicator = isHighlighted,
                onShownEvent = entryPoint?.let { MenuAction.OnTabGroupMenuItemExposed },
                onClickEvent = entryPoint?.let { MenuAction.Navigate.OpenTabGroupFlow(it) },
            )
        }
    }

    private var hasRecordedExposure = false

    override val itemsFlow: StateFlow<List<MenuItem>> =
        when {
            !isVisible -> MutableStateFlow(emptyList())
            target == MenuTarget.CustomizeMenu -> MutableStateFlow(listOf(ADD_ITEM))
            target != MenuTarget.BrowserTab -> MutableStateFlow(emptyList())
            else -> {
                // Read once so the highlight stays stable while the menu is open, even after the exposure it records.
                val isHighlighted = discoveryConfiguration.shouldHighlightMenuItem
                tabDataCoordinator.tabsSnapshot
                    .map { snapshot -> listOfNotNull(snapshot?.toMenuItem(isHighlighted = isHighlighted)) }
                    .stateIn(
                        scope = scope,
                        started = SharingStarted.Eagerly,
                        initialValue =
                            listOfNotNull(
                                tabDataCoordinator.tabsSnapshot.value?.toMenuItem(isHighlighted = isHighlighted)
                            ),
                    )
            }
        }

    private fun TabsSnapshot.toMenuItem(isHighlighted: Boolean): MenuItem? {
        val tab = selectedTab ?: return null
        if (tab.private) return null
        return if (tab.tabGroupId != null) {
            tabGroupItem(
                isHighlighted = isHighlighted,
                title = R.string.browser_menu_move_to_tab_group,
                entryPoint = TabGroupFlowEntryPoint.MoveTabToGroup(tab.id),
            )
        } else {
            tabGroupItem(
                isHighlighted = isHighlighted,
                title = R.string.browser_menu_add_to_tab_group,
                entryPoint = TabGroupFlowEntryPoint.AddTabToGroup(tab.id),
            )
        }
    }

    override fun handles(event: MenuEvent) =
        event is MenuAction.Navigate.OpenTabGroupFlow || event == MenuAction.OnTabGroupMenuItemExposed

    override fun onEvent(
        event: MenuEvent,
        menu: MenuHost,
    ) {
        when (event) {
            MenuAction.OnTabGroupMenuItemExposed -> {
                if (!hasRecordedExposure) {
                    hasRecordedExposure = true
                    discoveryConfiguration.recordMenuItemExposure()
                }
            }
            is MenuAction.Navigate.OpenTabGroupFlow -> {
                discoveryConfiguration.recordMenuItemUsed()
                val entryPoint = event.entryPoint
                menu.navigate(MenuFragmentDirections.actionMenuFragmentToTabGroupFlowFragment(entryPoint = entryPoint))
            }
            else -> Unit
        }
    }
}
