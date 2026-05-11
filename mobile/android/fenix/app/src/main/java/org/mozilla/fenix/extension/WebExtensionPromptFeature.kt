/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.extension

import android.content.Context
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.widget.TextView
import androidx.annotation.UiContext
import androidx.annotation.VisibleForTesting
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.FragmentManager
import androidx.navigation.NavController
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.mapNotNull
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.ui.AddonDialogFragment
import mozilla.components.feature.addons.ui.AddonInstallationDialogFragment
import mozilla.components.feature.addons.ui.PermissionsDialogFragment
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.ktx.android.content.appVersionName
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.AddonsManagementFragmentDirections
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.pixelSizeFor
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.ThemeManager
import com.google.android.material.R as materialR
import mozilla.components.feature.addons.R as addonsR

/**
 * Feature implementation for handling [WebExtensionPromptRequest] and showing the respective UI.
 */
class WebExtensionPromptFeature(
    private val store: BrowserStore,
    @param:UiContext private val context: Context,
    private val fragmentManager: FragmentManager,
    private val onLinkClicked: (String, Boolean) -> Unit,
    private val navController: NavController,
    private val addonManager: AddonManager = context.components.addonManager,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {

    /**
     * Whether or not an add-on installation is in progress.
     */
    private var isInstallationInProgress = false
    private var scope: CoroutineScope? = null

    /**
     * Starts observing the selected session to listen for window requests
     * and opens / closes tabs as needed.
     */
    override fun start() {
        scope = store.flowScoped(dispatcher = mainDispatcher) { flow ->
            flow.mapNotNull { state ->
                state.webExtensionPromptRequest
            }.distinctUntilChanged().collect { promptRequest ->

                when (promptRequest) {
                    is WebExtensionPromptRequest.AfterInstallation -> {
                        handleAfterInstallationRequest(promptRequest)
                    }

                    is WebExtensionPromptRequest.BeforeInstallation.InstallationFailed -> {
                        handleBeforeInstallationRequest(promptRequest)
                        consumePromptRequest()
                    }
                }
            }
        }
        tryToReAttachButtonHandlersToPreviousDialog()
    }

    @VisibleForTesting
    internal fun handleAfterInstallationRequest(promptRequest: WebExtensionPromptRequest.AfterInstallation) {
        val installedState = addonManager.toInstalledState(promptRequest.extension)
        val addon = Addon.newFromWebExtension(promptRequest.extension, installedState)
        when (promptRequest) {
            is WebExtensionPromptRequest.AfterInstallation.Permissions.Required -> handleRequiredPermissionRequest(
                addon,
                promptRequest,
            )

            is WebExtensionPromptRequest.AfterInstallation.Permissions.Optional -> handleOptionalPermissionsRequest(
                addon,
                promptRequest,
            )

            is WebExtensionPromptRequest.AfterInstallation.PostInstallation -> handlePostInstallationRequest(
                addon,
            )
        }
    }

    private fun handleBeforeInstallationRequest(promptRequest: WebExtensionPromptRequest.BeforeInstallation) {
        when (promptRequest) {
            is WebExtensionPromptRequest.BeforeInstallation.InstallationFailed -> {
                handleInstallationFailedRequest(
                    exception = promptRequest.exception,
                )
                consumePromptRequest()
            }
        }
    }

    private fun handlePostInstallationRequest(
        addon: Addon,
    ) {
        showPostInstallationDialog(addon)
    }

    private fun handleRequiredPermissionRequest(
        addon: Addon,
        promptRequest: WebExtensionPromptRequest.AfterInstallation.Permissions.Required,
    ) {
        showPermissionDialog(
            addon = addon,
            promptRequest = promptRequest,
            permissions = promptRequest.permissions,
            origins = promptRequest.origins,
            dataCollectionPermissions = promptRequest.dataCollectionPermissions,
        )
    }

    @VisibleForTesting
    internal fun handleOptionalPermissionsRequest(
        addon: Addon,
        promptRequest: WebExtensionPromptRequest.AfterInstallation.Permissions.Optional,
    ) {
        val shouldGrantWithoutPrompt = Addon.localizePermissions(
            promptRequest.permissions,
            context,
        ).isEmpty() && promptRequest.origins.isEmpty() && promptRequest.dataCollectionPermissions.isEmpty()

        // If we don't have any promptable permissions, just proceed.
        if (shouldGrantWithoutPrompt) {
            handlePermissions(
                promptRequest,
                granted = true,
                privateBrowsingAllowed = false,
                technicalAndInteractionDataGranted = false,
            )
            return
        }

        showPermissionDialog(
            addon = addon,
            promptRequest = promptRequest,
            forOptionalPermissions = true,
            permissions = promptRequest.permissions,
            origins = promptRequest.origins,
            dataCollectionPermissions = promptRequest.dataCollectionPermissions,
        )
    }

    @VisibleForTesting
    internal fun handleInstallationFailedRequest(
        exception: WebExtensionInstallException,
    ): AlertDialog? {
        val addonName = exception.extensionName ?: ""
        val appName = context.getString(R.string.app_name)

        var title = context.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
        var url: String? = null
        val message = when (exception) {
            is WebExtensionInstallException.Blocklisted -> {
                url = formatBlocklistURL(exception)
                context.getString(addonsR.string.mozac_feature_addons_blocklisted_2, addonName, appName)
            }

            is WebExtensionInstallException.SoftBlocked -> {
                url = formatBlocklistURL(exception)
                context.getString(addonsR.string.mozac_feature_addons_soft_blocked_2, addonName, appName)
            }

            is WebExtensionInstallException.UserCancelled -> {
                // We don't want to show an error message when users cancel installation.
                return null
            }

            is WebExtensionInstallException.UnsupportedAddonType,
            is WebExtensionInstallException.Unknown,
            -> {
                // Making sure we don't have a
                // Title = Can't install extension
                // Message = Failed to install $addonName
                title = ""
                if (addonName.isNotEmpty()) {
                    context.getString(addonsR.string.mozac_feature_addons_failed_to_install, addonName)
                } else {
                    context.getString(addonsR.string.mozac_feature_addons_extension_failed_to_install)
                }
            }

            is WebExtensionInstallException.AdminInstallOnly -> {
                context.getString(addonsR.string.mozac_feature_addons_admin_install_only, addonName)
            }

            is WebExtensionInstallException.NetworkFailure -> {
                context.getString(addonsR.string.mozac_feature_addons_extension_failed_to_install_network_error)
            }

            is WebExtensionInstallException.CorruptFile -> {
                context.getString(addonsR.string.mozac_feature_addons_extension_failed_to_install_corrupt_error)
            }

            is WebExtensionInstallException.NotSigned -> {
                context.getString(
                    addonsR.string.mozac_feature_addons_extension_failed_to_install_not_signed_error,
                )
            }

            is WebExtensionInstallException.Incompatible -> {
                val version = context.appVersionName
                context.getString(
                    addonsR.string.mozac_feature_addons_failed_to_install_incompatible_error,
                    addonName,
                    appName,
                    version,
                )
            }
        }

        return showDialog(
            title = title,
            message = message,
            url = url,
        )
    }

    private fun formatBlocklistURL(exception: WebExtensionInstallException): String? {
        var url: String? = exception.extensionId?.let { AMO_BLOCKED_PAGE_URL.format(it) }
        // Only append the version if the URL is valid and we have a version. The AMO "blocked" page
        // can be loaded without a version, but it's always better to specify a version if we have one.
        if (url != null && exception.extensionVersion != null) {
            url += "${exception.extensionVersion}/"
        }

        return url
    }

    /**
     * Stops observing the selected session for incoming window requests.
     */
    override fun stop() {
        scope?.cancel()
    }

    @VisibleForTesting
    internal fun showPermissionDialog(
        addon: Addon,
        promptRequest: WebExtensionPromptRequest.AfterInstallation.Permissions,
        forOptionalPermissions: Boolean = false,
        permissions: List<String> = emptyList(),
        origins: List<String> = emptyList(),
        dataCollectionPermissions: List<String> = emptyList(),
    ): PermissionsDialogFragment? {
        if (isInstallationInProgress || hasExistingPermissionDialogFragment()) {
            return null
        }

        val dialog = PermissionsDialogFragment.newInstance(
            addon = addon,
            forOptionalPermissions = forOptionalPermissions,
            permissions = permissions,
            origins = origins,
            dataCollectionPermissions = dataCollectionPermissions,
            promptsStyling = AddonDialogFragment.PromptsStyling(
                gravity = Gravity.BOTTOM,
                shouldWidthMatchParent = true,
                confirmButtonBackgroundColor = ThemeManager.resolveAttribute(
                    R.attr.actionPrimary,
                    context,
                ),
                confirmButtonTextColor = ThemeManager.resolveAttribute(
                    R.attr.textActionPrimary,
                    context,
                ),
                confirmButtonDisabledBackgroundColor = ThemeManager.resolveAttribute(
                    R.attr.actionPrimaryDisabled,
                    context,
                ),
                confirmButtonDisabledTextColor = ThemeManager.resolveAttribute(
                    R.attr.textActionPrimaryDisabled,
                    context,
                ),
                confirmButtonRadius = context.pixelSizeFor(R.dimen.tab_corner_radius).toFloat(),
                learnMoreLinkTextColor = ThemeManager.resolveAttribute(
                    materialR.attr.colorTertiary,
                    context,
                ),
            ),
            onPositiveButtonClicked = { _, privateBrowsingAllowed, technicalAndInteractionDataAllowed ->
                handlePermissions(
                    promptRequest,
                    granted = true,
                    privateBrowsingAllowed,
                    technicalAndInteractionDataAllowed,
                )
            },
            onNegativeButtonClicked = {
                handlePermissions(
                    promptRequest,
                    granted = false,
                    privateBrowsingAllowed = false,
                    technicalAndInteractionDataGranted = false,
                )
            },
            onLearnMoreClicked = {
                onLinkClicked.invoke(
                    SupportUtils.getSumoURLForTopic(
                        context,
                        SupportUtils.SumoTopic.EXTENSION_PERMISSIONS,
                    ),
                    false,
                )
            },
        )
        dialog.show(fragmentManager, PERMISSIONS_DIALOG_FRAGMENT_TAG)

        return dialog
    }

    private fun tryToReAttachButtonHandlersToPreviousDialog() {
        findPreviousPermissionDialogFragment()?.let { dialog ->
            dialog.onPositiveButtonClicked = { addon, privateBrowsingAllowed, technicalAndInteractionDataGranted ->
                store.state.webExtensionPromptRequest?.let { promptRequest ->
                    if (promptRequest is WebExtensionPromptRequest.AfterInstallation.Permissions &&
                        addon.id == promptRequest.extension.id
                    ) {
                        handlePermissions(
                            promptRequest,
                            granted = true,
                            privateBrowsingAllowed,
                            technicalAndInteractionDataGranted,
                        )
                    }
                }
            }
            dialog.onNegativeButtonClicked = {
                store.state.webExtensionPromptRequest?.let { promptRequest ->
                    if (promptRequest is WebExtensionPromptRequest.AfterInstallation.Permissions) {
                        handlePermissions(
                            promptRequest,
                            granted = false,
                            privateBrowsingAllowed = false,
                            technicalAndInteractionDataGranted = false,
                        )
                    }
                }
            }
            dialog.onLearnMoreClicked = {
                store.state.webExtensionPromptRequest?.let { promptRequest ->
                    if (promptRequest is WebExtensionPromptRequest.AfterInstallation.Permissions) {
                        onLinkClicked.invoke(
                            SupportUtils.getSumoURLForTopic(
                                context,
                                SupportUtils.SumoTopic.EXTENSION_PERMISSIONS,
                            ),
                            false,
                        )
                    }
                }
            }
        }

        findPreviousPostInstallationDialogFragment()?.let { dialog ->
            dialog.onDismissed = {
                store.state.webExtensionPromptRequest?.let { _ ->
                    consumePromptRequest()
                }
            }
        }
    }

    private fun handlePermissions(
        promptRequest: WebExtensionPromptRequest.AfterInstallation.Permissions,
        granted: Boolean,
        privateBrowsingAllowed: Boolean,
        technicalAndInteractionDataGranted: Boolean,
    ) {
        when (promptRequest) {
            is WebExtensionPromptRequest.AfterInstallation.Permissions.Optional -> {
                promptRequest.onConfirm(granted)
            }

            is WebExtensionPromptRequest.AfterInstallation.Permissions.Required -> {
                val response = PermissionPromptResponse(
                    isPermissionsGranted = granted,
                    isPrivateModeGranted = privateBrowsingAllowed,
                    isTechnicalAndInteractionDataGranted = technicalAndInteractionDataGranted,
                )
                promptRequest.onConfirm(response)
            }
        }
        consumePromptRequest()
    }

    @VisibleForTesting
    internal fun consumePromptRequest() {
        store.dispatch(WebExtensionAction.ConsumePromptRequestWebExtensionAction)
    }

    private fun hasExistingPermissionDialogFragment(): Boolean {
        return findPreviousPermissionDialogFragment() != null
    }

    private fun hasExistingAddonPostInstallationDialogFragment(): Boolean {
        return fragmentManager.findFragmentByTag(POST_INSTALLATION_DIALOG_FRAGMENT_TAG)
            as? AddonInstallationDialogFragment != null
    }

    private fun findPreviousPermissionDialogFragment(): PermissionsDialogFragment? {
        return fragmentManager.findFragmentByTag(PERMISSIONS_DIALOG_FRAGMENT_TAG) as? PermissionsDialogFragment
    }

    private fun findPreviousPostInstallationDialogFragment(): AddonInstallationDialogFragment? {
        return fragmentManager.findFragmentByTag(
            POST_INSTALLATION_DIALOG_FRAGMENT_TAG,
        ) as? AddonInstallationDialogFragment
    }

    @VisibleForTesting
    internal fun showPostInstallationDialog(addon: Addon): AddonInstallationDialogFragment? {
        if (!isInstallationInProgress && !hasExistingAddonPostInstallationDialogFragment()) {
            val dialog = AddonInstallationDialogFragment.newInstance(
                addon = addon,
                promptsStyling = AddonDialogFragment.PromptsStyling(
                    gravity = Gravity.BOTTOM,
                    shouldWidthMatchParent = true,
                    confirmButtonBackgroundColor = ThemeManager.resolveAttribute(
                        R.attr.actionPrimary,
                        context,
                    ),
                    confirmButtonTextColor = ThemeManager.resolveAttribute(
                        R.attr.textActionPrimary,
                        context,
                    ),
                    confirmButtonRadius = context.pixelSizeFor(R.dimen.tab_corner_radius).toFloat(),
                ),
                onDismissed = {
                    consumePromptRequest()
                },
                onConfirmButtonClicked = { _ ->
                    consumePromptRequest()
                },
                onExtensionSettingsLinkClicked = {
                    navController.navigate(
                        AddonsManagementFragmentDirections.actionGlobalToInstalledAddonDetailsFragment(it),
                    )
                    consumePromptRequest()
                },
            )
            dialog.show(fragmentManager, POST_INSTALLATION_DIALOG_FRAGMENT_TAG)

            return dialog
        }

        return null
    }

    @VisibleForTesting
    internal fun showDialog(
        title: String,
        message: String,
        url: String? = null,
    ): AlertDialog {
        context.let {
            var dialog: AlertDialog? = null
            val inflater = LayoutInflater.from(it)
            val view = inflater.inflate(R.layout.addon_installation_failed_dialog, null)
            val messageView = view.findViewById<TextView>(R.id.message)
            messageView.text = message

            if (url != null) {
                val linkView = view.findViewById<TextView>(R.id.link)
                linkView.visibility = View.VISIBLE
                linkView.setOnClickListener {
                    onLinkClicked(url, true) // shouldOpenInBrowser
                    dialog?.dismiss()
                }
            }

            dialog = MaterialAlertDialogBuilder(it)
                .setTitle(title)
                .setPositiveButton(android.R.string.ok) { _, _ -> }
                .setCancelable(false)
                .setView(view)
                .create()
                .withCenterAlignedButtons()
            dialog.show()

            return dialog
        }
    }

    companion object {
        private const val PERMISSIONS_DIALOG_FRAGMENT_TAG = "ADDONS_PERMISSIONS_DIALOG_FRAGMENT"
        private const val POST_INSTALLATION_DIALOG_FRAGMENT_TAG =
            "ADDONS_INSTALLATION_DIALOG_FRAGMENT"
        private const val AMO_BLOCKED_PAGE_URL = "${BuildConfig.AMO_BASE_URL}/android/blocked-addon/%s/"
    }
}
