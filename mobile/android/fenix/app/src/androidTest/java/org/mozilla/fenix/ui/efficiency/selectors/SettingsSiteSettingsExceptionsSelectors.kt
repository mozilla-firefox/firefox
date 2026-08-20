/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsSiteSettingsExceptionsSelectors {

    val TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "Exceptions",
            description = "Site settings toolbar title",
            groups = listOf("requiredForPage"),
        )

    val EMPTY_EXCEPTIONS_LIST =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "No site exceptions",
            description = "Empty site settings exceptions list",
            groups = listOf("emptySiteSettingsExceptionsList"),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun EXCEPTION_ROW(origin: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = origin,
            description = "the site exception row for $origin",
            groups = listOf(),
        )

    val CLEAR_PERMISSIONS_ON_ALL_SITES_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_ID,
            value = "delete_all_site_permissions_button",
            description = "the Clear permissions on all sites button",
            groups = listOf(),
        )

    val CLEAR_PERMISSIONS_DIALOG_TITLE =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.clear_permissions),
            description = "the Clear permissions dialog title",
            groups = listOf(),
        )

    val CLEAR_PERMISSIONS_DIALOG_CANCEL_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.clear_permissions_negative),
            description = "the Clear permissions dialog Cancel button",
            groups = listOf(),
        )

    val CLEAR_PERMISSIONS_DIALOG_OK_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.clear_permissions_positive),
            description = "the Clear permissions dialog OK button",
            groups = listOf(),
        )

    val all =
        listOf(
            TOOLBAR_TITLE,
            EMPTY_EXCEPTIONS_LIST,
            EXCEPTION_ROW(),
            CLEAR_PERMISSIONS_ON_ALL_SITES_BUTTON,
            CLEAR_PERMISSIONS_DIALOG_TITLE,
            CLEAR_PERMISSIONS_DIALOG_CANCEL_BUTTON,
            CLEAR_PERMISSIONS_DIALOG_OK_BUTTON,
        )
}
