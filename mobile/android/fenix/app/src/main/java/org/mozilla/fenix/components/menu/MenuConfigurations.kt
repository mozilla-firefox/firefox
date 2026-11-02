/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu

import androidx.annotation.VisibleForTesting
import org.mozilla.fenix.components.menu.FenixMenuItem.AddToHomeScreen
import org.mozilla.fenix.components.menu.FenixMenuItem.Back
import org.mozilla.fenix.components.menu.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.FenixMenuItem.Bookmarks
import org.mozilla.fenix.components.menu.FenixMenuItem.CustomTabAppItems
import org.mozilla.fenix.components.menu.FenixMenuItem.CustomizeHomepage
import org.mozilla.fenix.components.menu.FenixMenuItem.CustomizeReaderView
import org.mozilla.fenix.components.menu.FenixMenuItem.DefaultBrowserBanner
import org.mozilla.fenix.components.menu.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.FenixMenuItem.Downloads
import org.mozilla.fenix.components.menu.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.FenixMenuItem.Forward
import org.mozilla.fenix.components.menu.FenixMenuItem.History
import org.mozilla.fenix.components.menu.FenixMenuItem.IPProtection
import org.mozilla.fenix.components.menu.FenixMenuItem.More
import org.mozilla.fenix.components.menu.FenixMenuItem.MoveToNormalTabs
import org.mozilla.fenix.components.menu.FenixMenuItem.MozillaAccount
import org.mozilla.fenix.components.menu.FenixMenuItem.OpenInApp
import org.mozilla.fenix.components.menu.FenixMenuItem.OpenInFirefox
import org.mozilla.fenix.components.menu.FenixMenuItem.Passwords
import org.mozilla.fenix.components.menu.FenixMenuItem.Print
import org.mozilla.fenix.components.menu.FenixMenuItem.Quit
import org.mozilla.fenix.components.menu.FenixMenuItem.Refresh
import org.mozilla.fenix.components.menu.FenixMenuItem.ReportBrokenSite
import org.mozilla.fenix.components.menu.FenixMenuItem.SaveAsPdf
import org.mozilla.fenix.components.menu.FenixMenuItem.SaveToCollection
import org.mozilla.fenix.components.menu.FenixMenuItem.Settings
import org.mozilla.fenix.components.menu.FenixMenuItem.Share
import org.mozilla.fenix.components.menu.FenixMenuItem.Shortcut
import org.mozilla.fenix.components.menu.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.FenixMenuItem.Translate
import org.mozilla.fenix.components.menu.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.MenuPresentationMode.Row

/** The sections of the menu shown on each screen, in the order they should be shown in. */
object MenuConfigurations {
    @VisibleForTesting internal val BROWSER_MENU_NAVIGATION_ID = "browser_navigation"
    @VisibleForTesting internal val BROWSER_MENU_GROUP_1_ID = "browser_group_1"
    @VisibleForTesting internal val BROWSER_MENU_GROUP_2_ID = "browser_group_2"
    @VisibleForTesting internal val BROWSER_MENU_GROUP_3_ID = "browser_group_3"
    @VisibleForTesting internal val BROWSER_MENU_GROUP_4_ID = "browser_group_4"
    @VisibleForTesting internal val BROWSER_MENU_GROUP_5_ID = "browser_group_5"
    @VisibleForTesting internal val BROWSER_MENU_GROUP_6_ID = "browser_group_6"
    @VisibleForTesting internal val HOME_MENU_GROUP_1_ID = "home_group_1"
    @VisibleForTesting internal val CUSTOM_TAB_MENU_NAVIGATION_ID = "custom_tab_navigation"
    @VisibleForTesting internal val CUSTOM_TAB_MENU_GROUP_1_ID = "custom_tab_group_1"
    @VisibleForTesting internal val CUSTOM_TAB_MENU_GROUP_2_ID = "custom_tab_group_2"
    @VisibleForTesting internal val HOME_MENU_GROUP_2_ID = "home_group_2"
    @VisibleForTesting internal val HOME_MENU_GROUP_3_ID = "home_group_3"
    @VisibleForTesting internal val HOME_MENU_GROUP_4_ID = "home_group_4"
    @VisibleForTesting internal val HOME_MENU_GROUP_5_ID = "home_group_5"
    @VisibleForTesting internal val HOME_MENU_GROUP_6_ID = "home_group_6"
    @VisibleForTesting internal val CUSTOMIZE_MENU_NAVIGATION_ID = "customize_menu_navigation"
    @VisibleForTesting internal val CUSTOMIZE_MENU_GROUP_2_ID = "customize_menu_group_2"
    @VisibleForTesting internal val CUSTOMIZE_MENU_GROUP_3_ID = "customize_menu_group_3"
    @VisibleForTesting internal val CUSTOMIZE_MENU_GROUP_4_ID = "customize_menu_group_4"
    @VisibleForTesting internal val CUSTOMIZE_MENU_GROUP_5_ID = "customize_menu_group_5"
    @VisibleForTesting internal val CUSTOMIZE_MENU_GROUP_6_ID = "customize_menu_group_6"

