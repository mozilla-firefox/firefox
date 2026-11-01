/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package org.mozilla.fenix.extension

import android.content.Context
import android.view.Gravity
import android.view.LayoutInflater
import android.view.View
import android.widget.TextView
import androidx.annotation.UiContext
import androidx.annotation.VisibleForTesting
import androidx.appcompat.R as appcompatR
import androidx.appcompat.app.AlertDialog
import androidx.fragment.app.FragmentManager
import androidx.navigation.NavController
import com.google.android.material.R as materialR
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.state.extension.PendingExtensionUpdate
import mozilla.components.browser.state.state.extension.WebExtensionPromptRequest
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.CancellableOperation
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.PermissionPromptResponse
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import mozilla.components.feature.addons.Addon
import mozilla.components.feature.addons.AddonManager
import mozilla.components.feature.addons.R as addonsR
import mozilla.components.feature.addons.ui.AddonDialogFragment
import mozilla.components.feature.addons.ui.AddonInstallationDialogFragment
import mozilla.components.feature.addons.ui.PermissionsDialogFragment
import mozilla.components.feature.addons.ui.PermissionsDialogMode
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.ktx.android.content.appVersionName
import mozilla.components.support.ktx.android.content.pixelSizeFor
import mozilla.components.support.webextensions.WebExtensionSupport
import mozilla.components.ui.widgets.withCenterAlignedButtons
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.R
import org.mozilla.fenix.addons.AddonsManagementFragmentDirections
import org.mozilla.fenix.addons.DownloadAddonDialogFragment
import org.mozilla.fenix.addons.DownloadAddonDialogFragmentArgs
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.settings.SupportUtils
import org.mozilla.fenix.theme.ThemeManager

