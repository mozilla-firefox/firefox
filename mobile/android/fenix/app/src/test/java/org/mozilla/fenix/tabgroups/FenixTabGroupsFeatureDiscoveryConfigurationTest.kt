/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups

import io.mockk.every
import io.mockk.mockk
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.toList
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Test
import org.mozilla.fenix.tabgroups.FenixTabGroupsFeatureDiscoveryConfiguration.Companion.TAB_GROUP_MENU_IMPRESSION_LIMIT
import org.mozilla.fenix.tabgroups.FenixTabGroupsFeatureDiscoveryConfiguration.Companion.TAB_GROUP_TOOLBAR_HIGHLIGHT_IMPRESSION_LIMIT
import org.mozilla.fenix.tabstray.fakes.FakeTabManagerUiStateRepository
import org.mozilla.fenix.tabstray.repository.uistate.data.PersistedUIState
import org.mozilla.fenix.utils.Settings

@OptIn(ExperimentalCoroutinesApi::class)
class FenixTabGroupsFeatureDiscoveryConfigurationTest {

    @Test
    fun `WHEN tab groups are shown in the menu or not THEN the menu item visibility follows`() = runTest {
        listOf(true, false).forEach { showTabGroupsInMenu ->
            val configuration = configuration(PersistedUIState(), showTabGroupsInMenu = showTabGroupsInMenu)

            assertEquals(showTabGroupsInMenu, configuration.showMenuItem)
        }
    }

    @Test
    fun `GIVEN the item has not been shown or used WHEN observing the toolbar highlight THEN highlight it`() = runTest {
        val configuration = configuration(PersistedUIState())

        assertTrue(configuration.toolbarMenuButtonHighlight.first())
    }

    @Test
    fun `GIVEN the discovery state is not loaded yet WHEN observing the toolbar highlight THEN don't highlight it`() =
        runTest {
            val configuration = configuration(uiState = null)

            assertFalse(configuration.toolbarMenuButtonHighlight.first())
        }

    @Test
    fun `GIVEN tab groups are not shown in the menu WHEN observing the toolbar highlight THEN don't highlight it`() =
        runTest {
            val configuration = configuration(PersistedUIState(), showTabGroupsInMenu = false)

            assertFalse(configuration.toolbarMenuButtonHighlight.first())
        }

    @Test
    fun `GIVEN the toolbar is highlighted WHEN tab groups are removed from the menu THEN stop highlighting it`() =
        runTest {
            val showTabGroupsInMenu = MutableStateFlow(true)
            val configuration = configuration(FakeTabManagerUiStateRepository(PersistedUIState()), showTabGroupsInMenu)
            val highlights = mutableListOf<Boolean>()
            backgroundScope.launch { configuration.toolbarMenuButtonHighlight.toList(highlights) }
            runCurrent()

            showTabGroupsInMenu.value = false
            runCurrent()

            assertEquals(listOf(true, false), highlights)
        }

    @Test
    fun `GIVEN tab groups are not shown in the menu WHEN they are added to it THEN highlight the toolbar`() = runTest {
        val showTabGroupsInMenu = MutableStateFlow(false)
        val configuration = configuration(FakeTabManagerUiStateRepository(PersistedUIState()), showTabGroupsInMenu)
        val highlights = mutableListOf<Boolean>()
        backgroundScope.launch { configuration.toolbarMenuButtonHighlight.toList(highlights) }
        runCurrent()

        showTabGroupsInMenu.value = true
        runCurrent()

        assertEquals(listOf(false, true), highlights)
    }

    @Test
    fun `GIVEN the item was shown as many times as the limit WHEN observing the toolbar highlight THEN don't highlight it`() =
        runTest {
            val configuration =
                configuration(PersistedUIState(tabGroupMenuImpressionCount = TAB_GROUP_MENU_IMPRESSION_LIMIT))

            assertFalse(configuration.toolbarMenuButtonHighlight.first())
        }

    @Test
    fun `GIVEN the item was used WHEN observing the toolbar highlight THEN don't highlight it`() = runTest {
        val configuration = configuration(PersistedUIState(hasUserEverUsedTabGroupMenuItem = true))

        assertFalse(configuration.toolbarMenuButtonHighlight.first())
    }

    @Test
    fun `GIVEN the item is under the impression limit WHEN recording an exposure THEN increment the impression count`() =
        runTest {
            val repository = FakeTabManagerUiStateRepository(PersistedUIState(tabGroupMenuImpressionCount = 1))
            val configuration = configuration(repository)

            configuration.recordMenuItemExposure()
            runCurrent()

            assertEquals(2, repository.uiState.value?.tabGroupMenuImpressionCount)
        }

    @Test
    fun `GIVEN the item reached the impression limit WHEN recording an exposure THEN don't count it`() = runTest {
        val repository =
            FakeTabManagerUiStateRepository(
                PersistedUIState(tabGroupMenuImpressionCount = TAB_GROUP_MENU_IMPRESSION_LIMIT)
            )
        val configuration = configuration(repository)

        configuration.recordMenuItemExposure()
        runCurrent()

        assertEquals(TAB_GROUP_MENU_IMPRESSION_LIMIT, repository.uiState.value?.tabGroupMenuImpressionCount)
    }

    @Test
    fun `GIVEN the discovery state is not loaded yet WHEN recording events THEN don't record them`() = runTest {
        val repository = FakeTabManagerUiStateRepository(initialPersistedUIState = null)
        val configuration = configuration(repository)

        configuration.recordMenuItemExposure()
        configuration.recordMenuItemUsed()
        configuration.recordToolbarOpened()
        runCurrent()

        assertNull(repository.uiState.value)
    }

