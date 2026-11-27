/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import org.junit.Test
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.PAGE_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.READER_VIEW_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TABS_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmarks
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DefaultBrowserBanner
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Downloads
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.History
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.IPProtection
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.MozillaAccount
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Passwords
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Quit
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.EXTENSIONS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.LIBRARY_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.PROMO_BANNER_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.QUIT_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.VPN_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

class HomeMenuConfigurationTest {
    @Test
    fun `WHEN building the home menu THEN show only what needs no page, grouped as before`() {
        assertEquals(
            listOf(
                section(PROMO_BANNER_GROUP_ID, Row, DefaultBrowserBanner),
                section(VPN_GROUP_ID, Row, IPProtection),
                section(EXTENSIONS_GROUP_ID, Row, Extensions),
                section(LIBRARY_GROUP_ID, Grid, History, Bookmarks, Downloads, Passwords),
                section(ACCOUNTS_AND_SETTINGS_GROUP_ID, Row, MozillaAccount, CustomizeHomepage, Settings),
                section(QUIT_GROUP_ID, Row, Quit),
            ),
            HomeMenuConfiguration.sections(),
        )
    }

    @Test
    fun `GIVEN no customization or the default browser menu structure WHEN building the home menu THEN show the same sections`() {
        val layouts =
            listOf(
                false to false,
                true to false,
                false to true,
            )
        layouts.forEach { (isToolbarAtBottom, isExpandedToolbarEnabled) ->
            val defaultBrowserInfo =
                BrowserMenuConfiguration.defaultMenuInfo(
                    isToolbarAtBottom = isToolbarAtBottom,
                    isExpandedToolbarEnabled = isExpandedToolbarEnabled,
                )

            assertEquals(
                HomeMenuConfiguration.sections(),
                HomeMenuConfiguration.sections(customizationInfo = defaultBrowserInfo),
            )
        }
    }

    @Test
    fun `GIVEN the browser menu sections were reordered WHEN building the home menu THEN follow that order after the promo banner`() {
        val customization =
            menuInfo(
                shown =
                    listOf(
                        BrowserMenuConfiguration.LIBRARY_GROUP_ID,
                        BrowserMenuConfiguration.VPN_GROUP_ID,
                        TOOLS_GROUP_ID,
                        BrowserMenuConfiguration.QUIT_GROUP_ID,
                        BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    )
            )

        val result = HomeMenuConfiguration.sections(customizationInfo = customization)

        assertEquals(
            listOf(
                PROMO_BANNER_GROUP_ID,
                LIBRARY_GROUP_ID,
                VPN_GROUP_ID,
                EXTENSIONS_GROUP_ID,
                QUIT_GROUP_ID,
                ACCOUNTS_AND_SETTINGS_GROUP_ID,
            ),
            result.map { it.id },
        )
    }

    @Test
    fun `GIVEN a browser menu section is hidden WHEN building the home menu THEN hide its home counterpart and keep the promo banner first`() {
        val defaultVisible =
            listOf(
                BrowserMenuConfiguration.VPN_GROUP_ID,
                TOOLS_GROUP_ID,
                BrowserMenuConfiguration.LIBRARY_GROUP_ID,
                BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID,
                BrowserMenuConfiguration.QUIT_GROUP_ID,
            )
        val hiddenPairs =
            listOf(
                BrowserMenuConfiguration.VPN_GROUP_ID to VPN_GROUP_ID,
                TOOLS_GROUP_ID to EXTENSIONS_GROUP_ID,
                BrowserMenuConfiguration.LIBRARY_GROUP_ID to LIBRARY_GROUP_ID,
                BrowserMenuConfiguration.QUIT_GROUP_ID to QUIT_GROUP_ID,
            )

        hiddenPairs.forEach { (browserId, homeId) ->
            val customization =
                menuInfo(
                    shown = defaultVisible - browserId,
                    hidden = listOf(browserId),
                )

            val result = HomeMenuConfiguration.sections(customizationInfo = customization)

            assertEquals(PROMO_BANNER_GROUP_ID, result.first().id)
            assertEquals(
                HomeMenuConfiguration.sections().filterNot { it.id == homeId },
                result,
            )
        }
    }

    @Test
    fun `GIVEN browser-only or unknown section ids in the visible list WHEN building the home menu THEN ignore them`() {
        val customization =
            menuInfo(
                shown =
                    listOf(
                        PAGE_CONTROLS_GROUP_ID,
                        READER_VIEW_GROUP_ID,
                        "unknown",
                        BrowserMenuConfiguration.LIBRARY_GROUP_ID,
                        TABS_CONTROLS_GROUP_ID,
                        BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    )
            )

        val result = HomeMenuConfiguration.sections(customizationInfo = customization)

        assertEquals(
            listOf(PROMO_BANNER_GROUP_ID, LIBRARY_GROUP_ID, ACCOUNTS_AND_SETTINGS_GROUP_ID),
            result.map { it.id },
        )
    }

    @Test
    fun `GIVEN an empty visible list WHEN building the home menu THEN show only the promo banner`() {
        val result = HomeMenuConfiguration.sections(customizationInfo = menuInfo(shown = emptyList()))

        assertEquals(listOf(PROMO_BANNER_GROUP_ID), result.map { it.id })
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)

    private fun section(id: String, presentationMode: MenuPresentationMode, vararg items: FenixMenuItem) =
        MenuSectionConfiguration(id = id, presentationMode = presentationMode, items = items.toList())
}
