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
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuAction
import mozilla.components.compose.menu.store.CustomizableMenuAction.AttemptedRemovalOfMandatorySection
import mozilla.components.compose.menu.store.CustomizableMenuAction.CustomizationStatusChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.MenuStructureChanged
import mozilla.components.compose.menu.store.CustomizableMenuAction.ResetToDefaultConfirmed
import mozilla.components.compose.menu.store.CustomizableMenuAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuState
import mozilla.components.compose.menu.store.CustomizableMenuStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SnackbarAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.menu.CustomizedItemInfo
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
import org.mozilla.fenix.components.menu.ExpandableMenuItemProvider
import org.mozilla.fenix.components.menu.MenuCustomizationRepository
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.LIBRARY_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.PAGE_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.QUIT_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.READER_VIEW_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TABS_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.VPN_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Print
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Translate
import org.mozilla.fenix.utils.Settings

@OptIn(ExperimentalCoroutinesApi::class)
class CustomizeMenuMiddlewareTest {
    private val repository: MenuCustomizationRepository = mockk(relaxed = true)
    private val settings: Settings = mockk(relaxed = true)
    private val captureActions = CaptureActionsMiddleware<CustomizableMenuState, CustomizableMenuAction>()
    private val captureAppActions = CaptureActionsMiddleware<AppState, AppAction>()
    private val appStore = AppStore(middlewares = listOf(captureAppActions))
    private val cannotHideSectionMessage = "Can't hide this section"
    private val moreId = More(emptyList()).id
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
                    listOf(
                        PAGE_CONTROLS_GROUP_ID,
                        TOOLS_GROUP_ID,
                        READER_VIEW_GROUP_ID,
                        VPN_GROUP_ID,
                        LIBRARY_GROUP_ID,
                        ACCOUNTS_AND_SETTINGS_GROUP_ID,
                        QUIT_GROUP_ID,
                    ),
                    it.items.map { group -> group.id },
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
            sectionsWithNothingToShow = setOf(READER_VIEW_GROUP_ID, TABS_CONTROLS_GROUP_ID),
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
    fun `GIVEN the user moved the navigation section WHEN they move it back THEN keep it independent of the toolbar`() =
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

