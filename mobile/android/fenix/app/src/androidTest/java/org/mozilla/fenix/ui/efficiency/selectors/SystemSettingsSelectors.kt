/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy

object SystemSettingsSelectors {

    // Will need support for querying multiple values eg: res id and description (itemWithResIdAndDescription)
    val PRIVATE_BROWSING_SYSTEM_SETTINGS_NOTIFICATIONS_TOGGLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Private browsing session",
            description = "Private browsing system settings notifications toggle",
            groups = listOf("requiredForPage"),
        )

    // Android's permission dialog. Package-qualified raw res-ids: these belong to the permission controller,
    // not to Fenix, so they must not be prefixed with the app's package. UiObject2 rather than UiObject: a
    // UiObject click goes through clickAndSync, which intermittently reported this dismissal as failed.
    val PERMISSION_DENY_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RAW_RES,
            value = "com.android.permissioncontroller:id/permission_deny_button",
            description = "System permission Deny button",
            groups = listOf(),
        )

    // The dialog container itself, so a caller can tell whether ANOTHER permission request is still
    // queued. A single request can fan out into several consecutive dialogs (a bare <input type="file">
    // asks for audio, then music-and-audio), and there is no other signal that the chain is drained.
    val PERMISSION_GRANT_DIALOG =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RAW_RES,
            value = "com.android.permissioncontroller:id/grant_dialog",
            description = "System permission grant dialog",
            groups = listOf(),
        )

    val PERMISSION_DENY_AND_DONT_ASK_AGAIN_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR2_BY_RAW_RES,
            value = "com.android.permissioncontroller:id/permission_deny_and_dont_ask_again_button",
            description = "System permission Deny and don't ask again button",
            groups = listOf(),
        )

    // A row in Gecko's file chooser, addressed by the label the device actually shows for it.
    //
    // Parametrised rather than fixed to "Files": which content picker the chooser offers varies by OS release
    // (API 37 shows "Files"/documentsui, API 34 shows "Media"/photo picker and no "Files" row at all), so the
    // caller resolves the label from the device via SystemPickerCapabilities and passes it in. Text rather than
    // res-id because every chooser row shares one id, so the label is the only thing distinguishing them.
    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun FILE_CHOOSER_OPTION(label: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = label,
            description = "System file chooser '$label' option",
            groups = listOf(),
        )

    // The App info screen's "Permissions" row, and then a named permission on the app-permissions list.
    // Text matches, as the legacy robot does: these screens are rendered by the Settings app on some builds
    // and by the permission controller on others, so a res-id would not be portable across either.
    val APP_INFO_PERMISSIONS_ROW =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "Permissions",
            description = "App info Permissions row",
            groups = listOf(),
        )

    @Suppress("ktlint:standard:function-naming", "FunctionName")
    fun APP_PERMISSION_ROW(permissionName: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = permissionName,
            description = "'$permissionName' app permission row",
            groups = listOf(),
        )

    // "Allow" is a CONTAINS match, matching the legacy robot: on newer Android the camera permission screen
    // offers "Allow only while using the app" rather than a bare "Allow".
    val APP_PERMISSION_ALLOW_OPTION =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT_CONTAINS,
            value = "Allow",
            description = "App permission Allow option",
            groups = listOf(),
        )

    val all =
        listOf(
            PERMISSION_GRANT_DIALOG,
            PERMISSION_DENY_BUTTON,
            PERMISSION_DENY_AND_DONT_ASK_AGAIN_BUTTON,
            FILE_CHOOSER_OPTION(),
            APP_INFO_PERMISSIONS_ROW,
            APP_PERMISSION_ROW(),
            APP_PERMISSION_ALLOW_OPTION,
            PRIVATE_BROWSING_SYSTEM_SETTINGS_NOTIFICATIONS_TOGGLE,
        )
}
