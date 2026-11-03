/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import org.junit.Test
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeReaderView
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row
import org.mozilla.fenix.components.menu.configuration.MenuSectionConfiguration

class MenuBuilderTest {
    @Test
    fun `WHEN a provider offers an item THEN show it in the section it is configured in`() = runTest {
        val builder = createSingleSectionMenuBuilder(Row, providing = readerViewItem)

        assertEquals(
            listOf(MenuItemsGroup.Row(id = MENU_GROUP_ID, items = listOf(readerViewItem))),
            builder.menuStructure.first(),
        )
    }

    @Test
    fun `GIVEN a section is configured as a list WHEN building the menu THEN show its items in a list`() = runTest {
        val builder = createSingleSectionMenuBuilder(Row, providing = readerViewItem)

        assertEquals(
            listOf(MenuItemsGroup.Row(id = MENU_GROUP_ID, items = listOf(readerViewItem))),
            builder.menuStructure.first(),
        )
    }

    @Test
    fun `GIVEN a section is configured as a grid WHEN building the menu THEN show its items in a grid`() = runTest {
        val builder = createSingleSectionMenuBuilder(Grid, providing = readerViewItem)

        assertEquals(
            listOf(MenuItemsGroup.Grid(id = MENU_GROUP_ID, items = listOf(readerViewItem))),
            builder.menuStructure.first(),
        )
    }

    @Test
    fun `GIVEN a provider offers no items WHEN building the menu THEN don't show the section it would be in`() =
        runTest {
            val builder = createSingleSectionMenuBuilder(Row, providing = null)

            assertTrue(builder.menuStructure.first().isEmpty())
        }

    @Test
    fun `WHEN an item changes THEN rebuild the menu with it`() = runTest {
        val provided = MutableStateFlow<List<MenuItem>>(emptyList())
        val builder =
            createMenuBuilder(
                resolver = { FakeMenuItemProvider(provided) },
                configuration = sectionOf(Row),
            )

        assertTrue(builder.menuStructure.first().isEmpty())

        provided.value = listOf(readerViewItem)

        assertEquals(listOf(readerViewItem), builder.menuStructure.first().single().items)
    }

    @Test
    fun `WHEN building the default menu THEN keep the sections in the configured order`() = runTest {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)
        val builder =
            createMenuBuilder(
                resolver = { FakeMenuItemProvider(MutableStateFlow(listOf(readerViewItem))) },
                configuration = configuration,
            )

