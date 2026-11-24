/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import io.mockk.every
import io.mockk.mockk
import io.mockk.verify
import kotlin.test.assertEquals
import kotlin.test.assertNull
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.support.test.middleware.CaptureActionsMiddleware
import org.junit.Test
import org.mozilla.fenix.components.menu.CustomizedItemInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
import org.mozilla.fenix.components.menu.MenuCustomizationRepository
import org.mozilla.fenix.components.menu.MenuHost
import org.mozilla.fenix.components.menu.MenuItemProvider
import org.mozilla.fenix.components.menu.MenuItemsRegistry
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Print

@OptIn(ExperimentalCoroutinesApi::class)
class EditMenuItemsMiddlewareTest {
    private val repository: MenuCustomizationRepository = mockk(relaxed = true)
    private val captureActions = CaptureActionsMiddleware<CustomizableMenuItemsState, CustomizableMenuItemsAction>()

    @Test
    fun `GIVEN the items were customized WHEN the screen is shown THEN show the shown and the hidden items as saved`() =
        runTest {
            val saved =
                CustomizedSectionInfo(
                    sectionId = TOOLS_GROUP_ID,
                    items =
                        listOf(
                            CustomizedItemInfo(FindInPage.id),
                            CustomizedItemInfo(More(emptyList()).id, subItemIds = listOf(Print.id)),
                        ),
                    hiddenItems = listOf(CustomizedItemInfo(DesktopSite.id)),
                )
            createStore(sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to saved)))
            runCurrent()

            captureActions.assertLastAction(Update::class) {
                assertEquals(
                    listOf(
                        CustomizableMenuItem(FindInPage.id, itemOf(FindInPage.id)),
                        CustomizableMenuItem(
                            More(emptyList()).id,
                            itemOf(More(emptyList()).id),
                            subItems = listOf(CustomizableMenuItem(Print.id, itemOf(Print.id))),
                        ),
                    ),
                    it.items,
                )
                assertEquals(listOf(CustomizableMenuItem(DesktopSite.id, itemOf(DesktopSite.id))), it.hiddenItems)
            }
        }

    @Test
    fun `GIVEN the items were not customized WHEN the screen is shown THEN show the default ones`() = runTest {
        createStore(sectionsCustomization = MutableStateFlow(emptyMap()))
        runCurrent()

        val default = BrowserMenuConfiguration.defaultSectionsInfo().getValue(TOOLS_GROUP_ID)
        captureActions.assertLastAction(Update::class) {
            assertEquals(default.items.map { item -> item.id }, it.items.map { item -> item.id })
            assertEquals(
                default.items.flatMap { item -> item.subItemIds },
                it.items.flatMap { item -> item.subItems }.map { item -> item.id },
            )
            assertEquals(emptyList(), it.hiddenItems)
        }
    }

    @Test
    fun `GIVEN the customization was not read yet WHEN the screen is shown THEN show the items only after reading it`() =
        runTest {
            val sectionsCustomization = MutableStateFlow<Map<String, CustomizedSectionInfo>?>(null)
            createStore(sectionsCustomization = sectionsCustomization)
            runCurrent()

            captureActions.assertNotDispatched(Update::class)

            sectionsCustomization.value = emptyMap()
            runCurrent()

            captureActions.assertLastAction(Update::class)
        }

    @Test
    fun `GIVEN an item the app has no provider for or whose provider offers nothing WHEN the screen is shown THEN keep its place without how it looks`() =
        runTest {
            val saved =
                CustomizedSectionInfo(
                    sectionId = TOOLS_GROUP_ID,
                    items = listOf(CustomizedItemInfo(FindInPage.id), CustomizedItemInfo("unknown")),
                    hiddenItems = listOf(CustomizedItemInfo(DesktopSite.id)),
                )
            createStore(
                sectionsCustomization = MutableStateFlow(mapOf(TOOLS_GROUP_ID to saved)),
                providedItemIds = setOf(FindInPage.id),
            )
            runCurrent()

            captureActions.assertLastAction(Update::class) {
                assertEquals(listOf(FindInPage.id, "unknown"), it.items.map { item -> item.id })
                assertNull(it.items[1].item)
                assertNull(it.hiddenItems.single().item)
            }
        }

    @Test
    fun `WHEN the user moves an item THEN save the shown and the hidden items with their sub-items`() = runTest {
        val store = createStore(sectionsCustomization = MutableStateFlow(emptyMap()))
        runCurrent()
        val more = More(emptyList()).id

        store.dispatch(
            ItemsChanged(
                items =
                    listOf(
                        CustomizableMenuItem(FindInPage.id, itemOf(FindInPage.id)),
                        CustomizableMenuItem(
                            more,
                            itemOf(more),
                            subItems = listOf(CustomizableMenuItem(Print.id, itemOf(Print.id))),
                        ),
                    ),
                hiddenItems = listOf(CustomizableMenuItem(DesktopSite.id, itemOf(DesktopSite.id))),
            )
        )

        verify {
            repository.save(
                CustomizedSectionInfo(
                    sectionId = TOOLS_GROUP_ID,
                    items =
                        listOf(
                            CustomizedItemInfo(FindInPage.id),
                            CustomizedItemInfo(more, subItemIds = listOf(Print.id)),
                        ),
                    hiddenItems = listOf(CustomizedItemInfo(DesktopSite.id)),
                )
            )
        }
    }

    private fun itemOf(id: String): MenuItem = StandardMenuItem(title = MenuItemTitle(Text.String(id)))

    // The customization is read on a scope that runTest cancels at the end of each test.
    private fun TestScope.createStore(
        sectionsCustomization: MutableStateFlow<Map<String, CustomizedSectionInfo>?>,
        providedItemIds: Set<String>? = null,
    ): CustomizableMenuItemsStore {
        every { repository.customizedSectionsInfo } returns sectionsCustomization
        val registry =
            MenuItemsRegistry(
                configuration = BrowserMenuConfiguration.allSections().filter { it.id == TOOLS_GROUP_ID },
                resolver = { item ->
                    val isProvided = providedItemIds == null || item.id in providedItemIds
                    FakeMenuItemProvider(if (isProvided) listOf(itemOf(item.id)) else emptyList())
                },
            )

        return CustomizableMenuItemsStore(
            middleware =
                listOf(
                    captureActions,
                    EditMenuItemsMiddleware(
                        repository = repository,
                        registry = registry,
                        sectionId = TOOLS_GROUP_ID,
                        scope = backgroundScope,
                    ),
                )
        )
    }

    private class FakeMenuItemProvider(items: List<MenuItem>) : MenuItemProvider {
        override val itemsFlow = MutableStateFlow(items)

        override fun handles(event: MenuEvent) = false

        override fun onEvent(event: MenuEvent, menu: MenuHost) = Unit
    }
}
