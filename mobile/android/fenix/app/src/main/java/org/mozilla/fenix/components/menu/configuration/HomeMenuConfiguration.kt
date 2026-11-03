/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import androidx.annotation.VisibleForTesting
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
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

/** The sections of the menu shown on the home screen. */
object HomeMenuConfiguration {
    @VisibleForTesting internal val GROUP_1_ID = "home_group_1"
    @VisibleForTesting internal val GROUP_2_ID = "home_group_2"
    @VisibleForTesting internal val GROUP_3_ID = "home_group_3"
    @VisibleForTesting internal val GROUP_4_ID = "home_group_4"
    @VisibleForTesting internal val GROUP_5_ID = "home_group_5"
    @VisibleForTesting internal val GROUP_6_ID = "home_group_6"

    /** The menu shown on the home screen. */
    fun sections(): List<MenuSectionConfiguration> =
        listOf(
            MenuSectionConfiguration(
                id = GROUP_1_ID,
                presentationMode = Row,
                items = listOf(DefaultBrowserBanner),
            ),
            MenuSectionConfiguration(
                id = GROUP_2_ID,
                presentationMode = Row,
                items = listOf(IPProtection),
            ),
            MenuSectionConfiguration(
                id = GROUP_3_ID,
                presentationMode = Row,
                items = listOf(Extensions),
            ),
            MenuSectionConfiguration(
                id = GROUP_4_ID,
                presentationMode = Grid,
                items = listOf(History, Bookmarks, Downloads, Passwords),
            ),
            MenuSectionConfiguration(
                id = GROUP_5_ID,
                presentationMode = Row,
                items = listOf(MozillaAccount, CustomizeHomepage, Settings),
            ),
            MenuSectionConfiguration(
                id = GROUP_6_ID,
                presentationMode = Row,
                items = listOf(Quit),
            ),
        )
}
