/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ui.efficiency.selectors

import mozilla.components.feature.addons.R as addonsR
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.AddonsTestTag
import org.mozilla.fenix.helpers.DataGenerationHelper.getStringResource
import org.mozilla.fenix.ui.efficiency.helpers.PageReadinessProfiles
import org.mozilla.fenix.ui.efficiency.helpers.Selector
import org.mozilla.fenix.ui.efficiency.helpers.SelectorContainer
import org.mozilla.fenix.ui.efficiency.helpers.SelectorGroup
import org.mozilla.fenix.ui.efficiency.helpers.SelectorStrategy
import org.mozilla.fenix.ui.efficiency.helpers.SwipeDirection

object SettingsAddonsManagerSelectors : SelectorContainer {
    enum class Group : SelectorGroup {
        EXTENSION_DETAILS,
        ADD_ONS,
    }

    val TOOLBAR_TITLE =
        navigationToolbarTitle(
            title = getStringResource(R.string.preferences_extensions),
            description = "Extensions toolbar title",
        )

    val NAVIGATE_BACK_TOOLBAR_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_CONTENT_DESC,
            value = "Navigate up",
            description = "Navigate back toolbar button",
        )

    val ENABLE_OR_DISABLE_EXTENSION_TOGGLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "enable_switch",
            description = "Enable or disable an extension toggle",
            groups = setOf(Group.EXTENSION_DETAILS),
        )

    val ADD_ONS_LIST =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "add_ons_list",
            description = "Add-ons List",
            groups = setOf(Group.ADD_ONS),
            readiness = PageReadinessProfiles.READY_CONTENT,
        )

    // The progress bar shown while the add-ons manager list loads. Mirrors the legacy
    // waitForAddonsListProgressBarToBeGone (itemWithResId("add_ons_progress_bar")).
    val ADD_ONS_PROGRESS_BAR =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "add_ons_progress_bar",
            description = "Add-ons list loading progress bar",
        )

    // The "Install <addon>" button on a recommended add-on row in the add-ons manager list, keyed on
    // the addon name via its content description. The scroll direction lets the framework bring the row into
    // view before clicking (mirrors the legacy addonsList().scrollIntoView) — needed for add-ons below
    // the fold. Mirrors the legacy installButtonForAddon (withContentDescription("Install <addon>")).
    @Suppress("FunctionName")
    fun INSTALL_ADDON_BUTTON(addonTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_DESCRIPTION_CONTAINS,
            value = "Install $addonTitle",
            description = "Install add-on '$addonTitle' button",
            scrollDirection = SwipeDirection.UP,
        )

    // The "Allow extension to run in private browsing" checkbox on the add-on install permission
    // dialog. Mirrors the legacy selectAllowInPrivateBrowsing (onView(withId(allow_in_private_browsing))).
    val ALLOW_IN_PRIVATE_BROWSING_CHECKBOX =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "allow_in_private_browsing",
            description = "Add-on permission dialog Allow in private browsing checkbox",
        )

    // The "Add" button on the add-on install permission dialog. Mirrors the legacy
    // allowPermissionToInstall (By.res("$packageName:id/allow_button")).
    val ADDON_PERMISSION_ALLOW_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "allow_button",
            description = "Add-on install permission dialog Add button",
        )

    // Title of the add-on install permission dialog ("Add <addon>"). Keyed on the addon name via a
    // text-contains match on the shared dialog "title" id, mirroring the legacy verifyAddonPermissionPrompt.
    @Suppress("FunctionName")
    fun ADDON_PERMISSION_PROMPT_TITLE(addonTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "title",
            secondaryValue = addonTitle,
            description = "Add-on install permission dialog title for '$addonTitle'",
        )

    // Title of the install-completed dialog ("<addon> was added"). The addon's display name may be
    // longer than the recommended-list name (e.g. "Bitwarden" -> "Bitwarden Password Manager"), so a
    // text-contains match on the short name matches both. Mirrors legacy verifyAddonInstallCompletedPrompt.
    @Suppress("FunctionName")
    fun ADDON_INSTALL_COMPLETED_TITLE(addonTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "title",
            secondaryValue = addonTitle,
            description = "Add-on install completed prompt for '$addonTitle'",
        )

    // The "OK" button on the install-completed dialog. Matched by its unique id (only that dialog has
    // confirm_button), which also keeps it language-independent. Mirrors legacy closeAddonInstallCompletePrompt.
    val ADDON_INSTALL_COMPLETED_OK_BUTTON =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID,
            value = "confirm_button",
            description = "Add-on install completed prompt OK button",
        )

    // An installed add-on row in the add-ons manager list, keyed on its name label. A text-contains
    // match on the short name tolerates the longer display name shown in the list.
    @Suppress("FunctionName")
    fun INSTALLED_ADDON_ITEM(addonTitle: String = "") =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_RES_ID_CONTAINING_TEXT,
            value = "add_on_name",
            secondaryValue = addonTitle,
            description = "Installed add-on '$addonTitle' row",
        )

    // The "Remove" button on an add-on's detail screen. Mirrors the legacy removeAddon
    // (onView(withId(R.id.remove_add_on))).
    val REMOVE_ADDON_BUTTON =
        Selector(
            strategy = SelectorStrategy.ESPRESSO_BY_ID,
            value = "remove_add_on",
            description = "Remove add-on button",
        )

    // The "Enabled" section header in the add-ons manager, shown once an installed extension is
    // enabled. Mirrors the legacy verifyEnabledTitleDisplayed.
    val ENABLED_SECTION_TITLE =
        Selector(
            strategy = SelectorStrategy.UIAUTOMATOR_WITH_TEXT,
            value = getStringResource(addonsR.string.mozac_feature_addons_enabled),
            description = "Add-ons manager Enabled section title",
        )

    // The confirm button of the dialog shown when the user taps the button to remove an extension.
    val REMOVE_ADDON_DIALOG_CONFIRM_BUTTON =
        Selector(
            strategy = SelectorStrategy.COMPOSE_BY_TAG,
            value = AddonsTestTag.REMOVE_ADDON_DIALOG_CONFIRM_BUTTON,
            description = "Remove add-on confirm button",
        )
}
