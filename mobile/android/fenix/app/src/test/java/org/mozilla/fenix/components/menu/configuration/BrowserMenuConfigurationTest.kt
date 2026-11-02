/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_1_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_2_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_3_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_4_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_5_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.GROUP_6_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.NAVIGATION_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DefaultBrowserBanner

class BrowserMenuConfigurationTest {
    @Test
    fun `GIVEN toolbar is at bottom WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = true, isExpandedToolbarEnabled = false)

        assertEquals(NAVIGATION_ID, configuration.last().id)
    }

    @Test
    fun `GIVEN toolbar is at top WHEN building the browser menu THEN navigation block appears at the top`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        assertEquals(NAVIGATION_ID, configuration.first().id)
    }

    @Test
    fun `GIVEN toolbar is expanded WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = true)

        assertEquals(NAVIGATION_ID, configuration.last().id)
    }

    @Test
    fun `WHEN building the browser menu THEN don't show the items only meant for the home screen`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        assertTrue(configuration.none { DefaultBrowserBanner in it.items || CustomizeHomepage in it.items })
    }

    @Test
    fun `GIVEN the user customized the menu WHEN building the browser menu THEN show only the shown sections in the saved order`() {
        val customization = menuInfo(shown = listOf(GROUP_3_ID, GROUP_1_ID, GROUP_5_ID))
        val defaultSections =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(
            customization.visibleSectionIds.map { id -> defaultSections.first { it.id == id } },
            configuration,
        )
    }

    @Test
    fun `GIVEN the saved menu has sections no longer in the app WHEN building the browser menu THEN don't show them`() {
        val customization = menuInfo(shown = listOf(GROUP_1_ID, "removed", GROUP_5_ID))

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(listOf(GROUP_1_ID, GROUP_5_ID), configuration.map { it.id })
    }

    @Test
    fun `GIVEN the navigation block was saved first AND the toolbar is now at bottom WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val customization = menuInfo(shown = listOf(NAVIGATION_ID, GROUP_1_ID, GROUP_5_ID))

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(
            listOf(GROUP_1_ID, GROUP_5_ID, NAVIGATION_ID),
            configuration.map { it.id },
        )
    }

    @Test
    fun `GIVEN the navigation block was saved last AND the toolbar is now at top WHEN building the browser menu THEN navigation block appears at the top`() {
        val customization = menuInfo(shown = listOf(GROUP_1_ID, GROUP_5_ID, NAVIGATION_ID))

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(
            listOf(NAVIGATION_ID, GROUP_1_ID, GROUP_5_ID),
            configuration.map { it.id },
        )
    }

    @Test
    fun `GIVEN the navigation block was saved between other sections WHEN building the browser menu THEN keep it where the user placed it`() {
        val customization = menuInfo(shown = listOf(GROUP_1_ID, NAVIGATION_ID, GROUP_5_ID))

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(customization.visibleSectionIds, configuration.map { it.id })
    }

    @Test
    fun `GIVEN the user didn't customize the menu WHEN building the browser menu THEN show the same menu as for the default structure`() {
        listOf(true, false).forEach { isToolbarAtBottom ->
            val defaultMenuInfo =
                BrowserMenuConfiguration.defaultMenuInfo(
                    isToolbarAtBottom = isToolbarAtBottom,
                    isExpandedToolbarEnabled = false,
                )

            assertEquals(
                BrowserMenuConfiguration.sections(
                    isToolbarAtBottom = isToolbarAtBottom,
                    isExpandedToolbarEnabled = false,
                ),
                BrowserMenuConfiguration.sections(
                    isToolbarAtBottom = isToolbarAtBottom,
                    isExpandedToolbarEnabled = false,
                    customizationInfo = defaultMenuInfo,
                ),
            )
        }
    }

    @Test
    fun `GIVEN toolbar is at bottom WHEN getting the default browser menu structure THEN show all sections with the navigation block last`() {
        val defaultMenuInfo =
            BrowserMenuConfiguration.defaultMenuInfo(
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
            )

        assertEquals(
            BrowserMenuConfiguration.sections(isToolbarAtBottom = true, isExpandedToolbarEnabled = false).map { it.id },
            defaultMenuInfo.visibleSectionIds,
        )
        assertEquals(NAVIGATION_ID, defaultMenuInfo.visibleSectionIds.last())
        assertEquals(emptyList(), defaultMenuInfo.hiddenSectionIds)
    }

    @Test
    fun `GIVEN the user removed sections WHEN getting the hidden browser menu sections THEN return them in the saved order`() {
        val customization =
            menuInfo(
                shown = listOf(GROUP_1_ID, GROUP_5_ID),
                hidden = listOf(GROUP_4_ID, "removed", NAVIGATION_ID),
            )

        val hiddenSections = BrowserMenuConfiguration.hiddenSections(customization)

        assertEquals(listOf(GROUP_4_ID, NAVIGATION_ID), hiddenSections.map { it.id })
    }

    @Test
    fun `WHEN getting all the browser menu sections THEN return every section with the navigation block first`() {
        assertEquals(
            listOf(NAVIGATION_ID, GROUP_1_ID, GROUP_2_ID, GROUP_3_ID, GROUP_4_ID, GROUP_5_ID, GROUP_6_ID),
            BrowserMenuConfiguration.allSections().map { it.id },
        )
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)
}
