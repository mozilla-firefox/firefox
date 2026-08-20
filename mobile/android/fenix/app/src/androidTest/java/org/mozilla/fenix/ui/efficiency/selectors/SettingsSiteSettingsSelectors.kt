/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsSiteSettingsSelectors {

    val TOOLBAR_TITLE =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "Site settings",
            description = "Site settings toolbar title",
            groups = listOf("requiredForPage"),
        )

    val EXCEPTIONS_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Exceptions",
            description = "Site settings Exceptions button",
            groups = listOf("exceptions"),
        )

    val AUTOPLAY_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preference_browser_feature_autoplay),
            description = "Site settings Autoplay button",
            groups = listOf("autoplay"),
        )

    val CAMERA_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preference_phone_feature_camera),
            description = "Site settings Camera button",
            groups = listOf(),
        )

    val LOCATION_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preference_phone_feature_location),
            description = "Site settings Location button",
            groups = listOf(),
        )

    val MICROPHONE_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(R.string.preference_phone_feature_microphone),
            description = "Site settings Microphone button",
            groups = listOf(),
        )

    val all =
        listOf(
            TOOLBAR_TITLE,
            EXCEPTIONS_BUTTON,
            AUTOPLAY_BUTTON,
            CAMERA_BUTTON,
            LOCATION_BUTTON,
            MICROPHONE_BUTTON,
        )
}
