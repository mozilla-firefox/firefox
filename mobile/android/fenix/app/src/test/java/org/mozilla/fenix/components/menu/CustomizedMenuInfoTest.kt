/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertEquals
import org.junit.Test

class CustomizedMenuInfoTest {
    private val default = menuInfo(shown = listOf("a", "b", "c", "d"))
    private val defaultSection =
        sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("more", "x", "y", "z")))

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

    @Test
    fun `GIVEN the user moved the navigation section WHEN adapting the saved menu to the app THEN keep it moved`() {
        val saved =
            menuInfo(shown = listOf("d", "removed", "a"), hidden = listOf("b")).copy(isNavigationSectionMoved = true)

        assertEquals(
            menuInfo(shown = listOf("d", "a", "c"), hidden = listOf("b")).copy(isNavigationSectionMoved = true),
            saved.reconciledWith(default, requiredSectionIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the saved section has all the items of the app WHEN adapting it to the app THEN keep it as saved`() {
        val saved =
            sectionInfo(
                shown = listOf(item("b"), item("more", "z", "a"), item("x")),
                hidden = listOf(item("c"), item("y")),
            )

        assertEquals(saved, saved.reconciledWith(defaultSection, topLevelOnlyIds = emptySet()))
    }

    @Test
    fun `GIVEN the saved section has items no longer in the app WHEN adapting it to the app THEN drop them`() {
        val saved =
            sectionInfo(
                shown = listOf(item("a"), item("removed"), item("b"), item("more", "x", "alsoRemoved", "y", "z")),
                hidden = listOf(item("c"), item("removedToo")),
            )

        assertEquals(
            sectionInfo(
                shown = listOf(item("a"), item("b"), item("more", "x", "y", "z")),
                hidden = listOf(item("c")),
            ),
            saved.reconciledWith(defaultSection, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the saved section has repeated items WHEN adapting it to the app THEN keep only the first of each`() {
        val saved =
            sectionInfo(
                shown = listOf(item("a"), item("more", "x", "a", "y", "z"), item("b"), item("x")),
                hidden = listOf(item("c"), item("b")),
            )

        assertEquals(
            sectionInfo(
                shown = listOf(item("a"), item("more", "x", "y", "z"), item("b")),
                hidden = listOf(item("c")),
            ),
            saved.reconciledWith(defaultSection, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has a new item WHEN adapting the saved section to the app THEN show it at its default position`() {
        val saved =
            sectionInfo(shown = listOf(item("c"), item("a"), item("more", "x", "y", "z")), hidden = listOf(item("b")))
        val default =
            sectionInfo(shown = listOf(item("a"), item("new"), item("b"), item("c"), item("more", "x", "y", "z")))

        assertEquals(
            sectionInfo(
                shown = listOf(item("c"), item("new"), item("a"), item("more", "x", "y", "z")),
                hidden = listOf(item("b")),
            ),
            saved.reconciledWith(default, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has a new item hidden by default WHEN adapting the saved section to the app THEN add it to the hidden items`() {
        val saved = sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("more", "x", "y", "z")))
        val default = defaultSection.copy(hiddenItems = listOf(item("new")))

        assertEquals(
            saved.copy(hiddenItems = listOf(item("new"))),
            saved.reconciledWith(default, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has a new item inside another one WHEN adapting the saved section to the app THEN show it at its default position inside that item`() {
        val saved = sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("more", "z", "y", "x")))
        val default = sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("more", "x", "new", "y", "z")))

        assertEquals(
            sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("more", "z", "new", "y", "x"))),
            saved.reconciledWith(default, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has a new item inside a hidden item WHEN adapting the saved section to the app THEN add it inside the hidden item`() {
        val saved =
            sectionInfo(shown = listOf(item("a"), item("b"), item("c")), hidden = listOf(item("more", "x", "y", "z")))
        val default = sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("more", "x", "y", "z", "new")))

        assertEquals(
            sectionInfo(
                shown = listOf(item("a"), item("b"), item("c")),
                hidden = listOf(item("more", "x", "y", "z", "new")),
            ),
            saved.reconciledWith(default, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the app has a new item expanding to others WHEN adapting the saved section to the app THEN show it with the items not already shown elsewhere`() {
        val saved = sectionInfo(shown = listOf(item("a"), item("y"), item("b")), hidden = listOf(item("c")))
        val default = sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("new", "x", "y", "z")))

        assertEquals(
            sectionInfo(
                shown = listOf(item("a"), item("y"), item("b"), item("new", "x", "z")),
                hidden = listOf(item("c")),
            ),
            saved.reconciledWith(default, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN items saved inside an item not expanding to others in the app WHEN adapting the saved section to the app THEN show them at their default position`() {
        val saved = sectionInfo(shown = listOf(item("a", "x", "y"), item("b"), item("c"), item("more", "z")))

        assertEquals(
            sectionInfo(shown = listOf(item("a"), item("b"), item("c"), item("more", "x", "y", "z"))),
            saved.reconciledWith(defaultSection, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN an item expanding to others is saved inside another item WHEN adapting the saved section to the app THEN show it at its default position`() {
        val default = sectionInfo(shown = listOf(item("a"), item("other", "b"), item("more", "x", "y", "z")))
        val saved = sectionInfo(shown = listOf(item("a"), item("other", "b", "more")))

        assertEquals(
            sectionInfo(shown = listOf(item("a"), item("other", "b"), item("more", "x", "y", "z"))),
            saved.reconciledWith(default, topLevelOnlyIds = emptySet()),
        )
    }

    @Test
    fun `GIVEN the saved section has no item left to show WHEN adapting it to the app THEN show the default items`() {
        val saved =
            sectionInfo(
                shown = listOf(item("removed")),
                hidden = listOf(item("a"), item("b"), item("c"), item("more", "x", "y", "z")),
            )

        assertEquals(defaultSection, saved.reconciledWith(defaultSection, topLevelOnlyIds = emptySet()))
    }

    @Test
    fun `GIVEN the saved section has only an item expanding to none of its items left to show WHEN adapting it to the app THEN show the default items`() {
        val saved =
            sectionInfo(
                shown = listOf(item("more", "removed")),
                hidden = listOf(item("a"), item("b"), item("c"), item("x"), item("y"), item("z")),
            )

        assertEquals(defaultSection, saved.reconciledWith(defaultSection, topLevelOnlyIds = emptySet()))
    }

    @Test
    fun `GIVEN the saved section has an item expanding to none of its items and other items shown WHEN adapting it to the app THEN keep it as saved`() {
        val saved =
            sectionInfo(
                shown = listOf(item("a"), item("x"), item("more")),
                hidden = listOf(item("b"), item("c"), item("y"), item("z")),
            )

        assertEquals(saved, saved.reconciledWith(defaultSection, topLevelOnlyIds = emptySet()))
    }

    @Test
    fun `GIVEN a top level only item saved inside a shown item WHEN adapting it to the app THEN place it right after that item`() {
        val default = sectionInfo(shown = listOf(item("a"), item("ext"), item("more", "x", "y")))
        val saved = sectionInfo(shown = listOf(item("more", "x", "ext", "y"), item("a")))

        assertEquals(
            sectionInfo(shown = listOf(item("more", "x", "y"), item("ext"), item("a"))),
            saved.reconciledWith(default, topLevelOnlyIds = setOf("ext")),
        )
    }

    @Test
    fun `GIVEN a top level only item saved inside a hidden item WHEN adapting it to the app THEN hide it right after that item`() {
        val default = sectionInfo(shown = listOf(item("a"), item("ext"), item("more", "x", "y")))
        val saved = sectionInfo(shown = listOf(item("a")), hidden = listOf(item("more", "ext", "x", "y")))

        assertEquals(
            sectionInfo(shown = listOf(item("a")), hidden = listOf(item("more", "x", "y"), item("ext"))),
            saved.reconciledWith(default, topLevelOnlyIds = setOf("ext")),
        )
    }

    @Test
    fun `GIVEN a top level only item saved at the top level WHEN adapting it to the app THEN keep it as saved`() {
        val default = sectionInfo(shown = listOf(item("a"), item("ext"), item("more", "x", "y")))
        val saved = sectionInfo(shown = listOf(item("more", "x", "y")), hidden = listOf(item("ext"), item("a")))

        assertEquals(saved, saved.reconciledWith(default, topLevelOnlyIds = setOf("ext")))
    }

    @Test
    fun `GIVEN a top level only item saved as the only sub-item shown WHEN adapting it to the app THEN show it at the top level`() {
        val default = sectionInfo(shown = listOf(item("a"), item("ext"), item("more", "x", "y")))
        val saved =
            sectionInfo(
                shown = listOf(item("more", "ext")),
                hidden = listOf(item("a"), item("x"), item("y")),
            )

        assertEquals(
            sectionInfo(
                shown = listOf(item("more"), item("ext")),
                hidden = listOf(item("a"), item("x"), item("y")),
            ),
            saved.reconciledWith(default, topLevelOnlyIds = setOf("ext")),
        )
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)

    private fun sectionInfo(shown: List<CustomizedItemInfo>, hidden: List<CustomizedItemInfo> = emptyList()) =
        CustomizedSectionInfo(sectionId = "section", items = shown, hiddenItems = hidden)

    private fun item(id: String, vararg subItemIds: String) =
        CustomizedItemInfo(id = id, subItemIds = subItemIds.toList())
}