/** Feature implementation for handling [WebExtensionPromptRequest] and showing the respective UI. */
class WebExtensionPromptFeature(
    private val store: BrowserStore,
    @param:UiContext private val context: Context,
    private val fragmentManager: FragmentManager,
    private val onLinkClicked: (String, Boolean) -> Unit,
    private val navController: NavController,
    private val addonManager: AddonManager = context.components.addonManager,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
) : LifecycleAwareFeature {

    /** Whether or not an add-on installation is in progress. */
    private var isInstallationInProgress = false
    private var downloadAddonOperation: CancellableOperation? = null
    private var scope: CoroutineScope? = null
    private var reviewRequestScope: CoroutineScope? = null

    /**
     * Job that completes once the [DownloadAddonDialogFragment] has been on screen for at least
     * [MIN_DOWNLOAD_DIALOG_DISPLAY_MS]. Used to gate dismissals and any follow-up dialogs (permissions, post-install,
     * error) so that the download dialog isn't shown and then immediately replaced, which users would see as a flicker.
     */
    private var minDownloadDialogDisplayJob: Job? = null

    /** Starts observing the selected session to listen for window requests and opens / closes tabs as needed. */
    override fun start() {
        scope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .mapNotNull { state ->
                        state.webExtensionPromptRequest
                    }
                    .distinctUntilChanged()
                    .collect { promptRequest ->
                        when (promptRequest) {
                            is WebExtensionPromptRequest.InstallationRequested -> {
                                handleInstallationStartedRequest(promptRequest)
                                consumePromptRequest()
                            }

                            is WebExtensionPromptRequest.AfterInstallation -> {
                                guardPromptActionWithDownloadDialogDelay {
                                    handleAfterInstallationRequest(promptRequest)
                                }
                            }

                            is WebExtensionPromptRequest.BeforeInstallation.InstallationFailed -> {
                                guardPromptActionWithDownloadDialogDelay {
                                    handleBeforeInstallationRequest(promptRequest)
                                    consumePromptRequest()
                                }
                            }
                        }
                    }
            }
        reviewRequestScope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .map { state -> state.pendingExtensionUpdateReviewRequest }
                    .distinctUntilChanged()
                    .filterNotNull()
                    .collect { extensionId -> handlePendingExtensionUpdateReviewRequest(extensionId) }
            }
        tryToReAttachButtonHandlersToPreviousDialog()
    }

    private fun handleInstallationStartedRequest(promptRequest: WebExtensionPromptRequest.InstallationRequested) {
        startInstallingAddon(
            addonDownloadUrl = promptRequest.url,
            addonInstallationSource = promptRequest.installationMethod,
        )

        showDownloadAddonDialog(
            addonDownloadUrl = promptRequest.url,
            addonName = promptRequest.name,
            addonImageUrl = promptRequest.iconUrl,
            addonInstallationSource = promptRequest.installationMethod,
        )
    }

    @VisibleForTesting
    internal fun handleAfterInstallationRequest(promptRequest: WebExtensionPromptRequest.AfterInstallation) {
        val installedState = addonManager.toInstalledState(promptRequest.extension)
        val addon = Addon.newFromWebExtension(promptRequest.extension, installedState)
        when (promptRequest) {
            is WebExtensionPromptRequest.AfterInstallation.Permissions.Required ->
                handleRequiredPermissionRequest(
                    addon,
                    promptRequest,
                )

            is WebExtensionPromptRequest.AfterInstallation.Permissions.Optional ->
                handleOptionalPermissionsRequest(
                    addon,
                    promptRequest,
                )

            is WebExtensionPromptRequest.AfterInstallation.PostInstallation -> handlePostInstallationRequest(addon)
        }
    }

    private fun handleBeforeInstallationRequest(promptRequest: WebExtensionPromptRequest.BeforeInstallation) {
        when (promptRequest) {
            is WebExtensionPromptRequest.BeforeInstallation.InstallationFailed -> {
                handleInstallationFailedRequest(exception = promptRequest.exception)
                consumePromptRequest()
            }
        }
    }

    private fun handlePostInstallationRequest(addon: Addon) {
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
        val shouldGrantWithoutPrompt =
            Addon.localizePermissions(
                    promptRequest.permissions,
                    context,
                )
                .isEmpty() && promptRequest.origins.isEmpty() && promptRequest.dataCollectionPermissions.isEmpty()

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
            mode = PermissionsDialogMode.OPTIONAL,
            permissions = promptRequest.permissions,
            origins = promptRequest.origins,
            dataCollectionPermissions = promptRequest.dataCollectionPermissions,
        )
    }

    @VisibleForTesting
    internal fun handleInstallationFailedRequest(exception: WebExtensionInstallException): AlertDialog? {
        val addonName = exception.extensionName ?: ""
        val appName = context.getString(R.string.app_name)

        var title = context.getString(addonsR.string.mozac_feature_addons_cant_install_extension)
        var url: String? = null
        val message =
            when (exception) {
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
                is WebExtensionInstallException.Unknown -> {
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
                    context.getString(addonsR.string.mozac_feature_addons_extension_failed_to_install_not_signed_error)
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

    /** Stops observing the selected session for incoming window requests. */
    override fun stop() {
        scope?.cancel()
        reviewRequestScope?.cancel()
    }

    @VisibleForTesting
    internal fun startInstallingAddon(
        addonDownloadUrl: String,
        addonInstallationSource: InstallationMethod,
    ) {
        downloadAddonOperation =
            addonManager.installAddon(
                url = addonDownloadUrl,
                installationMethod = addonInstallationSource,
                onSuccess = {
                    scope?.launch {
                        minDownloadDialogDisplayJob?.join()
                        findPreviousDownloadAddonDialogFragment()?.dismissAllowingStateLoss()
                    }
                },
                onError = {
                    scope?.launch {
                        minDownloadDialogDisplayJob?.join()
                        findPreviousDownloadAddonDialogFragment()?.dismissAllowingStateLoss()
                    }
                },
            )
    }

    @VisibleForTesting
    internal fun showDownloadAddonDialog(
        addonDownloadUrl: String,
        addonName: String?,
        addonImageUrl: String?,
        addonInstallationSource: InstallationMethod,
    ): DownloadAddonDialogFragment? {
        if (hasExistingDownloadAddonDialogFragment()) {
            return null
        }

        val dialog =
            DownloadAddonDialogFragment().apply {
                arguments =
                    DownloadAddonDialogFragmentArgs(
                            addonDownloadUrl = addonDownloadUrl,
                            addonName = addonName,
                            addonImageUrl = addonImageUrl,
                            addonInstallationSource = addonInstallationSource,
                        )
                        .toBundle()
            }
        dialog.onCancelled = ::handleDownloadAddonDialogCancelled
        dialog.show(fragmentManager, DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG)

        minDownloadDialogDisplayJob = scope?.launch {
            delay(MIN_DOWNLOAD_DIALOG_DISPLAY_MS)
        }

        return dialog
    }

    @VisibleForTesting
    internal fun showPermissionDialog(
        addon: Addon,
        promptRequest: WebExtensionPromptRequest.AfterInstallation.Permissions,
        mode: PermissionsDialogMode = PermissionsDialogMode.INSTALL,
        permissions: List<String> = emptyList(),
        origins: List<String> = emptyList(),
        dataCollectionPermissions: List<String> = emptyList(),
    ): PermissionsDialogFragment? =
        showPermissionDialog(
            addon = addon,
            mode = mode,
            permissions = permissions,
            origins = origins,
            dataCollectionPermissions = dataCollectionPermissions,
            promptInstanceId = promptRequest.instanceId,
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
        )

    private fun handlePendingExtensionUpdateReviewRequest(extensionId: String) {
        store.dispatch(WebExtensionAction.ConsumePendingExtensionUpdateReviewRequestAction)
        val pendingUpdate = store.state.pendingExtensionUpdates[extensionId] ?: return
        val extension = WebExtensionSupport.installedExtensions[extensionId] ?: return
        showUpdatePermissionDialog(
            Addon.newFromWebExtension(extension, addonManager.toInstalledState(extension)),
            pendingUpdate,
        )
    }

    /**
     * Shows the prompt for [pendingUpdate]. Cancelling the prompt leaves the update pending.
     *
     * @param addon The installed add-on the update is for.
     * @param pendingUpdate The update waiting for the user.
     */
    @VisibleForTesting
    internal fun showUpdatePermissionDialog(
        addon: Addon,
        pendingUpdate: PendingExtensionUpdate,
    ): PermissionsDialogFragment? =
        showPermissionDialog(
            addon = addon,
            mode = PermissionsDialogMode.UPDATE,
            permissions = pendingUpdate.permissions,
            origins = pendingUpdate.origins,
            dataCollectionPermissions = pendingUpdate.dataCollectionPermissions,
            promptInstanceId = pendingUpdate.instanceId,
            onPositiveButtonClicked = { _, _, _ -> acceptPendingExtensionUpdate(pendingUpdate.instanceId) },
        )

    private fun showPermissionDialog(
        addon: Addon,
        mode: PermissionsDialogMode,
        permissions: List<String>,
        origins: List<String>,
        dataCollectionPermissions: List<String>,
        promptInstanceId: String? = null,
        onPositiveButtonClicked: (Addon, Boolean, Boolean) -> Unit,
        onNegativeButtonClicked: (() -> Unit)? = null,
    ): PermissionsDialogFragment? {
        if (isInstallationInProgress || hasExistingPermissionDialogFragment()) {
            return null
        }

        val dialog =
            PermissionsDialogFragment.newInstance(
                addon = addon,
                mode = mode,
                promptInstanceId = promptInstanceId,
                permissions = permissions,
                origins = origins,
                dataCollectionPermissions = dataCollectionPermissions,
                promptsStyling =
                    AddonDialogFragment.PromptsStyling(
                        gravity = Gravity.BOTTOM,
                        shouldWidthMatchParent = true,
                        confirmButtonBackgroundColor =
                            ThemeManager.resolveAttribute(
                                appcompatR.attr.colorPrimary,
                                context,
                            ),
                        confirmButtonTextColor =
                            ThemeManager.resolveAttribute(
                                materialR.attr.colorOnPrimary,
                                context,
                            ),
                        confirmButtonDisabledBackgroundColor =
                            ThemeManager.resolveAttribute(
                                R.attr.actionPrimaryDisabled,
                                context,
                            ),
                        confirmButtonDisabledTextColor =
                            ThemeManager.resolveAttribute(
                                R.attr.textActionPrimaryDisabled,
                                context,
                            ),
                        confirmButtonRadius = context.pixelSizeFor(R.dimen.tab_corner_radius).toFloat(),
                        learnMoreLinkTextColor =
                            ThemeManager.resolveAttribute(
                                materialR.attr.colorTertiary,
                                context,
                            ),
                    ),
                onPositiveButtonClicked = onPositiveButtonClicked,
                onNegativeButtonClicked = onNegativeButtonClicked,
                onLearnMoreClicked = ::handleLearnMoreClicked,
            )
        dialog.show(fragmentManager, PERMISSIONS_DIALOG_FRAGMENT_TAG)

        return dialog
    }

    /** Installs the pending update identified by [instanceId]. Does nothing if it was answered or replaced. */
    private fun acceptPendingExtensionUpdate(instanceId: String?) {
        val pendingUpdate = store.state.pendingExtensionUpdates.values.find { it.instanceId == instanceId } ?: return

        store.dispatch(WebExtensionAction.RemovePendingExtensionUpdateAction(pendingUpdate.extensionId))
        pendingUpdate.complete(granted = true)
    }

    private fun handleLearnMoreClicked() {
        onLinkClicked(SupportUtils.getSumoURLForTopic(context, SupportUtils.SumoTopic.EXTENSION_PERMISSIONS), false)
    }

    private fun tryToReAttachButtonHandlersToPreviousDialog() {
        findPreviousDownloadAddonDialogFragment()?.let { dialog ->
            dialog.onCancelled = ::handleDownloadAddonDialogCancelled
        }

        reAttachPermissionDialog()

        findPreviousPostInstallationDialogFragment()?.let { dialog ->
            dialog.onDismissed = {
                store.state.webExtensionPromptRequest?.let { _ ->
                    consumePromptRequest()
                }
            }
        }
    }

    /**
     * Re-attaches a recreated permissions dialog to the prompt request or extension update with its
     * [PermissionsDialogFragment.promptInstanceId], or dismisses the dialog if neither has that id.
     */
    private fun reAttachPermissionDialog() {
        val dialog = findPreviousPermissionDialogFragment() ?: return
        val instanceId = dialog.promptInstanceId
        val promptRequest = store.state.webExtensionPromptRequest
        if (
            promptRequest is WebExtensionPromptRequest.AfterInstallation.Permissions &&
                promptRequest.instanceId == instanceId
        ) {
            dialog.onPositiveButtonClicked = { _, privateBrowsingAllowed, technicalAndInteractionDataGranted ->
                handlePermissions(
                    promptRequest,
                    granted = true,
                    privateBrowsingAllowed,
                    technicalAndInteractionDataGranted,
                )
            }
            dialog.onNegativeButtonClicked = {
                handlePermissions(
                    promptRequest,
                    granted = false,
                    privateBrowsingAllowed = false,
                    technicalAndInteractionDataGranted = false,
                )
            }
            dialog.onLearnMoreClicked = ::handleLearnMoreClicked
        } else if (store.state.pendingExtensionUpdates.values.any { it.instanceId == instanceId }) {
            dialog.onPositiveButtonClicked = { _, _, _ -> acceptPendingExtensionUpdate(instanceId) }
            dialog.onLearnMoreClicked = ::handleLearnMoreClicked
        } else {
            // Dismissed now, so that the request waiting in the store can show its own dialog.
            dialog.dismissNow()
        }
    }

    private fun handleDownloadAddonDialogCancelled() {
        scope?.launch(mainDispatcher) {
            downloadAddonOperation?.cancel()?.await()
            minDownloadDialogDisplayJob?.cancel()
            minDownloadDialogDisplayJob = null
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
                val response =
                    PermissionPromptResponse(
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

    private fun hasExistingDownloadAddonDialogFragment(): Boolean {
        return findPreviousDownloadAddonDialogFragment() != null
    }

    private fun hasExistingPermissionDialogFragment(): Boolean {
        return findPreviousPermissionDialogFragment() != null
    }

    private fun hasExistingAddonPostInstallationDialogFragment(): Boolean {
        return fragmentManager.findFragmentByTag(POST_INSTALLATION_DIALOG_FRAGMENT_TAG)
            as? AddonInstallationDialogFragment != null
    }

    private fun findPreviousDownloadAddonDialogFragment(): DownloadAddonDialogFragment? {
        return fragmentManager.findFragmentByTag(DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG) as? DownloadAddonDialogFragment
    }

    private fun findPreviousPermissionDialogFragment(): PermissionsDialogFragment? {
        return fragmentManager.findFragmentByTag(PERMISSIONS_DIALOG_FRAGMENT_TAG) as? PermissionsDialogFragment
    }

    private fun findPreviousPostInstallationDialogFragment(): AddonInstallationDialogFragment? {
        return fragmentManager.findFragmentByTag(POST_INSTALLATION_DIALOG_FRAGMENT_TAG)
            as? AddonInstallationDialogFragment
    }

    @VisibleForTesting
    internal fun showPostInstallationDialog(addon: Addon): AddonInstallationDialogFragment? {
        if (!isInstallationInProgress && !hasExistingAddonPostInstallationDialogFragment()) {
            val dialog =
                AddonInstallationDialogFragment.newInstance(
                    addon = addon,
                    promptsStyling =
                        AddonDialogFragment.PromptsStyling(
                            gravity = Gravity.BOTTOM,
                            shouldWidthMatchParent = true,
                            confirmButtonBackgroundColor =
                                ThemeManager.resolveAttribute(
                                    appcompatR.attr.colorPrimary,
                                    context,
                                ),
                            confirmButtonTextColor =
                                ThemeManager.resolveAttribute(
                                    materialR.attr.colorOnPrimary,
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
                            AddonsManagementFragmentDirections.actionGlobalToInstalledAddonDetailsFragment(it)
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

            dialog =
                MaterialAlertDialogBuilder(it)
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

    /**
     * Execute the given [action] after [MIN_DOWNLOAD_DIALOG_DISPLAY_MS] if the download dialog is shown or consume any
     * prompt requests received before [MIN_DOWNLOAD_DIALOG_DISPLAY_MS] if the download dialog is canceled.
     */
    private fun guardPromptActionWithDownloadDialogDelay(action: () -> Unit) {
        val delayJob = minDownloadDialogDisplayJob

        scope?.launch {
            delayJob?.join()

            if (delayJob != null && delayJob.isCancelled) {
                consumePromptRequest()
            } else {
                action()
            }
        }
    }

    companion object {
        private const val DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG = "DOWNLOAD_ADDON_DIALOG_FRAGMENT_TAG"
        private const val PERMISSIONS_DIALOG_FRAGMENT_TAG = "ADDONS_PERMISSIONS_DIALOG_FRAGMENT"
        private const val POST_INSTALLATION_DIALOG_FRAGMENT_TAG = "ADDONS_INSTALLATION_DIALOG_FRAGMENT"
        private const val AMO_BLOCKED_PAGE_URL = "${BuildConfig.AMO_BASE_URL}/android/blocked-addon/%s/"

        @VisibleForTesting internal const val MIN_DOWNLOAD_DIALOG_DISPLAY_MS = 1500L
    }
}
