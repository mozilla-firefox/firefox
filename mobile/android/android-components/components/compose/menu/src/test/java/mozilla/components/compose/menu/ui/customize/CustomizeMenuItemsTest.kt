/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.CustomizableMenuItem
import mozilla.components.compose.menu.data.ExpandableMenuItem
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.StandardMenuItem
import mozilla.components.compose.menu.store.MenuEvent
import mozilla.components.compose.menu.ui.customize.CustomizeMenuDropTarget.NextTo
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_TITLE
import mozilla.components.compose.menu.ui.customize.EditMenuItemsRow.Item
import mozilla.components.compose.menu.ui.customize.EditMenuItemsRow.SubItemsEnd
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class CustomizeMenuItemsTest {
    private val a = item("a")
    private val b = item("b")
    private val c = item("c")
    private val d = item("d")

    @Test
    fun `GIVEN shown and hidden items THEN the rows of the shown ones are followed by the ones of the hidden ones`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c), expandedIds = emptySet())

        assertEquals(
            listOf(
                Item(a, parentId = null, isHidden = false, isExpanded = false),
                Item(b, parentId = null, isHidden = false, isExpanded = false),
                Item(c, parentId = null, isHidden = true, isExpanded = false),
            ),
            items.shownRows + items.hiddenRows,
        )
    }

    @Test
    fun `GIVEN an expanded item THEN its sub-items and the end of them follow its row`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(
            listOf(
                Item(a, parentId = null, isHidden = false, isExpanded = false),
                Item(more, parentId = null, isHidden = false, isExpanded = true),
                Item(c, parentId = "more", isHidden = false, isExpanded = false),
                Item(d, parentId = "more", isHidden = false, isExpanded = false),
                SubItemsEnd(parentId = "more", isHidden = false),
            ),
            items.shownRows + items.hiddenRows,
        )
    }

    @Test
    fun `GIVEN a collapsed item THEN only its row is listed`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(
            listOf(
                Item(a, parentId = null, isHidden = false, isExpanded = false),
                Item(more, parentId = null, isHidden = false, isExpanded = false),
            ),
            items.shownRows + items.hiddenRows,
        )
    }

    @Test
    fun `GIVEN items the app does not show THEN they are not listed`() {
        val hiddenFromApp = CustomizableMenuItem(id = "app hidden", item = null)
        val more = expandable("more", hiddenFromApp, c)
        val items =
            CustomizeMenuItems(
                shown = listOf(hiddenFromApp, a, more),
                hidden = listOf(CustomizableMenuItem(id = "other", item = null)),
                expandedIds = setOf("more"),
            )

        assertEquals(
            listOf("a", "more", "c", "more#sub_items_end"),
            (items.shownRows + items.hiddenRows).map { it.key },
        )
    }

    @Test
    fun `GIVEN hidden expandable items THEN they are listed as hidden, expanded or collapsed`() {
        val expanded = expandable("expanded", c)
        val collapsed = expandable("collapsed", d)
        val items =
            CustomizeMenuItems(shown = listOf(a), hidden = listOf(expanded, collapsed), expandedIds = setOf("expanded"))

        assertEquals(
            listOf(
                Item(a, parentId = null, isHidden = false, isExpanded = false),
                Item(expanded, parentId = null, isHidden = true, isExpanded = true),
                Item(c, parentId = "expanded", isHidden = true, isExpanded = false),
                SubItemsEnd(parentId = "expanded", isHidden = true),
                Item(collapsed, parentId = null, isHidden = true, isExpanded = false),
            ),
            items.shownRows + items.hiddenRows,
        )
    }

    @Test
    fun `THEN the keys of the rows are the ids of the items and the end of the sub-items of an item has its own`() {
        assertEquals("c", Item(c, parentId = "more", isHidden = false, isExpanded = false).key)
        assertEquals("more#sub_items_end", SubItemsEnd(parentId = "more", isHidden = true).key)
    }

    @Test
    fun `THEN the hidden items title is after the drag bar and the rows of the shown items`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(b), expandedIds = setOf("more"))

        assertEquals(1 + 5, items.hiddenTitleIndex)
        assertEquals(listOf<Any>("b"), items.hiddenKeys)
    }

    @Test
    fun `THEN only the rows of the shown items are shown keys`() {
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(b), expandedIds = emptySet())

        assertTrue(items.isShownKey("a"))
        assertFalse(items.isShownKey("b"))
        assertFalse(items.isShownKey(HIDDEN_TITLE))
    }

    @Test
    fun `THEN every item is kept from being hidden`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = emptyList(), expandedIds = emptySet())

        assertTrue(items.cannotBeHidden("a"))
    }

    @Test
    fun `WHEN a top level item is dropped before or after another one THEN it is placed there`() {
        val items = CustomizeMenuItems(shown = listOf(a, b, c), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(listOf(c, a, b), items.drop("c", NextTo("a", placeAfter = false)).shown)
        assertEquals(listOf(b, a, c), items.drop("a", NextTo("b", placeAfter = true)).shown)
    }

    @Test
    fun `WHEN a top level item is dropped before the hidden items title THEN it is the last shown one`() {
        val items = CustomizeMenuItems(shown = listOf(a, b, c), hidden = listOf(d), expandedIds = emptySet())

        val moved = items.drop("a", NextTo(HIDDEN_TITLE, placeAfter = false))

        assertEquals(listOf(b, c, a), moved.shown)
        assertEquals(listOf(d), moved.hidden)
    }

    @Test
    fun `WHEN a top level item is dropped after the hidden items title THEN nothing changes`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c), expandedIds = emptySet())

        assertEquals(items, items.drop("a", NextTo(HIDDEN_TITLE, placeAfter = true)))
        assertEquals(items, items.drop("a", CustomizeMenuDropTarget.EmptyHiddenArea))
        assertEquals(items, items.drop("a", CustomizeMenuDropTarget.InPlace))
    }

    @Test
    fun `WHEN a top level item is dropped before an expanded item THEN it is placed before it`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, b, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf(b, a, more), items.drop("a", NextTo("more", placeAfter = false)).shown)
    }

    @Test
    fun `WHEN a top level item is dropped after an expanded item THEN it stays at the top level and nothing changes`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(items, items.drop("a", NextTo("more", placeAfter = true)))
    }

    @Test
    fun `WHEN a top level item is dropped after a collapsed item THEN it is placed after it`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(listOf(more, a, b), items.drop("a", NextTo("more", placeAfter = true)).shown)
    }

    @Test
    fun `WHEN a top level item is dropped after the sub-items of an expanded item THEN it is placed after it`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf(more, a, b), items.drop("a", NextTo(subItemsEndKey("more"), placeAfter = true)).shown)
    }

    @Test
    fun `WHEN a sub-item is dropped before or after another sub-item THEN it is placed there`() {
        val more = expandable("more", b, c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf(d, b, c), items.drop("d", NextTo("b", placeAfter = false)).subItemsOf("more"))
        assertEquals(listOf(c, b, d), items.drop("b", NextTo("c", placeAfter = true)).subItemsOf("more"))
        assertEquals(listOf("a", "more"), items.drop("d", NextTo("b", placeAfter = false)).shown.map { it.id })
    }

    @Test
    fun `WHEN a sub-item is dropped after the expanded item THEN it is the first sub-item`() {
        val more = expandable("more", b, c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf(d, b, c), items.drop("d", NextTo("more", placeAfter = true)).subItemsOf("more"))
    }

    @Test
    fun `WHEN a sub-item is dropped before the end of the sub-items THEN it is the last sub-item`() {
        val more = expandable("more", b, c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.drop("b", NextTo(subItemsEndKey("more"), placeAfter = false))

        assertEquals(listOf(c, d, b), moved.subItemsOf("more"))
    }

    @Test
    fun `WHEN a sub-item is dropped outside its expanded item THEN nothing changes`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(items, items.drop("b", NextTo("a", placeAfter = true)))
        assertEquals(items, items.drop("b", NextTo("more", placeAfter = false)))
        assertEquals(items, items.drop("b", NextTo(subItemsEndKey("more"), placeAfter = true)))
        assertEquals(items, items.drop("b", NextTo(HIDDEN_TITLE, placeAfter = false)))
    }

    @Test
    fun `WHEN a top level item is dropped among the sub-items of another one THEN nothing changes`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(items, items.drop("a", NextTo("b", placeAfter = true)))
        assertEquals(items, items.drop("a", NextTo(subItemsEndKey("more"), placeAfter = false)))
    }

    @Test
    fun `WHEN an item is dropped next to a hidden item or next to itself THEN nothing changes`() {
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b),
                hidden = listOf(d, more),
                expandedIds = setOf("more"),
            )

        assertEquals(items, items.drop("a", NextTo("d", placeAfter = true)))
        assertEquals(items, items.drop("a", NextTo("more", placeAfter = false)))
        assertEquals(items, items.drop("a", NextTo("c", placeAfter = false)))
        assertEquals(items, items.drop("a", NextTo("a", placeAfter = true)))
    }

    @Test
    fun `WHEN a hidden item is dropped next to a shown one THEN nothing changes`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c), expandedIds = emptySet())

        assertEquals(items, items.drop("c", NextTo("a", placeAfter = false)))
    }

    @Test
    fun `WHEN an item is dropped where it is THEN the items are equal`() {
        val items = CustomizeMenuItems(shown = listOf(a, b, c), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(items, items.drop("b", NextTo("a", placeAfter = true)))
        assertEquals(items, items.drop("b", NextTo("c", placeAfter = false)))
    }

    @Test
    fun `WHEN items are moved THEN the items the app does not show keep their index`() {
        val x = CustomizableMenuItem(id = "x", item = null)
        val y = CustomizableMenuItem(id = "y", item = null)
        val more = expandable("more", x, b, y, c)
        val items =
            CustomizeMenuItems(shown = listOf(x, a, y, d, more), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.drop("d", NextTo("a", placeAfter = false)).drop("c", NextTo("b", placeAfter = false))

        assertEquals(listOf("x", "d", "y", "a", "more"), moved.shown.map { it.id })
        assertEquals(listOf(x, c, y, b), moved.subItemsOf("more"))
        assertEquals(setOf("more"), moved.expandedIds)
    }

    private fun CustomizeMenuItems.subItemsOf(id: String) = shown.first { it.id == id }.subItems

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
}
