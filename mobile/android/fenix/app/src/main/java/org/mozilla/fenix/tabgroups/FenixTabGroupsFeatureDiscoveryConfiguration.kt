/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import androidx.annotation.VisibleForTesting
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import org.mozilla.fenix.tabstray.repository.uistate.TabManagerUiStateRepository
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState
import org.mozilla.fenix.utils.Settings

/**
 * [TabGroupsFeatureDiscoveryConfiguration] backed by [Settings] for whether the feature is shown in the menu, and by
 * [TabManagerUiStateRepository] for how many times the menu item has been shown and whether it has been used.
 *
 * @param settings [Settings] telling whether tab groups are shown in the menu.
 * @param tabGroupUiStateRepository [TabManagerUiStateRepository] persisting the discovery state.
 * @param scope [CoroutineScope] outliving the menu, on which discovery events are recorded.
 */
class FenixTabGroupsFeatureDiscoveryConfiguration(
    private val settings: Settings,
    private val tabGroupUiStateRepository: TabManagerUiStateRepository,
    private val scope: CoroutineScope,
) : TabGroupsFeatureDiscoveryConfiguration {
    companion object {
        @VisibleForTesting internal const val TAB_GROUP_MENU_IMPRESSION_LIMIT = 3
        @VisibleForTesting internal const val TAB_GROUP_TOOLBAR_HIGHLIGHT_IMPRESSION_LIMIT = 1
    }

    override val showMenuItem: Boolean
        get() = settings.showTabGroupsInMenu

    override val toolbarMenuButtonHighlight: Flow<Boolean> =
        tabGroupUiStateRepository.uiState
            .map {
                it.showToolbarHighlight()
            }
            .distinctUntilChanged()

    override val shouldHighlightMenuItem: Boolean
        get() = tabGroupUiStateRepository.uiState.value.showMenuItemHighlight()

    private val shouldHighlightToolbar: Boolean
        get() = tabGroupUiStateRepository.uiState.value.showToolbarHighlight()

    override fun recordMenuItemExposure() {
        if (shouldHighlightMenuItem) {
            scope.launch { tabGroupUiStateRepository.incrementTabGroupMenuImpressionCount() }
        }
    }

    override fun recordMenuItemUsed() {
        if (shouldHighlightMenuItem) {
            scope.launch { tabGroupUiStateRepository.recordTabGroupMenuItemUsed() }
        }
    }

    override fun recordToolbarOpened() {
        if (shouldHighlightToolbar) {
            scope.launch { tabGroupUiStateRepository.incrementTabGroupToolbarMenuHighlightImpressionCount() }
        }
    }

    private fun PersistedUIState?.showMenuItemHighlight() =
        this != null &&
            showMenuItem &&
            tabGroupMenuImpressionCount < TAB_GROUP_MENU_IMPRESSION_LIMIT &&
            !hasUserEverUsedTabGroupMenuItem

    private fun PersistedUIState?.showToolbarHighlight() =
        this != null &&
            showMenuItemHighlight() &&
            tabGroupToolbarMenuHighlightImpressionCount < TAB_GROUP_TOOLBAR_HIGHLIGHT_IMPRESSION_LIMIT
}