    /** The menu shown while browsing. */
    fun browser(
        isToolbarAtBottom: Boolean,
        isExpandedToolbarEnabled: Boolean,
    ): List<MenuSectionConfiguration> {
        val navSection =
            MenuSectionConfiguration(
                id = BROWSER_MENU_NAVIGATION_ID,
                presentationMode = Grid,
                items = listOf(Back, Forward, Share, Refresh),
                isSticky = true,
            )
        val rest =
            listOf(
                MenuSectionConfiguration(
                    id = BROWSER_MENU_GROUP_1_ID,
                    presentationMode = Row,
                    items = listOf(CustomizeReaderView),
                ),
                MenuSectionConfiguration(
                    id = BROWSER_MENU_GROUP_2_ID,
                    presentationMode = Row,
                    items = listOf(IPProtection),
                ),
                MenuSectionConfiguration(
                    id = BROWSER_MENU_GROUP_3_ID,
                    presentationMode = Row,
                    items =
                        listOf(
                            Bookmark,
                            FindInPage,
                            DesktopSite,
                            Extensions,
                            More(
                                listOf(
                                    Translate,
                                    SummarizePage,
                                    MoveToNormalTabs,
                                    ReportBrokenSite,
                                    Shortcut,
                                    AddToHomeScreen,
                                    SaveToCollection,
                                    OpenInApp,
                                    SaveAsPdf,
                                    Print,
                                )
                            ),
                        ),
                ),
                MenuSectionConfiguration(
                    id = BROWSER_MENU_GROUP_4_ID,
                    presentationMode = Grid,
                    items = listOf(History, Bookmarks, Downloads, Passwords),
                ),
                MenuSectionConfiguration(
                    id = BROWSER_MENU_GROUP_5_ID,
                    presentationMode = Row,
                    items = listOf(MozillaAccount, Settings),
                ),
                MenuSectionConfiguration(
                    id = BROWSER_MENU_GROUP_6_ID,
                    presentationMode = Row,
                    items = listOf(Quit),
                ),
            )
        return if (isToolbarAtBottom || isExpandedToolbarEnabled) rest + navSection else listOf(navSection) + rest
    }

    /** The menu shown on the customize menu screen. */
    fun customizeMenu(): List<MenuSectionConfiguration> =
        listOf(
            MenuSectionConfiguration(
                id = CUSTOMIZE_MENU_NAVIGATION_ID,
                presentationMode = Grid,
                items = listOf(Back, Forward, Share, Refresh),
                isSticky = true,
            ),
            MenuSectionConfiguration(
                id = CUSTOMIZE_MENU_GROUP_2_ID,
                presentationMode = Row,
                items = listOf(IPProtection),
            ),
            MenuSectionConfiguration(
                id = CUSTOMIZE_MENU_GROUP_3_ID,
                presentationMode = Row,
                items =
                    listOf(
                        Bookmark,
                        FindInPage,
                        DesktopSite,
                        Translate,
                        SummarizePage,
                    ),
            ),
            MenuSectionConfiguration(
                id = CUSTOMIZE_MENU_GROUP_4_ID,
                presentationMode = Grid,
                items = listOf(History, Bookmarks, Downloads, Passwords),
            ),
            MenuSectionConfiguration(
                id = CUSTOMIZE_MENU_GROUP_5_ID,
                presentationMode = Row,
                items = listOf(MozillaAccount, Settings),
            ),
            MenuSectionConfiguration(
                id = CUSTOMIZE_MENU_GROUP_6_ID,
                presentationMode = Row,
                items = listOf(AddToHomeScreen, SaveToCollection),
            ),
        )

    /** The menu shown on the home screen. */
    fun home(): List<MenuSectionConfiguration> =
        listOf(
            MenuSectionConfiguration(
                id = HOME_MENU_GROUP_1_ID,
                presentationMode = Row,
                items = listOf(DefaultBrowserBanner),
            ),
            MenuSectionConfiguration(
                id = HOME_MENU_GROUP_2_ID,
                presentationMode = Row,
                items = listOf(IPProtection),
            ),
            MenuSectionConfiguration(
                id = HOME_MENU_GROUP_3_ID,
                presentationMode = Row,
                items = listOf(Extensions),
            ),
            MenuSectionConfiguration(
                id = HOME_MENU_GROUP_4_ID,
                presentationMode = Grid,
                items = listOf(History, Bookmarks, Downloads, Passwords),
            ),
            MenuSectionConfiguration(
                id = HOME_MENU_GROUP_5_ID,
                presentationMode = Row,
                items = listOf(MozillaAccount, CustomizeHomepage, Settings),
            ),
            MenuSectionConfiguration(
                id = HOME_MENU_GROUP_6_ID,
                presentationMode = Row,
                items = listOf(Quit),
            ),
        )

    /** The menu shown in custom tabs. */
    fun customTab(
        isToolbarAtBottom: Boolean,
        shouldShowExtensions: Boolean,
    ): List<MenuSectionConfiguration> {
        val navSection =
            MenuSectionConfiguration(
                id = CUSTOM_TAB_MENU_NAVIGATION_ID,
                presentationMode = Grid,
                items = listOf(Back, Forward, Share, Refresh),
                isSticky = true,
            )
        val rest =
            listOf(
                MenuSectionConfiguration(
                    id = CUSTOM_TAB_MENU_GROUP_1_ID,
                    presentationMode = Row,
                    items =
                        listOfNotNull(
                            OpenInFirefox,
                            Bookmark,
                            FindInPage,
                            DesktopSite,
                            if (shouldShowExtensions) Extensions else null,
                            Translate,
                            SummarizePage,
                        ),
                ),
                MenuSectionConfiguration(
                    id = CUSTOM_TAB_MENU_GROUP_2_ID,
                    presentationMode = Row,
                    items = listOf(CustomTabAppItems),
                ),
            )
        return if (isToolbarAtBottom) rest + navSection else listOf(navSection) + rest
    }
}
