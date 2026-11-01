/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.addons.ui

import mozilla.components.feature.addons.Addon

/** Provides methods for handling the add-on items in the add-on manager. */
interface AddonsManagerAdapterDelegate {
    /** Defines the different learn more links a user might click. */
    enum class LearnMoreLinks {
        /** The [Addon] is blocklisted and the learn more link should give more information to the user. */
        BLOCKLISTED_ADDON,

        /** The [Addon] is not correctly signed and the learn more link should give more information to the user. */
        ADDON_NOT_CORRECTLY_SIGNED,
    }

    /**
     * Handler for when an add-on item is clicked.
     *
     * @param addon The [Addon] that was clicked.
     */
    fun onAddonItemClicked(addon: Addon) = Unit

    /**
     * Handler for when the install add-on button is clicked.
     *
     * @param addon The [Addon] to install.
     */
    fun onInstallAddonButtonClicked(addon: Addon) = Unit

    /**
     * Handler for when the not yet supported section is clicked.
     *
     * @param unsupportedAddons The list of unsupported [Addon].
     */
    fun onNotYetSupportedSectionClicked(unsupportedAddons: List<Addon>) = Unit

    /** Handler to determine whether to show the "find more add-ons" button in the add-ons manager. */
    fun shouldShowFindMoreAddonsButton(): Boolean = false

    /** Handler for when the "find more add-ons" button is clicked. */
    fun onFindMoreAddonsButtonClicked() = Unit

    /** Handler for when a "learn more" link on an add-on item is clicked. */
    fun onLearnMoreLinkClicked(link: LearnMoreLinks, addon: Addon) = Unit

    /**
     * Handler for when "Review changes" is clicked on the message bar of a pending update.
     *
     * @param addon The [Addon] whose update is waiting for the user.
     */
    fun onReviewExtensionUpdateClicked(addon: Addon) = Unit

    /**
     * Handler for when the close button is clicked on the message bar of a pending update. The adapter does not observe
     * the store: remove the update from [mozilla.components.browser.state.state.BrowserState.pendingExtensionUpdates]
     * and rebind the row to hide the message bar.
     *
     * @param addon The [Addon] whose update is waiting for the user.
     */
    fun onCancelExtensionUpdateClicked(addon: Addon) = Unit
}
