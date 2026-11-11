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
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.MenuBuilder
import org.mozilla.fenix.components.menu.MenuCustomizationRepository
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.LIBRARY_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.PAGE_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.QUIT_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.READER_VIEW_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TABS_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.VPN_GROUP_ID

@OptIn(ExperimentalCoroutinesApi::class)
class CustomizeMenuMiddlewareTest {
    private val repository: MenuCustomizationRepository = mockk(relaxed = true)
    private val menuBuilder: MenuBuilder = mockk()
    private val captureActions = CaptureActionsMiddleware<CustomizableMenuState, CustomizableMenuAction>()
    private val captureAppActions = CaptureActionsMiddleware<AppState, AppAction>()
    private val appStore = AppStore(middlewares = listOf(captureAppActions))
    private val cannotHideSectionMessage = "Can't hide this section"
    private val defaultMenuInfo =
        BrowserMenuConfiguration.defaultMenuInfo(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

    @Test
    fun `GIVEN the menu was customized WHEN the screen is shown THEN show the sections in the saved order, the ones that cannot be moved and the removed ones`() =
        runTest {
            val customization =
                CustomizedMenuInfo(
                    visibleSectionIds =
                        listOf(
                            PAGE_CONTROLS_GROUP_ID,
                            TOOLS_GROUP_ID,
                            READER_VIEW_GROUP_ID,
                            VPN_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                        ),
                    hiddenSectionIds = listOf(QUIT_GROUP_ID),
                )
            createStore(customization = MutableStateFlow(customization))
            runCurrent()

            captureActions.assertLastAction(Update::class) {
                assertEquals(
                    groups(
                        PAGE_CONTROLS_GROUP_ID,
                        TOOLS_GROUP_ID,
                        READER_VIEW_GROUP_ID,
                        VPN_GROUP_ID,
                        LIBRARY_GROUP_ID,
                        ACCOUNTS_AND_SETTINGS_GROUP_ID,
                        QUIT_GROUP_ID,
                    ),
                    it.items,
                )
                assertEquals(setOf(ACCOUNTS_AND_SETTINGS_GROUP_ID), it.mandatoryGroupIds)
                assertEquals(setOf(QUIT_GROUP_ID), it.hiddenGroupIds)
                assertTrue(it.isCustomized)
            }
        }

    @Test
    fun `GIVEN the menu was not customized WHEN the screen is shown THEN show it as not customized`() = runTest {
        createStore(customization = MutableStateFlow(defaultMenuInfo))
        runCurrent()

        captureActions.assertLastAction(Update::class) {
            assertEquals(
                defaultMenuInfo.visibleSectionIds + defaultMenuInfo.hiddenSectionIds,
                it.items.map { group -> group.id },
            )
            assertEquals(setOf(TABS_CONTROLS_GROUP_ID), it.hiddenGroupIds)
            assertFalse(it.isCustomized)
        }
    }

    @Test
    fun `GIVEN a section has nothing to show WHEN the screen is shown THEN don't show it`() = runTest {
        createStore(
            customization = MutableStateFlow(defaultMenuInfo),
            menuGroups =
                groups(
                    PAGE_CONTROLS_GROUP_ID,
                    VPN_GROUP_ID,
                    TOOLS_GROUP_ID,
                    LIBRARY_GROUP_ID,
                    ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    QUIT_GROUP_ID,
                ),
        )
        runCurrent()

        captureActions.assertLastAction(Update::class) {
            assertEquals(
                listOf(
                    PAGE_CONTROLS_GROUP_ID,
                    VPN_GROUP_ID,
                    TOOLS_GROUP_ID,
                    LIBRARY_GROUP_ID,
                    ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    QUIT_GROUP_ID,
                ),
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
                items =
                    groups(
                        READER_VIEW_GROUP_ID,
                        PAGE_CONTROLS_GROUP_ID,
                        VPN_GROUP_ID,
                        TOOLS_GROUP_ID,
                        LIBRARY_GROUP_ID,
                        ACCOUNTS_AND_SETTINGS_GROUP_ID,
                        QUIT_GROUP_ID,
                    ),
                hiddenGroupIds = setOf(QUIT_GROUP_ID),
                movedGroupId = PAGE_CONTROLS_GROUP_ID,
            )
        )

        verify {
            repository.save(
                CustomizedMenuInfo(
                    visibleSectionIds =
                        listOf(
                            READER_VIEW_GROUP_ID,
                            PAGE_CONTROLS_GROUP_ID,
                            VPN_GROUP_ID,
                            TOOLS_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                        ),
                    hiddenSectionIds = listOf(QUIT_GROUP_ID),
                    isNavigationSectionMoved = true,
                )
            )
        }
    }

    @Test
    fun `GIVEN the navigation section follows the toolbar WHEN the user changes another section THEN keep it following the toolbar`() =
        runTest {
            val store = createStore()

            store.dispatch(
                MenuStructureChanged(
                    items =
                        groups(
                            PAGE_CONTROLS_GROUP_ID,
                            VPN_GROUP_ID,
                            READER_VIEW_GROUP_ID,
                            TOOLS_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                            QUIT_GROUP_ID,
                        ),
                    hiddenGroupIds = emptySet(),
                    movedGroupId = VPN_GROUP_ID,
                )
            )

            verify {
                repository.save(
                    CustomizedMenuInfo(
                        visibleSectionIds =
                            listOf(
                                PAGE_CONTROLS_GROUP_ID,
                                VPN_GROUP_ID,
                                READER_VIEW_GROUP_ID,
                                TOOLS_GROUP_ID,
                                LIBRARY_GROUP_ID,
                                ACCOUNTS_AND_SETTINGS_GROUP_ID,
                                QUIT_GROUP_ID,
                            ),
                        hiddenSectionIds = emptyList(),
                        isNavigationSectionMoved = false,
                    )
                )
            }
        }

    @Test
    fun `GIVEN the user moved the navigation section WHEN the toolbar moved to its side and the user changes another section THEN keep it independent of the toolbar`() =
        runTest {
            // Moved to the bottom while the toolbar was at the top, which is now the toolbar's side.
            val customization =
                CustomizedMenuInfo(
                    visibleSectionIds =
                        listOf(
                            READER_VIEW_GROUP_ID,
                            VPN_GROUP_ID,
                            TOOLS_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                            PAGE_CONTROLS_GROUP_ID,
                        ),
                    hiddenSectionIds = listOf(QUIT_GROUP_ID),
                    isNavigationSectionMoved = true,
                )
            val store = createStore(customization = MutableStateFlow(customization), isToolbarAtBottom = true)

            store.dispatch(
                MenuStructureChanged(
                    items =
                        groups(
                            VPN_GROUP_ID,
                            READER_VIEW_GROUP_ID,
                            TOOLS_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                            PAGE_CONTROLS_GROUP_ID,
                            QUIT_GROUP_ID,
                        ),
                    hiddenGroupIds = setOf(QUIT_GROUP_ID),
                    movedGroupId = VPN_GROUP_ID,
                )
            )

            verify {
                repository.save(
                    CustomizedMenuInfo(
                        visibleSectionIds =
                            listOf(
                                VPN_GROUP_ID,
                                READER_VIEW_GROUP_ID,
                                TOOLS_GROUP_ID,
                                LIBRARY_GROUP_ID,
                                ACCOUNTS_AND_SETTINGS_GROUP_ID,
                                PAGE_CONTROLS_GROUP_ID,
                            ),
                        hiddenSectionIds = listOf(QUIT_GROUP_ID),
                        isNavigationSectionMoved = true,
                    )
                )
            }
        }

    @Test
    fun `GIVEN the user moved the navigation section WHEN they move it back before the move is reported THEN keep it independent of the toolbar`() =
        runTest {
            val store = createStore()

            store.dispatch(
                MenuStructureChanged(
                    items =
                        groups(
                            READER_VIEW_GROUP_ID,
                            PAGE_CONTROLS_GROUP_ID,
                            VPN_GROUP_ID,
                            TOOLS_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                            QUIT_GROUP_ID,
                        ),
                    hiddenGroupIds = emptySet(),
                    movedGroupId = PAGE_CONTROLS_GROUP_ID,
                )
            )
            store.dispatch(
                MenuStructureChanged(
                    items =
                        groups(
                            PAGE_CONTROLS_GROUP_ID,
                            READER_VIEW_GROUP_ID,
                            VPN_GROUP_ID,
                            TOOLS_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                            QUIT_GROUP_ID,
                        ),
                    hiddenGroupIds = emptySet(),
                    movedGroupId = PAGE_CONTROLS_GROUP_ID,
                )
            )

            verify {
                repository.save(
                    CustomizedMenuInfo(
                        visibleSectionIds =
                            listOf(
                                PAGE_CONTROLS_GROUP_ID,
                                READER_VIEW_GROUP_ID,
                                VPN_GROUP_ID,
                                TOOLS_GROUP_ID,
                                LIBRARY_GROUP_ID,
                                ACCOUNTS_AND_SETTINGS_GROUP_ID,
                                QUIT_GROUP_ID,
                            ),
                        hiddenSectionIds = emptyList(),
                        isNavigationSectionMoved = true,
                    )
                )
            }
        }

    @Test
    fun `GIVEN the navigation section is independent of the toolbar but shown as by default WHEN the screen is shown THEN show the menu as customized`() =
        runTest {
            createStore(customization = MutableStateFlow(defaultMenuInfo.copy(isNavigationSectionMoved = true)))
            runCurrent()

            captureActions.assertLastAction(Update::class) { assertTrue(it.isCustomized) }
        }

    @Test
    fun `GIVEN the menu was not customized WHEN the user moves a section THEN show the menu as customized`() = runTest {
        val store = createStore(initialState = CustomizableMenuState(menuGroups = defaultGroups()))

        store.dispatch(
            MenuStructureChanged(
                items =
                    groups(
                        READER_VIEW_GROUP_ID,
                        PAGE_CONTROLS_GROUP_ID,
                        VPN_GROUP_ID,
                        TOOLS_GROUP_ID,
                        LIBRARY_GROUP_ID,
                        ACCOUNTS_AND_SETTINGS_GROUP_ID,
                        QUIT_GROUP_ID,
                    ),
                hiddenGroupIds = emptySet(),
                movedGroupId = PAGE_CONTROLS_GROUP_ID,
            )
        )

        assertTrue(store.state.isCustomized)
    }

    @Test
    fun `GIVEN the menu was customized WHEN the user restores the default structure THEN show the menu as not customized`() =
        runTest {
            val store =
                createStore(initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true))

            store.dispatch(
                MenuStructureChanged(
                    items = groups(*BrowserMenuConfiguration.allSections().map { it.id }.toTypedArray()),
                    hiddenGroupIds = defaultMenuInfo.hiddenSectionIds.toSet(),
                    movedGroupId = TABS_CONTROLS_GROUP_ID,
                )
            )

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
                        groups(
                            PAGE_CONTROLS_GROUP_ID,
                            READER_VIEW_GROUP_ID,
                            VPN_GROUP_ID,
                            TOOLS_GROUP_ID,
                            LIBRARY_GROUP_ID,
                            ACCOUNTS_AND_SETTINGS_GROUP_ID,
                            QUIT_GROUP_ID,
                        ),
                    hiddenGroupIds = setOf(QUIT_GROUP_ID),
                    movedGroupId = QUIT_GROUP_ID,
                )
            )

            captureActions.assertNotDispatched(CustomizationStatusChanged::class)
            assertTrue(store.state.isCustomized)
        }

    @Test
    fun `WHEN the user tries to remove a section which cannot be removed THEN tell them it cannot be hidden`() =
        runTest {
            val store = createStore(initialState = CustomizableMenuState(menuGroups = defaultGroups()))

            store.dispatch(AttemptedRemovalOfMandatorySection(ACCOUNTS_AND_SETTINGS_GROUP_ID))

            captureAppActions.assertLastAction(SnackbarAction.ShowSnackbar::class) { action ->
                assertEquals(cannotHideSectionMessage, action.title)
            }
        }

    @Test
    fun `WHEN the user resets to default THEN reset the repository and update store with default structure`() =
        runTest {
            val store =
                createStore(initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true))

            store.dispatch(ResetToDefaultConfirmed)
            runCurrent()

            verify { repository.reset() }
            captureActions.assertLastAction(Update::class) { action ->
                assertFalse(action.isCustomized)
            }
        }

    // The customization is read on a scope that runTest cancels at the end of each test.
    private fun TestScope.createStore(
        initialState: CustomizableMenuState = CustomizableMenuState(menuGroups = emptyList()),
        customization: StateFlow<CustomizedMenuInfo?> = MutableStateFlow(null),
        menuGroups: List<MenuItemsGroup> = groups(*BrowserMenuConfiguration.allSections().map { it.id }.toTypedArray()),
        isToolbarAtBottom: Boolean = false,
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
                        isToolbarAtBottom = isToolbarAtBottom,
                        isExpandedToolbarEnabled = false,
                        scope = backgroundScope,
                        appStore = appStore,
                        cannotHideSectionMessage = cannotHideSectionMessage,
                    ),
                ),
        )
    }

    private fun defaultGroups() = groups(*defaultMenuInfo.visibleSectionIds.toTypedArray())

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
