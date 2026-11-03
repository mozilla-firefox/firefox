/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.sitepermissions

import android.Manifest.permission.ACCESS_COARSE_LOCATION
import android.Manifest.permission.ACCESS_FINE_LOCATION
import android.Manifest.permission.CAMERA
import android.Manifest.permission.RECORD_AUDIO
import android.content.Context
import android.content.pm.PackageManager
import androidx.annotation.ColorRes
import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.annotation.VisibleForTesting
import androidx.fragment.app.FragmentManager
import java.net.URL
import java.security.InvalidParameterException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.distinctUntilChangedBy
import kotlinx.coroutines.flow.mapNotNull
import kotlinx.coroutines.launch
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.AutoPlayAudibleBlockingAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.AutoPlayAudibleChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.AutoPlayInAudibleBlockingAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.AutoPlayInAudibleChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.CameraChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.LocalDeviceAccessChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.LocalNetworkAccessChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.LocationChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.MediaKeySystemAccesChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.MicrophoneChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.NotificationChangedAction
import mozilla.components.browser.state.action.ContentAction.UpdatePermissionHighlightsStateAction.PersistentStorageChangedAction
import mozilla.components.browser.state.action.SystemPermissionRequestAction
import mozilla.components.browser.state.selector.findTabOrCustomTabOrSelectedTab
import mozilla.components.browser.state.state.ContentState
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.permission.Permission
import mozilla.components.concept.engine.permission.Permission.AppAudio
import mozilla.components.concept.engine.permission.Permission.AppCamera
import mozilla.components.concept.engine.permission.Permission.AppLocationCoarse
import mozilla.components.concept.engine.permission.Permission.AppLocationFine
import mozilla.components.concept.engine.permission.Permission.ContentAudioCapture
import mozilla.components.concept.engine.permission.Permission.ContentAudioMicrophone
import mozilla.components.concept.engine.permission.Permission.ContentAutoPlayAudible
import mozilla.components.concept.engine.permission.Permission.ContentAutoPlayInaudible
import mozilla.components.concept.engine.permission.Permission.ContentCrossOriginStorageAccess
import mozilla.components.concept.engine.permission.Permission.ContentGeoLocation
import mozilla.components.concept.engine.permission.Permission.ContentLocalDeviceAccess
import mozilla.components.concept.engine.permission.Permission.ContentLocalNetworkAccess
import mozilla.components.concept.engine.permission.Permission.ContentMediaKeySystemAccess
import mozilla.components.concept.engine.permission.Permission.ContentNotification
import mozilla.components.concept.engine.permission.Permission.ContentPersistentStorage
import mozilla.components.concept.engine.permission.Permission.ContentVideoCamera
import mozilla.components.concept.engine.permission.Permission.ContentVideoCapture
import mozilla.components.concept.engine.permission.PermissionRequest
import mozilla.components.concept.engine.permission.SitePermissions
import mozilla.components.concept.engine.permission.SitePermissions.Status.ALLOWED
import mozilla.components.concept.engine.permission.SitePermissions.Status.BLOCKED
import mozilla.components.concept.engine.permission.SitePermissionsStorage
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.feature.tabs.TabsUseCases.SelectOrAddUseCase
import mozilla.components.lib.state.ext.flowScoped
import mozilla.components.support.base.feature.LifecycleAwareFeature
import mozilla.components.support.base.feature.OnNeedToRequestPermissions
import mozilla.components.support.base.feature.PermissionsFeature
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.isPermissionGranted
import mozilla.components.support.ktx.kotlin.getOrigin
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.support.ktx.kotlinx.coroutines.flow.filterChanged
import mozilla.components.ui.icons.R as iconsR

internal const val PROMPT_FRAGMENT_TAG = "mozac_feature_sitepermissions_prompt_dialog"

/**
 * This feature will collect [PermissionRequest] from [ContentState] and display suitable
 * [SitePermissionsDialogFragment]. Once the dialog is closed the [PermissionRequest] will be consumed.
 *
 * @property context a reference to the context.
 * @property sessionId optional sessionId to be observed if null the selected session will be observed.
 * @property storage the object in charge of persisting all the [SitePermissions] objects.
 * @property sitePermissionsRules indicates how permissions should behave per permission category.
 * @property fragmentManager a reference to a [FragmentManager], used to show permissions prompts.
 * @property promptsStyling optional styling for prompts.
 * @property dialogConfig optional customization for dialog initial state. See [DialogConfig].
 * @property onNeedToRequestPermissions a callback invoked when permissions need to be requested. Once the request is
 *   completed, [onPermissionsResult] needs to be invoked.
 * @property onShouldShowRequestPermissionRationale a callback that allows the feature to query the
 *   ActivityCompat.shouldShowRequestPermissionRationale or the Fragment.shouldShowRequestPermissionRationale values.
 * @property exitFullscreenUseCase optional the use case in charge of exiting fullscreen
 * @property shouldShowDoNotAskAgainCheckBox optional Visibility for Do not ask again Checkbox
 * @property shouldHide Whether the site permissions prompt should be hidden.
 */
