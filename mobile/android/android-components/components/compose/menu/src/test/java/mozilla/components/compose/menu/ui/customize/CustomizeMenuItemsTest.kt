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
