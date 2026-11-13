/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.repository.uistate

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.datastore.core.DataStore
import androidx.datastore.core.IOException
import androidx.datastore.preferences.core.MutablePreferences
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.emptyPreferences
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.datastore.editOrCatch
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState

/** Repository for performing side effects related to persisted UI state of the Tab Manager feature. */
interface TabManagerUiStateRepository {

    /** [StateFlow] for observing the [PersistedUIState] saved on disk. */
    val uiState: StateFlow<PersistedUIState?>

    /** Marks the tab group onboarding as dismissed by the user. */
    suspend fun dismissTabGroupOnboarding(): Boolean

    /** Increments the number of times a user has seen the tab group onboarding. */
    suspend fun incrementTabGroupOnboardingImpressionCount(): Boolean

    /** Records the user as having at least one tab group during their use of the application. */
    suspend fun recordUserHadTabGroup(): Boolean

    /** Records the user as having viewed the Tab Groups page. */
    suspend fun recordTabGroupsPageViewed(): Boolean

    /** Records that the ungroup confirmation dialog should be skipped from now on. */
    suspend fun setSkipUngroupConfirmation(): Boolean

    /** Increments the number of times a user has seen the Tab Groups menu item. */
    suspend fun incrementTabGroupMenuImpressionCount(): Boolean

    /** Increments the number of times a user has seen the three dot menu highlight for Tab Groups. */
    suspend fun incrementTabGroupToolbarMenuHighlightImpressionCount(): Boolean

    /** Records that the Tab Groups menu item was used. */
    suspend fun recordTabGroupMenuItemUsed(): Boolean
}

/**
 * The default implementation of [TabManagerUiStateRepository].
 *
 * @param dataStore The [DataStore] used to read/write [PersistedUIState].
 * @param stateFlowScope The [CoroutineScope] used to construct the [StateFlow] for [uiState].
 */
class DefaultTabManagerUiStateRepository
internal constructor(
    private val dataStore: DataStore<Preferences>,
    stateFlowScope: CoroutineScope,
) : TabManagerUiStateRepository {

    private val logger = Logger(tag = "DefaultTabManagerUiStateRepository")

    constructor(
        context: Context,
        stateFlowScope: CoroutineScope,
    ) : this(
        dataStore = context.uiStateDataStore,
        stateFlowScope = stateFlowScope,
    )

    override val uiState: StateFlow<PersistedUIState?> =
        dataStore.data
            .catch { exception ->
                if (exception is IOException) {
                    logger.error("Error reading preferences.", exception)
                    emit(emptyPreferences())
                } else {
                    throw exception
                }
            }
            .map { preferences ->
                PersistedUIState(
                    hasUserDismissedTabGroupOnboarding = preferences[hasUserDismissedTabGroupOnboardingKey] ?: false,
                    tabGroupOnboardingImpressionCount = preferences[tabGroupOnboardingImpressionCountKey] ?: 0,
                    hasUserEverHadOneTabGroup = preferences[hasUserEverHadOneTabGroupKey] ?: false,
                    hasViewedTabGroupsPage = preferences[hasViewedTabGroupsPageKey] ?: false,
                    skipUngroupConfirmation = preferences[skipUngroupConfirmationKey] ?: false,
                    tabGroupMenuImpressionCount = preferences[tabGroupMenuItemImpressionCountKey] ?: 0,
                    tabGroupToolbarMenuHighlightImpressionCount =
                        preferences[tabGroupToolbarHighlightMenuOpenCountKey] ?: 0,
                    hasUserEverUsedTabGroupMenuItem = preferences[hasUserEverUsedTabGroupMenuItemKey] ?: false,
                )
            }
            .stateIn(
                scope = stateFlowScope,
                started = SharingStarted.Eagerly,
                initialValue = null,
            )

    override suspend fun dismissTabGroupOnboarding(): Boolean = updateDataStore { preferences ->
        preferences[hasUserDismissedTabGroupOnboardingKey] = true
    }

    override suspend fun incrementTabGroupOnboardingImpressionCount(): Boolean = updateDataStore { preferences ->
        val currentImpressionCount = preferences[tabGroupOnboardingImpressionCountKey] ?: 0
        preferences[tabGroupOnboardingImpressionCountKey] = currentImpressionCount + 1
    }

    override suspend fun recordUserHadTabGroup(): Boolean = updateDataStore { preferences ->
        preferences[hasUserEverHadOneTabGroupKey] = true
    }

    override suspend fun recordTabGroupsPageViewed(): Boolean = updateDataStore { preferences ->
        preferences[hasViewedTabGroupsPageKey] = true
    }

    override suspend fun setSkipUngroupConfirmation(): Boolean = updateDataStore { preferences ->
        preferences[skipUngroupConfirmationKey] = true
    }

    override suspend fun incrementTabGroupMenuImpressionCount(): Boolean = updateDataStore { preferences ->
        val currentImpressionCount = preferences[tabGroupMenuItemImpressionCountKey] ?: 0
        preferences[tabGroupMenuItemImpressionCountKey] = currentImpressionCount + 1
    }

    override suspend fun recordTabGroupMenuItemUsed(): Boolean = updateDataStore { preferences ->
        preferences[hasUserEverUsedTabGroupMenuItemKey] = true
    }

    override suspend fun incrementTabGroupToolbarMenuHighlightImpressionCount(): Boolean =
        updateDataStore { preferences ->
            val currentImpressionCount = preferences[tabGroupToolbarHighlightMenuOpenCountKey] ?: 0
            preferences[tabGroupToolbarHighlightMenuOpenCountKey] = currentImpressionCount + 1
        }

    @VisibleForTesting
    internal suspend fun initializeDataStore(initialUiState: PersistedUIState) {
        dataStore.editOrCatch(onError = {}) { preferences ->
            preferences[hasUserDismissedTabGroupOnboardingKey] = initialUiState.hasUserDismissedTabGroupOnboarding
            preferences[tabGroupOnboardingImpressionCountKey] = initialUiState.tabGroupOnboardingImpressionCount
            preferences[hasUserEverHadOneTabGroupKey] = initialUiState.hasUserEverHadOneTabGroup
            preferences[hasViewedTabGroupsPageKey] = initialUiState.hasViewedTabGroupsPage
            preferences[skipUngroupConfirmationKey] = initialUiState.skipUngroupConfirmation
            preferences[tabGroupMenuItemImpressionCountKey] = initialUiState.tabGroupMenuImpressionCount
            preferences[tabGroupToolbarHighlightMenuOpenCountKey] =
                initialUiState.tabGroupToolbarMenuHighlightImpressionCount
            preferences[hasUserEverUsedTabGroupMenuItemKey] = initialUiState.hasUserEverUsedTabGroupMenuItem
        }
    }

    private suspend fun updateDataStore(transform: suspend (MutablePreferences) -> Unit): Boolean {
        var preferenceWriteSucceeded = true
        dataStore.editOrCatch(
            onError = {
                preferenceWriteSucceeded = false
            },
            transform = transform,
        )

        return preferenceWriteSucceeded
    }
}
