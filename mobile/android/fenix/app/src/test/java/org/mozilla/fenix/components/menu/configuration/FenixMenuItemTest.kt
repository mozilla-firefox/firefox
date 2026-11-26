/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import org.junit.Test
import org.mozilla.fenix.components.menu.fake.allMenuItems

class FenixMenuItemTest {
    @Test
    fun `all menu items have different ids`() {
        val ids = allMenuItems().map { it.id }

        assertEquals(emptyMap(), ids.groupingBy { it }.eachCount().filterValues { it > 1 })
    }

    @Test
    fun `only the extensions item has to be directly in its section`() {
        assertEquals(listOf(FenixMenuItem.Extensions), allMenuItems().filter { it.isTopLevelOnly })
    }

    @Test
    fun `menu item ids stay the same so saved placements keep working`() {
        assertEquals(
            mapOf(
                FenixMenuItem.DefaultBrowserBanner::class to "default_browser_banner",
                FenixMenuItem.CustomizeReaderView::class to "customize_reader_view",
                FenixMenuItem.IPProtection::class to "ip_protection",
                FenixMenuItem.Bookmark::class to "bookmark",
                FenixMenuItem.Extensions::class to "extensions",
                FenixMenuItem.FindInPage::class to "find_in_page",
                FenixMenuItem.DesktopSite::class to "desktop_site",
                FenixMenuItem.More::class to "more",
                FenixMenuItem.Translate::class to "translate",
                FenixMenuItem.SummarizePage::class to "summarize_page",
                FenixMenuItem.MoveToNormalTabs::class to "move_to_normal_tabs",
                FenixMenuItem.ReportBrokenSite::class to "report_broken_site",
                FenixMenuItem.Shortcut::class to "shortcut",
                FenixMenuItem.AddToHomeScreen::class to "add_to_home_screen",
                FenixMenuItem.OpenInApp::class to "open_in_app",
                FenixMenuItem.OpenInFirefox::class to "open_in_firefox",
                FenixMenuItem.CustomTabAppItems::class to "custom_tab_app_items",
                FenixMenuItem.SaveAsPdf::class to "save_as_pdf",
                FenixMenuItem.Print::class to "print",
                FenixMenuItem.MozillaAccount::class to "mozilla_account",
                FenixMenuItem.CustomizeHomepage::class to "customize_homepage",
                FenixMenuItem.Settings::class to "settings",
                FenixMenuItem.Quit::class to "quit",
                FenixMenuItem.Back::class to "back",
                FenixMenuItem.History::class to "history",
                FenixMenuItem.Bookmarks::class to "bookmarks",
                FenixMenuItem.Downloads::class to "downloads",
                FenixMenuItem.Passwords::class to "passwords",
                FenixMenuItem.Forward::class to "forward",
                FenixMenuItem.Share::class to "share",
                FenixMenuItem.Refresh::class to "refresh",
                FenixMenuItem.NewTab::class to "new_tab",
                FenixMenuItem.NewPrivateTab::class to "new_private_tab",
                FenixMenuItem.TabGroup::class to "tab_group",
            ),
            allMenuItems().associate { it::class to it.id },
        )
    }
}