    @Test
    fun `WHEN the item reaches the impression limit THEN stop highlighting the toolbar`() = runTest {
        val repository =
            FakeTabManagerUiStateRepository(
                PersistedUIState(tabGroupMenuImpressionCount = TAB_GROUP_MENU_IMPRESSION_LIMIT - 1)
            )
        val configuration = configuration(repository)
        assertTrue(configuration.toolbarMenuButtonHighlight.first())

        configuration.recordMenuItemExposure()
        runCurrent()

        assertFalse(configuration.toolbarMenuButtonHighlight.first())
    }

    @Test
    fun `WHEN the item is used THEN record it and stop highlighting the toolbar`() = runTest {
        val repository = FakeTabManagerUiStateRepository(PersistedUIState())
        val configuration = configuration(repository)
        assertTrue(configuration.toolbarMenuButtonHighlight.first())

        configuration.recordMenuItemUsed()
        runCurrent()

        assertTrue(repository.uiState.value?.hasUserEverUsedTabGroupMenuItem == true)
        assertFalse(configuration.toolbarMenuButtonHighlight.first())
    }

    @Test
    fun `GIVEN the item has not been shown or used WHEN checking the menu item highlight THEN highlight it`() =
        runTest {
            val configuration = configuration(PersistedUIState())

            assertTrue(configuration.shouldHighlightMenuItem)
        }

    @Test
    fun `GIVEN the item should not be discovered anymore WHEN checking the menu item highlight THEN don't highlight it`() =
        runTest {
            listOf(
                    configuration(uiState = null),
                    configuration(PersistedUIState(), showTabGroupsInMenu = false),
                    configuration(PersistedUIState(tabGroupMenuImpressionCount = TAB_GROUP_MENU_IMPRESSION_LIMIT)),
                    configuration(PersistedUIState(hasUserEverUsedTabGroupMenuItem = true)),
                )
                .forEach { configuration ->
                    assertFalse(configuration.shouldHighlightMenuItem)
                }
        }

    @Test
    fun `GIVEN the menu was opened while the toolbar was highlighted WHEN observing the highlights THEN only highlight the menu item`() =
        runTest {
            val configuration =
                configuration(
                    PersistedUIState(
                        tabGroupToolbarMenuHighlightImpressionCount = TAB_GROUP_TOOLBAR_HIGHLIGHT_IMPRESSION_LIMIT
                    )
                )

            assertFalse(configuration.toolbarMenuButtonHighlight.first())
            assertTrue(configuration.shouldHighlightMenuItem)
        }

    @Test
    fun `GIVEN the toolbar is highlighted WHEN the menu is opened THEN stop highlighting the toolbar but keep highlighting the menu item`() =
        runTest {
            val repository = FakeTabManagerUiStateRepository(PersistedUIState())
            val configuration = configuration(repository)
            assertTrue(configuration.toolbarMenuButtonHighlight.first())

            configuration.recordToolbarOpened()
            runCurrent()

            assertEquals(1, repository.uiState.value?.tabGroupToolbarMenuHighlightImpressionCount)
            assertFalse(configuration.toolbarMenuButtonHighlight.first())
            assertTrue(configuration.shouldHighlightMenuItem)
        }

    @Test
    fun `GIVEN the toolbar is not highlighted anymore WHEN the menu is opened THEN don't count it`() = runTest {
        val repository =
            FakeTabManagerUiStateRepository(
                PersistedUIState(
                    tabGroupToolbarMenuHighlightImpressionCount = TAB_GROUP_TOOLBAR_HIGHLIGHT_IMPRESSION_LIMIT
                )
            )
        val configuration = configuration(repository)

        configuration.recordToolbarOpened()
        runCurrent()

        assertEquals(
            TAB_GROUP_TOOLBAR_HIGHLIGHT_IMPRESSION_LIMIT,
            repository.uiState.value?.tabGroupToolbarMenuHighlightImpressionCount,
        )
    }

    @Test
    fun `GIVEN the item was used WHEN the menu is opened THEN don't count it`() = runTest {
        val repository = FakeTabManagerUiStateRepository(PersistedUIState(hasUserEverUsedTabGroupMenuItem = true))
        val configuration = configuration(repository)

        configuration.recordToolbarOpened()
        runCurrent()

        assertEquals(0, repository.uiState.value?.tabGroupToolbarMenuHighlightImpressionCount)
    }

    private fun TestScope.configuration(
        uiState: PersistedUIState?,
        showTabGroupsInMenu: Boolean = true,
    ) = configuration(FakeTabManagerUiStateRepository(uiState), MutableStateFlow(showTabGroupsInMenu))

    private fun TestScope.configuration(
        repository: FakeTabManagerUiStateRepository,
        showTabGroupsInMenu: MutableStateFlow<Boolean> = MutableStateFlow(true),
    ): FenixTabGroupsFeatureDiscoveryConfiguration {
        val settings = mockk<Settings>()
        every { settings.showTabGroupsInMenu } answers { showTabGroupsInMenu.value }
        every { settings.showTabGroupsInMenuFlow } returns showTabGroupsInMenu

        return FenixTabGroupsFeatureDiscoveryConfiguration(
            settings = settings,
            tabGroupUiStateRepository = repository,
            scope = backgroundScope,
        )
    }
}
