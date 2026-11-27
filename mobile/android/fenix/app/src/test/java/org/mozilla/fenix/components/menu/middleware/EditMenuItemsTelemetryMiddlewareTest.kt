/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.middleware

import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedEmptySection
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.AttemptedMoveAsSubItem
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.BackPressed
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.ItemsChanged
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.PreviewClicked
import mozilla.components.compose.menu.store.CustomizableMenuItemsAction.Update
import mozilla.components.compose.menu.store.CustomizableMenuItemsState
import mozilla.components.compose.menu.store.CustomizableMenuItemsStore
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.support.test.robolectric.testContext
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.CustomizeMenu
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.LIBRARY_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.MoveToNormalTabs
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Print
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.SaveAsPdf
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Translate
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class EditMenuItemsTelemetryMiddlewareTest {
    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val moreId = More(emptyList()).id

    @Test
    fun `WHEN the items are read for the first time THEN record the shown, hidden and More items from top to bottom`() {
        val store = createStore()
        assertNull(CustomizeMenu.editItemsShown.testGetValue())

        store.dispatch(
            Update(
                items =
                    listOf(
                        item(FindInPage.id),
                        expandable(moreId, item(Translate.id), item(Print.id)),
                        item(Extensions.id),
                    ),
                hiddenItems = listOf(item(DesktopSite.id), item(SaveAsPdf.id)),
            )
        )

        assertShownRecorded(
            section = TOOLS_GROUP_ID,
            shownItems = listOf(FindInPage.id, moreId, Translate.id, Print.id, Extensions.id),
            hiddenItems = listOf(DesktopSite.id, SaveAsPdf.id),
            moreItems = listOf(Translate.id, Print.id),
        )
    }

    @Test
    fun `GIVEN the screen is recreated with shown items WHEN the items are read again THEN do not record shown again`() {
        val store = createStore(initialState = CustomizableMenuItemsState(items = listOf(item(FindInPage.id))))

        store.dispatch(Update(items = listOf(item(FindInPage.id)), hiddenItems = emptyList()))

        assertNull(CustomizeMenu.editItemsShown.testGetValue())
    }

    @Test
    fun `GIVEN the screen is recreated with only hidden items WHEN the items are read again THEN do not record shown again`() {
        val store = createStore(initialState = CustomizableMenuItemsState(hiddenItems = listOf(item(DesktopSite.id))))

        store.dispatch(Update(items = emptyList(), hiddenItems = listOf(item(DesktopSite.id))))

        assertNull(CustomizeMenu.editItemsShown.testGetValue())
    }

    @Test
    fun `WHEN items the app does not show are present THEN leave them out of all lists`() {
        val store = createStore()

        store.dispatch(
            Update(
                items =
                    listOf(
                        CustomizableMenuItem(MoveToNormalTabs.id, item = null),
                        item(FindInPage.id),
                        expandable(
                            moreId,
                            CustomizableMenuItem(Translate.id, item = null),
                            item(Print.id),
                        ),
                    ),
                hiddenItems = listOf(CustomizableMenuItem(SaveAsPdf.id, item = null), item(DesktopSite.id)),
            )
        )

        assertShownRecorded(
            section = TOOLS_GROUP_ID,
            shownItems = listOf(FindInPage.id, moreId, Print.id),
            hiddenItems = listOf(DesktopSite.id),
            moreItems = listOf(Print.id),
        )
    }

    @Test
    fun `WHEN an expandable item itself is not shown THEN leave it and its sub-items out of shown and More items`() {
        val store = createStore()

        store.dispatch(
            Update(
                items =
                    listOf(
                        item(FindInPage.id),
                        CustomizableMenuItem(moreId, item = null, subItems = listOf(item(Translate.id))),
                    ),
                hiddenItems = emptyList(),
            )
        )

        assertShownRecorded(
            section = TOOLS_GROUP_ID,
            shownItems = listOf(FindInPage.id),
            hiddenItems = emptyList(),
            moreItems = emptyList(),
        )
    }

    @Test
    fun `WHEN More is hidden THEN list More in hidden items without its sub-items and leave More items empty`() {
        val store = createStore()

        store.dispatch(
            Update(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(expandable(moreId, item(Translate.id), item(Print.id))),
            )
        )

        assertShownRecorded(
            section = TOOLS_GROUP_ID,
            shownItems = listOf(FindInPage.id),
            hiddenItems = listOf(moreId),
            moreItems = emptyList(),
        )
    }

    @Test
    fun `WHEN a sub-item is hidden on its own THEN list it in hidden items and not in More items`() {
        val store = createStore()

        store.dispatch(
            Update(
                items = listOf(item(FindInPage.id), expandable(moreId, item(Print.id))),
                hiddenItems = listOf(item(Translate.id)),
            )
        )

        assertShownRecorded(
            section = TOOLS_GROUP_ID,
            shownItems = listOf(FindInPage.id, moreId, Print.id),
            hiddenItems = listOf(Translate.id),
            moreItems = listOf(Print.id),
        )
    }

    @Test
    fun `WHEN an item was moved out of More to the top level THEN list it in shown items and not in More items`() {
        val store = createStore()

        store.dispatch(
            Update(
                items = listOf(item(FindInPage.id), item(Translate.id), expandable(moreId, item(Print.id))),
                hiddenItems = emptyList(),
            )
        )

        assertShownRecorded(
            section = TOOLS_GROUP_ID,
            shownItems = listOf(FindInPage.id, Translate.id, moreId, Print.id),
            hiddenItems = emptyList(),
            moreItems = listOf(Print.id),
        )
    }

    @Test
    fun `WHEN created for another section THEN record that section id`() {
        val store = createStore(sectionId = LIBRARY_GROUP_ID)

        store.dispatch(Update(items = listOf(item(FindInPage.id)), hiddenItems = emptyList()))

        assertShownRecorded(
            section = LIBRARY_GROUP_ID,
            shownItems = listOf(FindInPage.id),
            hiddenItems = emptyList(),
            moreItems = emptyList(),
        )
    }

    @Test
    fun `WHEN the user taps preview THEN record it with the section id`() {
        val store = createStore()
        assertNull(CustomizeMenu.editItemsPreviewClicked.testGetValue())

        store.dispatch(PreviewClicked)

        val events = CustomizeMenu.editItemsPreviewClicked.testGetValue()
        assertNotNull(events)
        assertEquals(TOOLS_GROUP_ID, events.single().extra?.getValue("section"))
    }

    @Test
    fun `WHEN the user presses back THEN record it with the section id`() {
        val store = createStore()
        assertNull(CustomizeMenu.editItemsBackPressed.testGetValue())

        store.dispatch(BackPressed)

        val events = CustomizeMenu.editItemsBackPressed.testGetValue()
        assertNotNull(events)
        assertEquals(TOOLS_GROUP_ID, events.single().extra?.getValue("section"))
    }

    @Test
    fun `WHEN the user tries to move an item inside another item it cannot be in THEN record the refusal`() {
        val store = createStore()
        assertNull(CustomizeMenu.editItemsMoveRefused.testGetValue())

        store.dispatch(AttemptedMoveAsSubItem(Extensions.id))

        assertMoveRefusedRecorded(section = TOOLS_GROUP_ID, item = Extensions.id, reason = "inside_item")
        assertNull(CustomizeMenu.itemReordered.testGetValue())
        assertNull(CustomizeMenu.itemHidden.testGetValue())
        assertNull(CustomizeMenu.itemUnhidden.testGetValue())
    }

    @Test
    fun `WHEN the user tries to hide the last shown item THEN record the refusal`() {
        val store = createStore()
        assertNull(CustomizeMenu.editItemsMoveRefused.testGetValue())

        store.dispatch(AttemptedEmptySection(DesktopSite.id))

        assertMoveRefusedRecorded(section = TOOLS_GROUP_ID, item = DesktopSite.id, reason = "last_shown_item")
        assertNull(CustomizeMenu.itemReordered.testGetValue())
        assertNull(CustomizeMenu.itemHidden.testGetValue())
        assertNull(CustomizeMenu.itemUnhidden.testGetValue())
    }

    @Test
    fun `WHEN the user reorders a shown item within the section THEN record that it was reordered`() {
        val store =
            createStore(
                initialState = CustomizableMenuItemsState(items = listOf(item(FindInPage.id), item(DesktopSite.id)))
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(DesktopSite.id), item(FindInPage.id)),
                hiddenItems = emptyList(),
                movedItemId = DesktopSite.id,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = DesktopSite.id,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = TOOLS_GROUP_ID,
            position = 1,
            hidden = false,
        )
    }

    @Test
    fun `WHEN the user moves a shown item into More THEN record that it was reordered into More with its position among the shown items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items =
                            listOf(
                                item(FindInPage.id),
                                item(DesktopSite.id),
                                expandable(moreId, item(Translate.id), item(Print.id)),
                            )
                    )
            )

        store.dispatch(
            ItemsChanged(
                items =
                    listOf(
                        item(FindInPage.id),
                        expandable(moreId, item(Translate.id), item(DesktopSite.id), item(Print.id)),
                    ),
                hiddenItems = emptyList(),
                movedItemId = DesktopSite.id,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = DesktopSite.id,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = moreId,
            position = 4,
            hidden = false,
        )
    }

    @Test
    fun `WHEN the user moves a shown item out of More to the section THEN record that it was reordered out of More`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id), item(Print.id)))
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id), item(Translate.id), expandable(moreId, item(Print.id))),
                hiddenItems = emptyList(),
                movedItemId = Translate.id,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = Translate.id,
            fromContainer = moreId,
            toContainer = TOOLS_GROUP_ID,
            position = 2,
            hidden = false,
        )
    }

    @Test
    fun `WHEN the user reorders a shown sub-item within More THEN record that it was reordered counting More and preceding sub-items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id), item(Print.id)))
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id), expandable(moreId, item(Print.id), item(Translate.id))),
                hiddenItems = emptyList(),
                movedItemId = Translate.id,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = Translate.id,
            fromContainer = moreId,
            toContainer = moreId,
            position = 4,
            hidden = false,
        )
    }

    @Test
    fun `WHEN the user moves More among the shown items THEN record only More in the section`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id), item(Print.id)))
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(expandable(moreId, item(Translate.id), item(Print.id)), item(FindInPage.id)),
                hiddenItems = emptyList(),
                movedItemId = moreId,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = moreId,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = TOOLS_GROUP_ID,
            position = 1,
            hidden = false,
        )
    }

    @Test
    fun `WHEN the user reorders an item inside a hidden More THEN record that it was reordered among the hidden items with its index among More's items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id)),
                        hiddenItems =
                            listOf(item(DesktopSite.id), expandable(moreId, item(Translate.id), item(Print.id))),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(item(DesktopSite.id), expandable(moreId, item(Print.id), item(Translate.id))),
                movedItemId = Translate.id,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = Translate.id,
            fromContainer = moreId,
            toContainer = moreId,
            position = 2,
            hidden = true,
        )
    }

    @Test
    fun `WHEN the user moves an item between a hidden More and the hidden top level THEN record that it was reordered among the hidden items and ignore More's items in top-level position`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id)),
                        hiddenItems =
                            listOf(expandable(moreId, item(Translate.id), item(Print.id)), item(DesktopSite.id)),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(expandable(moreId, item(Print.id)), item(DesktopSite.id), item(Translate.id)),
                movedItemId = Translate.id,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = Translate.id,
            fromContainer = moreId,
            toContainer = TOOLS_GROUP_ID,
            position = 3,
            hidden = true,
        )
    }

    @Test
    fun `WHEN the user hides or unhides an item THEN do not record it as reordered`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), item(DesktopSite.id)),
                        hiddenItems = listOf(item(SaveAsPdf.id)),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(item(DesktopSite.id), item(SaveAsPdf.id)),
                movedItemId = DesktopSite.id,
            )
        )
        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id), item(SaveAsPdf.id)),
                hiddenItems = listOf(item(DesktopSite.id)),
                movedItemId = SaveAsPdf.id,
            )
        )

        assertNull(CustomizeMenu.itemReordered.testGetValue())
    }

    @Test
    fun `WHEN an item is moved twice in a row THEN the second record says the item came from where the first move put it`() {
        val store =
            createStore(
                sectionId = LIBRARY_GROUP_ID,
                initialState =
                    CustomizableMenuItemsState(items = listOf(item(FindInPage.id), expandable(moreId, item(Print.id)))),
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(expandable(moreId, item(FindInPage.id), item(Print.id))),
                hiddenItems = emptyList(),
                movedItemId = FindInPage.id,
            )
        )
        store.dispatch(
            ItemsChanged(
                items = listOf(expandable(moreId, item(Print.id)), item(FindInPage.id)),
                hiddenItems = emptyList(),
                movedItemId = FindInPage.id,
            )
        )

        val events = CustomizeMenu.itemReordered.testGetValue()
        assertNotNull(events)
        assertEquals(2, events.size)
        val second = events[1].extra
        assertEquals(LIBRARY_GROUP_ID, second?.getValue("section"))
        assertEquals(FindInPage.id, second?.getValue("item"))
        assertEquals(moreId, second?.getValue("from_container"))
        assertEquals(LIBRARY_GROUP_ID, second?.getValue("to_container"))
        assertEquals("3", second?.getValue("position"))
        assertEquals("false", second?.getValue("hidden"))
    }

    @Test
    fun `WHEN the moved item id is unknown THEN record nothing`() {
        val store = createStore(initialState = CustomizableMenuItemsState(items = listOf(item(FindInPage.id))))

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = emptyList(),
                movedItemId = DesktopSite.id,
            )
        )

        assertNull(CustomizeMenu.itemReordered.testGetValue())
    }

    @Test
    fun `WHEN the moved item or its parent is not shown by the app THEN record nothing`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items =
                            listOf(
                                item(FindInPage.id),
                                CustomizableMenuItem(MoveToNormalTabs.id, item = null),
                                CustomizableMenuItem(moreId, item = null, subItems = listOf(item(Translate.id))),
                            )
                    )
            )

        store.dispatch(
            ItemsChanged(
                items =
                    listOf(
                        CustomizableMenuItem(MoveToNormalTabs.id, item = null),
                        item(FindInPage.id),
                        CustomizableMenuItem(moreId, item = null, subItems = listOf(item(Translate.id))),
                    ),
                hiddenItems = emptyList(),
                movedItemId = MoveToNormalTabs.id,
            )
        )
        store.dispatch(
            ItemsChanged(
                items =
                    listOf(
                        CustomizableMenuItem(MoveToNormalTabs.id, item = null),
                        item(FindInPage.id),
                        CustomizableMenuItem(moreId, item = null, subItems = listOf(item(Translate.id))),
                    ),
                hiddenItems = emptyList(),
                movedItemId = Translate.id,
            )
        )

        assertNull(CustomizeMenu.itemReordered.testGetValue())
    }

    @Test
    fun `WHEN items the app does not show are among siblings THEN position ignores them`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items =
                            listOf(
                                CustomizableMenuItem(MoveToNormalTabs.id, item = null),
                                item(Print.id),
                                item(FindInPage.id),
                                CustomizableMenuItem(SaveAsPdf.id, item = null),
                            )
                    )
            )

        store.dispatch(
            ItemsChanged(
                items =
                    listOf(
                        CustomizableMenuItem(MoveToNormalTabs.id, item = null),
                        item(FindInPage.id),
                        CustomizableMenuItem(SaveAsPdf.id, item = null),
                        item(Print.id),
                    ),
                hiddenItems = emptyList(),
                movedItemId = Print.id,
            )
        )

        assertItemReorderedRecorded(
            section = TOOLS_GROUP_ID,
            item = Print.id,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = TOOLS_GROUP_ID,
            position = 2,
            hidden = false,
        )
    }

    @Test
    fun `WHEN the user hides a shown item from the section THEN record that it was hidden with its position among the hidden items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), item(DesktopSite.id)),
                        hiddenItems = listOf(item(SaveAsPdf.id)),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(item(SaveAsPdf.id), item(DesktopSite.id)),
                movedItemId = DesktopSite.id,
            )
        )

        assertItemHiddenRecorded(
            section = TOOLS_GROUP_ID,
            item = DesktopSite.id,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = TOOLS_GROUP_ID,
            position = 2,
        )
    }

    @Test
    fun `WHEN the user hides a shown item from More THEN record that it was hidden from More`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id), item(Print.id))),
                        hiddenItems = listOf(item(SaveAsPdf.id)),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id), expandable(moreId, item(Print.id))),
                hiddenItems = listOf(item(Translate.id), item(SaveAsPdf.id)),
                movedItemId = Translate.id,
            )
        )

        assertItemHiddenRecorded(
            section = TOOLS_GROUP_ID,
            item = Translate.id,
            fromContainer = moreId,
            toContainer = TOOLS_GROUP_ID,
            position = 1,
        )
    }

    @Test
    fun `WHEN the user hides More THEN record only More with its position among hidden items without counting sub-items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id), item(Print.id))),
                        hiddenItems = listOf(item(DesktopSite.id)),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(item(DesktopSite.id), expandable(moreId, item(Translate.id), item(Print.id))),
                movedItemId = moreId,
            )
        )

        assertItemHiddenRecorded(
            section = TOOLS_GROUP_ID,
            item = moreId,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = TOOLS_GROUP_ID,
            position = 2,
        )
    }

    @Test
    fun `WHEN the user hides a shown item into an expanded hidden More THEN record that it was hidden into More with its index among More's items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), item(DesktopSite.id)),
                        hiddenItems = listOf(item(SaveAsPdf.id), expandable(moreId, item(Translate.id))),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(item(SaveAsPdf.id), expandable(moreId, item(Translate.id), item(DesktopSite.id))),
                movedItemId = DesktopSite.id,
            )
        )

        assertItemHiddenRecorded(
            section = TOOLS_GROUP_ID,
            item = DesktopSite.id,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = moreId,
            position = 2,
        )
    }

    @Test
    fun `WHEN the user unhides a hidden item to the section THEN record that it was unhidden with its position among the shown items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id))),
                        hiddenItems = listOf(item(DesktopSite.id)),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id)), item(DesktopSite.id)),
                hiddenItems = emptyList(),
                movedItemId = DesktopSite.id,
            )
        )

        assertItemUnhiddenRecorded(
            section = TOOLS_GROUP_ID,
            item = DesktopSite.id,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = TOOLS_GROUP_ID,
            position = 4,
        )
    }

    @Test
    fun `WHEN the user unhides a hidden item into More THEN record that it was unhidden into More with its position among the shown items`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id), expandable(moreId, item(Print.id))),
                        hiddenItems = listOf(item(Translate.id)),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id), expandable(moreId, item(Translate.id), item(Print.id))),
                hiddenItems = emptyList(),
                movedItemId = Translate.id,
            )
        )

        assertItemUnhiddenRecorded(
            section = TOOLS_GROUP_ID,
            item = Translate.id,
            fromContainer = TOOLS_GROUP_ID,
            toContainer = moreId,
            position = 3,
        )
    }

    @Test
    fun `WHEN the user unhides an item from a hidden More THEN record that it was unhidden from More`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id)),
                        hiddenItems = listOf(expandable(moreId, item(Translate.id), item(Print.id))),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id), item(Translate.id)),
                hiddenItems = listOf(expandable(moreId, item(Print.id))),
                movedItemId = Translate.id,
            )
        )

        assertItemUnhiddenRecorded(
            section = TOOLS_GROUP_ID,
            item = Translate.id,
            fromContainer = moreId,
            toContainer = TOOLS_GROUP_ID,
            position = 2,
        )
    }

    @Test
    fun `WHEN the user reorders an item among the hidden items THEN do not record it as unhidden`() {
        val store =
            createStore(
                initialState =
                    CustomizableMenuItemsState(
                        items = listOf(item(FindInPage.id)),
                        hiddenItems = listOf(expandable(moreId, item(Translate.id), item(Print.id))),
                    )
            )

        store.dispatch(
            ItemsChanged(
                items = listOf(item(FindInPage.id)),
                hiddenItems = listOf(expandable(moreId, item(Print.id), item(Translate.id))),
                movedItemId = Translate.id,
            )
        )

        assertNull(CustomizeMenu.itemUnhidden.testGetValue())
    }

    private fun createStore(
        sectionId: String = TOOLS_GROUP_ID,
        initialState: CustomizableMenuItemsState = CustomizableMenuItemsState(),
    ) =
        CustomizableMenuItemsStore(
            initialState = initialState,
            middleware = listOf(EditMenuItemsTelemetryMiddleware(sectionId)),
        )

    private fun item(id: String) =
        CustomizableMenuItem(id = id, item = StandardMenuItem(title = MenuItemTitle(Text.String(id))))

    private fun expandable(id: String, vararg subItems: CustomizableMenuItem) =
        CustomizableMenuItem(
            id = id,
            item =
                ExpandableMenuItem(
                    title = MenuItemTitle(Text.String(id)),
                    onClickEvent = object : MenuEvent {},
                    subMenuItems = emptyList(),
                ),
            subItems = subItems.toList(),
        )

    private fun assertShownRecorded(
        section: String,
        shownItems: List<String>,
        hiddenItems: List<String>,
        moreItems: List<String>,
    ) {
        val events = CustomizeMenu.editItemsShown.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(shownItems.joinToString(","), extras?.getValue("shown_items"))
        assertEquals(hiddenItems.joinToString(","), extras?.getValue("hidden_items"))
        assertEquals(moreItems.joinToString(","), extras?.getValue("more_items"))
    }

    private fun assertMoveRefusedRecorded(section: String, item: String, reason: String) {
        val events = CustomizeMenu.editItemsMoveRefused.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(item, extras?.getValue("item"))
        assertEquals(reason, extras?.getValue("reason"))
    }

    private fun assertItemReorderedRecorded(
        section: String,
        item: String,
        fromContainer: String,
        toContainer: String,
        position: Int,
        hidden: Boolean,
    ) {
        val events = CustomizeMenu.itemReordered.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(item, extras?.getValue("item"))
        assertEquals(fromContainer, extras?.getValue("from_container"))
        assertEquals(toContainer, extras?.getValue("to_container"))
        assertEquals(position.toString(), extras?.getValue("position"))
        assertEquals(hidden.toString(), extras?.getValue("hidden"))
    }

    private fun assertItemHiddenRecorded(
        section: String,
        item: String,
        fromContainer: String,
        toContainer: String,
        position: Int,
    ) {
        val events = CustomizeMenu.itemHidden.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(item, extras?.getValue("item"))
        assertEquals(fromContainer, extras?.getValue("from_container"))
        assertEquals(toContainer, extras?.getValue("to_container"))
        assertEquals(position.toString(), extras?.getValue("position"))
    }

    private fun assertItemUnhiddenRecorded(
        section: String,
        item: String,
        fromContainer: String,
        toContainer: String,
        position: Int,
    ) {
        val events = CustomizeMenu.itemUnhidden.testGetValue()
        assertNotNull(events)

        val extras = events.single().extra
        assertEquals(section, extras?.getValue("section"))
        assertEquals(item, extras?.getValue("item"))
        assertEquals(fromContainer, extras?.getValue("from_container"))
        assertEquals(toContainer, extras?.getValue("to_container"))
        assertEquals(position.toString(), extras?.getValue("position"))
    }
}
