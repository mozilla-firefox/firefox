/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertTrue
import org.junit.Test
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.SupportedMenuNotifications
import org.mozilla.fenix.components.menu.CustomizedItemInfo
import org.mozilla.fenix.components.menu.CustomizedMenuInfo
import org.mozilla.fenix.components.menu.CustomizedSectionInfo
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.ACCOUNTS_AND_SETTINGS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.LIBRARY_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.PAGE_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.QUIT_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.READER_VIEW_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TABS_CONTROLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.TOOLS_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.BrowserMenuConfiguration.VPN_GROUP_ID
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Back
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DefaultBrowserBanner
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Forward
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.More
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.NewPrivateTab
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.NewTab
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.OpenInApp
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Print
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Refresh
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Share
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Translate
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
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
    fun `WHEN checking the page controls section THEN it contains navigation and bookmark items in Grid mode`() {
        val pageControlsSection = BrowserMenuConfiguration.allSections().first { it.id == PAGE_CONTROLS_GROUP_ID }
        assertEquals(Grid, pageControlsSection.presentationMode)
        assertEquals(listOf(Back, Forward, Bookmark, Share, Refresh), pageControlsSection.items)
        assertTrue(pageControlsSection.isSticky)
        assertFalse(pageControlsSection.showTitles)
    }

    @Test
    fun `WHEN getting all the browser menu sections THEN only the page controls hide the titles of their items`() {
        val sectionsHidingTitles = BrowserMenuConfiguration.allSections().filterNot { it.showTitles }.map { it.id }

        assertEquals(listOf(PAGE_CONTROLS_GROUP_ID), sectionsHidingTitles)
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

    @Test
    fun `GIVEN no customization WHEN checking notifications THEN return whether item is shown by default`() {
        assertFalse(
            BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.NotDefaultBrowser), null)
        )
        assertTrue(BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.Downloads), null))
        assertTrue(BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.OpenInApp), null))
        assertTrue(BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.Summarize), null))
        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.ExtensionUpdate), null)
        )
    }

    @Test
    fun `GIVEN the user customized the tools items WHEN building the browser menu THEN show the shown ones in the saved order`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items =
                    listOf(
                        CustomizedItemInfo(Translate.id),
                        CustomizedItemInfo("more", subItemIds = listOf(Print.id, FenixMenuItem.SummarizePage.id)),
                        CustomizedItemInfo(FindInPage.id),
                    ),
                hiddenItems = listOf(CustomizedItemInfo(DesktopSite.id), CustomizedItemInfo(Extensions.id)),
            )

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizedSectionsInfo = mapOf(TOOLS_GROUP_ID to toolsInfo),
            )

        assertEquals(
            listOf(Translate, More(listOf(Print, FenixMenuItem.SummarizePage)), FindInPage),
            configuration.first { it.id == TOOLS_GROUP_ID }.items,
        )
    }

    @Test
    fun `GIVEN customization info WHEN checking notifications THEN check visible section ids`() {
        val customization = menuInfo(shown = listOf(TOOLS_GROUP_ID))

        assertFalse(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.NotDefaultBrowser),
                customization,
            )
        )
        assertFalse(
            BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.Downloads), customization)
        )
        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.OpenInApp), customization)
        )
        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(listOf(SupportedMenuNotifications.Summarize), customization)
        )
        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.ExtensionUpdate),
                customization,
            )
        )
    }

    @Test
    fun `GIVEN the user hid tools items WHEN checking notifications THEN only the items still shown count`() {
        val sectionsInfo =
            mapOf(
                TOOLS_GROUP_ID to
                    CustomizedSectionInfo(
                        sectionId = TOOLS_GROUP_ID,
                        items =
                            listOf(
                                CustomizedItemInfo(FindInPage.id),
                                CustomizedItemInfo("more", subItemIds = listOf(FenixMenuItem.SummarizePage.id)),
                            ),
                        hiddenItems =
                            listOf(CustomizedItemInfo(Extensions.id), CustomizedItemInfo(FenixMenuItem.OpenInApp.id)),
                    )
            )
        val customization = menuInfo(shown = listOf(TOOLS_GROUP_ID))

        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.Summarize),
                customization,
                sectionsInfo,
            )
        )
        assertFalse(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.ExtensionUpdate),
                customization,
                sectionsInfo,
            )
        )
        assertFalse(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.OpenInApp),
                customization,
                sectionsInfo,
            )
        )
    }

    @Test
    fun `GIVEN the Extensions item is hidden WHEN an extension update is pending THEN the menu button is not highlighted`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items = listOf(CustomizedItemInfo(Bookmark.id)),
                hiddenItems = listOf(CustomizedItemInfo(Extensions.id)),
            )
        val itemsInfo = mapOf(TOOLS_GROUP_ID to toolsInfo)

        assertFalse(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.ExtensionUpdate),
                customizationInfo = null,
                customizedSectionsInfo = itemsInfo,
            )
        )
    }

    @Test
    fun `GIVEN the Extensions item and Open in app inside More are shown WHEN either has a notification THEN the menu button is highlighted`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items = listOf(CustomizedItemInfo(Extensions.id), CustomizedItemInfo("more", listOf(OpenInApp.id))),
                hiddenItems = listOf(CustomizedItemInfo(Bookmark.id)),
            )
        val itemsInfo = mapOf(TOOLS_GROUP_ID to toolsInfo)

        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.ExtensionUpdate),
                customizationInfo = null,
                customizedSectionsInfo = itemsInfo,
            )
        )
        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.OpenInApp),
                customizationInfo = null,
                customizedSectionsInfo = itemsInfo,
            )
        )
    }

    @Test
    fun `GIVEN More is hidden WHEN items inside it have notifications THEN the menu button is not highlighted`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items = listOf(CustomizedItemInfo(Bookmark.id)),
                hiddenItems = listOf(CustomizedItemInfo("more", listOf(OpenInApp.id, SummarizePage.id))),
            )

        assertFalse(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.OpenInApp, SupportedMenuNotifications.Summarize),
                customizationInfo = null,
                customizedSectionsInfo = mapOf(TOOLS_GROUP_ID to toolsInfo),
            )
        )
    }

    @Test
    fun `GIVEN only the Tools items are customized WHEN Downloads has a notification THEN the menu button is highlighted`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items = listOf(CustomizedItemInfo(Bookmark.id)),
                hiddenItems = emptyList(),
            )

        assertTrue(
            BrowserMenuConfiguration.hasNotificationToShow(
                listOf(SupportedMenuNotifications.Downloads),
                customizationInfo = null,
                customizedSectionsInfo = mapOf(TOOLS_GROUP_ID to toolsInfo),
            )
        )
    }

    @Test
    fun `GIVEN the user customized the tools items WHEN building the browser menu THEN show the other sections with their default items`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items = listOf(CustomizedItemInfo(FindInPage.id)),
                hiddenItems = emptyList(),
            )
        val defaultSections =
            BrowserMenuConfiguration.sections(isToolbarAtBottom = false, isExpandedToolbarEnabled = false)

        val configuration =
            BrowserMenuConfiguration.sections(
                isToolbarAtBottom = false,
                isExpandedToolbarEnabled = false,
                customizedSectionsInfo = mapOf(TOOLS_GROUP_ID to toolsInfo),
            )

        assertEquals(
            defaultSections.filterNot { it.id == TOOLS_GROUP_ID },
            configuration.filterNot { it.id == TOOLS_GROUP_ID },
        )
    }

    @Test
    fun `GIVEN the user customized the tools items WHEN getting all the browser menu sections THEN the tools section has the saved items and the others are unchanged`() {
        val toolsInfo =
            CustomizedSectionInfo(
                sectionId = TOOLS_GROUP_ID,
                items =
                    listOf(
                        CustomizedItemInfo(Translate.id),
                        CustomizedItemInfo("more", subItemIds = listOf(Print.id, FenixMenuItem.SummarizePage.id)),
                        CustomizedItemInfo(FindInPage.id),
                    ),
                hiddenItems = listOf(CustomizedItemInfo(DesktopSite.id), CustomizedItemInfo(Extensions.id)),
            )

        val sections = BrowserMenuConfiguration.allSections(mapOf(TOOLS_GROUP_ID to toolsInfo))

        assertEquals(
            listOf(Translate, More(listOf(Print, FenixMenuItem.SummarizePage)), FindInPage),
            sections.first { it.id == TOOLS_GROUP_ID }.items,
        )
        assertEquals(
            BrowserMenuConfiguration.allSections().filterNot { it.id == TOOLS_GROUP_ID },
            sections.filterNot { it.id == TOOLS_GROUP_ID },
        )
    }

    @Test
    fun `WHEN getting the default items of the sections THEN return only the tools items with the ids they are saved with`() {
        assertEquals(
            mapOf(
                TOOLS_GROUP_ID to
                    CustomizedSectionInfo(
                        sectionId = TOOLS_GROUP_ID,
                        items =
                            listOf(
                                CustomizedItemInfo("find_in_page"),
                                CustomizedItemInfo("desktop_site"),
                                CustomizedItemInfo("extensions"),
                                CustomizedItemInfo(
                                    id = "more",
                                    subItemIds =
                                        listOf(
                                            "translate",
                                            "summarize_page",
                                            "move_to_normal_tabs",
                                            "report_broken_site",
                                            "shortcut",
                                            "add_to_home_screen",
                                            "tab_group",
                                            "open_in_app",
                                            "save_as_pdf",
                                            "print",
                                        ),
                                ),
                            ),
                        hiddenItems = emptyList(),
                    )
            ),
            BrowserMenuConfiguration.defaultSectionsInfo(),
        )
    }

    @Test
    fun `WHEN getting the items which can only be directly in their section THEN only extensions is returned`() {
        assertEquals(setOf(Extensions.id), BrowserMenuConfiguration.topLevelOnlyItemIds())
    }

    @Test
    fun `WHEN getting the default items of the sections THEN none which can only be directly in their section is inside another item`() {
        val topLevelOnlyIds = BrowserMenuConfiguration.topLevelOnlyItemIds()
        val subItemIds =
            BrowserMenuConfiguration.defaultSectionsInfo()
                .values
                .flatMap { it.items + it.hiddenItems }
                .flatMap { it.subItemIds }

        assertEquals(emptyList(), subItemIds.filter { it in topLevelOnlyIds })
    }

    private fun menuInfo(shown: List<String>, hidden: List<String> = emptyList()) =
        CustomizedMenuInfo(visibleSectionIds = shown, hiddenSectionIds = hidden)

    @Test
    fun `WHEN getting all the browser menu sections THEN page controls, tools, library, accounts and settings and new tab are named`() {
        val names = BrowserMenuConfiguration.allSections().associate { it.id to it.name }

        assertEquals(
            mapOf(
                PAGE_CONTROLS_GROUP_ID to R.string.browser_menu_section_page_controls,
                READER_VIEW_GROUP_ID to null,
                VPN_GROUP_ID to null,
                TOOLS_GROUP_ID to R.string.browser_menu_section_tools,
                LIBRARY_GROUP_ID to R.string.browser_menu_section_library,
                ACCOUNTS_AND_SETTINGS_GROUP_ID to R.string.browser_menu_section_accounts_and_settings,
                QUIT_GROUP_ID to null,
                TABS_CONTROLS_GROUP_ID to R.string.browser_menu_section_new_tab,
            ),
            names,
        )
    }
}
