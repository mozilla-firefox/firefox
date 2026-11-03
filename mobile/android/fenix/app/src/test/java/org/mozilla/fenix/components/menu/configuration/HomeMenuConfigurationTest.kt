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
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.GROUP_1_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.GROUP_2_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.GROUP_3_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.GROUP_4_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.GROUP_5_ID
import org.mozilla.fenix.components.menu.configuration.HomeMenuConfiguration.GROUP_6_ID
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

class HomeMenuConfigurationTest {
    @Test
    fun `WHEN building the home menu THEN show only what needs no page, grouped as before`() {
        assertEquals(
            listOf(
                section(GROUP_1_ID, Row, DefaultBrowserBanner),
                section(GROUP_2_ID, Row, IPProtection),
                section(GROUP_3_ID, Row, Extensions),
                section(GROUP_4_ID, Grid, History, Bookmarks, Downloads, Passwords),
                section(GROUP_5_ID, Row, MozillaAccount, CustomizeHomepage, Settings),
                section(GROUP_6_ID, Row, Quit),
            ),
            HomeMenuConfiguration.sections(),
        )
    }

    private fun section(id: String, presentationMode: MenuPresentationMode, vararg items: FenixMenuItem) =
        MenuSectionConfiguration(id = id, presentationMode = presentationMode, items = items.toList())
}
