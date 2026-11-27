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
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class CustomizeMenuItemsTest {
    private val a = item("a")
    private val b = item("b")
    private val c = item("c")
    private val d = item("d")
    private val topLevelOnlyItem = item("top level only").copy(isTopLevelOnly = true)

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
    fun `GIVEN other shown items THEN an item can be hidden, but not the last one shown`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c), expandedIds = emptySet())
        val last = CustomizeMenuItems(shown = listOf(a), hidden = listOf(c), expandedIds = emptySet())

        assertFalse(items.cannotBeHidden("a"))
        assertTrue(last.cannotBeHidden("a"))
        assertFalse(last.cannotBeHidden("c"))
    }

    @Test
    fun `GIVEN only the sub-items of an item shown THEN the last sub-item and the item cannot be hidden`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(more), hidden = listOf(a), expandedIds = setOf("more"))
        val oneLeft = items.copy(shown = listOf(expandable("more", c)))

        assertFalse(items.cannotBeHidden("c"))
        assertTrue(oneLeft.cannotBeHidden("c"))
        assertTrue(oneLeft.cannotBeHidden("more"))
    }

    @Test
    fun `GIVEN an item and an empty expandable item shown THEN the item cannot be hidden`() {
        val items =
            CustomizeMenuItems(shown = listOf(a, expandable("more")), hidden = emptyList(), expandedIds = emptySet())

        assertTrue(items.cannotBeHidden("a"))
    }

    @Test
    fun `GIVEN items the app does not show THEN they do not count as shown ones`() {
        val x = CustomizableMenuItem(id = "x", item = null)
        val items = CustomizeMenuItems(shown = listOf(x, a), hidden = emptyList(), expandedIds = emptySet())

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
    fun `WHEN an item is dropped after the hidden items title or into the empty area THEN it is hidden first`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c), expandedIds = emptySet())
        val withoutHidden = items.copy(hidden = emptyList())

        val afterTitle = items.drop("a", NextTo(HIDDEN_TITLE, placeAfter = true))
        val intoArea = withoutHidden.drop("a", CustomizeMenuDropTarget.EmptyHiddenArea)

        assertEquals(listOf(b), afterTitle.shown)
        assertEquals(listOf(a, c), afterTitle.hidden)
        assertEquals(listOf(b), intoArea.shown)
        assertEquals(listOf(a), intoArea.hidden)
    }

    @Test
    fun `WHEN an item is dropped back in place THEN nothing changes`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c), expandedIds = emptySet())

        assertEquals(items, items.drop("a", CustomizeMenuDropTarget.InPlace))
    }

    @Test
    fun `WHEN a top level item is dropped before an expanded item THEN it is placed before it`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, b, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf(b, a, more), items.drop("a", NextTo("more", placeAfter = false)).shown)
    }

    @Test
    fun `WHEN a top level item is dropped after an expanded item THEN it is the first of its sub-items`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.drop("a", NextTo("more", placeAfter = true))

        assertEquals(listOf("more"), moved.shown.map { it.id })
        assertEquals(listOf(a, c), moved.subItemsOf("more"))
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
    fun `WHEN a sub-item is dropped next to a top level item THEN it leaves the sub-items and is placed there`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more, d), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.drop("b", NextTo("a", placeAfter = true))

        assertEquals(listOf("a", "b", "more", "d"), moved.shown.map { it.id })
        assertEquals(listOf(c), moved.subItemsOf("more"))
    }

    @Test
    fun `WHEN a sub-item is dropped before or after the expanded item THEN it is placed before or after it`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        val before = items.drop("b", NextTo("more", placeAfter = false))
        val after = items.drop("b", NextTo(subItemsEndKey("more"), placeAfter = true))

        assertEquals(listOf("a", "b", "more"), before.shown.map { it.id })
        assertEquals(listOf("a", "more", "b"), after.shown.map { it.id })
        assertEquals(listOf(c), before.subItemsOf("more"))
        assertEquals(listOf(c), after.subItemsOf("more"))
    }

    @Test
    fun `WHEN a sub-item is dropped before the hidden items title THEN it is the last shown item`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(d), expandedIds = setOf("more"))

        val moved = items.drop("b", NextTo(HIDDEN_TITLE, placeAfter = false))

        assertEquals(listOf("a", "more", "b"), moved.shown.map { it.id })
        assertEquals(listOf(c), moved.subItemsOf("more"))
        assertEquals(listOf(d), moved.hidden)
    }

    @Test
    fun `WHEN a top level item is dropped among the sub-items of another one THEN it becomes a sub-item there`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        val inMiddle = items.drop("a", NextTo("c", placeAfter = false))
        val last = items.drop("a", NextTo(subItemsEndKey("more"), placeAfter = false))

        assertEquals(listOf("more"), inMiddle.shown.map { it.id })
        assertEquals(listOf(b, a, c), inMiddle.subItemsOf("more"))
        assertEquals(listOf(b, c, a), last.subItemsOf("more"))
    }

    @Test
    fun `WHEN a top level item is dropped into an expanded item without sub-items THEN it is its only sub-item`() {
        val more = expandable("more")
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.drop("a", NextTo(subItemsEndKey("more"), placeAfter = false))

        assertEquals(listOf(a), moved.subItemsOf("more"))
    }

    @Test
    fun `WHEN an expandable item is dropped among the sub-items of an expanded item THEN nothing changes`() {
        val more = expandable("more", c)
        val other = expandable("other", d)
        val items =
            CustomizeMenuItems(shown = listOf(a, more, other), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(items, items.drop("other", NextTo("c", placeAfter = true)))
        assertEquals(items, items.drop("other", NextTo("more", placeAfter = true)))
        assertEquals(items, items.drop("other", NextTo(subItemsEndKey("more"), placeAfter = false)))
    }

    @Test
    fun `WHEN a collapsed expandable item is dropped at the top level THEN it is moved with its sub-items`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, b, more), hidden = emptyList(), expandedIds = emptySet())

        val moved = items.drop("more", NextTo("a", placeAfter = false))

        assertEquals(listOf("more", "a", "b"), moved.shown.map { it.id })
        assertEquals(listOf(c, d), moved.subItemsOf("more"))
    }

    @Test
    fun `WHEN an item is dropped next to itself THEN nothing changes`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c), expandedIds = emptySet())

        assertEquals(items, items.drop("a", NextTo("a", placeAfter = true)))
        assertEquals(items, items.drop("c", NextTo("c", placeAfter = false)))
    }

    @Test
    fun `WHEN a shown item is dropped next to a hidden one THEN it is hidden there`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c, d), expandedIds = emptySet())

        val moved = items.drop("a", NextTo("d", placeAfter = false))

        assertEquals(listOf(b), moved.shown)
        assertEquals(listOf(c, a, d), moved.hidden)
    }

    @Test
    fun `WHEN a shown sub-item is dropped next to a hidden item THEN it is hidden and leaves its parent`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(d), expandedIds = setOf("more"))

        val moved = items.drop("b", NextTo("d", placeAfter = true))

        assertEquals(listOf(a, more.copy(subItems = listOf(c))), moved.shown)
        assertEquals(listOf(d, b), moved.hidden)
    }

    @Test
    fun `WHEN a shown expandable item is hidden THEN it keeps its sub-items`() {
        val more = expandable("more", b, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = emptySet())

        val moved = items.drop("more", CustomizeMenuDropTarget.EmptyHiddenArea)

        assertEquals(listOf(a), moved.shown)
        assertEquals(listOf(more), moved.hidden)
    }

    @Test
    fun `WHEN the last shown item is dropped among the hidden ones THEN nothing changes`() {
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(b), expandedIds = emptySet())

        assertEquals(items, items.drop("a", NextTo("b", placeAfter = true)))
        assertEquals(items, items.drop("a", NextTo(HIDDEN_TITLE, placeAfter = true)))
    }

    @Test
    fun `WHEN the only item shown in an expandable item is dropped among the hidden ones THEN nothing changes`() {
        val more = expandable("more", b)
        val items = CustomizeMenuItems(shown = listOf(more), hidden = listOf(a), expandedIds = setOf("more"))

        assertEquals(items, items.drop("b", NextTo("a", placeAfter = false)))
        assertEquals(items, items.drop("more", NextTo("a", placeAfter = false)))
    }

    @Test
    fun `WHEN a hidden item is dropped before or after another hidden one THEN it is placed there`() {
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(b, c, d), expandedIds = emptySet())

        assertEquals(listOf(d, b, c), items.drop("d", NextTo("b", placeAfter = false)).hidden)
        assertEquals(listOf(c, b, d), items.drop("b", NextTo("c", placeAfter = true)).hidden)
        assertEquals(listOf(a), items.drop("d", NextTo("b", placeAfter = false)).shown)
    }

    @Test
    fun `WHEN a hidden item is dropped next to a shown one or before the hidden items title THEN it is shown there`() {
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(c, d), expandedIds = emptySet())

        val next = items.drop("c", NextTo("a", placeAfter = true))
        val last = items.drop("d", NextTo(HIDDEN_TITLE, placeAfter = false))

        assertEquals(listOf(a, c, b), next.shown)
        assertEquals(listOf(d), next.hidden)
        assertEquals(listOf(a, b, d), last.shown)
        assertEquals(listOf(c), last.hidden)
    }

    @Test
    fun `WHEN a hidden item is dropped among the sub-items of a shown expandable item THEN it becomes one of them`() {
        val more = expandable("more", b)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(c), expandedIds = setOf("more"))

        val moved = items.drop("c", NextTo(subItemsEndKey("more"), placeAfter = false))

        assertEquals(listOf(b, c), moved.subItemsOf("more"))
        assertEquals(emptyList<CustomizableMenuItem>(), moved.hidden)
    }

    @Test
    fun `WHEN a hidden expandable item is dropped among the shown ones THEN it is shown with its sub-items`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(more), expandedIds = emptySet())

        val moved = items.drop("more", NextTo("a", placeAfter = false))

        assertEquals(listOf("more", "a", "b"), moved.shown.map { it.id })
        assertEquals(listOf(c, d), moved.subItemsOf("more"))
        assertEquals(emptyList<CustomizableMenuItem>(), moved.hidden)
    }

    @Test
    fun `WHEN a hidden expandable item is dropped among the sub-items of an expanded one THEN nothing changes`() {
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, more),
                hidden = listOf(expandable("other", d)),
                expandedIds = setOf("more"),
            )

        assertEquals(items, items.drop("other", NextTo("c", placeAfter = true)))
        assertEquals(items, items.drop("other", NextTo(subItemsEndKey("more"), placeAfter = false)))
    }

    @Test
    fun `WHEN a shown expandable item is dropped among the sub-items of a hidden one THEN nothing changes`() {
        val other = expandable("other", c)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", b)),
                hidden = listOf(other),
                expandedIds = setOf("other"),
            )

        assertEquals(items, items.drop("more", NextTo("c", placeAfter = true)))
        assertEquals(items, items.drop("more", NextTo(subItemsEndKey("other"), placeAfter = false)))
    }

    @Test
    fun `WHEN a shown item is dropped among the sub-items of a hidden expandable item THEN it is hidden in it`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(more), expandedIds = setOf("more"))

        val moved = items.drop("a", NextTo("c", placeAfter = false))

        assertEquals(listOf(b), moved.shown)
        assertEquals(listOf(a, c), moved.hidden.single().subItems)
    }

    @Test
    fun `WHEN a hidden sub-item is dropped next to a hidden item THEN it leaves its parent and stays hidden`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(b, more), expandedIds = setOf("more"))

        val moved = items.drop("c", NextTo("b", placeAfter = false))

        assertEquals(listOf(a), moved.shown)
        assertEquals(listOf(c, b, more.copy(subItems = listOf(d))), moved.hidden)
    }

    @Test
    fun `WHEN a hidden sub-item is dropped next to a shown item THEN it is shown and leaves its parent`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(more), expandedIds = setOf("more"))

        val moved = items.drop("c", NextTo("a", placeAfter = true))

        assertEquals(listOf(a, c), moved.shown)
        assertEquals(listOf(more.copy(subItems = listOf(d))), moved.hidden)
    }

    @Test
    fun `WHEN a hidden sub-item is dropped before or after another one THEN it is placed there`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(more), expandedIds = setOf("more"))

        assertEquals(listOf(d, c), items.drop("d", NextTo("c", placeAfter = false)).hidden.single().subItems)
        assertEquals(
            listOf(d, c),
            items.drop("c", NextTo(subItemsEndKey("more"), placeAfter = false)).hidden.single().subItems,
        )
    }

    @Test
    fun `WHEN an item is dropped after the sub-items of a hidden expandable item THEN it is hidden after it`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(more, d), expandedIds = setOf("more"))

        val moved = items.drop("a", NextTo(subItemsEndKey("more"), placeAfter = true))

        assertEquals(listOf(b), moved.shown)
        assertEquals(listOf(more, a, d), moved.hidden)
    }

    @Test
    fun `WHEN an item is dropped into a hidden expandable item without sub-items THEN it is its only one`() {
        val more = expandable("more")
        val items = CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(more), expandedIds = setOf("more"))

        val moved = items.drop("a", NextTo(subItemsEndKey("more"), placeAfter = false))

        assertEquals(listOf(a), moved.hidden.single().subItems)
    }

    @Test
    fun `WHEN items are hidden THEN the items the app does not show keep their index`() {
        val x = CustomizableMenuItem(id = "x", item = null)
        val y = CustomizableMenuItem(id = "y", item = null)
        val items = CustomizeMenuItems(shown = listOf(a, x, b), hidden = listOf(y, c), expandedIds = emptySet())

        val moved = items.drop("a", NextTo("c", placeAfter = false))

        assertEquals(listOf("x", "b"), moved.shown.map { it.id })
        assertEquals(listOf("y", "a", "c"), moved.hidden.map { it.id })
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

    @Test
    fun `WHEN an item is moved to the start of a container THEN it is placed after the items the app does not show`() {
        val x = CustomizableMenuItem(id = "x", item = null)
        val y = CustomizableMenuItem(id = "y", item = null)
        val more = expandable("more", x, b)
        val items = CustomizeMenuItems(shown = listOf(y, a, more), hidden = emptyList(), expandedIds = setOf("more"))

        val intoSubItems = items.drop("a", NextTo("more", placeAfter = true))
        val toTop = items.drop("b", NextTo("a", placeAfter = false))

        assertEquals(listOf("y", "more"), intoSubItems.shown.map { it.id })
        assertEquals(listOf(x, a, b), intoSubItems.subItemsOf("more"))
        assertEquals(listOf("y", "b", "a", "more"), toTop.shown.map { it.id })
        assertEquals(listOf(x), toTop.subItemsOf("more"))
    }

    @Test
    fun `WHEN an item is moved to the end of a container THEN items the app does not show keep their index`() {
        val x = CustomizableMenuItem(id = "x", item = null)
        val y = CustomizableMenuItem(id = "y", item = null)
        val more = expandable("more", a, y)
        val items = CustomizeMenuItems(shown = listOf(more, b, x), hidden = emptyList(), expandedIds = setOf("more"))

        val intoSubItems = items.drop("b", NextTo(subItemsEndKey("more"), placeAfter = false))
        val toTop = items.drop("a", NextTo(HIDDEN_TITLE, placeAfter = false))

        assertEquals(listOf(a, y, b), intoSubItems.subItemsOf("more"))
        assertEquals(listOf("more", "x"), intoSubItems.shown.map { it.id })
        assertEquals(listOf("more", "b", "x", "a"), toTop.shown.map { it.id })
        assertEquals(listOf(y), toTop.subItemsOf("more"))
    }

    @Test
    fun `WHEN an item which can only be directly in its section is dropped among the sub-items of a shown expandable item THEN nothing changes`() {
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, topLevelOnlyItem, more),
                hidden = emptyList(),
                expandedIds = setOf("more"),
            )

        assertEquals(items, items.drop("top level only", NextTo("c", placeAfter = false)))
        assertEquals(items, items.drop("top level only", NextTo("more", placeAfter = true)))
        assertEquals(items, items.drop("top level only", NextTo(subItemsEndKey("more"), placeAfter = false)))
    }

    @Test
    fun `WHEN an item which can only be directly in its section is dropped among the sub-items of a hidden expandable item THEN nothing changes`() {
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(shown = listOf(a, topLevelOnlyItem), hidden = listOf(more), expandedIds = setOf("more"))

        assertEquals(items, items.drop("top level only", NextTo("c", placeAfter = false)))
        assertEquals(items, items.drop("top level only", NextTo(subItemsEndKey("more"), placeAfter = false)))
    }

    @Test
    fun `WHEN an item which can only be directly in its section is dropped at the top level or among the hidden items THEN it is placed there`() {
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, topLevelOnlyItem, more),
                hidden = listOf(b),
                expandedIds = setOf("more"),
            )

        val first = items.drop("top level only", NextTo("a", placeAfter = false))
        val hidden = items.drop("top level only", NextTo("b", placeAfter = false))

        assertEquals(listOf("top level only", "a", "more"), first.shown.map { it.id })
        assertEquals(listOf("top level only", "b"), hidden.hidden.map { it.id })
    }

    @Test
    fun `WHEN an item which can only be directly in its section is dropped inside an expandable item THEN it is rejected as a sub-item`() {
        val more = expandable("more", c)
        val hiddenMore = expandable("hidden more", d)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, topLevelOnlyItem, more),
                hidden = listOf(hiddenMore),
                expandedIds = setOf("more", "hidden more"),
            )

        assertEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("c", placeAfter = false)),
        )
        assertEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("more", placeAfter = true)),
        )
        assertEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo(subItemsEndKey("hidden more"), placeAfter = false)),
        )
        assertEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("d", placeAfter = true)),
        )
    }

    @Test
    fun `WHEN an expandable item which can only be directly in its section is dropped inside another expandable item THEN it is rejected as a sub-item`() {
        val more = expandable("more", c)
        val extensions = expandable("extensions").copy(isTopLevelOnly = true)
        val items =
            CustomizeMenuItems(shown = listOf(a, extensions, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(DropOutcome.RejectedAsSubItem, items.dropOutcome("extensions", NextTo("c", placeAfter = false)))
        assertEquals(items, items.drop("extensions", NextTo("c", placeAfter = false)))
    }

    @Test
    fun `WHEN an item which can only be directly in its section is dropped at the top level, hidden or in place THEN it is not rejected as a sub-item`() {
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, topLevelOnlyItem, more),
                hidden = listOf(b),
                expandedIds = setOf("more"),
            )

        assertNotEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("a", placeAfter = false)),
        )
        assertNotEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("more", placeAfter = false)),
        )
        assertNotEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("b", placeAfter = true)),
        )
        assertNotEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", CustomizeMenuDropTarget.EmptyHiddenArea),
        )
        assertNotEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", CustomizeMenuDropTarget.InPlace),
        )
    }

    @Test
    fun `WHEN an item which can be inside other items is dropped inside an expandable item THEN it is not rejected as a sub-item`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertNotEquals(DropOutcome.RejectedAsSubItem, items.dropOutcome("a", NextTo("c", placeAfter = false)))
    }

    @Test
    fun `WHEN a hidden item which can only be directly in its section is dropped inside a shown or a hidden expandable item THEN nothing changes`() {
        val more = expandable("more", c)
        val hiddenMore = expandable("hidden more", d)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, more),
                hidden = listOf(topLevelOnlyItem, hiddenMore),
                expandedIds = setOf("more", "hidden more"),
            )

        assertEquals(items, items.drop("top level only", NextTo("c", placeAfter = false)))
        assertEquals(items, items.drop("top level only", NextTo("d", placeAfter = false)))
        assertEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("c", placeAfter = false)),
        )
        assertEquals(
            DropOutcome.RejectedAsSubItem,
            items.dropOutcome("top level only", NextTo("d", placeAfter = false)),
        )
    }

    @Test
    fun `WHEN a hidden item which can only be directly in its section is dropped at the top level of the shown items THEN it is shown there`() {
        val items =
            CustomizeMenuItems(shown = listOf(a, b), hidden = listOf(topLevelOnlyItem), expandedIds = emptySet())

        val moved = items.drop("top level only", NextTo("b", placeAfter = false))

        assertEquals(listOf("a", "top level only", "b"), moved.shown.map { it.id })
        assertTrue(moved.hidden.isEmpty())
    }

    @Test
    fun `GIVEN an expanded item THEN it and its sub-items are counted and have a position among the shown rows`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(5, items.shownWithSubItemsCount)
        assertEquals(listOf(1, 2, 3, 4, 5), listOf("a", "more", "c", "d", "b").map { items.shownPositionOf(it) })
    }

    @Test
    fun `GIVEN a collapsed item THEN its sub-items are not counted and have no position`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(3, items.shownWithSubItemsCount)
        assertEquals(3, items.shownPositionOf("b"))
        assertNull(items.shownPositionOf("c"))
    }

    @Test
    fun `GIVEN items the app does not show THEN they are not counted and have no position`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items = CustomizeMenuItems(shown = listOf(notShown, a, b), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(2, items.shownWithSubItemsCount)
        assertNull(items.shownPositionOf("not shown"))
        assertEquals(1, items.shownPositionOf("a"))
    }

    @Test
    fun `GIVEN a hidden item or the end of sub-items THEN it has no position among the shown items`() {
        val more = expandable("more", c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(b, d), expandedIds = setOf("more"))

        assertNull(items.shownPositionOf("b"))
        assertNull(items.shownPositionOf(subItemsEndKey("more")))
        assertEquals(3, items.shownWithSubItemsCount)
    }

    @Test
    fun `GIVEN an expanded item THEN its sub-items are counted and have a position among them only`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(2, items.shownSubItemsCountOf("more"))
        assertEquals(1, items.shownSubItemPositionOf("more", "c"))
        assertEquals(2, items.shownSubItemPositionOf("more", "d"))
    }

    @Test
    fun `GIVEN a collapsed item THEN it has no sub-items counted and its sub-items have no position among them`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(0, items.shownSubItemsCountOf("more"))
        assertNull(items.shownSubItemPositionOf("more", "c"))
    }

    @Test
    fun `GIVEN items that are not sub-items of an item THEN they have no position among its sub-items`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val more = expandable("more", notShown, c)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(b), expandedIds = setOf("more"))

        assertEquals(1, items.shownSubItemsCountOf("more"))
        assertEquals(1, items.shownSubItemPositionOf("more", "c"))
        assertNull(items.shownSubItemPositionOf("more", "a"))
        assertNull(items.shownSubItemPositionOf("more", "b"))
        assertNull(items.shownSubItemPositionOf("more", "not shown"))
        assertNull(items.shownSubItemPositionOf("more", subItemsEndKey("more")))
        assertEquals(0, items.shownSubItemsCountOf("a"))
    }

    @Test
    fun `WHEN a top level item is moved up or down THEN it swaps places with its neighbour`() {
        val items = CustomizeMenuItems(shown = listOf(a, b, c), hidden = listOf(d), expandedIds = emptySet())

        assertEquals(listOf("b", "a", "c"), items.moveDown("a")?.shown?.map { it.id })
        assertEquals(listOf("a", "c", "b"), items.moveDown("b")?.shown?.map { it.id })
        assertEquals(listOf("b", "a", "c"), items.moveUp("b")?.shown?.map { it.id })
        assertEquals(listOf("a", "c", "b"), items.moveUp("c")?.shown?.map { it.id })
        assertEquals(listOf(d), items.moveDown("a")?.hidden)
    }

    @Test
    fun `GIVEN the first or the last shown item THEN it cannot be moved further up or down`() {
        val items = CustomizeMenuItems(shown = listOf(a, b, c), hidden = emptyList(), expandedIds = emptySet())

        assertNull(items.moveUp("a"))
        assertNull(items.moveDown("c"))
        assertNull(CustomizeMenuItems(listOf(a), emptyList(), emptySet()).moveUp("a"))
        assertNull(CustomizeMenuItems(listOf(a), emptyList(), emptySet()).moveDown("a"))
    }

    @Test
    fun `WHEN an item is moved down past an expanded item THEN it is after its sub-items`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.moveDown("a")

        assertEquals(listOf("more", "a", "b"), moved?.shown?.map { it.id })
        assertEquals(listOf(c, d), moved?.subItemsOf("more"))
        assertEquals(listOf("more", "c", "d", "more#sub_items_end", "a", "b"), moved?.shownRows?.map { it.key })
    }

    @Test
    fun `WHEN an item is moved up past an expanded item THEN it is before it`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.moveUp("b")

        assertEquals(listOf("a", "b", "more"), moved?.shown?.map { it.id })
        assertEquals(listOf("a", "b", "more", "c", "d", "more#sub_items_end"), moved?.shownRows?.map { it.key })
    }

    @Test
    fun `WHEN an expanded item is moved by its neighbour THEN it keeps its sub-items`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf(c, d), items.moveDown("a")?.subItemsOf("more"))
        assertEquals(listOf(c, d), items.moveUp("b")?.subItemsOf("more"))
    }

    @Test
    fun `WHEN an item is moved past a collapsed item THEN the collapsed item keeps its sub-items`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(listOf("more", "a", "b"), items.moveDown("a")?.shown?.map { it.id })
        assertEquals(listOf("a", "b", "more"), items.moveUp("b")?.shown?.map { it.id })
        assertEquals(listOf(c, d), items.moveUp("b")?.subItemsOf("more"))
    }

    @Test
    fun `WHEN a sub-item is moved up or down THEN it swaps places with its neighbour and stays inside the item`() {
        val more = expandable("more", c, d, b)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf("d", "c", "b"), items.moveDown("c")?.subItemsOf("more")?.map { it.id })
        assertEquals(listOf("d", "c", "b"), items.moveUp("d")?.subItemsOf("more")?.map { it.id })
        assertEquals(listOf("a", "more"), items.moveDown("c")?.shown?.map { it.id })
        assertNull(items.moveUp("c"))
        assertNull(items.moveDown("b"))
    }

    @Test
    fun `GIVEN items the app does not show THEN they are never a neighbour`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items =
            CustomizeMenuItems(shown = listOf(notShown, a, notShown.copy(id = "other"), b), emptyList(), emptySet())

        assertNull(items.moveUp("a"))
        assertNull(items.moveDown("b"))
        assertNull(items.moveUp("not shown"))
        assertNull(items.moveDown("other"))
        assertEquals(listOf("not shown", "b", "other", "a"), items.moveUp("b")?.shown?.map { it.id })
        assertEquals(listOf("not shown", "b", "other", "a"), items.moveDown("a")?.shown?.map { it.id })
    }

    @Test
    fun `GIVEN an expanded item THEN it cannot be moved but a collapsed one can`() {
        val more = expandable("more", c)
        val expanded = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))
        val collapsed = expanded.copy(expandedIds = emptySet())

        assertNull(expanded.moveUp("more"))
        assertNull(expanded.moveDown("more"))
        assertTrue(expanded.actionsOf("more").isEmpty())
        assertEquals(listOf("more", "a", "b"), collapsed.moveUp("more")?.shown?.map { it.id })
        assertEquals(listOf("a", "b", "more"), collapsed.moveDown("more")?.shown?.map { it.id })
    }

    @Test
    fun `GIVEN a hidden item, a sub-item of a collapsed item or an unknown key THEN it cannot be moved up or down`() {
        val more = expandable("more", c)
        val hidden = expandable("hidden more", d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(b, hidden), expandedIds = emptySet())

        listOf("b", "hidden more", "d", "c", "unknown", subItemsEndKey("more")).forEach {
            assertNull(items.moveUp(it))
            assertNull(items.moveDown(it))
        }
        listOf("d", "c", "unknown", subItemsEndKey("more")).forEach { assertTrue(items.actionsOf(it).isEmpty()) }
    }

    @Test
    fun `GIVEN shown items THEN they are offered to move up and down where they can move`() {
        val items = CustomizeMenuItems(shown = listOf(a, b, c), hidden = emptyList(), expandedIds = emptySet())

        assertEquals(
            listOf(EditMenuItemAction.Kind.MOVE_DOWN, EditMenuItemAction.Kind.HIDE),
            items.actionsOf("a").map { it.kind },
        )
        assertEquals(
            listOf(EditMenuItemAction.Kind.MOVE_UP, EditMenuItemAction.Kind.MOVE_DOWN, EditMenuItemAction.Kind.HIDE),
            items.actionsOf("b").map { it.kind },
        )
        assertEquals(
            listOf(EditMenuItemAction.Kind.MOVE_UP, EditMenuItemAction.Kind.HIDE),
            items.actionsOf("c").map { it.kind },
        )
        assertEquals(items.moveUp("b"), items.actionsOf("b").first().result)
        assertEquals(items.moveDown("b"), items.actionsOf("b")[1].result)
        assertEquals(items.moveToHidden("b"), items.actionsOf("b").last().result)
        listOf("a", "b", "c").forEach { key -> assertTrue(items.actionsOf(key).none { it.isRefused }) }
    }

    @Test
    fun `GIVEN a move which leaves the items unchanged THEN it is not offered`() {
        val more = expandable("more", c, topLevelOnlyItem)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(items, items.moveUp("top level only"))
        assertEquals(
            listOf(EditMenuItemAction.Kind.MOVE_OUT_OF, EditMenuItemAction.Kind.HIDE),
            items.actionsOf("top level only").map { it.kind },
        )
    }

    @Test
    fun `GIVEN a shown expandable item THEN the items at the top level are offered to move into it before hiding`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", c), b),
                hidden = emptyList(),
                expandedIds = emptySet(),
            )

        assertEquals(
            listOf(EditMenuItemAction.Kind.MOVE_DOWN, EditMenuItemAction.Kind.MOVE_INTO, EditMenuItemAction.Kind.HIDE),
            items.actionsOf("a").map { it.kind },
        )
        assertEquals(
            listOf(EditMenuItemAction.Kind.MOVE_UP, EditMenuItemAction.Kind.MOVE_INTO, EditMenuItemAction.Kind.HIDE),
            items.actionsOf("b").map { it.kind },
        )
        assertEquals(items.moveIntoExpandable("a"), items.actionsOf("a")[1].result)
        assertTrue(items.actionsOf("a").none { it.isRefused })
    }

    @Test
    fun `GIVEN an expanded expandable item THEN its sub-items are offered to move out of it, up or down and hide`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", c, d)),
                hidden = emptyList(),
                expandedIds = setOf("more"),
            )

        assertEquals(
            listOf(
                EditMenuItemAction.Kind.MOVE_DOWN,
                EditMenuItemAction.Kind.MOVE_OUT_OF,
                EditMenuItemAction.Kind.HIDE,
            ),
            items.actionsOf("c").map { it.kind },
        )
        assertEquals(
            listOf(
                EditMenuItemAction.Kind.MOVE_UP,
                EditMenuItemAction.Kind.MOVE_OUT_OF,
                EditMenuItemAction.Kind.HIDE,
            ),
            items.actionsOf("d").map { it.kind },
        )
        assertEquals(items.moveOutOfExpandable("c"), items.actionsOf("c")[1].result)
    }

    @Test
    fun `WHEN an item is moved into the expanded item THEN it is the first sub-item`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.moveIntoExpandable("b")

        assertEquals(listOf("a", "more"), moved?.shown?.map { it.id })
        assertEquals(listOf("b", "c", "d"), moved?.subItemsOf("more")?.map { it.id })
        assertEquals(setOf("more"), moved?.expandedIds)
    }

    @Test
    fun `WHEN an item is moved into the collapsed item THEN it is the first sub-item and the item stays collapsed`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", c), b),
                hidden = listOf(d),
                expandedIds = emptySet(),
            )

        val moved = items.moveIntoExpandable("a")

        assertEquals(listOf("more", "b"), moved?.shown?.map { it.id })
        assertEquals(listOf("a", "c"), moved?.subItemsOf("more")?.map { it.id })
        assertEquals(listOf(d), moved?.hidden)
        assertEquals(emptySet<String>(), moved?.expandedIds)
        assertEquals(1, moved?.listIndexOf("more"))
        assertNull(moved?.listIndexOf("a"))
    }

    @Test
    fun `GIVEN an item the app does not show in the expandable item WHEN an item is moved into it THEN it is first among the shown items, after that item`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", notShown, c)),
                hidden = emptyList(),
                expandedIds = emptySet(),
            )

        assertEquals(listOf("not shown", "a", "c"), items.moveIntoExpandable("a")?.subItemsOf("more")?.map { it.id })
    }

    @Test
    fun `WHEN an item is moved into an empty collapsed expandable item THEN it is its only sub-item and the item stays collapsed`() {
        val more = expandable("more")
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = emptySet())

        val moved = checkNotNull(items.moveIntoExpandable("a"))

        assertEquals(emptySet<String>(), moved.expandedIds)
        assertEquals(
            listOf(Item(more.copy(subItems = listOf(a)), parentId = null, isHidden = false, isExpanded = false)),
            moved.shownRows,
        )
    }

    @Test
    fun `WHEN an item is moved into an empty expanded expandable item THEN it is its only sub-item`() {
        val more = expandable("more")
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = checkNotNull(items.moveIntoExpandable("a"))

        assertEquals(setOf("more"), moved.expandedIds)
        assertEquals(
            listOf(
                Item(more.copy(subItems = listOf(a)), parentId = null, isHidden = false, isExpanded = true),
                Item(a, parentId = "more", isHidden = false, isExpanded = false),
                SubItemsEnd(parentId = "more", isHidden = false),
            ),
            moved.shownRows,
        )
    }

    @Test
    fun `GIVEN an item with sub-items that the app does not show THEN no item can be moved into it`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null, subItems = listOf(c))
        val items = CustomizeMenuItems(shown = listOf(a, notShown, b), hidden = emptyList(), expandedIds = emptySet())

        assertNull(items.moveIntoExpandable("a"))
        assertNull(items.moveIntoExpandable("b"))
        assertTrue(items.actionsOf("a").none { it.kind == EditMenuItemAction.Kind.MOVE_INTO })
    }

    @Test
    fun `GIVEN an item which can only be directly in its section, an expandable item or a sub-item THEN it cannot be moved into the expandable item`() {
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, topLevelOnlyItem, more),
                hidden = emptyList(),
                expandedIds = setOf("more"),
            )

        assertNull(items.moveIntoExpandable("top level only"))
        assertNull(items.moveIntoExpandable("more"))
        assertNull(items.moveIntoExpandable("c"))
        assertTrue(items.actionsOf("top level only").none { it.kind == EditMenuItemAction.Kind.MOVE_INTO })
        assertTrue(items.actionsOf("more").none { it.kind == EditMenuItemAction.Kind.MOVE_INTO })
        assertTrue(items.actionsOf("c").none { it.kind == EditMenuItemAction.Kind.MOVE_INTO })
    }

    @Test
    fun `GIVEN no shown expandable item, a hidden item or an unknown key THEN it cannot be moved into an expandable item`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b),
                hidden = listOf(expandable("more", c), d),
                expandedIds = setOf("more"),
            )
        val withMore = items.copy(shown = listOf(a, expandable("shown more", b)), expandedIds = setOf("shown more"))

        assertNull(items.moveIntoExpandable("a"))
        assertNull(items.moveIntoExpandable("d"))
        assertNull(items.moveIntoExpandable("c"))
        assertNull(withMore.moveIntoExpandable("d"))
        assertNull(withMore.moveIntoExpandable("unknown"))
        assertNull(withMore.moveIntoExpandable(subItemsEndKey("shown more")))
        assertTrue(items.actionsOf("a").none { it.kind == EditMenuItemAction.Kind.MOVE_INTO })
    }

    @Test
    fun `GIVEN the expanded item is the last one WHEN a sub-item is moved out of it THEN it is just above it`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, b, more), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.moveOutOfExpandable("d")

        assertEquals(listOf("a", "b", "d", "more"), moved?.shown?.map { it.id })
        assertEquals(listOf(c), moved?.subItemsOf("more"))
    }

    @Test
    fun `GIVEN the expanded item is not the last one WHEN a sub-item is moved out of it THEN it is the last one`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.moveOutOfExpandable("c")

        assertEquals(listOf("a", "more", "b", "c"), moved?.shown?.map { it.id })
        assertEquals(listOf(d), moved?.subItemsOf("more"))
        assertEquals(setOf("more"), moved?.expandedIds)
        assertEquals(6, moved?.listIndexOf("c"))
    }

    @Test
    fun `GIVEN two shown expandable items WHEN a sub-item of the second one is moved out THEN it is just above the second one`() {
        val more = expandable("more", c)
        val other = expandable("other", d, b)
        val items =
            CustomizeMenuItems(shown = listOf(a, more, other), hidden = emptyList(), expandedIds = setOf("other"))

        val moved = items.moveOutOfExpandable("d")

        assertEquals(listOf("a", "more", "d", "other"), moved?.shown?.map { it.id })
        assertEquals(listOf(b), moved?.subItemsOf("other"))
        assertEquals(listOf(c), moved?.subItemsOf("more"))
    }

    @Test
    fun `GIVEN the expandable item is first WHEN a sub-item is moved out of it THEN it is last`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(more, a), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.moveOutOfExpandable("c")

        assertEquals(listOf("more", "a", "c"), moved?.shown?.map { it.id })
        assertEquals(listOf(d), moved?.subItemsOf("more"))
    }

    @Test
    fun `GIVEN an item the app does not show just before the expandable item WHEN a sub-item is moved out THEN it keeps its index`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val more = expandable("more", c)
        val items =
            CustomizeMenuItems(shown = listOf(a, notShown, more), hidden = emptyList(), expandedIds = setOf("more"))

        val moved = items.moveOutOfExpandable("c")

        assertEquals(listOf("a", "not shown", "c", "more"), moved?.shown?.map { it.id })
        assertTrue(moved?.subItemsOf("more")?.isEmpty() == true)
    }

    @Test
    fun `GIVEN a sub-item of a hidden item, an item not in an expandable item or an unknown key THEN it cannot be moved out`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", c)),
                hidden = listOf(expandable("hidden more", d), b),
                expandedIds = setOf("more", "hidden more"),
            )

        assertNull(items.moveOutOfExpandable("d"))
        assertNull(items.moveOutOfExpandable("b"))
        assertNull(items.moveOutOfExpandable("a"))
        assertNull(items.moveOutOfExpandable("more"))
        assertNull(items.moveOutOfExpandable("unknown"))
        assertNull(items.moveOutOfExpandable(subItemsEndKey("more")))
        assertNull(items.copy(expandedIds = emptySet()).moveOutOfExpandable("c"))
    }

    @Test
    fun `WHEN a shown item is hidden THEN it is the first hidden item`() {
        val items = CustomizeMenuItems(shown = listOf(a, b, c), hidden = listOf(d), expandedIds = emptySet())

        val moved = items.moveToHidden("b")

        assertEquals(listOf("a", "c"), moved?.shown?.map { it.id })
        assertEquals(listOf("b", "d"), moved?.hidden?.map { it.id })
    }

    @Test
    fun `WHEN a sub-item or a collapsed expandable item is hidden THEN it is the first hidden item and keeps what it has`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more), hidden = listOf(b), expandedIds = setOf("more"))

        val movedSubItem = items.moveToHidden("c")
        val movedMore = items.copy(expandedIds = emptySet()).moveToHidden("more")

        assertEquals(listOf("d"), movedSubItem?.subItemsOf("more")?.map { it.id })
        assertEquals(listOf("c", "b"), movedSubItem?.hidden?.map { it.id })
        assertEquals(listOf("a"), movedMore?.shown?.map { it.id })
        assertEquals(listOf("more", "b"), movedMore?.hidden?.map { it.id })
        assertEquals(listOf(c, d), movedMore?.hidden?.first()?.subItems)
    }

    @Test
    fun `WHEN the last shown item is hidden THEN the items are unchanged`() {
        val alone = CustomizeMenuItems(shown = listOf(a), hidden = listOf(b), expandedIds = emptySet())
        val withEmptyExpandable =
            CustomizeMenuItems(shown = listOf(a, expandable("more")), hidden = emptyList(), expandedIds = emptySet())
        val onlySubItem =
            CustomizeMenuItems(shown = listOf(expandable("more", c)), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(alone, alone.moveToHidden("a"))
        assertEquals(withEmptyExpandable, withEmptyExpandable.moveToHidden("a"))
        assertEquals(onlySubItem, onlySubItem.moveToHidden("c"))
    }

    @Test
    fun `GIVEN an expanded item, a hidden item, a sub-item of a collapsed item or an unknown key THEN it cannot be hidden`() {
        val more = expandable("more", c)
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items =
            CustomizeMenuItems(shown = listOf(a, more, notShown), hidden = listOf(b), expandedIds = setOf("more"))

        assertNull(items.moveToHidden("not shown"))
        assertNull(items.moveToHidden("more"))
        assertNull(items.moveToHidden("b"))
        assertNull(items.moveToHidden("unknown"))
        assertNull(items.moveToHidden(subItemsEndKey("more")))
        assertNull(items.copy(expandedIds = emptySet()).moveToHidden("c"))
    }

    @Test
    fun `GIVEN the shown expandable item is the last one WHEN a hidden item is shown THEN it is just above it`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b, expandable("more", c)),
                hidden = listOf(d),
                expandedIds = emptySet(),
            )

        val moved = items.moveToShown("d")

        assertEquals(listOf("a", "b", "d", "more"), moved?.shown?.map { it.id })
        assertTrue(moved?.hidden?.isEmpty() == true)
    }

    @Test
    fun `GIVEN the shown expandable item is not the last one WHEN a hidden item is shown THEN it is the last one`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", c), b),
                hidden = listOf(d),
                expandedIds = emptySet(),
            )

        val moved = items.moveToShown("d")

        assertEquals(listOf("a", "more", "b", "d"), moved?.shown?.map { it.id })
        assertTrue(moved?.hidden?.isEmpty() == true)
    }

    @Test
    fun `GIVEN only the shown expandable item is shown WHEN a hidden item is shown THEN it is just above it`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(expandable("more", c)),
                hidden = listOf(d),
                expandedIds = emptySet(),
            )

        assertEquals(listOf("d", "more"), items.moveToShown("d")?.shown?.map { it.id })
    }

    @Test
    fun `GIVEN no shown expandable item WHEN a hidden item is shown THEN it is the last shown item`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b),
                hidden = listOf(expandable("more", c), d),
                expandedIds = emptySet(),
            )

        assertEquals(listOf("a", "b", "d"), items.moveToShown("d")?.shown?.map { it.id })
        assertEquals(listOf("a", "b", "more"), items.moveToShown("more")?.shown?.map { it.id })
        assertEquals(listOf(c), items.moveToShown("more")?.shown?.last()?.subItems)
    }

    @Test
    fun `WHEN a sub-item of a hidden expandable item is shown THEN it leaves it and the item stays hidden`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", b)),
                hidden = listOf(expandable("hidden more", c, d)),
                expandedIds = setOf("hidden more"),
            )

        val moved = items.moveToShown("c")

        assertEquals(listOf("a", "c", "more"), moved?.shown?.map { it.id })
        assertEquals(listOf("hidden more"), moved?.hidden?.map { it.id })
        assertEquals(listOf(d), moved?.hidden?.single()?.subItems)
    }

    @Test
    fun `GIVEN a shown item, an expanded hidden item, a sub-item of a collapsed hidden item or an unknown key THEN it cannot be shown`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b),
                hidden = listOf(expandable("hidden more", c), notShown),
                expandedIds = setOf("hidden more"),
            )

        assertNull(items.moveToShown("not shown"))
        assertNull(items.moveToShown("a"))
        assertNull(items.moveToShown("hidden more"))
        assertNull(items.moveToShown("unknown"))
        assertNull(items.moveToShown(subItemsEndKey("hidden more")))
        assertNull(items.copy(expandedIds = emptySet()).moveToShown("c"))
    }

    @Test
    fun `GIVEN hidden items THEN a hidden item is only offered to be shown, and a hidden expanded item nothing`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b),
                hidden = listOf(c, expandable("hidden more", d), notShown),
                expandedIds = emptySet(),
            )

        assertTrue(items.actionsOf("not shown").isEmpty())
        assertTrue(CustomizeMenuItems(listOf(a, notShown), emptyList(), emptySet()).actionsOf("not shown").isEmpty())

        assertEquals(listOf(EditMenuItemAction.Kind.UNHIDE), items.actionsOf("c").map { it.kind })
        assertEquals(listOf(EditMenuItemAction.Kind.UNHIDE), items.actionsOf("hidden more").map { it.kind })
        assertEquals(items.moveToShown("c"), items.actionsOf("c").single().result)
        assertTrue(items.actionsOf("c").none { it.isRefused })
        assertTrue(items.copy(expandedIds = setOf("hidden more")).actionsOf("hidden more").isEmpty())
        assertTrue(items.actionsOf("unknown").isEmpty())
        assertTrue(items.actionsOf(subItemsEndKey("hidden more")).isEmpty())
        assertEquals(
            listOf(EditMenuItemAction.Kind.UNHIDE),
            items.copy(expandedIds = setOf("hidden more")).actionsOf("d").map { it.kind },
        )
    }

    @Test
    fun `GIVEN the last shown item THEN hiding it is offered with the unchanged items as its result`() {
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(b), expandedIds = emptySet())

        val action = items.actionsOf("a").single()

        assertEquals(EditMenuItemAction.Kind.HIDE, action.kind)
        assertEquals(items, action.result)
        assertTrue(action.isRefused)
    }

    @Test
    fun `THEN the index in the list of a shown item is after the drag bar and the rows before it`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a, more, b), hidden = emptyList(), expandedIds = setOf("more"))

        assertEquals(listOf(1, 2, 3, 4, 6), listOf("a", "more", "c", "d", "b").map { items.listIndexOf(it) })
        assertEquals(5, items.listIndexOf(subItemsEndKey("more")))
    }

    @Test
    fun `THEN the index in the list of a hidden item is after the hidden items title`() {
        val more = expandable("more", c, d)
        val items = CustomizeMenuItems(shown = listOf(a), hidden = listOf(more, b), expandedIds = setOf("more"))

        assertEquals(2, items.hiddenTitleIndex)
        assertEquals(listOf(3, 4, 5, 7), listOf("more", "c", "d", "b").map { items.listIndexOf(it) })
    }

    @Test
    fun `GIVEN a key without a row THEN it has no index in the list`() {
        val more = expandable("more", c)
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items = CustomizeMenuItems(listOf(notShown, a, more), hidden = emptyList(), expandedIds = emptySet())

        assertNull(items.listIndexOf("not shown"))
        assertNull(items.listIndexOf("c"))
        assertNull(items.listIndexOf("unknown"))
    }

    @Test
    fun `GIVEN a shown expandable item THEN the sheet actions of a shown item are move up, move down, move into and hide`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b, expandable("more", c), d),
                hidden = emptyList(),
                expandedIds = emptySet(),
            )
        val expected =
            listOf(
                EditMenuItemAction.Kind.MOVE_UP,
                EditMenuItemAction.Kind.MOVE_DOWN,
                EditMenuItemAction.Kind.MOVE_INTO,
                EditMenuItemAction.Kind.HIDE,
            )

        assertEquals(expected, items.sheetKindsOf("a"))
        assertEquals(expected, items.sheetKindsOf("b"))
        assertEquals(expected, items.sheetKindsOf("d"))
    }

    @Test
    fun `GIVEN an item which can only be directly in its section or a collapsed expandable item THEN its sheet actions have no move into`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, topLevelOnlyItem, expandable("more", c)),
                hidden = emptyList(),
                expandedIds = emptySet(),
            )
        val expected =
            listOf(EditMenuItemAction.Kind.MOVE_UP, EditMenuItemAction.Kind.MOVE_DOWN, EditMenuItemAction.Kind.HIDE)

        assertEquals(expected, items.sheetKindsOf("top level only"))
        assertEquals(expected, items.sheetKindsOf("more"))
    }

    @Test
    fun `GIVEN no shown expandable item THEN the sheet actions of a shown item have no move into`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null, subItems = listOf(c))
        val items =
            CustomizeMenuItems(
                shown = listOf(a, b, notShown),
                hidden = listOf(expandable("more", d)),
                expandedIds = emptySet(),
            )

        assertEquals(
            listOf(EditMenuItemAction.Kind.MOVE_UP, EditMenuItemAction.Kind.MOVE_DOWN, EditMenuItemAction.Kind.HIDE),
            items.sheetKindsOf("a"),
        )
    }

    @Test
    fun `GIVEN an expanded item THEN the sheet actions of its sub-items are move up, move down, move out of and hide`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a, expandable("more", c, d)),
                hidden = emptyList(),
                expandedIds = setOf("more"),
            )
        val expected =
            listOf(
                EditMenuItemAction.Kind.MOVE_UP,
                EditMenuItemAction.Kind.MOVE_DOWN,
                EditMenuItemAction.Kind.MOVE_OUT_OF,
                EditMenuItemAction.Kind.HIDE,
            )

        assertEquals(expected, items.sheetKindsOf("c"))
        assertEquals(expected, items.sheetKindsOf("d"))
    }

    @Test
    fun `GIVEN hidden items THEN the sheet actions of a hidden item and of a hidden sub-item are move up, move down and unhide`() {
        val items =
            CustomizeMenuItems(
                shown = listOf(a),
                hidden = listOf(b, expandable("hidden more", c, d), expandable("collapsed hidden", item("e"))),
                expandedIds = setOf("hidden more"),
            )
        val expected =
            listOf(EditMenuItemAction.Kind.MOVE_UP, EditMenuItemAction.Kind.MOVE_DOWN, EditMenuItemAction.Kind.UNHIDE)

        assertEquals(expected, items.sheetKindsOf("b"))
        assertEquals(expected, items.sheetKindsOf("c"))
        assertEquals(expected, items.sheetKindsOf("collapsed hidden"))
    }

    @Test
    fun `GIVEN an expanded item, a hidden expanded item, a sub-item of a collapsed item or an unknown key THEN it has no sheet actions`() {
        val notShown = CustomizableMenuItem(id = "not shown", item = null)
        val items =
            CustomizeMenuItems(
                shown = listOf(a, notShown, expandable("more", c), expandable("collapsed", d)),
                hidden = listOf(expandable("hidden more", b)),
                expandedIds = setOf("more", "hidden more"),
            )

        listOf("more", "hidden more", "d", "not shown", subItemsEndKey("more"), "unknown").forEach {
            assertTrue(items.sheetKindsOf(it).isEmpty())
        }
    }

    @Test
    fun `THEN the kinds of the available actions of any item are a subsequence of its sheet action kinds`() {
        val cases =
            listOf(
                CustomizeMenuItems(
                    shown = listOf(a, b, topLevelOnlyItem, expandable("more", c, d)),
                    hidden = listOf(item("h"), expandable("hidden more", item("hs"))),
                    expandedIds = setOf("more", "hidden more"),
                ) to listOf("a", "b", "top level only", "more", "c", "d", "h", "hidden more", "hs", "unknown"),
                CustomizeMenuItems(
                    shown = listOf(a, expandable("more", c)),
                    hidden = emptyList(),
                    expandedIds = emptySet(),
                ) to listOf("a", "more", "c"),
                CustomizeMenuItems(shown = listOf(a), hidden = listOf(b), expandedIds = emptySet()) to listOf("a", "b"),
            )

        cases.forEach { (items, keys) ->
            keys.forEach { key ->
                assertTrue(
                    "actionsOf($key) should be a subsequence of sheetKindsOf($key)",
                    items.actionsOf(key).map { it.kind }.isSubsequenceOf(items.sheetKindsOf(key)),
                )
            }
        }
    }

    private fun <T> List<T>.isSubsequenceOf(other: List<T>): Boolean {
        var index = 0
        for (element in other) {
            if (index < size && this[index] == element) index++
        }
        return index == size
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
