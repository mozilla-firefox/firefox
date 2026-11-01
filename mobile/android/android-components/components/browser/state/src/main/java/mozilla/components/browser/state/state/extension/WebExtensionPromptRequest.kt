/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state.extension

import java.util.UUID
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionInstallException

/**
 * Value type that represents a request for showing a native dialog from a [WebExtension].
 *
 * @param extension The [WebExtension] that requested the dialog to be shown.
 */
sealed class WebExtensionPromptRequest {
    /**
     * Installing a specific addon from a specific [url] is requested
     *
     * @property url The url from where to download the addon to install.
     * @property name Optional name of the addon.
     * @property iconUrl Optional url of the icon of the addon to install.
     * @property installationMethod What feature triggered installing the addon. Defaults to
     *   [InstallationMethod.MANAGER].
     */
    data class InstallationRequested(
        val url: String,
        val name: String?,
        val iconUrl: String?,
        val installationMethod: InstallationMethod = InstallationMethod.MANAGER,
    ) : WebExtensionPromptRequest()

    /**
     * Value type that represents a request for showing a native dialog from a [WebExtension] before the installation
     * succeeds.
     */
    sealed class BeforeInstallation(open val extension: WebExtension?) : WebExtensionPromptRequest() {
        /**
         * Value type that represents a request for showing error prompt when an installation failed.
         *
         * @property extension The exception with failed to installed.
         * @property exception The reason why the installation failed.
         */
        data class InstallationFailed(
            override val extension: WebExtension?,
            val exception: WebExtensionInstallException,
        ) : BeforeInstallation(extension)
    }

    /**
     * Value type that represents a request for showing a native dialog from a [WebExtension] after installation
     * succeeds.
     *
     * @param extension The [WebExtension] that requested the dialog to be shown.
     */
    sealed class AfterInstallation(open val extension: WebExtension) : WebExtensionPromptRequest() {
        /** Value type that represents a request for showing a permissions prompt. */
        sealed class Permissions(override val extension: WebExtension) : AfterInstallation(extension) {
            /** A unique ID to find this request again, e.g. from a recreated dialog. */
            val instanceId: String = UUID.randomUUID().toString()

            /**
             * Value type that represents a request for a required permissions prompt.
             *
             * @property extension The [WebExtension] that requested the dialog to be shown.
             * @property permissions The permissions to list in the dialog.
             * @property origins The origins to list in the dialog.
             * @property dataCollectionPermissions The data collection permissions to list in the dialog.
             * @property onConfirm A callback indicating the prompt has been confirmed and pass
             *   [PermissionPromptResponse] result.
             */
            data class Required(
                override val extension: WebExtension,
                val permissions: List<String>,
                val origins: List<String>,
                val dataCollectionPermissions: List<String>,
                val onConfirm: (PermissionPromptResponse) -> Unit,
            ) : Permissions(extension)

            /**
             * Value type that represents a request for an optional permissions prompt.
             *
             * @property extension The [WebExtension] that requested the dialog to be shown.
             * @property permissions The optional permissions to list in the dialog.
             * @property origins The optional host permissions to list in the dialog.
             * @property dataCollectionPermissions The optional data collection permissions to list in the dialog.
             * @property onConfirm A callback indicating whether the permissions were granted or not.
             */
            data class Optional(
                override val extension: WebExtension,
                val permissions: List<String>,
                val origins: List<String>,
                val dataCollectionPermissions: List<String>,
                val onConfirm: (Boolean) -> Unit,
            ) : Permissions(extension)
        }

        /**
         * Value type that represents a request for showing post-installation prompt. Normally used to give users an
         * opportunity to enable the [extension] in private browsing mode.
         *
         * @property extension The installed extension.
         */
        data class PostInstallation(override val extension: WebExtension) : AfterInstallation(extension)
    }
}
