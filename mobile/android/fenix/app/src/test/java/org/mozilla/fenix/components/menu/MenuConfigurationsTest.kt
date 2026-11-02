/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertEquals
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.menu.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.FenixMenuItem.Bookmarks
import org.mozilla.fenix.components.menu.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.FenixMenuItem.DefaultBrowserBanner
import org.mozilla.fenix.components.menu.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.FenixMenuItem.Downloads
import org.mozilla.fenix.components.menu.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.FenixMenuItem.History
import org.mozilla.fenix.components.menu.FenixMenuItem.IPProtection
import org.mozilla.fenix.components.menu.FenixMenuItem.MozillaAccount
import org.mozilla.fenix.components.menu.FenixMenuItem.OpenInFirefox
import org.mozilla.fenix.components.menu.FenixMenuItem.Passwords
import org.mozilla.fenix.components.menu.FenixMenuItem.Quit
import org.mozilla.fenix.components.menu.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.FenixMenuItem.Translate
import org.mozilla.fenix.components.menu.MenuConfigurations.BROWSER_MENU_GROUP_1_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.BROWSER_MENU_GROUP_3_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.BROWSER_MENU_GROUP_4_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.BROWSER_MENU_GROUP_5_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.BROWSER_MENU_NAVIGATION_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.CUSTOMIZE_MENU_NAVIGATION_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.CUSTOM_TAB_MENU_GROUP_1_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.HOME_MENU_GROUP_1_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.HOME_MENU_GROUP_2_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.HOME_MENU_GROUP_3_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.HOME_MENU_GROUP_4_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.HOME_MENU_GROUP_5_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.HOME_MENU_GROUP_6_ID
import org.mozilla.fenix.components.menu.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.MenuPresentationMode.Row

class MenuConfigurationsTest {
    @Test
    fun `GIVEN toolbar is at bottom WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val configuration = MenuConfigurations.browser(isToolbarAtBottom = true, isExpandedToolbarEnabled = false)

        assertEquals(BROWSER_MENU_NAVIGATION_ID, configuration.last().id)
    }

    @Test
    fun `GIVEN toolbar is at top WHEN building the browser menu THEN navigation block appears at the top`() {
        val configuration = MenuConfigurations.browser(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        assertEquals(BROWSER_MENU_NAVIGATION_ID, configuration.first().id)
    }

    @Test
    fun `WHEN building the customize menu THEN navigation block appears at the top`() {
        val configuration = MenuConfigurations.customizeMenu()

        assertEquals(CUSTOMIZE_MENU_NAVIGATION_ID, configuration.first().id)
    }

    @Test
    fun `GIVEN toolbar is expanded WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val configuration = MenuConfigurations.browser(isToolbarAtBottom = false, isExpandedToolbarEnabled = true)

