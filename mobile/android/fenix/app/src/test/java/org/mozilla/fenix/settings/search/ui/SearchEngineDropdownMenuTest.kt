/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.search.ui

import mozilla.components.compose.base.menu.MenuItem
import mozilla.components.compose.base.text.Text
import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.R

class SearchEngineDropdownMenuTest {

    private val edit = Text.Resource(R.string.search_engine_edit)
    private val delete = Text.Resource(R.string.search_engine_delete)

    @Test
    fun `WHEN the engine is custom and deletable THEN Edit and Delete are shown`() {
        val items = menuItems(isCustomSearchEngine = true, allowDeletion = true)

        assertEquals(listOf(edit, delete), items.map { it.text })
    }

    @Test
    fun `WHEN the engine is custom but not deletable THEN only Edit is shown`() {
        val items = menuItems(isCustomSearchEngine = true, allowDeletion = false)

        assertEquals(listOf(edit), items.map { it.text })
    }

    @Test
    fun `WHEN the engine is deletable but not custom THEN only Delete is shown`() {
        val items = menuItems(isCustomSearchEngine = false, allowDeletion = true)

        assertEquals(listOf(delete), items.map { it.text })
    }

    @Test
    fun `WHEN the engine is neither custom nor deletable THEN no item is shown`() {
        val items = menuItems(isCustomSearchEngine = false, allowDeletion = false)

        assertEquals(emptyList<MenuItem.TextItem>(), items)
    }

    @Test
    fun `GIVEN a custom deletable engine THEN Edit is a default item and Delete a critical one`() {
        val items = menuItems(isCustomSearchEngine = true, allowDeletion = true)

        assertEquals(
            listOf(MenuItem.FixedItem.Level.Default, MenuItem.FixedItem.Level.Critical),
            items.map { it.level },
        )
    }

    @Test
    fun `WHEN a menu item is clicked THEN its callback is invoked`() {
        val clicks = mutableListOf<String>()
        val items =
            searchEngineMenuItems(
                    isCustomSearchEngine = true,
                    allowDeletion = true,
                    onEditClick = { clicks.add("edit") },
                    onDeleteClick = { clicks.add("delete") },
                )
                .filterIsInstance<MenuItem.TextItem>()

        items.forEach { it.onClick() }

        assertEquals(listOf("edit", "delete"), clicks)
    }

    private fun menuItems(isCustomSearchEngine: Boolean, allowDeletion: Boolean) =
        searchEngineMenuItems(
                isCustomSearchEngine = isCustomSearchEngine,
                allowDeletion = allowDeletion,
                onEditClick = {},
                onDeleteClick = {},
            )
            .filterIsInstance<MenuItem.TextItem>()
}