    @Test
    fun `GIVEN the user changed the tools items WHEN the screen is shown THEN show the menu as customized`() = runTest {
        createStore(
            customization = MutableStateFlow(defaultMenuInfo),
            sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to changedToolsInfo())),
            isToolsMenuCustomizationEnabled = true,
        )
        runCurrent()

        captureActions.assertLastAction(Update::class) { assertTrue(it.isCustomized) }
    }

    @Test
    fun `GIVEN the tools items can be customized WHEN the screen is shown THEN the items of the tools section can be edited`() =
        runTest {
            createStore(customization = MutableStateFlow(defaultMenuInfo), isToolsMenuCustomizationEnabled = true)
            runCurrent()

            captureActions.assertLastAction(Update::class) { assertEquals(setOf(TOOLS_GROUP_ID), it.editableGroupIds) }
        }

    @Test
    fun `GIVEN the tools items cannot be customized WHEN the screen is shown THEN no items can be edited`() = runTest {
        createStore(customization = MutableStateFlow(defaultMenuInfo), isToolsMenuCustomizationEnabled = false)
        runCurrent()

        captureActions.assertLastAction(Update::class) { assertEquals(emptySet(), it.editableGroupIds) }
    }

    @Test
    fun `GIVEN the user saved the tools items AND they can be customized WHEN the screen is shown THEN show the saved ones`() =
        runTest {
            createStore(
                customization = MutableStateFlow(defaultMenuInfo),
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to savedToolsInfo())),
                isToolsMenuCustomizationEnabled = true,
            )
            runCurrent()

            captureActions.assertLastAction(Update::class) {
                assertEquals(
                    listOf(Text.String(FindInPage.id), Text.String(DesktopSite.id)),
                    it.items.first { group -> group.id == TOOLS_GROUP_ID }.items.map { item -> item.title.text },
                )
            }
        }

    @Test
    fun `GIVEN the user saved the tools items with the more items reordered AND they can be customized WHEN the screen is shown THEN show the more items in the saved order`() =
        runTest {
            createStore(
                customization = MutableStateFlow(defaultMenuInfo),
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to savedToolsInfoWithMore())),
                isToolsMenuCustomizationEnabled = true,
            )
            runCurrent()

            captureActions.assertLastAction(Update::class) {
                val toolsItems = it.toolsItems()
                assertEquals(
                    listOf(FindInPage.id, moreId, DesktopSite.id).map { id -> Text.String(id) },
                    toolsItems.map { item -> item.title.text },
                )
                assertEquals(
                    listOf(Print.id, Translate.id).map { id -> Text.String(id) },
                    (toolsItems[1] as ExpandableMenuItem).subMenuItems.map { item -> item.title.text },
                )
            }
        }

    @Test
    fun `GIVEN the user saved the tools items with the more items reordered AND they cannot be customized WHEN the screen is shown THEN show the default more items`() =
        runTest {
            createStore(
                customization = MutableStateFlow(defaultMenuInfo),
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to savedToolsInfoWithMore())),
                isToolsMenuCustomizationEnabled = false,
            )
            runCurrent()

            captureActions.assertLastAction(Update::class) { assertEquals(defaultToolsTitles(), it.toolsTitles()) }
        }

    @Test
    fun `GIVEN the user saved the tools items WHEN the user resets to default THEN show the default tools items`() =
        runTest {
            val store =
                createStore(
                    initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true),
                    sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to savedToolsInfoWithMore())),
                    isToolsMenuCustomizationEnabled = true,
                )

            store.dispatch(ResetToDefaultConfirmed)
            runCurrent()

            captureActions.assertLastAction(Update::class) { assertEquals(defaultToolsTitles(), it.toolsTitles()) }
        }

    @Test
    fun `GIVEN the user changed the tools items but they are not shown WHEN the screen is shown THEN show the menu as not customized`() =
        runTest {
            createStore(
                customization = MutableStateFlow(defaultMenuInfo),
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to changedToolsInfo())),
                isToolsMenuCustomizationEnabled = false,
            )
            runCurrent()

            captureActions.assertLastAction(Update::class) { assertFalse(it.isCustomized) }
        }

    @Test
    fun `GIVEN the user moved the tools items back as by default WHEN the screen is shown THEN show the menu as not customized`() =
        runTest {
            val defaultToolsInfo = BrowserMenuConfiguration.defaultSectionsInfo().getValue(TOOLS_GROUP_ID)
            createStore(
                customization = MutableStateFlow(defaultMenuInfo),
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to defaultToolsInfo)),
                isToolsMenuCustomizationEnabled = true,
            )
            runCurrent()

            captureActions.assertLastAction(Update::class) { assertFalse(it.isCustomized) }
        }

    @Test
    fun `GIVEN the user changed the tools items WHEN the user resets to default THEN show the menu as not customized`() =
        runTest {
            val store =
                createStore(
                    initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true),
                    sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to changedToolsInfo())),
                    isToolsMenuCustomizationEnabled = true,
                )

            store.dispatch(ResetToDefaultConfirmed)
            runCurrent()

            verify { repository.reset() }
            captureActions.assertLastAction(Update::class) { assertFalse(it.isCustomized) }
        }

    @Test
    fun `GIVEN the user changed the tools items and reset to default WHEN the user restores the default structure THEN show the menu as not customized`() =
        runTest {
            val store =
                createStore(
                    initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true),
                    sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to changedToolsInfo())),
                    isToolsMenuCustomizationEnabled = true,
                )
            store.dispatch(ResetToDefaultConfirmed)
            runCurrent()

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
    fun `GIVEN the user moved the navigation section WHEN the user resets to default and changes the menu THEN keep the navigation section following the toolbar`() =
        runTest {
            val store =
                createStore(
                    customization = MutableStateFlow(defaultMenuInfo.copy(isNavigationSectionMoved = true)),
                    initialState = CustomizableMenuState(menuGroups = defaultGroups(), isCustomized = true),
                )
            store.dispatch(ResetToDefaultConfirmed)
            runCurrent()

            store.dispatch(
                MenuStructureChanged(
                    items = groups(*BrowserMenuConfiguration.allSections().map { it.id }.toTypedArray()),
                    hiddenGroupIds = defaultMenuInfo.hiddenSectionIds.toSet(),
                    movedGroupId = TABS_CONTROLS_GROUP_ID,
                )
            )

            verify { repository.save(match<CustomizedMenuInfo> { !it.isNavigationSectionMoved }) }
        }

    @Test
    fun `GIVEN the menu sections were customized WHEN the screen is shown THEN turn off the customize menu banner`() =
        runTest {
            createStore(customization = MutableStateFlow(defaultMenuInfo.copy(isNavigationSectionMoved = true)))
            runCurrent()

            verify { settings.shouldShowCustomizeMenuBanner = false }
        }

    @Test
    fun `GIVEN the customize menu banner is disabled in this build WHEN the menu is customized THEN leave its settings untouched`() =
        runTest {
            val store =
                createStore(
                    customization = MutableStateFlow(defaultMenuInfo.copy(isNavigationSectionMoved = true)),
                    sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to changedToolsInfo())),
                    isToolsMenuCustomizationEnabled = true,
                    isCustomizeMenuBannerEnabled = false,
                )
            runCurrent()

            store.dispatch(
                MenuStructureChanged(
                    items = groups(*defaultMenuInfo.visibleSectionIds.reversed().toTypedArray()),
                    hiddenGroupIds = emptySet(),
                    movedGroupId = PAGE_CONTROLS_GROUP_ID,
                )
            )

            verify(exactly = 0) { settings.shouldShowCustomizeMenuBanner = any() }
        }

    @Test
    fun `GIVEN the menu was not customized WHEN the screen is shown THEN keep the customize menu banner`() = runTest {
        createStore(
            customization = MutableStateFlow(defaultMenuInfo),
            sectionsCustomization = MutableStateFlow(BrowserMenuConfiguration.defaultSectionsInfo()),
            isToolsMenuCustomizationEnabled = true,
        )
        runCurrent()

        verify(exactly = 0) { settings.shouldShowCustomizeMenuBanner = any() }
    }

    @Test
    fun `GIVEN only the tools items were customized WHEN the screen is shown THEN turn off the customize menu banner`() =
        runTest {
            createStore(
                customization = MutableStateFlow(defaultMenuInfo),
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to changedToolsInfo())),
                isToolsMenuCustomizationEnabled = true,
            )
            runCurrent()

            verify { settings.shouldShowCustomizeMenuBanner = false }
        }

    @Test
    fun `GIVEN the tools items were customized but cannot be anymore WHEN the screen is shown THEN keep the customize menu banner`() =
        runTest {
            createStore(
                customization = MutableStateFlow(defaultMenuInfo),
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to changedToolsInfo())),
                isToolsMenuCustomizationEnabled = false,
            )
            runCurrent()

            verify(exactly = 0) { settings.shouldShowCustomizeMenuBanner = any() }
        }

    @Test
    fun `WHEN the user changes the menu structure from its default THEN turn off the customize menu banner`() =
        runTest {
            val store = createStore(customization = MutableStateFlow(defaultMenuInfo))
            runCurrent()

            store.dispatch(
                MenuStructureChanged(
                    items = groups(*defaultMenuInfo.visibleSectionIds.reversed().toTypedArray()),
                    hiddenGroupIds = emptySet(),
                    movedGroupId = PAGE_CONTROLS_GROUP_ID,
                )
            )

            verify { settings.shouldShowCustomizeMenuBanner = false }
        }

    @Test
    fun `WHEN the user saves the default menu structure THEN keep the customize menu banner`() = runTest {
        val store = createStore(customization = MutableStateFlow(defaultMenuInfo))
        runCurrent()

        store.dispatch(
            MenuStructureChanged(
                items = defaultGroups() + groups(*defaultMenuInfo.hiddenSectionIds.toTypedArray()),
                hiddenGroupIds = defaultMenuInfo.hiddenSectionIds.toSet(),
                movedGroupId = VPN_GROUP_ID,
            )
        )

        verify(exactly = 0) { settings.shouldShowCustomizeMenuBanner = any() }
    }

    // The customization is read on a scope that runTest cancels at the end of each test.
    private fun TestScope.createStore(
        initialState: CustomizableMenuState = CustomizableMenuState(menuGroups = emptyList()),
        customization: MutableStateFlow<CustomizedMenuInfo?> = MutableStateFlow(null),
        sectionsCustomization: MutableStateFlow<Map<String, CustomizedSectionInfo>?> = MutableStateFlow(emptyMap()),
        sectionsWithNothingToShow: Set<String> = emptySet(),
        isToolbarAtBottom: Boolean = false,
        isToolsMenuCustomizationEnabled: Boolean = false,
        isCustomizeMenuBannerEnabled: Boolean = true,
    ): CustomizableMenuStore {
        every { repository.customizedMenuInfo } returns customization
        every { repository.customizedSectionsInfo } returns sectionsCustomization
        every { repository.save(any<CustomizedMenuInfo>()) } answers { customization.value = firstArg() }
        every { repository.reset() } answers
            {
                customization.value = defaultMenuInfo
                sectionsCustomization.value = emptyMap()
            }
        val unavailableItemIds =
            BrowserMenuConfiguration.allSections()
                .filter { it.id in sectionsWithNothingToShow }
                .flatMap { it.items }
                .map { it.id }
                .toSet()
        val registry =
            MenuItemsRegistry(
                configuration = BrowserMenuConfiguration.allSections(),
                resolver = { item ->
                    val isOffered = item.id !in unavailableItemIds
                    val offered =
                        if (isOffered) listOf(if (item is More) moreItem() else itemOf(item.id)) else emptyList()
                    if (item is More) FakeExpandableMenuItemProvider(offered) else FakeMenuItemProvider(offered)
                },
            )

        return CustomizableMenuStore(
            initialState = initialState,
            middleware =
                listOf(
                    captureActions,
                    CustomizeMenuMiddleware(
                        repository = repository,
                        registry = registry,
                        isToolbarAtBottom = isToolbarAtBottom,
                        isExpandedToolbarEnabled = false,
                        isToolsMenuCustomizationEnabled = isToolsMenuCustomizationEnabled,
                        scope = backgroundScope,
                        appStore = appStore,
                        cannotHideSectionMessage = cannotHideSectionMessage,
                        settings = settings,
                        isCustomizeMenuBannerEnabled = isCustomizeMenuBannerEnabled,
                    ),
                ),
        )
    }

    private fun savedToolsInfo() =
        CustomizedSectionInfo(
            sectionId = TOOLS_GROUP_ID,
            items = listOf(CustomizedItemInfo(FindInPage.id), CustomizedItemInfo(DesktopSite.id)),
            hiddenItems = listOf(CustomizedItemInfo(Extensions.id)),
        )

    private fun savedToolsInfoWithMore() =
        CustomizedSectionInfo(
            sectionId = TOOLS_GROUP_ID,
            items =
                listOf(
                    CustomizedItemInfo(FindInPage.id),
                    CustomizedItemInfo(moreId, subItemIds = listOf(Print.id, Translate.id)),
                    CustomizedItemInfo(DesktopSite.id),
                ),
            hiddenItems = listOf(CustomizedItemInfo(Extensions.id)),
        )

    private fun Update.toolsItems() = items.first { group -> group.id == TOOLS_GROUP_ID }.items

    private fun Update.toolsTitles() =
        toolsItems().map { item ->
            listOf(item.title.text) + (item as? ExpandableMenuItem)?.subMenuItems?.map { it.title.text }.orEmpty()
        }

    private fun defaultToolsTitles() =
        BrowserMenuConfiguration.defaultSectionsInfo().getValue(TOOLS_GROUP_ID).items.map { item ->
            listOf(Text.String(item.id)) + item.subItemIds.map { id -> Text.String(id) }
        }

    private fun changedToolsInfo(): CustomizedSectionInfo {
        val defaultToolsInfo = BrowserMenuConfiguration.defaultSectionsInfo().getValue(TOOLS_GROUP_ID)
        return defaultToolsInfo.copy(items = defaultToolsInfo.items.reversed())
    }

    private fun itemOf(id: String): MenuItem = StandardMenuItem(title = MenuItemTitle(Text.String(id)))

    private fun moreItem(): MenuItem =
        ExpandableMenuItem(
            title = MenuItemTitle(Text.String(moreId)),
            onClickEvent = object : MenuEvent {},
            subMenuItems = emptyList(),
        )

    private fun defaultGroups() = groups(*defaultMenuInfo.visibleSectionIds.toTypedArray())

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }

    private class FakeExpandableMenuItemProvider(items: List<MenuItem>) : ExpandableMenuItemProvider {
        override val itemsFlow = MutableStateFlow(items)

        override fun handles(event: MenuEvent) = false

        override fun onEvent(event: MenuEvent, menu: MenuHost) = Unit
    }

    private class FakeMenuItemProvider(items: List<MenuItem>) : MenuItemProvider {
        override val itemsFlow = MutableStateFlow(items)

        override fun handles(event: MenuEvent) = false

        override fun onEvent(event: MenuEvent, menu: MenuHost) = Unit
    }
}