        assertEquals(BROWSER_MENU_NAVIGATION_ID, configuration.last().id)
    }

    @Test
    fun `WHEN building the browser menu THEN don't show the items only meant for the home screen`() {
        val configuration = MenuConfigurations.browser(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        assertTrue(configuration.none { DefaultBrowserBanner in it.items || CustomizeHomepage in it.items })
    }

    @Test
    fun `GIVEN the user customized the menu WHEN building the browser menu THEN show only the shown sections in the saved order`() {
        val customization =
            menuInfo(shown = listOf(BROWSER_MENU_GROUP_3_ID, BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_GROUP_5_ID))
        val defaultSections = MenuConfigurations.browser(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        val configuration =
            MenuConfigurations.browser(
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
        val customization = menuInfo(shown = listOf(BROWSER_MENU_GROUP_1_ID, "removed", BROWSER_MENU_GROUP_5_ID))

        val configuration =
            MenuConfigurations.browser(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(listOf(BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_GROUP_5_ID), configuration.map { it.id })
    }

    @Test
    fun `GIVEN the navigation block was saved first AND the toolbar is now at bottom WHEN building the browser menu THEN navigation block appears at the bottom`() {
        val customization =
            menuInfo(shown = listOf(BROWSER_MENU_NAVIGATION_ID, BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_GROUP_5_ID))

        val configuration =
            MenuConfigurations.browser(
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(
            listOf(BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_GROUP_5_ID, BROWSER_MENU_NAVIGATION_ID),
            configuration.map { it.id },
        )
    }

    @Test
    fun `GIVEN the navigation block was saved last AND the toolbar is now at top WHEN building the browser menu THEN navigation block appears at the top`() {
        val customization =
            menuInfo(shown = listOf(BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_GROUP_5_ID, BROWSER_MENU_NAVIGATION_ID))

        val configuration =
            MenuConfigurations.browser(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizationInfo = customization,
            )

        assertEquals(
            listOf(BROWSER_MENU_NAVIGATION_ID, BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_GROUP_5_ID),
            configuration.map { it.id },
        )
    }

    @Test
    fun `GIVEN the navigation block was saved between other sections WHEN building the browser menu THEN keep it where the user placed it`() {
        val customization =
            menuInfo(shown = listOf(BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_NAVIGATION_ID, BROWSER_MENU_GROUP_5_ID))

        val configuration =
            MenuConfigurations.browser(
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
                MenuConfigurations.browserDefaultMenuInfo(
                    isToolbarAtBottom = isToolbarAtBottom,
                    isExpandedToolbarEnabled = false,
                )

            assertEquals(
                MenuConfigurations.browser(isToolbarAtBottom = isToolbarAtBottom, isExpandedToolbarEnabled = false),
                MenuConfigurations.browser(
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
            MenuConfigurations.browserDefaultMenuInfo(
                isToolbarAtBottom = true,
                isExpandedToolbarEnabled = false,
            )

        assertEquals(
            MenuConfigurations.browser(isToolbarAtBottom = true, isExpandedToolbarEnabled = false).map { it.id },
            defaultMenuInfo.visibleSectionIds,
        )
        assertEquals(BROWSER_MENU_NAVIGATION_ID, defaultMenuInfo.visibleSectionIds.last())
        assertEquals(emptyList(), defaultMenuInfo.hiddenSectionIds)
    }

    @Test
    fun `GIVEN the user removed sections WHEN getting the hidden browser menu sections THEN return them in the saved order`() {
        val customization =
            menuInfo(
                shown = listOf(BROWSER_MENU_GROUP_1_ID, BROWSER_MENU_GROUP_5_ID),
                hidden = listOf(BROWSER_MENU_GROUP_4_ID, "removed", BROWSER_MENU_NAVIGATION_ID),
            )

        val hiddenSections = MenuConfigurations.browserHiddenSections(customization)

        assertEquals(listOf(BROWSER_MENU_GROUP_4_ID, BROWSER_MENU_NAVIGATION_ID), hiddenSections.map { it.id })
    }

    @Test
    fun `WHEN building the home menu THEN show only what needs no page, grouped as before`() {
        assertEquals(
            listOf(
                section(HOME_MENU_GROUP_1_ID, Row, DefaultBrowserBanner),
                section(HOME_MENU_GROUP_2_ID, Row, IPProtection),
                section(HOME_MENU_GROUP_3_ID, Row, Extensions),
                section(HOME_MENU_GROUP_4_ID, Grid, History, Bookmarks, Downloads, Passwords),
                section(HOME_MENU_GROUP_5_ID, Row, MozillaAccount, CustomizeHomepage, Settings),
                section(HOME_MENU_GROUP_6_ID, Row, Quit),
            ),
            MenuConfigurations.home(),
        )
    }

    @Test
    fun `WHEN building the custom tab menu AND extensions are enabled THEN show items for custom tab including Extensions under DesktopSite`() {
        val configuration = MenuConfigurations.customTab(isToolbarAtBottom = false, shouldShowExtensions = true)

        val group1 = configuration.first { it.id == CUSTOM_TAB_MENU_GROUP_1_ID }
        assertEquals(
            listOf(OpenInFirefox, Bookmark, FindInPage, DesktopSite, Extensions, Translate, SummarizePage),
            group1.items,
        )
    }

    @Test
    fun `WHEN building the custom tab menu AND extensions are disabled THEN show items for custom tab excluding Extensions`() {
        val configuration = MenuConfigurations.customTab(isToolbarAtBottom = false, shouldShowExtensions = false)

        val group1 = configuration.first { it.id == CUSTOM_TAB_MENU_GROUP_1_ID }
        assertEquals(
            listOf(OpenInFirefox, Bookmark, FindInPage, DesktopSite, Translate, SummarizePage),
            group1.items,
        )
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)

    private fun section(
        id: String,
        presentationMode: MenuPresentationMode,
        vararg items: FenixMenuItem,
    ) =
        MenuSectionConfiguration(
            id = id,
            presentationMode = presentationMode,
            items = items.toList(),
        )
}