@OptIn(ExperimentalAndroidComponentsApi::class) // permissionRequest.notifyShown()
@Suppress("TooManyFunctions", "LargeClass")
class SitePermissionsFeature(
    private val context: Context,
    @set:VisibleForTesting internal var sessionId: String? = null,
    private val storage: SitePermissionsStorage,
    var sitePermissionsRules: SitePermissionsRules? = null,
    private val fragmentManager: FragmentManager,
    var promptsStyling: PromptsStyling? = null,
    private val dialogConfig: DialogConfig? = null,
    override val onNeedToRequestPermissions: OnNeedToRequestPermissions,
    val onShouldShowRequestPermissionRationale: (permission: String) -> Boolean,
    private val store: BrowserStore,
    private val coroutineScope: CoroutineScope,
    private val exitFullscreenUseCase: SessionUseCases.ExitFullScreenUseCase = SessionUseCases(store).exitFullscreen,
    private val shouldShowDoNotAskAgainCheckBox: Boolean = true,
    private val mainDispatcher: CoroutineDispatcher = Dispatchers.Main,
    private val shouldHide: () -> Boolean = { false },
) : LifecycleAwareFeature, PermissionsFeature {
    @VisibleForTesting
    internal val selectOrAddUseCase by lazy {
        SelectOrAddUseCase(store)
    }

    /**
     * Provider that helps create the "learn more" url for a given permission.
     *
     * This provider is automatically unregistered when the lifecycle `onStop()` is called. As a result, it is
     * recommended to set this in the corresponding `onStart()` lifecycle method.
     */
    var learnMoreUrlProvider: SitePermissionsLearnMoreUrlProvider? = null

    private val logger = Logger("SitePermissionsFeature")

    private var sitePermissionScope: CoroutineScope? = null
    private var appPermissionScope: CoroutineScope? = null
    private var loadingScope: CoroutineScope? = null

    override fun start() {
        fragmentManager.findFragmentByTag(PROMPT_FRAGMENT_TAG)?.let { fragment ->
            // There's still a [SitePermissionsDialogFragment] visible from the last time. Re-attach
            // this feature so that the fragment can invoke the callback on this feature once the user
            // makes a selection. This can happen when the app was in the background and on resume
            // the activity and fragments get recreated.
            reattachFragment(fragment as SitePermissionsDialogFragment)
        }

        setupPermissionRequestsCollector()
        setupAppPermissionRequestsCollector()
        setupLoadingCollector()
        maybeSetUpDefaultLearnMoreUrlProvider()
    }

    /**
     * Conditionally sets a default value for [learnMoreUrlProvider] in case nothing was set at the start.
     *
     * The [DefaultSitePermissionsLearnMoreUrlProvider] helps to preserve existing behavior for the
     * [ContentCrossOriginStorageAccess] permission
     */
    private fun maybeSetUpDefaultLearnMoreUrlProvider() {
        if (learnMoreUrlProvider == null) {
            learnMoreUrlProvider = DefaultSitePermissionsLearnMoreUrlProvider()
        }
    }

    @VisibleForTesting
    internal fun setupLoadingCollector() {
        loadingScope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .mapNotNull { state ->
                        state.findTabOrCustomTabOrSelectedTab(sessionId)
                    }
                    .distinctUntilChangedBy { it.content.loading }
                    .collect { tab ->
                        if (tab.content.loading) {
                            // Clears stale permission indicators in the toolbar,
                            // after the session starts loading.
                            store.dispatch(UpdatePermissionHighlightsStateAction.Reset(tab.id))
                            storage.clearTemporaryPermissions()
                        }
                    }
            }
    }

    @VisibleForTesting
    internal fun setupAppPermissionRequestsCollector() {
        appPermissionScope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .mapNotNull { state ->
                        state.findTabOrCustomTabOrSelectedTab(sessionId)?.content?.appPermissionRequestsList
                    }
                    .filterChanged { it }
                    .collect { appPermissionRequest ->
                        val permissions = appPermissionRequest.permissions.map { it.id ?: "" }
                        onNeedToRequestPermissions(permissions.toTypedArray())
                    }
            }
    }

    @VisibleForTesting
    internal fun setupPermissionRequestsCollector() {
        sitePermissionScope =
            store.flowScoped(dispatcher = mainDispatcher) { flow ->
                flow
                    .mapNotNull { state ->
                        state.findTabOrCustomTabOrSelectedTab(sessionId)?.content?.permissionRequestsList
                    }
                    .filterChanged { it }
                    .collect { permissionRequest ->
                        val origin: String = permissionRequest.uri?.getOrigin().orEmpty()

                        if (origin.isEmpty()) {
                            permissionRequest.consumeAndReject()
                        } else {
                            if (permissionRequest.permissions.all { it.isSupported() }) {
                                onContentPermissionRequested(
                                    permissionRequest,
                                    origin,
                                )
                            } else {
                                permissionRequest.consumeAndReject()
                            }
                        }
                    }
            }
    }

    private fun PermissionRequest.consumeAndReject() {
        consumePermissionRequest(this)
        this.reject()
    }

    @VisibleForTesting
    internal fun consumePermissionRequest(
        permissionRequest: PermissionRequest,
        optionalSessionId: String? = null,
    ) {
        val thisSessionId = optionalSessionId ?: getCurrentTabState()?.id
        thisSessionId?.let { sessionId ->
            store.dispatch(ContentAction.ConsumePermissionsRequest(sessionId, permissionRequest))
        }
    }

    @VisibleForTesting
    internal fun consumeAppPermissionRequest(
        appPermissionRequest: PermissionRequest,
        optionalSessionId: String? = null,
    ) {
        val thisSessionId = optionalSessionId ?: getCurrentTabState()?.id
        thisSessionId?.let { sessionId ->
            store.dispatch(
                ContentAction.ConsumeAppPermissionsRequest(
                    sessionId,
                    appPermissionRequest,
                )
            )
        }
    }

    override fun stop() {
        sitePermissionScope?.cancel()
        appPermissionScope?.cancel()
        loadingScope?.cancel()
        storage.clearTemporaryPermissions()
        learnMoreUrlProvider = null
        if (shouldHide()) {
            hideSitePermissionsPrompt()
        }
    }

    /**
     * Notifies the feature that the permissions requested were completed.
     *
     * @param grantResults the grant results for the corresponding permissions
     * @see [onNeedToRequestPermissions].
     */
    @Suppress("NestedBlockDepth")
    override fun onPermissionsResult(permissions: Array<String>, grantResults: IntArray) {
        val currentContentState = getCurrentContentState()
        val appPermissionRequest = findRequestedAppPermission(permissions) ?: return

        if (currentContentState == null) {
            store.dispatch(SystemPermissionRequestAction.SystemPermissionStateRequestNotInProgress)
            return
        }

        if (grantResults.isNotEmpty() && areAllPermissionsGranted(permissions, grantResults)) {
            appPermissionRequest.grant()
        } else {
            appPermissionRequest.reject()
            permissions.forEach { systemPermission ->
                if (!onShouldShowRequestPermissionRationale(systemPermission)) {
                    // The system permission is denied permanently
                    storeSitePermissions(
                        currentContentState,
                        appPermissionRequest,
                        status = BLOCKED,
                    )
                }
            }
        }
        store.dispatch(SystemPermissionRequestAction.SystemPermissionStateRequestNotInProgress)
        consumeAppPermissionRequest(appPermissionRequest)
    }

    /**
     * If caller is asking for fine and coarse location, say the call succeeded as long as the app was granted either
     * permission. (practically speaking this means the user only granted access to approximate location)
     *
     * @param permissions the requested permissions
     * @param grantResults the grant results for the corresponding permissions
     * @return true if all the requested permissions were granted
     */
    private fun areAllPermissionsGranted(
        permissions: Array<String>,
        grantResults: IntArray,
    ): Boolean =
        permissions
            .mapIndexed { index, permission ->
                permission to (grantResults[index] == PackageManager.PERMISSION_GRANTED)
            }
            .toMap()
            .let { grantedPermissionsMap ->
                if (
                    grantedPermissionsMap.containsKey(ACCESS_FINE_LOCATION) &&
                        grantedPermissionsMap.containsKey(ACCESS_COARSE_LOCATION)
                ) {
                    val locationPermissions = arrayOf(ACCESS_COARSE_LOCATION, ACCESS_FINE_LOCATION)
                    val isAnyLocationPermissionGranted = grantedPermissionsMap.any {
                        it.key in locationPermissions && it.value
                    }
                    val areAllNonLocationPermissionsGranted =
                        grantedPermissionsMap.filterKeys { it !in locationPermissions }.all { it.value }
                    isAnyLocationPermissionGranted && areAllNonLocationPermissionsGranted
                } else {
                    grantedPermissionsMap.all { it.value }
                }
            }

    @VisibleForTesting internal fun getCurrentContentState() = getCurrentTabState()?.content

    @VisibleForTesting internal fun getCurrentTabState() = store.state.findTabOrCustomTabOrSelectedTab(sessionId)

    @VisibleForTesting
    internal fun findRequestedAppPermission(permissions: Array<String>): PermissionRequest? {
        return getCurrentContentState()?.appPermissionRequestsList?.find {
            permissions.contains(it.permissions.first().id)
        }
    }

    @VisibleForTesting
    internal fun findRequestedPermission(permissionId: String): PermissionRequest? {
        return getCurrentContentState()?.permissionRequestsList?.find {
            it.id == permissionId
        }
    }

    @VisibleForTesting
    internal fun onContentPermissionGranted(
        permissionRequest: PermissionRequest,
        shouldStore: Boolean,
        onCheckSystemNotificationPermission: () -> Unit = {},
    ) {
        permissionRequest.grant()
        if (shouldStore) {
            getCurrentContentState()?.let { contentState ->
                storeSitePermissions(contentState, permissionRequest, ALLOWED)
            }
            val requestedPermission = permissionRequest.permissions[0]
            if (requestedPermission is ContentNotification) {
                onCheckSystemNotificationPermission()
            }
        } else {
            storage.saveTemporary(permissionRequest)
        }
    }

    internal fun onPositiveButtonPress(
        permissionId: String,
        sessionId: String,
        shouldStore: Boolean,
        onCheckSystemNotificationPermission: () -> Unit = {},
    ) {
        findRequestedPermission(permissionId)?.let { permissionRequest ->
            consumePermissionRequest(permissionRequest, sessionId)
            onContentPermissionGranted(permissionRequest, shouldStore) {
                onCheckSystemNotificationPermission()
            }

            if (!permissionRequest.containsVideoAndAudioSources()) {
                emitPermissionAllowed(permissionRequest.permissions.first())
            } else {
                emitPermissionsAllowed(permissionRequest.permissions)
            }
        }
    }

    internal fun onNegativeButtonPress(
        permissionId: String,
        sessionId: String,
        shouldStore: Boolean,
    ) {
        findRequestedPermission(permissionId)?.let { permissionRequest ->
            consumePermissionRequest(permissionRequest, sessionId)
            onContentPermissionDeny(permissionRequest, shouldStore)

            if (!permissionRequest.containsVideoAndAudioSources()) {
                emitPermissionDenied(permissionRequest.permissions.first())
            } else {
                emitPermissionsDenied(permissionRequest.permissions)
            }
        }
    }

    internal fun onLearnMorePress(
        permissionId: String,
        sessionId: String,
        learnMoreLink: String,
    ) {
        findRequestedPermission(permissionId)?.let { permissionRequest ->
            consumePermissionRequest(permissionRequest, sessionId)
            onContentPermissionDeny(permissionRequest, false)

            if (learnMoreLink.isNotEmpty()) {
                store.state.findTabOrCustomTabOrSelectedTab(sessionId)?.let {
                    selectOrAddUseCase.invoke(
                        url = learnMoreLink,
                        private = it.content.private,
                        source = SessionState.Source.Internal.TextSelection,
                    )
                }
            }
        }
    }

    internal fun onDismiss(
        permissionId: String,
        sessionId: String,
    ) {
        findRequestedPermission(permissionId)?.let { permissionRequest ->
            // When the screen is locked, shouldHide() is true.
            // Interactions with the UnlockScreen should not dismiss the prompt.
            if (!shouldHide()) {
                consumePermissionRequest(permissionRequest, sessionId)
            }
            onContentPermissionDeny(permissionRequest, false)
        }
    }

    internal fun storeSitePermissions(
        contentState: ContentState,
        request: PermissionRequest,
        status: SitePermissions.Status,
        coroutineScope: CoroutineScope = this@SitePermissionsFeature.coroutineScope,
    ) {
        if (contentState.private) {
            return
        }
        updatePermissionToolbarIndicator(request, status, true)
        coroutineScope.launch {
            request.uri?.getOrigin()?.let { origin ->
                var sitePermissions = storage.findSitePermissionsBy(origin, private = false)

                if (sitePermissions == null) {
                    sitePermissions =
                        request.toSitePermissions(
                            origin,
                            status = status,
                            permissions = request.permissions,
                        )
                    storage.save(sitePermissions, request, private = false)
                } else {
                    sitePermissions = request.toSitePermissions(origin, status, sitePermissions)
                    storage.update(sitePermissions = sitePermissions, private = false)
                }
            }
        }
    }

    internal fun onContentPermissionDeny(
        permissionRequest: PermissionRequest,
        shouldStore: Boolean,
    ) {
        permissionRequest.reject()
        if (shouldStore) {
            getCurrentContentState()?.let { contentState ->
                storeSitePermissions(contentState, permissionRequest, BLOCKED)
            }
        } else {
            storage.saveTemporary(permissionRequest)
        }
    }

    internal suspend fun onContentPermissionRequested(
        permissionRequest: PermissionRequest,
        origin: String,
    ): SitePermissionsDialogFragment? {
        // We want to warranty that all media permissions have the required system
        // permissions are granted first, otherwise, we reject the request
        if (permissionRequest.isMedia && !permissionRequest.areAllMediaPermissionsGranted) {
            permissionRequest.reject()
            consumePermissionRequest(permissionRequest)
            return null
        }
        val tab = store.state.findTabOrCustomTabOrSelectedTab(sessionId)
        if (tab == null) {
            logger.error("Unable to find a tab for $sessionId rejecting the prompt request")
            permissionRequest.reject()
            consumePermissionRequest(permissionRequest)
            return null
        }

        val permissionFromStorage = storage.findSitePermissionsBy(origin, private = tab.content.private)
        val prompt =
            if (shouldApplyRules(permissionFromStorage)) {
                handleRuledFlow(permissionRequest, origin)
            } else {
                handleNoRuledFlow(permissionFromStorage, permissionRequest, origin)
            }

        return if (prompt == null || cannotShowPrompt()) {
            null
        } else {
            // If we are in fullscreen, then exit to show the permission prompt.
            // This won't have any effect if we are not in fullscreen.
            exitFullscreenUseCase.invoke(tab.id)
            prompt.show(fragmentManager, PROMPT_FRAGMENT_TAG)
            permissionRequest.notifyShown()
            prompt
        }
    }

    /** Whether a prompt cannot currently be committed to [fragmentManager]. */
    private fun cannotShowPrompt(): Boolean = fragmentManager.isDestroyed || fragmentManager.isStateSaved

    @VisibleForTesting
    internal fun handleNoRuledFlow(
        permissionFromStorage: SitePermissions?,
        permissionRequest: PermissionRequest,
        origin: String,
    ): SitePermissionsDialogFragment? {
        return if (shouldShowPrompt(permissionRequest, permissionFromStorage)) {
            createPrompt(permissionRequest, origin)
        } else {
            val status =
                if (permissionFromStorage.isGranted(permissionRequest)) {
                    permissionRequest.grant()
                    ALLOWED
                } else {
                    permissionRequest.reject()
                    BLOCKED
                }
            updatePermissionToolbarIndicator(
                permissionRequest,
                status,
                permissionFromStorage != null,
            )
            consumePermissionRequest(permissionRequest)
            null
        }
    }

    @VisibleForTesting
    internal fun shouldShowPrompt(
        permissionRequest: PermissionRequest,
        permissionFromStorage: SitePermissions?,
    ): Boolean {
        return if (permissionRequest.isForAutoplay()) {
            false
        } else {
            (permissionFromStorage == null || !permissionRequest.doNotAskAgain(permissionFromStorage))
        }
    }

    @VisibleForTesting
    internal fun handleRuledFlow(
        permissionRequest: PermissionRequest,
        origin: String,
    ): SitePermissionsDialogFragment? {
        return when (sitePermissionsRules?.getActionFrom(permissionRequest)) {
            SitePermissionsRules.Action.ALLOWED -> {
                permissionRequest.grant()
                consumePermissionRequest(permissionRequest)
                updatePermissionToolbarIndicator(permissionRequest, ALLOWED)
                null
            }
            SitePermissionsRules.Action.BLOCKED -> {
                permissionRequest.reject()
                consumePermissionRequest(permissionRequest)
                updatePermissionToolbarIndicator(permissionRequest, BLOCKED)
                null
            }
            SitePermissionsRules.Action.ASK_TO_ALLOW -> {
                createPrompt(permissionRequest, origin)
            }
            null -> {
                consumePermissionRequest(permissionRequest)
                null
            }
        }
    }

    @VisibleForTesting
    @Suppress("CyclomaticComplexMethod")
    internal fun updatePermissionToolbarIndicator(
        request: PermissionRequest,
        value: SitePermissions.Status,
        permanent: Boolean = false,
    ) {
        val isAutoPlayAudibleBlocking: Boolean? = if (request.isForAutoplayAudible()) value == BLOCKED else null
        val isAutoPlayInAudibleBlocking: Boolean? = if (request.isForAutoplayInaudible()) value == BLOCKED else null

        getCurrentTabState()?.let { tab ->
            // At the moment, we don't have APIs for controlling temporary permissions,
            // after they are ALLOWED/BLOCKED, for this reason, we are not notifying users
            // when permissions have changed from their default values,
            // as they are not going have a way to change permissions.
            // Either way, temporary permissions have to be ALLOWED/BLOCKED per sessions
            // users are already aware of them.
            // The autoplay permissions work a bit different, as it is a global permissions,
            // users are never prompt to select its value, and it can't be temporary,
            // they only can change it's value per site persistently.
            if (permanent || request.isForAutoplay()) {
                val action =
                    when {
                        request.isForNotification() ->
                            NotificationChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.notification?.toStatus(),
                            )
                        request.isForCamera() ->
                            CameraChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.camera?.toStatus(),
                            )
                        request.isForLocation() ->
                            LocationChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.location?.toStatus(),
                            )
                        request.isForMicrophone() ->
                            MicrophoneChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.microphone?.toStatus(),
                            )
                        request.isForPersistentStorage() ->
                            PersistentStorageChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.persistentStorage?.toStatus(),
                            )
                        request.isForMediaKeySystemAccess() ->
                            MediaKeySystemAccesChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.mediaKeySystemAccess?.toStatus(),
                            )
                        request.isForLocalDeviceAccess() ->
                            LocalDeviceAccessChangedAction(
                                tabId = tab.id,
                                value = value != sitePermissionsRules?.localDeviceAccess?.toStatus(),
                            )
                        request.isForLocalNetworkAccess() ->
                            LocalNetworkAccessChangedAction(
                                tabId = tab.id,
                                value = value != sitePermissionsRules?.localNetworkAccess?.toStatus(),
                            )
                        request.isForAutoplayAudible() ->
                            AutoPlayAudibleChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.autoplayAudible?.toAutoplayStatus()?.toStatus(),
                            )
                        request.isForAutoplayInaudible() ->
                            AutoPlayInAudibleChangedAction(
                                tab.id,
                                value != sitePermissionsRules?.autoplayInaudible?.toAutoplayStatus()?.toStatus(),
                            )
                        else -> null
                    }
                action?.let {
                    store.dispatch(it)
                }
            }
            isAutoPlayAudibleBlocking?.let {
                store.dispatch(AutoPlayAudibleBlockingAction(tab.id, it))
            }
            isAutoPlayInAudibleBlocking?.let {
                store.dispatch(AutoPlayInAudibleBlockingAction(tab.id, it))
            }
        }
    }

    @VisibleForTesting
    internal fun shouldApplyRules(permissionFromStorage: SitePermissions?) =
        sitePermissionsRules != null && permissionFromStorage == null

    private fun PermissionRequest.doNotAskAgain(permissionFromStore: SitePermissions): Boolean {
        return permissions.any { permission ->
            when (permission) {
                is ContentGeoLocation -> {
                    permissionFromStore.location.doNotAskAgain()
                }
                is ContentNotification -> {
                    permissionFromStore.notification.doNotAskAgain()
                }
                is ContentAudioCapture,
                is ContentAudioMicrophone -> {
                    permissionFromStore.microphone.doNotAskAgain()
                }
                is ContentVideoCamera,
                is ContentVideoCapture -> {
                    permissionFromStore.camera.doNotAskAgain()
                }
                is ContentPersistentStorage -> {
                    permissionFromStore.localStorage.doNotAskAgain()
                }
                is ContentMediaKeySystemAccess -> {
                    permissionFromStore.mediaKeySystemAccess.doNotAskAgain()
                }
                is ContentCrossOriginStorageAccess -> {
                    permissionFromStore.crossOriginStorageAccess.doNotAskAgain()
                }
                is ContentLocalDeviceAccess -> {
                    permissionFromStore.localDeviceAccess.doNotAskAgain()
                }
                is ContentLocalNetworkAccess -> {
                    permissionFromStore.localNetworkAccess.doNotAskAgain()
                }
                else -> false
            }
        }
    }

    private fun PermissionRequest.toSitePermissions(
        origin: String,
        status: SitePermissions.Status,
        initialSitePermission: SitePermissions = getInitialSitePermissions(origin),
        permissions: List<Permission> = this.permissions,
    ): SitePermissions {
        var sitePermissions = initialSitePermission
        for (permission in permissions) {
            sitePermissions = updateSitePermissionsStatus(status, permission, sitePermissions)
        }
        return sitePermissions
    }

    @VisibleForTesting
    internal fun getInitialSitePermissions(origin: String): SitePermissions {
        val rules = sitePermissionsRules
        return rules?.toSitePermissions(origin) ?: SitePermissions(origin)
    }

    private fun PermissionRequest.isForAutoplay() =
        this.permissions.any { it is ContentAutoPlayInaudible || it is ContentAutoPlayAudible }

    private fun PermissionRequest.isForNotification() = this.permissions.any { it is ContentNotification }

    private fun PermissionRequest.isForCamera() =
        this.permissions.any {
            it is ContentVideoCamera || it is ContentVideoCapture || it is AppCamera
        }

    private fun PermissionRequest.isForAutoplayInaudible() = this.permissions.any { it is ContentAutoPlayInaudible }

    private fun PermissionRequest.isForAutoplayAudible() = this.permissions.any { it is ContentAutoPlayAudible }

    private fun PermissionRequest.isForLocation() =
        this.permissions.any {
            it is ContentGeoLocation || it is AppLocationCoarse || it is AppLocationFine
        }

    private fun PermissionRequest.isForMicrophone() =
        this.permissions.any {
            it is ContentAudioCapture || it is ContentAudioMicrophone || it is AppAudio
        }

    private fun PermissionRequest.isForPersistentStorage() = this.permissions.any { it is ContentPersistentStorage }

    private fun PermissionRequest.isForMediaKeySystemAccess() =
        this.permissions.any { it is ContentMediaKeySystemAccess }

    private fun PermissionRequest.isForLocalDeviceAccess() = this.permissions.any { it is ContentLocalDeviceAccess }

    private fun PermissionRequest.isForLocalNetworkAccess() = this.permissions.any { it is ContentLocalNetworkAccess }

    @VisibleForTesting
    internal fun updateSitePermissionsStatus(
        status: SitePermissions.Status,
        permission: Permission,
        sitePermissions: SitePermissions,
    ): SitePermissions {
        return when (permission) {
            is ContentGeoLocation,
            is AppLocationCoarse,
            is AppLocationFine -> {
                sitePermissions.copy(location = status)
            }
            is ContentNotification -> {
                sitePermissions.copy(notification = status)
            }
            is ContentAudioCapture,
            is ContentAudioMicrophone,
            is AppAudio -> {
                sitePermissions.copy(microphone = status)
            }
            is ContentVideoCamera,
            is ContentVideoCapture,
            is AppCamera -> {
                sitePermissions.copy(camera = status)
            }
            is ContentAutoPlayAudible -> {
                sitePermissions.copy(autoplayAudible = status.toAutoplayStatus())
            }
            is ContentAutoPlayInaudible -> {
                sitePermissions.copy(autoplayInaudible = status.toAutoplayStatus())
            }
            is ContentPersistentStorage -> {
                sitePermissions.copy(localStorage = status)
            }
            is ContentMediaKeySystemAccess -> {
                sitePermissions.copy(mediaKeySystemAccess = status)
            }
            is ContentCrossOriginStorageAccess -> {
                sitePermissions.copy(crossOriginStorageAccess = status)
            }
            is ContentLocalDeviceAccess -> {
                sitePermissions.copy(localDeviceAccess = status)
            }
            is ContentLocalNetworkAccess -> {
                sitePermissions.copy(localNetworkAccess = status)
            }
            else -> throw InvalidParameterException("$permission is not a valid permission.")
        }
    }

    @VisibleForTesting
    internal fun createPrompt(
        permissionRequest: PermissionRequest,
        origin: String,
    ): SitePermissionsDialogFragment {
        return if (!permissionRequest.containsVideoAndAudioSources()) {
            val permission = permissionRequest.permissions.first()
            handlingSingleContentPermissions(permissionRequest, permission, origin).also {
                emitPermissionDialogDisplayed(permission)
            }
        } else {
            createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    R.string.mozac_feature_sitepermissions_camera_and_microphone,
                    iconsR.drawable.mozac_ic_microphone_24,
                    showDoNotAskAgainCheckBox = shouldShowDoNotAskAgainCheckBox,
                    shouldSelectRememberChoice =
                        dialogConfig?.shouldPreselectDoNotAskAgain ?: DialogConfig.DEFAULT_PRESELECT_DO_NOT_ASK_AGAIN,
                )
                .also {
                    emitPermissionsDialogDisplayed(permissionRequest.permissions)
                }
        }
    }

    @Suppress("LongMethod")
    @VisibleForTesting
    internal fun handlingSingleContentPermissions(
        permissionRequest: PermissionRequest,
        permission: Permission,
        origin: String,
    ): SitePermissionsDialogFragment {
        val learnMoreLink = learnMoreUrlProvider?.getUrl(permission).orEmpty()
        return when (permission) {
            is ContentGeoLocation -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    R.string.mozac_feature_sitepermissions_location_title,
                    iconsR.drawable.mozac_ic_location_24,
                    showDoNotAskAgainCheckBox = shouldShowDoNotAskAgainCheckBox,
                    shouldSelectRememberChoice =
                        dialogConfig?.shouldPreselectDoNotAskAgain ?: DialogConfig.DEFAULT_PRESELECT_DO_NOT_ASK_AGAIN,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentNotification -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    R.string.mozac_feature_sitepermissions_notification_title,
                    iconsR.drawable.mozac_ic_notification_24,
                    showDoNotAskAgainCheckBox = false,
                    shouldSelectRememberChoice = false,
                    isNotificationRequest = true,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentAudioCapture,
            is ContentAudioMicrophone -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    R.string.mozac_feature_sitepermissions_microfone_title,
                    iconsR.drawable.mozac_ic_microphone_24,
                    showDoNotAskAgainCheckBox = shouldShowDoNotAskAgainCheckBox,
                    shouldSelectRememberChoice =
                        dialogConfig?.shouldPreselectDoNotAskAgain ?: DialogConfig.DEFAULT_PRESELECT_DO_NOT_ASK_AGAIN,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentVideoCamera,
            is ContentVideoCapture -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    R.string.mozac_feature_sitepermissions_camera_title,
                    iconsR.drawable.mozac_ic_camera_24,
                    showDoNotAskAgainCheckBox = shouldShowDoNotAskAgainCheckBox,
                    shouldSelectRememberChoice =
                        dialogConfig?.shouldPreselectDoNotAskAgain ?: DialogConfig.DEFAULT_PRESELECT_DO_NOT_ASK_AGAIN,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentPersistentStorage -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    R.string.mozac_feature_sitepermissions_persistent_storage_title,
                    iconsR.drawable.mozac_ic_storage_24,
                    showDoNotAskAgainCheckBox = false,
                    shouldSelectRememberChoice = true,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentMediaKeySystemAccess -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    R.string.mozac_feature_sitepermissions_media_key_system_access_title,
                    iconsR.drawable.mozac_ic_link_24,
                    showDoNotAskAgainCheckBox = false,
                    shouldSelectRememberChoice = true,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentCrossOriginStorageAccess -> {
                createContentCrossOriginStorageAccessPermissionPrompt(
                    context = context,
                    origin = origin,
                    permissionRequest = permissionRequest,
                    showDoNotAskAgainCheckBox = false,
                    shouldSelectRememberChoice = true,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentLocalDeviceAccess -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    titleId = R.string.mozac_feature_sitepermissions_local_device_access_title,
                    iconId = iconsR.drawable.mozac_ic_device_desktop_24,
                    showDoNotAskAgainCheckBox = true,
                    doNotAskAgainCheckBoxLabel = R.string.mozac_feature_sitepermissions_do_not_ask_again_on_this_site4,
                    shouldSelectRememberChoice = false,
                    negativeButtonResId = R.string.mozac_feature_sitepermissions_block,
                    learnMoreLink = learnMoreLink,
                )
            }
            is ContentLocalNetworkAccess -> {
                createSinglePermissionPrompt(
                    context,
                    origin,
                    permissionRequest,
                    titleId = R.string.mozac_feature_sitepermissions_local_network_access_title,
                    iconId = iconsR.drawable.mozac_ic_local_network_24,
                    showDoNotAskAgainCheckBox = true,
                    doNotAskAgainCheckBoxLabel = R.string.mozac_feature_sitepermissions_do_not_ask_again_on_this_site4,
                    shouldSelectRememberChoice = false,
                    negativeButtonResId = R.string.mozac_feature_sitepermissions_block,
                    learnMoreLink = learnMoreLink,
                )
            }
            else -> throw InvalidParameterException("$permission is not a valid permission.")
        }
    }

    @VisibleForTesting
    internal fun trimOriginHttpsSchemeAndPort(origin: String): String {
        // Since Gecko scopes permissions to origins (like "https://www.example.com:443"), we want
        // all permission checks to use origins, not just hostnames like "www.example.com". Only
        // when we format the origin for the permission prompt UI here do we trim the HTTPS scheme
        // or default HTTP ports 80 and 443. Any other scheme or port is unusual, so show them!
        val url = URL(origin)
        val scheme =
            if (url.protocol == "https") {
                ""
            } else {
                "${url.protocol}://"
            }
        val port =
            if (url.port == url.defaultPort) {
                ""
            } else {
                ":${url.port}"
            }
        return "$scheme${url.host}$port"
    }

    @VisibleForTesting
    internal fun createSinglePermissionPrompt(
        context: Context,
        origin: String,
        permissionRequest: PermissionRequest,
        @StringRes titleId: Int,
        @DrawableRes iconId: Int,
        showDoNotAskAgainCheckBox: Boolean,
        @StringRes doNotAskAgainCheckBoxLabel: Int? = null,
        shouldSelectRememberChoice: Boolean,
        isNotificationRequest: Boolean = false,
        @StringRes negativeButtonResId: Int? = null,
        learnMoreLink: String? = null,
    ): SitePermissionsDialogFragment {
        val trimmedOrigin = trimOriginHttpsSchemeAndPort(origin)
        val title = context.getString(titleId, trimmedOrigin)

        val currentSessionId: String =
            store.state.findTabOrCustomTabOrSelectedTab(sessionId)?.id
                ?: throw IllegalStateException("Unable to find session for $sessionId or selected session")

        return SitePermissionsDialogFragment.newInstance(
            sessionId = currentSessionId,
            title = title,
            titleIcon = iconId,
            permissionRequestId = permissionRequest.id,
            feature = this,
            learnMoreLink = learnMoreLink,
            shouldShowDoNotAskAgainCheckBox = showDoNotAskAgainCheckBox,
            doNotAskAgainCheckBoxLabel =
                if (doNotAskAgainCheckBoxLabel != null) {
                    context.getString(doNotAskAgainCheckBoxLabel)
                } else {
                    null
                },
            isNotificationRequest = isNotificationRequest,
            shouldSelectDoNotAskAgainCheckBox = shouldSelectRememberChoice,
            negativeButtonText = if (negativeButtonResId != null) context.getString(negativeButtonResId) else null,
        )
    }

    @VisibleForTesting
    internal fun createContentCrossOriginStorageAccessPermissionPrompt(
        context: Context,
        origin: String,
        permissionRequest: PermissionRequest,
        showDoNotAskAgainCheckBox: Boolean,
        shouldSelectRememberChoice: Boolean,
        learnMoreLink: String,
    ): SitePermissionsDialogFragment {
        val trimmedOrigin = trimOriginHttpsSchemeAndPort(origin)
        val currentSession =
            store.state.findTabOrCustomTabOrSelectedTab(sessionId)
                ?: throw IllegalStateException("Unable to find session for $sessionId or selected session")

        val title =
            context.getString(
                R.string.mozac_feature_sitepermissions_storage_access_title,
                trimmedOrigin,
                currentSession.content.url.tryGetHostFromUrl(),
            )
        val message =
            context.getString(
                R.string.mozac_feature_sitepermissions_storage_access_message,
                trimmedOrigin,
            )
        val negativeButtonText = context.getString(R.string.mozac_feature_sitepermissions_storage_access_not_allow)

        return SitePermissionsDialogFragment.newInstance(
            sessionId = currentSession.id,
            title = title,
            titleIcon = iconsR.drawable.mozac_ic_cookies_24,
            message = message,
            negativeButtonText = negativeButtonText,
            permissionRequestId = permissionRequest.id,
            feature = this,
            shouldShowDoNotAskAgainCheckBox = showDoNotAskAgainCheckBox,
            isNotificationRequest = false,
            shouldSelectDoNotAskAgainCheckBox = shouldSelectRememberChoice,
            learnMoreLink = learnMoreLink,
        )
    }

    private val PermissionRequest.isMedia: Boolean
        get() {
            return when (permissions.first()) {
                is ContentVideoCamera,
                is ContentVideoCapture,
                is ContentAudioCapture,
                is ContentAudioMicrophone -> true
                else -> false
            }
        }

    private val PermissionRequest.areAllMediaPermissionsGranted: Boolean
        get() {
            val systemPermissions = mutableListOf<String>()
            permissions.forEach { permission ->
                when (permission) {
                    is ContentVideoCamera,
                    is ContentVideoCapture -> {
                        systemPermissions.add(CAMERA)
                    }
                    is ContentAudioCapture,
                    is ContentAudioMicrophone -> {
                        systemPermissions.add(RECORD_AUDIO)
                    }
                    else -> {
                        // no-op
                    }
                }
            }
            return systemPermissions.all { context.isPermissionGranted((it)) }
        }

    data class PromptsStyling(
        val gravity: Int,
        val shouldWidthMatchParent: Boolean = false,
        @param:ColorRes val positiveButtonBackgroundColor: Int? = null,
        @param:ColorRes val positiveButtonTextColor: Int? = null,
    )

    /** Customization options for feature request dialog */
    data class DialogConfig(
        /** Use **true** to pre-select "Do not ask again" checkbox. */
        val shouldPreselectDoNotAskAgain: Boolean = false
    ) {

        companion object {
            /** Default values for [DialogConfig.shouldPreselectDoNotAskAgain] */
            internal const val DEFAULT_PRESELECT_DO_NOT_ASK_AGAIN = false
        }
    }

    /** Re-attaches a fragment that is still visible but not linked to this feature anymore. */
    private fun reattachFragment(fragment: SitePermissionsDialogFragment) {
        val currentTab = store.state.findTabOrCustomTabOrSelectedTab(fragment.sessionId)?.content
        if (currentTab == null || (noPermissionRequests(currentTab))) {
            fragmentManager.beginTransaction().remove(fragment).commitAllowingStateLoss()
        } else {
            // Re-assign the feature instance so that the fragment can invoke us once the
            // user makes a selection or cancels the dialog.
            fragment.feature = this
        }
    }

    internal fun hideSitePermissionsPrompt() {
        fragmentManager.findFragmentByTag(PROMPT_FRAGMENT_TAG)?.let { fragment ->
            fragmentManager.beginTransaction().remove(fragment).commitAllowingStateLoss()
        }
    }

    @VisibleForTesting
    internal fun noPermissionRequests(contentState: ContentState) =
        contentState.appPermissionRequestsList.isEmpty() && contentState.permissionRequestsList.isEmpty()
}

