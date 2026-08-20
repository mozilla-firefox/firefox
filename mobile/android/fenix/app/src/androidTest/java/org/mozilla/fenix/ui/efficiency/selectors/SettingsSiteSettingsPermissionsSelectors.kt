/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.R
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SettingsSiteSettingsPermissionsSelectors {

    val ASK_TO_ALLOW_RADIO_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_ID,
            value = "ask_to_allow_radio",
            description = "Ask to allow radio button",
            // requiredForPage: the permission detail screen always shows the "Ask to allow" radio, so it is
            // what proves we arrived. The page registers a real nav edge, so without an anchor here
            // navigateToPage() would report success for whatever screen happened to be in front of it.
            groups = listOf("requiredForPage", "askToAllow"),
        )

    // The whole blocked-state section, taken from a live dump. Legacy waited on this container going away to call a
    // permission unblocked, which is stronger than the heading alone: the heading is one child of it.
    val BLOCKED_BY_ANDROID_CONTAINER =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "permissions_blocked_container",
            description = "the Blocked by Android container",
            groups = listOf(),
        )

    val BLOCKED_BY_ANDROID_HEADING =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = getStringResource(R.string.phone_feature_blocked_by_android),
            description = "the Blocked by Android heading",
            groups = listOf("blockedByAndroid"),
        )

    val BLOCKED_BY_ANDROID_INTRO =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.phone_feature_blocked_intro),
            description = "the 'To allow it:' line",
            groups = listOf("blockedByAndroid"),
        )

    val BLOCKED_BY_ANDROID_STEP_SETTINGS =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = getStringResource(R.string.phone_feature_blocked_step_settings),
            description = "the '1. Go to Android Settings' step",
            groups = listOf("blockedByAndroid"),
        )

    // The string resource is CDATA with a <b> tag, so the rendered text is what has to be matched.
    val BLOCKED_BY_ANDROID_STEP_PERMISSIONS =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_TEXT,
            value = "2. Tap Permissions",
            description = "the '2. Tap Permissions' step",
            groups = listOf("blockedByAndroid"),
        )

    val GO_TO_SETTINGS_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_ID,
            value = "settings_button",
            description = "the Go to settings button",
            groups = listOf("blockedByAndroid"),
        )

    val all =
        listOf(
            ASK_TO_ALLOW_RADIO_BUTTON,
            BLOCKED_BY_ANDROID_CONTAINER,
            BLOCKED_BY_ANDROID_HEADING,
            BLOCKED_BY_ANDROID_INTRO,
            BLOCKED_BY_ANDROID_STEP_SETTINGS,
            BLOCKED_BY_ANDROID_STEP_PERMISSIONS,
            GO_TO_SETTINGS_BUTTON,
        )
}
