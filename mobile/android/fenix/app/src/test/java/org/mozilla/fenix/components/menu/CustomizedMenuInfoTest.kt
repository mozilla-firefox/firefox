/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertEquals
import org.junit.Test

class CustomizedMenuInfoTest {
    private val default = menuInfo(shown = listOf("a", "b", "c", "d"))

    @Test
    fun `GIVEN the saved menu has all the sections of the app WHEN adapting it to the app THEN keep it as saved`() {
        val saved = menuInfo(shown = listOf("d", "a", "c"), hidden = listOf("b"))

        assertEquals(saved, saved.reconciledWith(default, requiredSectionIds = emptySet()))
    }

    @Test
    fun `GIVEN the saved menu has sections no longer in the app WHEN adapting it to the app THEN drop them`() {
        val saved = menuInfo(shown = listOf("a", "removed", "b", "c"), hidden = listOf("d", "alsoRemoved"))

        assertEquals(
            menuInfo(shown = listOf("a", "b", "c"), hidden = listOf("d")),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the saved menu has repeated sections WHEN adapting it to the app THEN keep only the first of each`() {
        val saved = menuInfo(shown = listOf("a", "b", "a", "c"), hidden = listOf("d", "d"))

        assertEquals(
            menuInfo(shown = listOf("a", "b", "c"), hidden = listOf("d")),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN a section is both shown and hidden in the saved menu WHEN adapting it to the app THEN show it`() {
        val saved = menuInfo(shown = listOf("a", "b", "c", "d"), hidden = listOf("b"))

        assertEquals(
            menuInfo(shown = listOf("a", "b", "c", "d")),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN a section which cannot be removed is hidden in the saved menu WHEN adapting it to the app THEN show it at its default position`() {
        val saved = menuInfo(shown = listOf("a", "c", "d"), hidden = listOf("b"))

        assertEquals(
            menuInfo(shown = listOf("a", "b", "c", "d")),
            saved.reconciledWith(default, requiredSectionIds = setOf("b")),
        )
    }

    @Test
    fun `GIVEN the app has a new section WHEN adapting the saved menu to the app THEN show it at its default position`() {
        val saved = menuInfo(shown = listOf("d", "a", "b"), hidden = listOf("c"))
        val default = menuInfo(shown = listOf("a", "b", "new", "c", "d"))

        assertEquals(
            menuInfo(shown = listOf("d", "a", "new", "b"), hidden = listOf("c")),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has a new section with a default position past the saved sections WHEN adapting the saved menu to the app THEN show it last`() {
        val saved = menuInfo(shown = listOf("a", "b"), hidden = listOf("c", "d"))
        val default = menuInfo(shown = listOf("a", "b", "c", "d", "new"))

        assertEquals(
            menuInfo(shown = listOf("a", "b", "new"), hidden = listOf("c", "d")),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has more new sections WHEN adapting the saved menu to the app THEN show each at its default position`() {
        val saved = menuInfo(shown = listOf("b", "d"))
        val default = menuInfo(shown = listOf("new1", "b", "new2", "d"))

        assertEquals(
            menuInfo(shown = listOf("new1", "b", "new2", "d")),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has a new section hidden by default WHEN adapting the saved menu to the app THEN add it to the hidden sections`() {
        val saved = menuInfo(shown = listOf("a", "b", "c"), hidden = listOf("d"))
        val default = menuInfo(shown = listOf("a", "b", "c", "d"), hidden = listOf("new"))

        assertEquals(
            menuInfo(shown = listOf("a", "b", "c"), hidden = listOf("d", "new")),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the user shows a section hidden by default WHEN adapting the saved menu to the app THEN keep it shown`() {
        val saved = menuInfo(shown = listOf("a", "extra", "b", "c", "d"))
        val default = menuInfo(shown = listOf("a", "b", "c", "d"), hidden = listOf("extra"))

        assertEquals(saved, saved.reconciledWith(default, requiredSectionIds = emptySet()))
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)
}