        assertEquals(configuration.map { it.id }, builder.menuStructure.first().map { it.id })
    }

    @Test
    fun `GIVEN a section is configured as sticky WHEN building the menu THEN keep it sticky`() = runTest {
        val configuration =
            listOf(
                MenuSectionConfiguration(
                    id = MENU_GROUP_ID,
                    presentationMode = Row,
                    items = listOf(CustomizeReaderView),
                    isSticky = true,
                )
            )
        val builder =
            createMenuBuilder(
                resolver = { FakeMenuItemProvider(MutableStateFlow(listOf(readerViewItem))) },
                configuration = configuration,
            )

        assertEquals(true, builder.menuStructure.first().isNotEmpty())
        assertEquals(true, configuration.first().isSticky)
    }

    @Test
    fun `GIVEN an item expanding to others WHEN building the menu THEN show inside it what their providers offer`() =
        runTest {
            val builder = createExpandingMenuBuilder(expandingTo = MutableStateFlow(listOf(findInPageItem)))

            val shown = builder.menuStructure.first().single().items.single()

            assertEquals(moreItem.copy(subMenuItems = listOf(findInPageItem)), shown)
        }

    @Test
    fun `GIVEN nothing is offered for what it expands to WHEN building the menu THEN don't show the item`() = runTest {
        val builder = createExpandingMenuBuilder(expandingTo = MutableStateFlow(emptyList()))

        assertTrue(builder.menuStructure.first().isEmpty())
    }

    @Test
    fun `WHEN one of the items another expands to changes THEN rebuild the menu with it`() = runTest {
        val provided = MutableStateFlow<List<MenuItem>>(emptyList())
        val builder = createExpandingMenuBuilder(expandingTo = provided)

        assertTrue(builder.menuStructure.first().isEmpty())

        provided.value = listOf(findInPageItem)

        val shown = builder.menuStructure.first().single().items.single()
        assertEquals(moreItem.copy(subMenuItems = listOf(findInPageItem)), shown)
    }

    private fun createExpandingMenuBuilder(
        expandingTo: StateFlow<List<MenuItem>>,
        header: StateFlow<List<MenuItem>> = MutableStateFlow(listOf(moreItem)),
    ) =
        createMenuBuilder(
            resolver = { item ->
                when (item) {
                    is More -> FakeExpandableMenuItemProvider(header)
                    else -> FakeMenuItemProvider(expandingTo)
                }
            },
            configuration =
                listOf(
                    MenuSectionConfiguration(
                        id = MENU_GROUP_ID,
                        presentationMode = Row,
                        items = listOf(More(subMenuItems = listOf(FindInPage))),
                    )
                ),
        )

    private fun createSingleSectionMenuBuilder(
        presentationMode: MenuPresentationMode,
        providing: MenuItem?,
    ) =
        createMenuBuilder(
            resolver = { FakeMenuItemProvider(MutableStateFlow(listOfNotNull(providing))) },
            configuration = sectionOf(presentationMode),
        )

    private fun createMenuBuilder(
        resolver: (FenixMenuItem) -> MenuItemProvider,
        configuration: List<MenuSectionConfiguration>,
    ) = MenuBuilder(MenuItemsRegistry(configuration = configuration, resolver = resolver))

    private fun sectionOf(presentationMode: MenuPresentationMode) =
        listOf(
            MenuSectionConfiguration(
                id = MENU_GROUP_ID,
                presentationMode = presentationMode,
                items = listOf(CustomizeReaderView),
            )
        )

    @Test
    fun `WHEN assembling More THEN use the captured header even if the provider advances`() = runTest {
        var receivedChildren: List<StandardMenuItem>? = null
        val provider =
            object : ExpandableMenuItemProvider {
                override val itemsFlow = MutableStateFlow<List<MenuItem>>(listOf(moreItem))

                override fun handles(event: MenuEvent) = false

                override fun onEvent(event: MenuEvent, menu: MenuHost) = Unit

                override fun updateWithSubMenuItems(
                    item: ExpandableMenuItem,
                    subMenuItems: List<StandardMenuItem>,
                ): ExpandableMenuItem? {
                    receivedChildren = subMenuItems

                    itemsFlow.value = listOf(moreItem.copy(title = MenuItemTitle(Text.String("Newer header"))))
                    return super.updateWithSubMenuItems(item, subMenuItems)
                }
            }
        val builder =
            createMenuBuilder(
                resolver = { item ->
                    when (item) {
                        is More -> provider
                        FindInPage -> FakeMenuItemProvider(MutableStateFlow(listOf(findInPageItem)))
                        else -> FakeMenuItemProvider(MutableStateFlow(emptyList()))
                    }
                },
                configuration =
                    listOf(
                        MenuSectionConfiguration(
                            id = MENU_GROUP_ID,
                            presentationMode = Row,
                            items = listOf(More(listOf(FindInPage, CustomizeReaderView))),
                        )
                    ),
            )

        val shown = builder.menuStructure.first().single().items.single()

        assertEquals(listOf(findInPageItem), receivedChildren)
        assertEquals(moreItem.title, shown.title)
    }

    @Test
    fun `WHEN only the header changes THEN rebuild the expandable item`() = runTest {
        val header = MutableStateFlow<List<MenuItem>>(listOf(moreItem))
        val builder = createExpandingMenuBuilder(MutableStateFlow(listOf(findInPageItem)), header)
        assertEquals(moreItem.title, builder.menuStructure.first().single().items.single().title)

        header.value = listOf(moreItem.copy(title = MenuItemTitle(Text.String("Updated More"))))

        assertEquals(Text.String("Updated More"), builder.menuStructure.first().single().items.single().title.text)
    }

    @Test
    fun `GIVEN the expanding item itself is not offered WHEN building the menu THEN don't show it`() = runTest {
        val builder =
            createExpandingMenuBuilder(
                expandingTo = MutableStateFlow(listOf(findInPageItem)),
                header = MutableStateFlow(emptyList()),
            )

        assertTrue(builder.menuStructure.first().isEmpty())
    }

    @Test
    fun `GIVEN its provider cannot configure what it expands to WHEN building the menu THEN don't show it`() = runTest {
        val builder =
            createMenuBuilder(
                // A plain provider knows nothing about the items this one expands to, so it cannot be shown.
                resolver = { item ->
                    when (item) {
                        is More -> FakeMenuItemProvider(MutableStateFlow(listOf(moreItem)))
                        else -> FakeMenuItemProvider(MutableStateFlow(listOf(findInPageItem)))
                    }
                },
                configuration =
                    listOf(
                        MenuSectionConfiguration(
                            id = MENU_GROUP_ID,
                            presentationMode = Row,
                            items = listOf(More(subMenuItems = listOf(FindInPage))),
                        )
                    ),
            )

        assertTrue(builder.menuStructure.first().isEmpty())
    }

    @Test
    fun `WHEN the menu is rebuilt THEN keep using the providers built when the menu was first built`() = runTest {
        val header = MutableStateFlow<List<MenuItem>>(listOf(moreItem))
        val expandingTo = MutableStateFlow<List<MenuItem>>(listOf(findInPageItem))
        val resolved = mutableListOf<FenixMenuItem>()
        val builder =
            createMenuBuilder(
                resolver = { item ->
                    resolved += item
                    when (item) {
                        is More -> FakeExpandableMenuItemProvider(header)
                        else -> FakeMenuItemProvider(expandingTo)
                    }
                },
                configuration =
                    listOf(
                        MenuSectionConfiguration(
                            id = MENU_GROUP_ID,
                            presentationMode = Row,
                            items = listOf(More(subMenuItems = listOf(FindInPage)), FindInPage),
                        )
                    ),
            )

        builder.menuStructure.first()
        header.value = listOf(moreItem.copy(title = MenuItemTitle(Text.String("Updated More"))))
        builder.menuStructure.first()
        val updatedFindInPage = findInPageItem.copy(title = MenuItemTitle(Text.String("Updated Find in page")))
        expandingTo.value = listOf(updatedFindInPage)

        assertEquals(
            listOf(
                moreItem.copy(
                    title = MenuItemTitle(Text.String("Updated More")),
                    subMenuItems = listOf(updatedFindInPage),
                ),
                updatedFindInPage,
            ),
            builder.menuStructure.first().single().items,
        )
        assertEquals(listOf(More(subMenuItems = listOf(FindInPage)), FindInPage), resolved)
    }

    private class FakeMenuItemProvider(override val itemsFlow: StateFlow<List<MenuItem>>) : MenuItemProvider {
        override fun handles(event: MenuEvent) = false

        override fun onEvent(event: MenuEvent, menu: MenuHost) = Unit
    }

    /** Configures its item with whatever it expands to, which is all an expanding item needs by default. */
    private class FakeExpandableMenuItemProvider(override val itemsFlow: StateFlow<List<MenuItem>>) :
        ExpandableMenuItemProvider {
        override fun handles(event: MenuEvent) = false

        override fun onEvent(event: MenuEvent, menu: MenuHost) = Unit
    }

    private data object TestMenuEvent : MenuEvent

    private companion object {
        const val MENU_GROUP_ID = "section"

        val readerViewItem = StandardMenuItem(title = MenuItemTitle(Text.String("Item")), onClickEvent = TestMenuEvent)

        val findInPageItem =
            StandardMenuItem(title = MenuItemTitle(Text.String("Find in page")), onClickEvent = TestMenuEvent)

        val moreItem =
            ExpandableMenuItem(
                title = MenuItemTitle(Text.String("More")),
                onClickEvent = TestMenuEvent,
                subMenuItems = emptyList(),
            )
    }
}
