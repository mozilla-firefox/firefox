/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.menu.data.MenuItemTitle
import mozilla.components.compose.menu.data.MenuItemsGroup
import mozilla.components.compose.menu.data.StandardMenuItem
import org.junit.Test
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

class MenuSectionConfigurationTest {
    private val shownItems = listOf(StandardMenuItem(title = MenuItemTitle(Text.String("Find in page"))))

    @Test
    fun `GIVEN a section shown as a row WHEN getting its menu group THEN show the given items as a row`() {
        val section = MenuSectionConfiguration(id = "section", presentationMode = Row, items = listOf(FindInPage))

        assertEquals(
            MenuItemsGroup.Row(id = "section", items = shownItems, isSticky = false),
            section.asMenuItemsGroup(shownItems),
        )
    }

    @Test
    fun `GIVEN a sticky section shown as a grid WHEN getting its menu group THEN show the given items as a sticky grid`() {
        val section =
            MenuSectionConfiguration(
                id = "section",
                presentationMode = Grid,
                items = listOf(FindInPage),
                isSticky = true,
            )

        assertEquals(
            MenuItemsGroup.Grid(id = "section", items = shownItems, isSticky = true),
            section.asMenuItemsGroup(shownItems),
        )
    }
}