internal fun SitePermissions?.isGranted(permissionRequest: PermissionRequest): Boolean {
    return if (this != null) {
        permissionRequest.permissions.all { permission ->
            isPermissionGranted(permission, this)
        }
    } else {
        false
    }
}

@VisibleForTesting
internal fun isPermissionGranted(
    permission: Permission,
    permissionFromStorage: SitePermissions,
): Boolean {
    return when (permission) {
        is ContentGeoLocation -> {
            permissionFromStorage.location.isAllowed()
        }
        is ContentNotification -> {
            permissionFromStorage.notification.isAllowed()
        }
        is ContentAudioCapture,
        is ContentAudioMicrophone -> {
            permissionFromStorage.microphone.isAllowed()
        }
        is ContentVideoCamera,
        is ContentVideoCapture -> {
            permissionFromStorage.camera.isAllowed()
        }
        is ContentPersistentStorage -> {
            permissionFromStorage.localStorage.isAllowed()
        }
        is ContentCrossOriginStorageAccess -> {
            permissionFromStorage.crossOriginStorageAccess.isAllowed()
        }
        is ContentMediaKeySystemAccess -> {
            permissionFromStorage.mediaKeySystemAccess.isAllowed()
        }
        is ContentAutoPlayAudible -> {
            permissionFromStorage.autoplayAudible.isAllowed()
        }
        is ContentAutoPlayInaudible -> {
            permissionFromStorage.autoplayInaudible.isAllowed()
        }
        is ContentLocalDeviceAccess -> {
            permissionFromStorage.localDeviceAccess.isAllowed()
        }
        is ContentLocalNetworkAccess -> {
            permissionFromStorage.localNetworkAccess.isAllowed()
        }
        else -> throw InvalidParameterException("$permission is not a valid permission.")
    }
}

private fun Permission.isSupported(): Boolean {
    return when (this) {
        is ContentGeoLocation,
        is ContentNotification,
        is ContentPersistentStorage,
        is ContentCrossOriginStorageAccess,
        is ContentAudioCapture,
        is ContentAudioMicrophone,
        is ContentVideoCamera,
        is ContentVideoCapture,
        is ContentAutoPlayAudible,
        is ContentAutoPlayInaudible,
        is ContentMediaKeySystemAccess,
        is ContentLocalDeviceAccess,
        is ContentLocalNetworkAccess -> true
        else -> false
    }
}
