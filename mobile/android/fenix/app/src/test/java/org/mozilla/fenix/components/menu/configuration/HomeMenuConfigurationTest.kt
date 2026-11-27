/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.CustomizedItemInfo
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
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
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
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

    @Test
    fun `GIVEN a downloads notification WHEN checking highlight THEN highlight by default and stop when library is hidden`() {
        val notifications = setOf(SupportedMenuNotifications.Downloads)
        val libraryHidden =
            menuInfo(
                shown = listOf(TOOLS_GROUP_ID, BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID),
                hidden = listOf(BrowserMenuConfiguration.LIBRARY_GROUP_ID),
            )

        assertTrue(HomeMenuConfiguration.hasNotificationToShow(notifications, customizationInfo = null))
        assertFalse(HomeMenuConfiguration.hasNotificationToShow(notifications, customizationInfo = libraryHidden))
    }

    @Test
    fun `GIVEN an extension update notification WHEN checking highlight THEN highlight by default and stop when tools is hidden`() {
        val notifications = setOf(SupportedMenuNotifications.ExtensionUpdate)
        val toolsHidden =
            menuInfo(
                shown =
                    listOf(
                        BrowserMenuConfiguration.LIBRARY_GROUP_ID,
                        BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    ),
                hidden = listOf(TOOLS_GROUP_ID),
            )

        assertTrue(HomeMenuConfiguration.hasNotificationToShow(notifications, customizationInfo = null))
        assertFalse(HomeMenuConfiguration.hasNotificationToShow(notifications, customizationInfo = toolsHidden))
    }

    @Test
    fun `GIVEN a default browser notification WHEN checking highlight THEN always highlight`() {
        val notifications = setOf(SupportedMenuNotifications.NotDefaultBrowser)

        assertTrue(HomeMenuConfiguration.hasNotificationToShow(notifications, customizationInfo = null))
        assertTrue(
            HomeMenuConfiguration.hasNotificationToShow(notifications, customizationInfo = menuInfo(emptyList()))
        )
    }

    @Test
    fun `GIVEN browser-only notifications WHEN checking highlight THEN never highlight`() {
        val notifications =
            listOf(
                SupportedMenuNotifications.Summarize,
                SupportedMenuNotifications.TabGroups,
                SupportedMenuNotifications.OpenInApp,
            )

        notifications.forEach { notification ->
            assertFalse(HomeMenuConfiguration.hasNotificationToShow(setOf(notification), customizationInfo = null))
        }
    }

    @Test
    fun `GIVEN extensions is hidden in browser tools WHEN building the home menu THEN hide extensions and keep all other sections`() {
        val sectionsInfo = toolsSectionInfoWithoutExtensions()
        val expected = HomeMenuConfiguration.sections().filterNot { it.id == EXTENSIONS_GROUP_ID }
        val toolsVisibleInfo =
            BrowserMenuConfiguration.defaultMenuInfo(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        assertEquals(expected, HomeMenuConfiguration.sections(customizedSectionsInfo = sectionsInfo))
        assertEquals(
            expected,
            HomeMenuConfiguration.sections(
                customizationInfo = toolsVisibleInfo,
                customizedSectionsInfo = sectionsInfo,
            ),
        )
    }

    @Test
    fun `GIVEN extensions is kept or reordered in browser tools WHEN building the home menu THEN keep the extensions section`() {
        val defaultToolsInfo = BrowserMenuConfiguration.defaultSectionsInfo().getValue(TOOLS_GROUP_ID)
        val reorderedToolsInfo = defaultToolsInfo.copy(items = defaultToolsInfo.items.reversed())

        assertEquals(
            HomeMenuConfiguration.sections(),
            HomeMenuConfiguration.sections(customizedSectionsInfo = mapOf(TOOLS_GROUP_ID to defaultToolsInfo)),
        )
        assertEquals(
            HomeMenuConfiguration.sections(),
            HomeMenuConfiguration.sections(customizedSectionsInfo = mapOf(TOOLS_GROUP_ID to reorderedToolsInfo)),
        )
        assertEquals(
            HomeMenuConfiguration.sections(),
            HomeMenuConfiguration.sections(customizedSectionsInfo = emptyMap()),
        )
    }

    @Test
    fun `GIVEN extensions is only inside the browser tools more item WHEN building the home menu THEN keep the extensions section`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items = listOf(CustomizedItemInfo(More(emptyList()).id, subItemIds = listOf(Extensions.id))),
                hiddenItems = emptyList(),
            )

        assertEquals(
            HomeMenuConfiguration.sections(),
            HomeMenuConfiguration.sections(customizedSectionsInfo = mapOf(TOOLS_GROUP_ID to toolsInfo)),
        )
    }

    @Test
    fun `GIVEN browser tools is hidden even when its items keep extensions WHEN building the home menu THEN hide extensions`() {
        val toolsHidden =
            menuInfo(
                shown =
                    listOf(
                        BrowserMenuConfiguration.LIBRARY_GROUP_ID,
                        BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    ),
                hidden = listOf(TOOLS_GROUP_ID),
            )
        val sectionsInfo = BrowserMenuConfiguration.defaultSectionsInfo()

        val result =
            HomeMenuConfiguration.sections(
                customizationInfo = toolsHidden,
                customizedSectionsInfo = sectionsInfo,
            )

        assertFalse(result.any { it.id == EXTENSIONS_GROUP_ID })
    }

    @Test
    fun `GIVEN an extension update notification WHEN checking highlight with customized tools items THEN follow whether extensions is shown`() {
        val notifications = setOf(SupportedMenuNotifications.ExtensionUpdate)
        val extensionsHidden = toolsSectionInfoWithoutExtensions()
        val extensionsKept = BrowserMenuConfiguration.defaultSectionsInfo()
        val toolsHidden =
            menuInfo(
                shown =
                    listOf(
                        BrowserMenuConfiguration.LIBRARY_GROUP_ID,
                        BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID,
                    ),
                hidden = listOf(TOOLS_GROUP_ID),
            )

        assertFalse(
            HomeMenuConfiguration.hasNotificationToShow(
                notifications = notifications,
                customizationInfo = null,
                customizedSectionsInfo = extensionsHidden,
            )
        )
        assertTrue(
            HomeMenuConfiguration.hasNotificationToShow(
                notifications = notifications,
                customizationInfo = null,
                customizedSectionsInfo = extensionsKept,
            )
        )
        assertFalse(
            HomeMenuConfiguration.hasNotificationToShow(
                notifications = notifications,
                customizationInfo = toolsHidden,
                customizedSectionsInfo = extensionsKept,
            )
        )
    }

    @Test
    fun `GIVEN extension update and downloads notifications WHEN only extensions is hidden THEN still highlight for downloads`() {
        val notifications = setOf(SupportedMenuNotifications.ExtensionUpdate, SupportedMenuNotifications.Downloads)

        assertTrue(
            HomeMenuConfiguration.hasNotificationToShow(
                notifications = notifications,
                customizationInfo = null,
                customizedSectionsInfo = toolsSectionInfoWithoutExtensions(),
            )
        )
    }

    private fun toolsSectionInfoWithoutExtensions(): Map<String, CustomizedSectionInfo> {
        val defaultTools = BrowserMenuConfiguration.defaultSectionsInfo().getValue(TOOLS_GROUP_ID)
        val (extensionsItems, otherItems) = defaultTools.items.partition { it.id == Extensions.id }
        return mapOf(
            TOOLS_GROUP_ID to
                defaultTools.copy(
                    items = otherItems,
                    hiddenItems = defaultTools.hiddenItems + extensionsItems,
                )
        )
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)

    private fun section(id: String, presentationMode: MenuPresentationMode, vararg items: FenixMenuItem) =
        MenuSectionConfiguration(id = id, presentationMode = presentationMode, items = items.toList())
}
