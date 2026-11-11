/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.menu.configuration

import androidx.annotation.VisibleForTesting
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Back
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Bookmark
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.CustomTabAppItems
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.DesktopSite
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Extensions
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.FindInPage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Forward
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.OpenInFirefox
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Refresh
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Share
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.SummarizePage
import org.mozilla.fenix.components.menu.configuration.FenixMenuItem.Translate
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Grid
import org.mozilla.fenix.components.menu.configuration.MenuPresentationMode.Row

/** The sections of the menu shown in custom tabs. */
object CustomTabMenuConfiguration {
    @VisibleForTesting internal val PAGE_CONTROLS_GROUP_ID = "page_controls"
    @VisibleForTesting internal val TOOLS_GROUP_ID = "tools"
    @VisibleForTesting internal val APP_ITEMS_GROUP_ID = "app_items"

    /** The menu shown in custom tabs. */
    fun sections(
        isToolbarAtBottom: Boolean,
        shouldShowExtensions: Boolean,
    ): List<MenuSectionConfiguration> {
        val navSection =
            MenuSectionConfiguration(
                id = PAGE_CONTROLS_GROUP_ID,
                presentationMode = Grid,
                items = listOf(Back, Forward, Share, Refresh),
                isSticky = true,
            )
        val rest =
            listOf(
                MenuSectionConfiguration(
                    id = TOOLS_GROUP_ID,
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
                    id = APP_ITEMS_GROUP_ID,
                    presentationMode = Row,
                    items = listOf(CustomTabAppItems),
                ),
            )
        return if (isToolbarAtBottom) rest + navSection else listOf(navSection) + rest
    }
}
