/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import kotlin.test.assertEquals
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.menu.FenixMenuItem.AddToHomeScreen
import org.mozilla.fenix.components.menu.FenixMenuItem.Back
import org.mozilla.fenix.components.menu.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.FenixMenuItem.Bookmarks
import org.mozilla.fenix.components.menu.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.FenixMenuItem.DefaultBrowserBanner
import org.mozilla.fenix.components.menu.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.FenixMenuItem.Downloads
import org.mozilla.fenix.components.menu.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.FenixMenuItem.Forward
import org.mozilla.fenix.components.menu.FenixMenuItem.History
import org.mozilla.fenix.components.menu.FenixMenuItem.IPProtection
import org.mozilla.fenix.components.menu.FenixMenuItem.MozillaAccount
import org.mozilla.fenix.components.menu.FenixMenuItem.OpenInFirefox
import org.mozilla.fenix.components.menu.FenixMenuItem.Passwords
import org.mozilla.fenix.components.menu.FenixMenuItem.Quit
import org.mozilla.fenix.components.menu.FenixMenuItem.Refresh
import org.mozilla.fenix.components.menu.FenixMenuItem.SaveToCollection
import org.mozilla.fenix.components.menu.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.FenixMenuItem.Share
import org.mozilla.fenix.components.menu.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.FenixMenuItem.Translate
import org.mozilla.fenix.components.menu.MenuConfigurations.BROWSER_MENU_NAVIGATION_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.CUSTOMIZE_MENU_GROUP_2_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.CUSTOMIZE_MENU_GROUP_3_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.CUSTOMIZE_MENU_GROUP_4_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.CUSTOMIZE_MENU_GROUP_5_ID
import org.mozilla.fenix.components.menu.MenuConfigurations.CUSTOMIZE_MENU_GROUP_6_ID
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
    fun `WHEN building the customize menu THEN show configured items grouped as expected`() {
        assertEquals(
            listOf(
                MenuSectionConfiguration(
                    id = CUSTOMIZE_MENU_NAVIGATION_ID,
                    presentationMode = Grid,
                    items = listOf(Back, Forward, Share, Refresh),
                    isSticky = true,
                ),
                section(CUSTOMIZE_MENU_GROUP_2_ID, Row, IPProtection),
                section(CUSTOMIZE_MENU_GROUP_3_ID, Row, Bookmark, FindInPage, DesktopSite, Translate, SummarizePage),
                section(CUSTOMIZE_MENU_GROUP_4_ID, Grid, History, Bookmarks, Downloads, Passwords),
                section(CUSTOMIZE_MENU_GROUP_5_ID, Row, MozillaAccount, Settings),
                section(CUSTOMIZE_MENU_GROUP_6_ID, Row, AddToHomeScreen, SaveToCollection),
            ),
            MenuConfigurations.customizeMenu(),
        )
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
