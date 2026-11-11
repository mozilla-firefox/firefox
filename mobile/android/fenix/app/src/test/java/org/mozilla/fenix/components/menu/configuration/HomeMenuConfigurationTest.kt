/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import kotlin.test.assertEquals
import org.junit.Test
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

    private fun section(id: String, presentationMode: MenuPresentationMode, vararg items: FenixMenuItem) =
        MenuSectionConfiguration(id = id, presentationMode = presentationMode, items = items.toList())
}
