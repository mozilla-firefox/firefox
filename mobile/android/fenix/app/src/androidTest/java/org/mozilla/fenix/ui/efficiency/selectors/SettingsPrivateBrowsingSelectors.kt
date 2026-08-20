/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsPrivateBrowsingSelectors {

    val ADD_PRIVATE_BROWSING_SHORTCUT =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Add private browsing shortcut",
            description = "Add private browsing shortcut button",
            groups = listOf("requiredForPage"),
        )

    val OPEN_LINKS_IN_PRIVATE_TAB =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Open links in a private tab",
            description = "Open links in a private tab toggle",
            groups = listOf("defaultValues"),
        )

    val ALLOW_SCREENSHOTS_IN_PRIVATE_BROWSING =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.preferences_allow_screenshots_in_private_mode),
            description = "Allow screenshots in private browsing toggle",
            groups = listOf(),
        )

    val all =
        listOf(
            ADD_PRIVATE_BROWSING_SHORTCUT,
            OPEN_LINKS_IN_PRIVATE_TAB,
            ALLOW_SCREENSHOTS_IN_PRIVATE_BROWSING,
        )
}
