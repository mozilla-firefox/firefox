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
import mozilla.components.compose.menu.ui.customize.EditMenuItemsRow.Item
import mozilla.components.compose.menu.ui.customize.EditMenuItemsRow.SubItemsEnd
import org.junit.Assert.assertEquals
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
