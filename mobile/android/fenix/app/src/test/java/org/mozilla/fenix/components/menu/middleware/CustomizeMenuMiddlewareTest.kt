/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Test
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuCustomizationRepository
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_1_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_2_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_3_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_4_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_5_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_6_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.NAVIGATION_ID

@OptIn(ExperimentalCoroutinesApi::class)
class CustomizeMenuMiddlewareTest {
    private val repository: MenuCustomizationRepository = mockk(relaxed = true)
    private val menuBuilder: MenuBuilder = mockk()
    private val captureActions = CaptureActionsMiddleware<CustomizableMenuState, CustomizableMenuAction>()
    private val defaultMenuInfo =
        BrowserMenuConfiguration.defaultMenuInfo(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

    @Test
    fun `GIVEN the menu was customized WHEN the screen is shown THEN show the sections in the saved order, the ones that cannot be moved and the removed ones`() =
        runTest {
            val customization =
                CustomizedMenuInfo(
                    visibleSectionIds =
                        listOf(NAVIGATION_ID, GROUP_3_ID, GROUP_1_ID, GROUP_2_ID, GROUP_4_ID, GROUP_5_ID),
                    hiddenSectionIds = listOf(GROUP_6_ID),
                )
            createStore(customization = MutableStateFlow(customization))
            runCurrent()

            captureActions.assertLastAction(Update::class) {
                assertEquals(
                    groups(NAVIGATION_ID, GROUP_3_ID, GROUP_1_ID, GROUP_2_ID, GROUP_4_ID, GROUP_5_ID, GROUP_6_ID),
                    it.items,
                )
                assertEquals(setOf(GROUP_5_ID), it.mandatoryGroupIds)
                assertEquals(setOf(GROUP_6_ID), it.hiddenGroupIds)
                assertTrue(it.isCustomized)
            }
        }

    @Test
    fun `GIVEN the menu was not customized WHEN the screen is shown THEN show it as not customized`() = runTest {
        createStore(customization = MutableStateFlow(defaultMenuInfo))
        runCurrent()

        captureActions.assertLastAction(Update::class) {
            assertEquals(defaultMenuInfo.visibleSectionIds, it.items.map { group -> group.id })
            assertFalse(it.isCustomized)
        }
    }

    @Test
    fun `GIVEN a section has nothing to show WHEN the screen is shown THEN don't show it`() = runTest {
        createStore(
            customization = MutableStateFlow(defaultMenuInfo),
            menuGroups = groups(NAVIGATION_ID, GROUP_2_ID, GROUP_3_ID, GROUP_4_ID, GROUP_5_ID, GROUP_6_ID),
        )
        runCurrent()

        captureActions.assertLastAction(Update::class) {
            assertEquals(
                listOf(NAVIGATION_ID, GROUP_2_ID, GROUP_3_ID, GROUP_4_ID, GROUP_5_ID, GROUP_6_ID),
                it.items.map { group -> group.id },
            )
        }
    }

    @Test
    fun `GIVEN the customization was not read yet WHEN the screen is shown THEN show the menu only after reading it`() =
        runTest {
            val customization = MutableStateFlow<CustomizedMenuInfo?>(null)
            createStore(customization = customization)
            runCurrent()

            captureActions.assertNotDispatched(Update::class)

            customization.value = defaultMenuInfo
            runCurrent()

            captureActions.assertLastAction(Update::class)
        }

    @Test
    fun `WHEN the user changes the menu structure THEN save it`() = runTest {
        val store = createStore()

        store.dispatch(
            MenuStructureChanged(
                items = groups(GROUP_1_ID, NAVIGATION_ID, GROUP_2_ID, GROUP_3_ID, GROUP_4_ID, GROUP_5_ID, GROUP_6_ID),
                hiddenGroupIds = setOf(GROUP_6_ID),
            )
        )

        verify {
            repository.save(
                visibleSectionIds = listOf(GROUP_1_ID, NAVIGATION_ID, GROUP_2_ID, GROUP_3_ID, GROUP_4_ID, GROUP_5_ID),
                hiddenSectionIds = listOf(GROUP_6_ID),
            )
        }
    }

    @Test
    fun `GIVEN the menu was not customized WHEN the user moves a section THEN show the menu as customized`() = runTest {
        val store = createStore(initialState = CustomizableMenuState(menuGroups = defaultGroups()))

        store.dispatch(
            MenuStructureChanged(
                items = groups(GROUP_1_ID, NAVIGATION_ID, GROUP_2_ID, GROUP_3_ID, GROUP_4_ID, GROUP_5_ID, GROUP_6_ID),
                hiddenGroupIds = emptySet(),
            )
        )

        assertTrue(store.state.isCustomized)
    }

    @Test
    fun `GIVEN the menu was customized WHEN the user restores the default structure THEN show the menu as not customized`() =
        runTest {
            val store =
                createStore(initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true))

            store.dispatch(MenuStructureChanged(items = defaultGroups(), hiddenGroupIds = emptySet()))

            assertFalse(store.state.isCustomized)
        }

    @Test
    fun `GIVEN the menu was customized WHEN the user changes it but it still differs from the default THEN keep showing it as customized`() =
        runTest {
            val store =
                createStore(initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true))

            store.dispatch(
                MenuStructureChanged(
                    items =
                        groups(NAVIGATION_ID, GROUP_1_ID, GROUP_2_ID, GROUP_3_ID, GROUP_4_ID, GROUP_5_ID, GROUP_6_ID),
                    hiddenGroupIds = setOf(GROUP_6_ID),
                )
            )

            captureActions.assertNotDispatched(CustomizationStatusChanged::class)
            assertTrue(store.state.isCustomized)
        }

    // The customization is read on a scope that runTest cancels at the end of each test.
    private fun TestScope.createStore(
        initialState: CustomizableMenuState = CustomizableMenuState(menuGroups = emptyList()),
        customization: StateFlow<CustomizedMenuInfo?> = MutableStateFlow(null),
        menuGroups: List<MenuItemsGroup> = groups(*BrowserMenuConfiguration.allSections().map { it.id }.toTypedArray()),
    ): CustomizableMenuStore {
        every { repository.customizedMenuInfo } returns customization
        every { menuBuilder.menuStructure } returns flowOf(menuGroups)

        return CustomizableMenuStore(
            initialState = initialState,
            middleware =
                listOf(
                    captureActions,
                    CustomizeMenuMiddleware(
                        repository = repository,
                        menuBuilder = menuBuilder,
                        isToolbarAtBottom = false,
                        isExpandedToolbarEnabled = false,
                        scope = backgroundScope,
                    ),
                ),
        )
    }

    private fun defaultGroups() = groups(*defaultMenuInfo.visibleSectionIds.toTypedArray())

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
