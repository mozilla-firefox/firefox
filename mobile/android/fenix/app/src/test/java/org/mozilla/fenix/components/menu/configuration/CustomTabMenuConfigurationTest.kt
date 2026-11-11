/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import org.junit.Test
import org.mozilla.fenix.components.menu.configuration.CustomTabMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.OpenInFirefox
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Translate

class CustomTabMenuConfigurationTest {
    @Test
    fun `WHEN building the custom tab menu AND extensions are enabled THEN show items for custom tab including Extensions under DesktopSite`() {
        val configuration = CustomTabMenuConfiguration.sections(isToolbarAtBottom = false, shouldShowExtensions = true)

        val toolsSection = configuration.first { it.id == TOOLS_GROUP_ID }
        assertEquals(
            listOf(OpenInFirefox, Bookmark, FindInPage, DesktopSite, Extensions, Translate, SummarizePage),
            toolsSection.items,
        )
    }

    @Test
    fun `WHEN building the custom tab menu AND extensions are disabled THEN show items for custom tab excluding Extensions`() {
        val configuration = CustomTabMenuConfiguration.sections(isToolbarAtBottom = false, shouldShowExtensions = false)

        val toolsSection = configuration.first { it.id == TOOLS_GROUP_ID }
        assertEquals(
            listOf(OpenInFirefox, Bookmark, FindInPage, DesktopSite, Translate, SummarizePage),
            toolsSection.items,
        )
    }
}
