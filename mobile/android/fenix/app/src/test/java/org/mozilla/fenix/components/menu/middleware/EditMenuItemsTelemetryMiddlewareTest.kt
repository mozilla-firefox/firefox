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
    }

    @Test
    fun `WHEN the user tries to hide the last shown item THEN record the refusal`() {
        val store = createStore()
        assertNull(CustomizeMenu.editItemsMoveRefused.testGetValue())

        store.dispatch(AttemptedEmptySection(DesktopSite.id))

        assertMoveRefusedRecorded(section = TOOLS_GROUP_ID, item = DesktopSite.id, reason = "last_shown_item")
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
}
