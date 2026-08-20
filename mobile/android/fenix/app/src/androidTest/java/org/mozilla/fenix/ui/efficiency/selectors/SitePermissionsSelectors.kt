/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SitePermissionsSelectors {

    val PAGE_PERMISSION_DIALOG_ALLOW_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "allow_button",
            description = "Permission dialog allow button",
            groups = listOf("requiredForPage"),
        )

    val MICROPHONE_PERMISSION_PROMPT =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "to use your microphone?",
            description = "Microphone permission prompt",
            groups = listOf(),
        )

    val PAGE_PERMISSION_REMEMBER_DECISION_CHECKBOX =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "do_not_ask_again",
            description = "Remember permission decision checkbox",
            groups = listOf(),
        )

    val PAGE_PERMISSION_DIALOG_DENY_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "deny_button",
            description = "Permission dialog deny button",
            groups = listOf(),
        )

    /**
     * The prompt titles and the prompt buttons' LABELS, which differ per permission ("Never"/"Always" for
     * notifications, "Don't allow"/"Allow" elsewhere) -- legacy asserted them with assertItemTextEquals on the same two
     * res-ids, so matching res-id AND text keeps that assertion rather than just proving a button exists.
     */
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun NOTIFICATIONS_PERMISSION_PROMPT(host: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Allow $host to send notifications?",
            description = "the notifications permission prompt for $host",
            groups = listOf(),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun LOCATION_PERMISSION_PROMPT(host: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Allow $host to use your location?",
            description = "the location permission prompt for $host",
            groups = listOf(),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun AUDIO_VIDEO_PERMISSION_PROMPT(host: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = "Allow $host to use your camera and microphone?",
            description = "the camera and microphone permission prompt for $host",
            groups = listOf(),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun PROMPT_ALLOW_BUTTON_LABELLED(label: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_TEXT,
            value = "allow_button",
            secondaryValue = label,
            description = "the permission prompt allow button labelled '$label'",
            groups = listOf(),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun PROMPT_DENY_BUTTON_LABELLED(label: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_AND_TEXT,
            value = "deny_button",
            secondaryValue = label,
            description = "the permission prompt deny button labelled '$label'",
            groups = listOf(),
        )

    /**
     * A trigger button on the permissions test page, matched by its web DOM id AND its label. A text-only match is not
     * safe here: the page prints a heading above each button ("Test Camera & Microphone Dialogue" over "Camera &
     * Microphone"), so a text-contains selector resolves the non-clickable heading first and the click then fails with
     * "Failed to click UiObject" while reporting the element as found. Ids taken from a live dump.
     */
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun PAGE_PERMISSION_BUTTON(webId: String = "", label: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_WEB_ID_AND_TEXT,
            value = webId,
            secondaryValue = label,
            description = "the '$label' button on the permissions test page",
            groups = listOf(),
        )

    val all =
        listOf(
            PAGE_PERMISSION_DIALOG_ALLOW_BUTTON,
            MICROPHONE_PERMISSION_PROMPT,
            PAGE_PERMISSION_REMEMBER_DECISION_CHECKBOX,
            PAGE_PERMISSION_DIALOG_DENY_BUTTON,
            NOTIFICATIONS_PERMISSION_PROMPT(),
            LOCATION_PERMISSION_PROMPT(),
            AUDIO_VIDEO_PERMISSION_PROMPT(),
            PROMPT_ALLOW_BUTTON_LABELLED(),
            PROMPT_DENY_BUTTON_LABELLED(),
            PAGE_PERMISSION_BUTTON(),
        )
}
