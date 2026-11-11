/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.menu.ui.customize

import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.ui.customize.CustomizeMenuItemKey.HIDDEN_SECTIONS
import org.junit.Assert.assertEquals
import org.junit.Test

class CustomizeMenuGroupsTest {
    private val groups = CustomizeMenuGroups(shown = groups("a", "b", "c"), hidden = groups("d", "e"))

    @Test
    fun `WHEN a shown group is moved before another shown group THEN it is shown before it`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("c", "a", "b"), hidden = groups("d", "e")),
            groups.moveGroup(sourceId = "c", targetKey = "a", placeAfter = false),
        )
    }

    @Test
    fun `WHEN a shown group is moved after another shown group THEN it is shown after it`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("b", "c", "a"), hidden = groups("d", "e")),
            groups.moveGroup(sourceId = "a", targetKey = "c", placeAfter = true),
        )
    }

    @Test
    fun `WHEN a shown group is moved before the hidden sections THEN it is placed as the last visible group`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("b", "c", "a"), hidden = groups("d", "e")),
            groups.moveGroup(sourceId = "a", targetKey = HIDDEN_SECTIONS, placeAfter = false),
        )
    }

    @Test
    fun `WHEN a shown group is moved after the hidden sections THEN it is placed as the first hidden item`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("a", "c"), hidden = groups("b", "d", "e")),
            groups.moveGroup(sourceId = "b", targetKey = HIDDEN_SECTIONS, placeAfter = true),
        )
    }

    @Test
    fun `WHEN a shown group is moved next to a hidden group THEN it is hidden next to it`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("b", "c"), hidden = groups("d", "a", "e")),
            groups.moveGroup(sourceId = "a", targetKey = "d", placeAfter = true),
        )
    }

    @Test
    fun `WHEN the only shown group is hidden THEN no group is shown`() {
        val groups = CustomizeMenuGroups(shown = groups("a"), hidden = emptyList())

        assertEquals(
            CustomizeMenuGroups(shown = emptyList(), hidden = groups("a")),
            groups.moveGroup(sourceId = "a", targetKey = HIDDEN_SECTIONS, placeAfter = true),
        )
    }

    @Test
    fun `WHEN a hidden group is moved before the hidden sections THEN it is placed as the last visible group`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("a", "b", "c", "e"), hidden = groups("d")),
            groups.moveGroup(sourceId = "e", targetKey = HIDDEN_SECTIONS, placeAfter = false),
        )
    }

    @Test
    fun `WHEN a hidden group is moved next to a shown group THEN it is shown next to it`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("d", "a", "b", "c"), hidden = groups("e")),
            groups.moveGroup(sourceId = "d", targetKey = "a", placeAfter = false),
        )
    }

    @Test
    fun `WHEN a hidden group is moved after another hidden group THEN it is hidden after it`() {
        assertEquals(
            CustomizeMenuGroups(shown = groups("a", "b", "c"), hidden = groups("e", "d")),
            groups.moveGroup(sourceId = "d", targetKey = "e", placeAfter = true),
        )
    }

    @Test
    fun `WHEN a group is moved next to itself THEN nothing changes`() {
        assertEquals(groups, groups.moveGroup(sourceId = "b", targetKey = "b", placeAfter = true))
    }

    @Test
    fun `WHEN a shown group is moved next to a mandatory group THEN it is shown next to it`() {
        val groups = CustomizeMenuGroups(shown = groups("a", "m", "b"), hidden = emptyList(), mandatoryIds = setOf("m"))

        assertEquals(
            CustomizeMenuGroups(shown = groups("a", "b", "m"), hidden = emptyList(), mandatoryIds = setOf("m")),
            groups.moveGroup(sourceId = "b", targetKey = "m", placeAfter = false),
        )
    }

    @Test
    fun `WHEN a mandatory group is moved next to another shown group THEN it is shown next to it`() {
        val groups = CustomizeMenuGroups(shown = groups("a", "m", "b"), hidden = emptyList(), mandatoryIds = setOf("m"))

        assertEquals(
            CustomizeMenuGroups(shown = groups("m", "a", "b"), hidden = emptyList(), mandatoryIds = setOf("m")),
            groups.moveGroup(sourceId = "m", targetKey = "a", placeAfter = false),
        )
    }

    @Test
    fun `WHEN a mandatory group is moved after the hidden sections THEN nothing changes`() {
        val groups = CustomizeMenuGroups(shown = groups("a", "m"), hidden = groups("d"), mandatoryIds = setOf("m"))

        assertEquals(groups, groups.moveGroup(sourceId = "m", targetKey = HIDDEN_SECTIONS, placeAfter = true))
    }

    @Test
    fun `WHEN a mandatory group is moved next to a hidden group THEN nothing changes`() {
        val groups = CustomizeMenuGroups(shown = groups("a", "m"), hidden = groups("d"), mandatoryIds = setOf("m"))

        assertEquals(groups, groups.moveGroup(sourceId = "m", targetKey = "d", placeAfter = false))
    }

    @Test
    fun `WHEN a group which is not shown nor hidden is moved THEN nothing changes`() {
        assertEquals(groups, groups.moveGroup(sourceId = "unknown", targetKey = "a", placeAfter = false))
    }

    @Test
    fun `WHEN a group is moved next to an item which is not a group THEN nothing changes`() {
        assertEquals(groups, groups.moveGroup(sourceId = "a", targetKey = "unknown", placeAfter = false))
    }

    private fun groups(vararg ids: String) = ids.map { MenuItemsGroup.Row(id = it, items = emptyList()) }
}
