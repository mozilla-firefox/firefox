/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.menu.configuration.CustomTabMenuConfiguration.PAGE_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.CustomTabMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Back
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Forward
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.OpenInFirefox
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Refresh
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Share
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Translate
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid

class CustomTabMenuConfigurationTest {
    @Test
    fun `WHEN building the custom tab menu AND extensions are enabled THEN show items for custom tab including Extensions under DesktopSite`() {
        val configuration = CustomTabMenuConfiguration.sections(isToolbarAtBottom = false, shouldShowExtensions = true)

        val toolsSection = configuration.first { it.id == TOOLS_GROUP_ID }
        assertEquals(
            listOf(OpenInFirefox, FindInPage, DesktopSite, Extensions, Translate, SummarizePage),
            toolsSection.items,
        )
    }

    @Test
    fun `WHEN building the custom tab menu AND extensions are disabled THEN show items for custom tab excluding Extensions`() {
        val configuration = CustomTabMenuConfiguration.sections(isToolbarAtBottom = false, shouldShowExtensions = false)

        val toolsSection = configuration.first { it.id == TOOLS_GROUP_ID }
        assertEquals(
            listOf(OpenInFirefox, FindInPage, DesktopSite, Translate, SummarizePage),
            toolsSection.items,
        )
    }

    @Test
    fun `WHEN building the custom tab menu THEN the sticky page controls contain the bookmark item in Grid mode`() {
        listOf(true, false).forEach { isToolbarAtBottom ->
            val configuration =
                CustomTabMenuConfiguration.sections(isToolbarAtBottom = isToolbarAtBottom, shouldShowExtensions = true)

            val pageControlsSection = configuration.first { it.id == PAGE_CONTROLS_GROUP_ID }
            assertEquals(Grid, pageControlsSection.presentationMode)
            assertEquals(listOf(Back, Forward, Bookmark, Share, Refresh), pageControlsSection.items)
            assertTrue(pageControlsSection.isSticky)
            assertFalse(pageControlsSection.showTitles)
            assertEquals(1, configuration.count { Bookmark in it.items })
        }
    }
}
