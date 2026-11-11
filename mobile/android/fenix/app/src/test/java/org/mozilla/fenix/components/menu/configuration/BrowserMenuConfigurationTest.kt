/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.LIBRARY_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.PAGE_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.QUIT_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.READER_VIEW_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TABS_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.VPN_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DefaultBrowserBanner
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.NewPrivateTab
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.NewTab
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

class BrowserMenuConfigurationTest {
    @Test
    fun `GIVEN toolbar is at bottom WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = true, isExpandedToolbarEnabled = false)

        assertEquals(PAGE_CONTROLS_GROUP_ID, configuration.last().id)
    }

    @Test
    fun `GIVEN toolbar is at top WHEN building the browser menu THEN navigation block appears at the top`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        assertEquals(PAGE_CONTROLS_GROUP_ID, configuration.first().id)
    }

    @Test
    fun `GIVEN toolbar is expanded WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = true)

        assertEquals(PAGE_CONTROLS_GROUP_ID, configuration.last().id)
    }

    @Test
    fun `WHEN building the browser menu THEN don't show the items only meant for the home screen`() {
        val configuration =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        assertTrue(configuration.none { DefaultBrowserBanner in it.items || CustomizeHomepage in it.items })
    }

    @Test
    fun `GIVEN the user customized the menu WHEN building the browser menu THEN show only the shown sections in the saved order`() {
        val customization =
            menuInfo(shown = listOf(TOOLS_GROUP_ID, READER_VIEW_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID))
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
        val customization = menuInfo(shown = listOf(READER_VIEW_GROUP_ID, "removed", ACCOUNTS_AND_SETTINGS_GROUP_ID))

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(listOf(READER_VIEW_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID), configuration.map { it.id })
    }

    @Test
    fun `GIVEN the navigation block was saved first AND the toolbar is now at bottom WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val customization =
            menuInfo(shown = listOf(PAGE_CONTROLS_GROUP_ID, READER_VIEW_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID))

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(
            listOf(READER_VIEW_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID, PAGE_CONTROLS_GROUP_ID),
            configuration.map { it.id },
        )
    }

    @Test
    fun `GIVEN the navigation block was saved last AND the toolbar is now at top WHEN building the browser menu THEN navigation block appears at the top`() {
        val customization =
            menuInfo(shown = listOf(READER_VIEW_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID, PAGE_CONTROLS_GROUP_ID))

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(
            listOf(PAGE_CONTROLS_GROUP_ID, READER_VIEW_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
            configuration.map { it.id },
        )
    }

    @Test
    fun `GIVEN the user moved the navigation block WHEN building the browser menu THEN keep it where the user placed it`() {
        val customization =
            CustomizedMenuInfo(
                visibleSectionIds =
                    listOf(READER_VIEW_GROUP_ID, PAGE_CONTROLS_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hiddenSectionIds = emptyList(),
                isNavigationSectionMoved = true,
            )

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
        assertEquals(PAGE_CONTROLS_GROUP_ID, defaultMenuInfo.visibleSectionIds.last())
        assertEquals(listOf(TABS_CONTROLS_GROUP_ID), defaultMenuInfo.hiddenSectionIds)
    }

    @Test
    fun `GIVEN the user removed sections WHEN getting the hidden browser menu sections THEN return them in the saved order`() {
        val customization =
            menuInfo(
                shown = listOf(READER_VIEW_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(LIBRARY_GROUP_ID, "removed", PAGE_CONTROLS_GROUP_ID),
            )

        val hiddenSections = BrowserMenuConfiguration.hiddenSections(customization)

        assertEquals(listOf(LIBRARY_GROUP_ID, PAGE_CONTROLS_GROUP_ID), hiddenSections.map { it.id })
    }

    @Test
    fun `WHEN getting all the browser menu sections THEN return every section with the navigation block first`() {
        assertEquals(
            listOf(
                PAGE_CONTROLS_GROUP_ID,
                READER_VIEW_GROUP_ID,
                VPN_GROUP_ID,
                TOOLS_GROUP_ID,
                LIBRARY_GROUP_ID,
                ACCOUNTS_AND_SETTINGS_GROUP_ID,
                QUIT_GROUP_ID,
                TABS_CONTROLS_GROUP_ID,
            ),
            BrowserMenuConfiguration.allSections().map { it.id },
        )
    }

    @Test
    fun `WHEN checking the tabs controls section THEN it contains new tab items in Row mode and is hidden by default`() {
        val tabsControlsSection = BrowserMenuConfiguration.allSections().first { it.id == TABS_CONTROLS_GROUP_ID }
        assertEquals(Row, tabsControlsSection.presentationMode)
        assertEquals(listOf(NewTab, NewPrivateTab), tabsControlsSection.items)
        assertTrue(TABS_CONTROLS_GROUP_ID in BrowserMenuConfiguration.HIDDEN_BY_DEFAULT_SECTION_IDS)
    }

    @Test
    fun `GIVEN the toolbar is at the top WHEN the navigation section is first THEN it was not moved`() {
        assertFalse(
            BrowserMenuConfiguration.isNavigationSectionMoved(
                visibleSectionIds = listOf(PAGE_CONTROLS_GROUP_ID, READER_VIEW_GROUP_ID, VPN_GROUP_ID),
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
            )
        )
    }

    @Test
    fun `GIVEN the toolbar is at the top WHEN the navigation section is not first THEN it was moved`() {
        assertTrue(
            BrowserMenuConfiguration.isNavigationSectionMoved(
                visibleSectionIds = listOf(READER_VIEW_GROUP_ID, PAGE_CONTROLS_GROUP_ID, VPN_GROUP_ID),
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
            )
        )
    }

    @Test
    fun `GIVEN the toolbar is at the bottom WHEN the navigation section is last THEN it was not moved`() {
        assertFalse(
            BrowserMenuConfiguration.isNavigationSectionMoved(
                visibleSectionIds = listOf(READER_VIEW_GROUP_ID, VPN_GROUP_ID, PAGE_CONTROLS_GROUP_ID),
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
            )
        )
    }

    @Test
    fun `GIVEN the toolbar is at the bottom WHEN the navigation section is not last THEN it was moved`() {
        assertTrue(
            BrowserMenuConfiguration.isNavigationSectionMoved(
                visibleSectionIds = listOf(PAGE_CONTROLS_GROUP_ID, READER_VIEW_GROUP_ID, VPN_GROUP_ID),
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
            )
        )
    }

    @Test
    fun `GIVEN the expanded toolbar is used WHEN the navigation section is last THEN it was not moved`() {
        assertFalse(
            BrowserMenuConfiguration.isNavigationSectionMoved(
                visibleSectionIds = listOf(READER_VIEW_GROUP_ID, VPN_GROUP_ID, PAGE_CONTROLS_GROUP_ID),
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = true,
            )
        )
    }

    @Test
    fun `GIVEN the navigation section is not shown THEN it was not moved`() {
        assertFalse(
            BrowserMenuConfiguration.isNavigationSectionMoved(
                visibleSectionIds = listOf(READER_VIEW_GROUP_ID, VPN_GROUP_ID),
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
            )
        )
    }

    @Test
    fun `WHEN getting all the browser menu sections THEN their ids are the names recorded in telemetry`() {
        val ids = BrowserMenuConfiguration.allSections().map { it.id }

        assertEquals(
            listOf(
                "page_controls",
                "reader_view",
                "vpn",
                "tools",
                "library",
                "accounts_and_settings",
                "quit",
                "tabs_controls",
            ),
            ids,
        )
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)
}
