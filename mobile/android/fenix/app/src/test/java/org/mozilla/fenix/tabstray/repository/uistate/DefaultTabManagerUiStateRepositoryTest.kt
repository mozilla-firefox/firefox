/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabstray.repository.uistate

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.preferencesDataStore
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.support.test.robolectric.testContext
import org.junit.After
import org.junit.runner.RunWith
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState

private val Context.testDataStore: DataStore<Preferences> by
    preferencesDataStore(name = "TabsTrayUIStateRepositoryTest")

@RunWith(AndroidJUnit4::class)
class DefaultTabManagerUiStateRepositoryTest {

    @After
    fun teardown() = runTest {
        testContext.testDataStore.edit { it.clear() }
    }

    @Test
    fun `WHEN the repository has initialized THEN the first emission is the initial persisted UI state`() = runTest {
        val repository = createRepository()
        val expectedInitialState =
            PersistedUIState(
                hasUserDismissedTabGroupOnboarding = false,
                tabGroupOnboardingImpressionCount = 0,
                hasUserEverHadOneTabGroup = false,
            )

        assertEquals(expectedInitialState, repository.uiState.first())
    }

    @Test
    fun `GIVEN the onboarding was previously dismissed WHEN the repository has initialized THEN the first emission contains this state`() =
        runTest {
            val expectedInitialState =
                PersistedUIState(
                    hasUserDismissedTabGroupOnboarding = true,
                    tabGroupOnboardingImpressionCount = 0,
                    hasUserEverHadOneTabGroup = false,
                )
            val repository = createRepository(initialPersistedUIState = expectedInitialState)

            assertEquals(expectedInitialState, repository.uiState.first())
        }

    @Test
    fun `GIVEN the user has seen the tab group onboarding 3 times WHEN the repository has initialized THEN the first emission contains this state`() =
        runTest {
            val expectedInitialState =
                PersistedUIState(
                    hasUserDismissedTabGroupOnboarding = true,
                    tabGroupOnboardingImpressionCount = 3,
                    hasUserEverHadOneTabGroup = false,
                )
            val repository = createRepository(initialPersistedUIState = expectedInitialState)

            assertEquals(expectedInitialState, repository.uiState.first())
        }

    @Test
    fun `GIVEN the user has previously had a tab group WHEN the repository has initialized THEN the first emission contains this state`() =
        runTest {
            val expectedInitialState =
                PersistedUIState(
                    hasUserDismissedTabGroupOnboarding = false,
                    tabGroupOnboardingImpressionCount = 0,
                    hasUserEverHadOneTabGroup = true,
                )
            val repository = createRepository(initialPersistedUIState = expectedInitialState)

            assertEquals(expectedInitialState, repository.uiState.first())
        }

    @Test
    fun `WHEN the tab group onboarding is dismissed THEN the dismissal is saved to disk`() = runTest {
        val repository = createRepository()
        repository.dismissTabGroupOnboarding()

        assertTrue { repository.uiState.first()!!.hasUserDismissedTabGroupOnboarding }
    }

    @Test
    fun `WHEN the user has seen the tab group onboarding THEN increment the impression counter`() = runTest {
        val repository = createRepository()
        repository.incrementTabGroupOnboardingImpressionCount()

        assertEquals(1, repository.uiState.first()!!.tabGroupOnboardingImpressionCount)
    }

    @Test
    fun `WHEN the user already has a group THEN the update the state on disk`() = runTest {
        val repository = createRepository()
        repository.recordUserHadTabGroup()

        assertTrue { repository.uiState.first()!!.hasUserEverHadOneTabGroup }
    }

    @Test
    fun `GIVEN the user has previously viewed the tab groups page WHEN the repository has initialized THEN the first emission contains this state`() =
        runTest {
            val expectedInitialState = PersistedUIState(hasViewedTabGroupsPage = true)
            val repository = createRepository(initialPersistedUIState = expectedInitialState)

            assertEquals(expectedInitialState, repository.uiState.first())
        }

    @Test
    fun `WHEN the tab groups page is viewed THEN the update is saved to disk`() = runTest {
        val repository = createRepository()
        repository.recordTabGroupsPageViewed()

        assertTrue { repository.uiState.first()!!.hasViewedTabGroupsPage }
    }

    @Test
    fun `GIVEN the ungroup confirmation was previously dismissed WHEN the repository has initialized THEN the first emission contains this state`() =
        runTest {
            val expectedInitialState = PersistedUIState(skipUngroupConfirmation = true)
            val repository = createRepository(initialPersistedUIState = expectedInitialState)

            assertEquals(expectedInitialState, repository.uiState.first())
        }

    @Test
    fun `WHEN the ungroup confirmation is dismissed THEN the update is saved to disk`() = runTest {
        val repository = createRepository()
        repository.setSkipUngroupConfirmation()

        assertTrue { repository.uiState.first()!!.skipUngroupConfirmation }
    }

    @Test
    fun `GIVEN the tab groups menu item was previously shown and used WHEN the repository has initialized THEN the first emission contains this state`() =
        runTest {
            val expectedInitialState =
                PersistedUIState(tabGroupMenuImpressionCount = 2, hasUserEverUsedTabGroupMenuItem = true)
            val repository = createRepository(initialPersistedUIState = expectedInitialState)

            assertEquals(expectedInitialState, repository.uiState.first())
        }

    @Test
    fun `WHEN the tab groups menu item is shown THEN increment the menu impression counter`() = runTest {
        val repository = createRepository()
        repository.incrementTabGroupMenuImpressionCount()
        repository.incrementTabGroupMenuImpressionCount()

        assertEquals(2, repository.uiState.first()!!.tabGroupMenuImpressionCount)
    }

    @Test
    fun `WHEN the tab groups menu item is used THEN the update is saved to disk`() = runTest {
        val repository = createRepository()
        repository.recordTabGroupMenuItemUsed()

        assertTrue { repository.uiState.first()!!.hasUserEverUsedTabGroupMenuItem }
    }

    @Test
    fun `GIVEN the menu was previously opened while the toolbar was highlighted WHEN the repository has initialized THEN the first emission contains this state`() =
        runTest {
            val expectedInitialState = PersistedUIState(tabGroupToolbarMenuHighlightImpressionCount = 1)
            val repository = createRepository(initialPersistedUIState = expectedInitialState)

            assertEquals(expectedInitialState, repository.uiState.first())
        }

    @Test
    fun `WHEN the menu is opened while the toolbar is highlighted THEN increment the toolbar highlight impression counter`() =
        runTest {
            val repository = createRepository()
            repository.incrementTabGroupToolbarMenuHighlightImpressionCount()
            repository.incrementTabGroupToolbarMenuHighlightImpressionCount()

            assertEquals(2, repository.uiState.first()!!.tabGroupToolbarMenuHighlightImpressionCount)
        }

    private suspend fun TestScope.createRepository(
        initialPersistedUIState: PersistedUIState =
            PersistedUIState(
                hasUserDismissedTabGroupOnboarding = false,
                tabGroupOnboardingImpressionCount = 0,
                hasUserEverHadOneTabGroup = false,
            )
    ) =
        DefaultTabManagerUiStateRepository(
                context = testContext,
                stateFlowScope = backgroundScope,
            )
            .apply {
                initializeDataStore(initialUiState = initialPersistedUIState)
            }
}
