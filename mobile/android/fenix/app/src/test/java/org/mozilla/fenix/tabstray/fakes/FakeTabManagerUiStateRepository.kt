/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.fakes

import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import org.mozilla.fenix.tabstray.repository.uistate.TabManagerUiStateRepository
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState

class FakeTabManagerUiStateRepository(initialPersistedUIState: PersistedUIState? = null) : TabManagerUiStateRepository {

    private val uiStateFlow: MutableStateFlow<PersistedUIState?> = MutableStateFlow(initialPersistedUIState)

    override val uiState: StateFlow<PersistedUIState?> = uiStateFlow

    override suspend fun dismissTabGroupOnboarding(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        uiStateFlow.emit(currentSnapshot.copy(hasUserDismissedTabGroupOnboarding = true))
        return true
    }

    override suspend fun incrementTabGroupOnboardingImpressionCount(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        val impressionCount = currentSnapshot.tabGroupOnboardingImpressionCount
        uiStateFlow.emit(currentSnapshot.copy(tabGroupOnboardingImpressionCount = impressionCount + 1))
        return true
    }

    override suspend fun recordUserHadTabGroup(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        uiStateFlow.emit(currentSnapshot.copy(hasUserEverHadOneTabGroup = true))
        return true
    }

    override suspend fun recordTabGroupsPageViewed(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        uiStateFlow.emit(currentSnapshot.copy(hasViewedTabGroupsPage = true))
        return true
    }

    override suspend fun setSkipUngroupConfirmation(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        uiStateFlow.emit(currentSnapshot.copy(skipUngroupConfirmation = true))
        return true
    }

    override suspend fun incrementTabGroupMenuImpressionCount(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        val impressionCount = currentSnapshot.tabGroupMenuImpressionCount
        uiStateFlow.emit(currentSnapshot.copy(tabGroupMenuImpressionCount = impressionCount + 1))
        return true
    }

    override suspend fun recordTabGroupMenuItemUsed(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        uiStateFlow.emit(currentSnapshot.copy(hasUserEverUsedTabGroupMenuItem = true))
        return true
    }

    override suspend fun incrementTabGroupToolbarMenuHighlightImpressionCount(): Boolean {
        val currentSnapshot = uiStateFlow.value ?: PersistedUIState()
        val impressionCount = currentSnapshot.tabGroupToolbarMenuHighlightImpressionCount
        uiStateFlow.emit(currentSnapshot.copy(tabGroupToolbarMenuHighlightImpressionCount = impressionCount + 1))
        return true
    }
}
