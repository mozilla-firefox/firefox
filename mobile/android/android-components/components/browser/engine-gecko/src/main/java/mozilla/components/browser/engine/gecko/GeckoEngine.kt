/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.content.Context
import android.os.Parcelable
import android.util.AttributeSet
import android.util.JsonReader
import androidx.annotation.OptIn
import androidx.annotation.VisibleForTesting
import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.browser.engine.fission.GeckoWebContentIsolationMapper.intoWebContentIsolationStrategy
import mozilla.components.browser.engine.gecko.activity.GeckoActivityDelegate
import mozilla.components.browser.engine.gecko.activity.GeckoScreenOrientationDelegate
import mozilla.components.browser.engine.gecko.ai.DefaultGeckoAIFeaturesAccessor
import mozilla.components.browser.engine.gecko.ai.GeckoAIFeaturesAccessor
import mozilla.components.browser.engine.gecko.autofill.DefaultRuntimeAddressStructureAccessor
import mozilla.components.browser.engine.gecko.autofill.RuntimeAddressStructureAccessor
import mozilla.components.browser.engine.gecko.ext.getAntiTrackingPolicy
import mozilla.components.browser.engine.gecko.ext.getEtpCategory
import mozilla.components.browser.engine.gecko.ext.getEtpLevel
import mozilla.components.browser.engine.gecko.ext.getStrictSocialTrackingProtection
import mozilla.components.browser.engine.gecko.integration.LocaleSettingUpdater
import mozilla.components.browser.engine.gecko.mediaquery.from
import mozilla.components.browser.engine.gecko.mediaquery.toGeckoValue
import mozilla.components.browser.engine.gecko.preferences.DefaultGeckoPreferenceAccessor
import mozilla.components.browser.engine.gecko.preferences.GeckoPreferenceAccessor
import mozilla.components.browser.engine.gecko.preferences.GeckoPreferenceObserverDelegate
import mozilla.components.browser.engine.gecko.preferences.GeckoPreferencesUtils.intoBrowserPreference
import mozilla.components.browser.engine.gecko.preferences.GeckoPreferencesUtils.intoGeckoBranch
import mozilla.components.browser.engine.gecko.preferences.GeckoPreferencesUtils.intoSetGeckoPreference
import mozilla.components.browser.engine.gecko.profiler.Profiler
import mozilla.components.browser.engine.gecko.serviceworker.GeckoServiceWorkerDelegate
import mozilla.components.browser.engine.gecko.translate.DefaultRuntimeTranslationAccessor
import mozilla.components.browser.engine.gecko.translate.RuntimeTranslationAccessor
import mozilla.components.browser.engine.gecko.util.SpeculativeSessionFactory
import mozilla.components.browser.engine.gecko.webextension.GeckoWebExtension
import mozilla.components.browser.engine.gecko.webextension.GeckoWebExtensionException
import mozilla.components.browser.engine.gecko.webnotifications.GeckoWebNotificationDelegate
import mozilla.components.browser.engine.gecko.webpush.GeckoWebPushDelegate
import mozilla.components.browser.engine.gecko.webpush.GeckoWebPushHandler
import mozilla.components.concept.engine.CancellableOperation
import mozilla.components.concept.engine.DownloadDelegate
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.CookieBannerHandlingMode
import mozilla.components.concept.engine.EngineSession.SafeBrowsingPolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.TrackingCategory
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.concept.engine.EngineView
import mozilla.components.concept.engine.Settings
import mozilla.components.concept.engine.activity.ActivityDelegate
import mozilla.components.concept.engine.activity.OrientationDelegate
import mozilla.components.concept.engine.autofill.AddressStructure
import mozilla.components.concept.engine.content.blocking.TrackerLog
import mozilla.components.concept.engine.content.blocking.TrackingProtectionExceptionStorage
import mozilla.components.concept.engine.fission.WebContentIsolationStrategy
import mozilla.components.concept.engine.history.HistoryTrackingDelegate
import mozilla.components.concept.engine.mediaquery.PreferredColorScheme
import mozilla.components.concept.engine.preferences.Branch
import mozilla.components.concept.engine.preferences.BrowserPrefObserverDelegate
import mozilla.components.concept.engine.preferences.BrowserPreference
import mozilla.components.concept.engine.preferences.BrowserPreferencesRuntime
import mozilla.components.concept.engine.preferences.SetBrowserPreference
import mozilla.components.concept.engine.serviceworker.ServiceWorkerDelegate
import mozilla.components.concept.engine.translate.LanguageModel
import mozilla.components.concept.engine.translate.LanguageSetting
import mozilla.components.concept.engine.translate.ModelManagementOptions
import mozilla.components.concept.engine.translate.TranslationSupport
import mozilla.components.concept.engine.translate.TranslationsRuntime
import mozilla.components.concept.engine.utils.EngineVersion
import mozilla.components.concept.engine.webextension.Action
import mozilla.components.concept.engine.webextension.ActionHandler
import mozilla.components.concept.engine.webextension.EnableSource
import mozilla.components.concept.engine.webextension.InstallationMethod
import mozilla.components.concept.engine.webextension.TabHandler
import mozilla.components.concept.engine.webextension.WebExtension
import mozilla.components.concept.engine.webextension.WebExtensionDelegate
import mozilla.components.concept.engine.webextension.WebExtensionInstallException
import mozilla.components.concept.engine.webextension.WebExtensionRuntime
import mozilla.components.concept.engine.webnotifications.WebNotificationDelegate
import mozilla.components.concept.engine.webpush.WebPushDelegate
import mozilla.components.concept.engine.webpush.WebPushHandler
import mozilla.components.support.ktx.kotlin.isResourceUrl
import mozilla.components.support.utils.ThreadUtils
import org.json.JSONObject
import org.mozilla.geckoview.AllowOrDeny
import org.mozilla.geckoview.ContentBlocking
import org.mozilla.geckoview.ContentBlockingController
import org.mozilla.geckoview.ContentBlockingController.Event
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoWebExecutor
import org.mozilla.geckoview.WebExtensionController
import org.mozilla.geckoview.WebNotification
import java.lang.ref.WeakReference

typealias NativePermissionPromptResponse = org.mozilla.geckoview.WebExtension.PermissionPromptResponse

/**
 * Gecko-based implementation of Engine interface.
 */
@Suppress("LargeClass", "TooManyFunctions")
class GeckoEngine(
    context: Context,
    private val defaultSettings: Settings? = null,
    private val runtime: GeckoRuntime = GeckoRuntime.getDefault(context),
    executorProvider: () -> GeckoWebExecutor = { GeckoWebExecutor(runtime) },
    override val trackingProtectionExceptionStore: TrackingProtectionExceptionStorage =
        GeckoTrackingProtectionExceptionStorage(runtime),
    private val geckoPreferenceAccessor: GeckoPreferenceAccessor = DefaultGeckoPreferenceAccessor(),
    private val runtimeTranslationAccessor: RuntimeTranslationAccessor = DefaultRuntimeTranslationAccessor(),
    private val addressStructureAccessor: RuntimeAddressStructureAccessor = DefaultRuntimeAddressStructureAccessor(),
    override val aiFeatures: GeckoAIFeaturesAccessor = DefaultGeckoAIFeaturesAccessor(),
) : Engine, WebExtensionRuntime, TranslationsRuntime, BrowserPreferencesRuntime {
    private val executor by lazy { executorProvider.invoke() }
    private val localeUpdater = LocaleSettingUpdater(context, runtime)

    @VisibleForTesting internal val speculativeConnectionFactory = SpeculativeSessionFactory()
    private var webExtensionDelegate: WebExtensionDelegate? = null
    private val webExtensionActionHandler = object : ActionHandler {
        override fun onBrowserAction(extension: WebExtension, session: EngineSession?, action: Action) {
            webExtensionDelegate?.onBrowserActionDefined(extension, action)
        }

        override fun onPageAction(extension: WebExtension, session: EngineSession?, action: Action) {
            webExtensionDelegate?.onPageActionDefined(extension, action)
        }

        override fun onToggleActionPopup(extension: WebExtension, action: Action): EngineSession? {
            return webExtensionDelegate?.onToggleActionPopup(
                extension,
                GeckoEngineSession(
                    runtime = runtime,
                    defaultSettings = defaultSettings,
                ),
                action,
            )
        }
    }
    private val webExtensionTabHandler = object : TabHandler {
        override fun onNewTab(webExtension: WebExtension, engineSession: EngineSession, active: Boolean, url: String) {
            webExtensionDelegate?.onNewTab(webExtension, engineSession, active, url)
        }
    }

    private var webPushHandler: WebPushHandler? = null

    init {
        runtime.delegate = GeckoRuntime.Delegate {
            // On shutdown: The runtime is shutting down (possibly because of an unrecoverable error state). We crash
            // the app here for two reasons:
            //  - We want to know about those unsolicited shutdowns and fix those issues.
            //  - We can't recover easily from this situation. Just continuing will leave us with an engine that
            //    doesn't do anything anymore.
            @Suppress("TooGenericExceptionThrown")
            throw RuntimeException("GeckoRuntime is shutting down")
        }
    }

    /**
     * Notifies Gecko observers of "mobile-telemetry-pref-changed".
     * The data is "enabled" or "disabled" based on [enabled]
     *
     * @param enabled Whether the user enabled or disabled telemetry
     */
    override fun notifyTelemetryPrefChanged(enabled: Boolean) {
        runtime.notifyTelemetryPrefChanged(enabled)
    }

    /**
     * Fetch a list of trackers logged for a given [session] .
     *
     * @param session the session where the trackers were logged.
     * @param onSuccess callback invoked if the data was fetched successfully.
     * @param onError (optional) callback invoked if fetching the data caused an exception.
     */
    override fun getTrackersLog(
        session: EngineSession,
        onSuccess: (List<TrackerLog>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        val geckoSession = (session as GeckoEngineSession).geckoSession
        runtime.contentBlockingController.getLog(geckoSession).then(
            { contentLogList ->
                val list = contentLogList ?: emptyList()
                val logs = list.map { logEntry ->
                    logEntry.toTrackerLog()
                }.filterNot {
                    !it.cookiesHasBeenBlocked &&
                        it.blockedCategories.isEmpty() &&
                        it.loadedCategories.isEmpty()
                }

                onSuccess(logs)
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.warmUp].
     */
    override fun warmUp() {
        runtime.warmUp()
    }

    /**
     * Creates a new Gecko-based EngineView.
     */
    override fun createView(context: Context, attrs: AttributeSet?): EngineView {
        return GeckoEngineView(context, attrs).apply {
            setColorScheme(settings.preferredColorScheme)
        }
    }

    /**
     * See [Engine.createSession].
     */
    override fun createSession(private: Boolean, contextId: String?): EngineSession {
        ThreadUtils.assertOnUiThread()
        val speculativeSession = speculativeConnectionFactory.get(private, contextId)
        return speculativeSession ?: GeckoEngineSession(
            runtime = runtime,
            privateMode = private,
            defaultSettings = defaultSettings,
            contextId = contextId,
        )
    }

    /**
     * See [Engine.createSessionState].
     */
    override fun createSessionState(json: JSONObject): EngineSessionState {
        return GeckoEngineSessionState.fromJSON(json)
    }

    /**
     * See [Engine.createSessionStateFrom].
     */
    override fun createSessionStateFrom(reader: JsonReader): EngineSessionState {
        return GeckoEngineSessionState.from(reader)
    }

    /**
     * See [Engine.speculativeCreateSession].
     */
    override fun speculativeCreateSession(private: Boolean, contextId: String?) {
        ThreadUtils.assertOnUiThread()
        speculativeConnectionFactory.create(
            runtime = runtime,
            private = private,
            contextId = contextId,
            defaultSettings = defaultSettings,
        )
    }

    /**
     * See [Engine.clearSpeculativeSession].
     */
    override fun clearSpeculativeSession() {
        speculativeConnectionFactory.clear()
    }

    /**
     * Opens a speculative connection to the host of [url].
     *
     * This is useful if an app thinks it may be making a request to that host in the near future. If no request
     * is made, the connection will be cleaned up after an unspecified.
     */
    override fun speculativeConnect(url: String) {
        executor.speculativeConnect(url)
    }

    /**
     * See [Engine.installBuiltInWebExtension].
     */
    override fun installBuiltInWebExtension(
        id: String,
        url: String,
        onSuccess: ((WebExtension) -> Unit),
        onError: ((Throwable) -> Unit),
    ): CancellableOperation {
        require(url.isResourceUrl()) { "url should be a resource url" }

        val geckoResult = runtime.webExtensionController.ensureBuiltIn(url, id).apply {
            then(
                {
                    onExtensionInstalled(it!!, onSuccess)
                    GeckoResult<Void>()
                },
                { throwable ->
                    onError(GeckoWebExtensionException.createWebExtensionException(throwable))
                    GeckoResult<Void>()
                },
            )
        }
        return geckoResult.asCancellableOperation()
    }

    /**
     * See [Engine.installWebExtension].
     */
    override fun installWebExtension(
        url: String,
        installationMethod: InstallationMethod?,
        onSuccess: ((WebExtension) -> Unit),
        onError: ((Throwable) -> Unit),
    ): CancellableOperation {
        require(!url.isResourceUrl()) { "url shouldn't be a resource url" }

        val geckoResult = runtime.webExtensionController.install(
            url,
            installationMethod?.toGeckoInstallationMethod(),
        ).apply {
            then(
                {
                    onExtensionInstalled(it!!, onSuccess)
                    GeckoResult<Void>()
                },
                { throwable ->
                    onError(GeckoWebExtensionException.createWebExtensionException(throwable))
                    GeckoResult<Void>()
                },
            )
        }
        return geckoResult.asCancellableOperation()
    }

    /**
     * See [Engine.uninstallWebExtension].
     */
    override fun uninstallWebExtension(
        ext: WebExtension,
        onSuccess: () -> Unit,
        onError: (String, Throwable) -> Unit,
    ) {
        runtime.webExtensionController.uninstall((ext as GeckoWebExtension).nativeExtension).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(ext.id, throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.updateWebExtension].
     */
    override fun updateWebExtension(
        extension: WebExtension,
        onSuccess: (WebExtension?) -> Unit,
        onError: (String, Throwable) -> Unit,
    ) {
        runtime.webExtensionController.update((extension as GeckoWebExtension).nativeExtension).then(
            { geckoExtension ->
                val updatedExtension = if (geckoExtension != null) {
                    GeckoWebExtension(
                        nativeExtension = geckoExtension,
                        runtime = runtime,
                    ).also {
                        it.registerActionHandler(webExtensionActionHandler)
                        it.registerTabHandler(webExtensionTabHandler, defaultSettings)
                    }
                } else {
                    null
                }
                onSuccess(updatedExtension)
                GeckoResult<Void>()
            },
            { throwable ->
                onError(extension.id, GeckoWebExtensionException(throwable))
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.registerWebExtensionDelegate].
     */
    override fun registerWebExtensionDelegate(
        webExtensionDelegate: WebExtensionDelegate,
    ) {
        this.webExtensionDelegate = webExtensionDelegate

        val promptDelegate = object : WebExtensionController.PromptDelegate {

            override fun onInstallPromptRequest(
                ext: org.mozilla.geckoview.WebExtension,
                permissions: Array<out String>,
                origins: Array<out String>,
                dataCollectionPermissions: Array<out String>,
            ): GeckoResult<NativePermissionPromptResponse>? {
                val result = GeckoResult<NativePermissionPromptResponse>()

                webExtensionDelegate.onInstallPermissionRequest(
                    GeckoWebExtension(
                        nativeExtension = ext,
                        runtime = runtime,
                    ),
                    permissions.toList(),
                    origins.toList(),
                    dataCollectionPermissions.toList(),
                ) { data ->
                    result.complete(
                        NativePermissionPromptResponse(
                            data.isPermissionsGranted,
                            data.isPrivateModeGranted,
                            data.isTechnicalAndInteractionDataGranted,
                        ),
                    )
                }

                return result
            }

            override fun onUpdatePrompt(
                extension: org.mozilla.geckoview.WebExtension,
                newPermissions: Array<out String>,
                newOrigins: Array<out String>,
                newDataCollectionPermissions: Array<out String>,
            ): GeckoResult<AllowOrDeny>? {
                val result = GeckoResult<AllowOrDeny>()
                webExtensionDelegate.onUpdatePermissionRequest(
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    ),
                    newPermissions.toList(),
                    newOrigins.toList(),
                    newDataCollectionPermissions.toList(),
                ) { allow ->
                    if (allow) result.complete(AllowOrDeny.ALLOW) else result.complete(AllowOrDeny.DENY)
                }
                return result
            }

            override fun onOptionalPrompt(
                extension: org.mozilla.geckoview.WebExtension,
                permissions: Array<out String>,
                origins: Array<out String>,
                dataCollectionPermissions: Array<out String>,
            ): GeckoResult<AllowOrDeny>? {
                val result = GeckoResult<AllowOrDeny>()
                webExtensionDelegate.onOptionalPermissionsRequest(
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    ),
                    permissions.toList(),
                    origins.toList(),
                    dataCollectionPermissions.toList(),
                ) { allow ->
                    if (allow) result.complete(AllowOrDeny.ALLOW) else result.complete(AllowOrDeny.DENY)
                }
                return result
            }
        }

        val debuggerDelegate = object : WebExtensionController.DebuggerDelegate {
            override fun onExtensionListUpdated() {
                webExtensionDelegate.onExtensionListUpdated()
            }
        }

        val addonManagerDelegate = object : WebExtensionController.AddonManagerDelegate {
            override fun onDisabled(extension: org.mozilla.geckoview.WebExtension) {
                webExtensionDelegate.onDisabled(
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    ),
                )
            }

            override fun onEnabled(extension: org.mozilla.geckoview.WebExtension) {
                webExtensionDelegate.onEnabled(
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    ),
                )
            }

            override fun onReady(extension: org.mozilla.geckoview.WebExtension) {
                webExtensionDelegate.onReady(
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    ),
                )
            }

            override fun onUninstalled(extension: org.mozilla.geckoview.WebExtension) {
                webExtensionDelegate.onUninstalled(
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    ),
                )
            }

            override fun onInstalled(extension: org.mozilla.geckoview.WebExtension) {
                val installedExtension = GeckoWebExtension(
                    nativeExtension = extension,
                    runtime = runtime,
                )
                webExtensionDelegate.onInstalled(installedExtension)
                installedExtension.registerActionHandler(webExtensionActionHandler)
                installedExtension.registerTabHandler(webExtensionTabHandler, defaultSettings)
            }

            override fun onInstallationFailed(
                extension: org.mozilla.geckoview.WebExtension?,
                installException: org.mozilla.geckoview.WebExtension.InstallException,
            ) {
                val exception =
                    GeckoWebExtensionException.createWebExtensionException(installException)
                webExtensionDelegate.onInstallationFailedRequest(
                    extension.toSafeWebExtension(),
                    exception as WebExtensionInstallException,
                )
            }

            override fun onOptionalPermissionsChanged(extension: org.mozilla.geckoview.WebExtension) {
                webExtensionDelegate.onOptionalPermissionsChanged(
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    ),
                )
            }
        }

        val extensionProcessDelegate = object : WebExtensionController.ExtensionProcessDelegate {
            override fun onDisabledProcessSpawning() {
                webExtensionDelegate.onDisabledExtensionProcessSpawning()
            }
        }

        runtime.webExtensionController.setPromptDelegate(promptDelegate)
        runtime.webExtensionController.setDebuggerDelegate(debuggerDelegate)
        runtime.webExtensionController.setAddonManagerDelegate(addonManagerDelegate)
        runtime.webExtensionController.setExtensionProcessDelegate(extensionProcessDelegate)
    }

    /**
     * See [Engine.listInstalledWebExtensions].
     */
    override fun listInstalledWebExtensions(onSuccess: (List<WebExtension>) -> Unit, onError: (Throwable) -> Unit) {
        runtime.webExtensionController.list().then(
            {
                val extensions = it?.map { extension ->
                    GeckoWebExtension(
                        nativeExtension = extension,
                        runtime = runtime,
                    )
                } ?: emptyList()

                extensions.forEach { extension ->
                    extension.registerActionHandler(webExtensionActionHandler)
                    extension.registerTabHandler(webExtensionTabHandler, defaultSettings)
                }

                onSuccess(extensions)
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.enableWebExtension].
     */
    override fun enableWebExtension(
        extension: WebExtension,
        source: EnableSource,
        onSuccess: (WebExtension) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtime.webExtensionController.enable((extension as GeckoWebExtension).nativeExtension, source.id).then(
            {
                val enabledExtension = GeckoWebExtension(
                    nativeExtension = it!!,
                    runtime = runtime,
                )
                onSuccess(enabledExtension)
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.addOptionalPermissions].
     */
    override fun addOptionalPermissions(
        extensionId: String,
        permissions: List<String>,
        origins: List<String>,
        dataCollectionPermissions: List<String>,
        onSuccess: (WebExtension) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        if (permissions.isEmpty() && origins.isEmpty() && dataCollectionPermissions.isEmpty()) {
            onError(
                IllegalStateException(
                    "At least one of permissions, origins or dataCollectionPermissions must not be empty",
                ),
            )
            return
        }

        runtime.webExtensionController.addOptionalPermissions(
            extensionId,
            permissions.toTypedArray(),
            origins.toTypedArray(),
            dataCollectionPermissions.toTypedArray(),
        ).then(
            {
                val enabledExtension = GeckoWebExtension(
                    nativeExtension = it!!,
                    runtime = runtime,
                )
                onSuccess(enabledExtension)
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.removeOptionalPermissions].
     */
    override fun removeOptionalPermissions(
        extensionId: String,
        permissions: List<String>,
        origins: List<String>,
        dataCollectionPermissions: List<String>,
        onSuccess: (WebExtension) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        if (permissions.isEmpty() && origins.isEmpty() && dataCollectionPermissions.isEmpty()) {
            onError(
                IllegalStateException(
                    "At least one of permissions, origins or dataCollectionPermissions must not be empty",
                ),
            )
            return
        }

        runtime.webExtensionController.removeOptionalPermissions(
            extensionId,
            permissions.toTypedArray(),
            origins.toTypedArray(),
            dataCollectionPermissions.toTypedArray(),
        ).then(
            {
                val enabledExtension = GeckoWebExtension(
                    nativeExtension = it!!,
                    runtime = runtime,
                )
                onSuccess(enabledExtension)
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.disableWebExtension].
     */
    override fun disableWebExtension(
        extension: WebExtension,
        source: EnableSource,
        onSuccess: (WebExtension) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtime.webExtensionController.disable((extension as GeckoWebExtension).nativeExtension, source.id).then(
            {
                val disabledExtension = GeckoWebExtension(
                    nativeExtension = it!!,
                    runtime = runtime,
                )
                onSuccess(disabledExtension)
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.setAllowedInPrivateBrowsing].
     */
    override fun setAllowedInPrivateBrowsing(
        extension: WebExtension,
        allowed: Boolean,
        onSuccess: (WebExtension) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtime.webExtensionController.setAllowedInPrivateBrowsing(
            (extension as GeckoWebExtension).nativeExtension,
            allowed,
        ).then(
            { geckoExtension ->
                if (geckoExtension == null) {
                    onError(
                        Exception(
                            "Gecko extension was not returned after trying to" +
                                " setAllowedInPrivateBrowsing with value $allowed",
                        ),
                    )
                } else {
                    val ext = GeckoWebExtension(
                        nativeExtension = geckoExtension,
                        runtime = runtime,
                    )
                    webExtensionDelegate?.onAllowedInPrivateBrowsingChanged(ext)
                    onSuccess(ext)
                }
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.enableExtensionProcessSpawning].
     */
    override fun enableExtensionProcessSpawning() {
        runtime.webExtensionController.enableExtensionProcessSpawning()
    }

    /**
     * See [Engine.disableExtensionProcessSpawning].
     */
    override fun disableExtensionProcessSpawning() {
        runtime.webExtensionController.disableExtensionProcessSpawning()
    }

    /**
     * See [Engine.registerWebNotificationDelegate].
     */
    override fun registerWebNotificationDelegate(
        webNotificationDelegate: WebNotificationDelegate,
    ) {
        runtime.webNotificationDelegate = GeckoWebNotificationDelegate(webNotificationDelegate)
    }

    /**
     * See [Engine.registerWebPushDelegate].
     */
    override fun registerWebPushDelegate(
        webPushDelegate: WebPushDelegate,
    ): WebPushHandler {
        runtime.webPushController.setDelegate(GeckoWebPushDelegate(webPushDelegate))

        if (webPushHandler == null) {
            webPushHandler = GeckoWebPushHandler(runtime)
        }

        return requireNotNull(webPushHandler)
    }

    /**
     * See [Engine.registerActivityDelegate].
     */
    override fun registerActivityDelegate(
        activityDelegate: ActivityDelegate,
    ) {
        /*
         * Having the activity delegate on the engine can cause issues with resolving multiple requests to the delegate
         * from different sessions. Ideally, this should be moved to the [EngineView].
         *
         * See: https://bugzilla.mozilla.org/show_bug.cgi?id=1672195
         *
         * Attaching the delegate to the Gecko [Engine] implicitly assumes we have WebAuthn support. When a feature
         * implements the ActivityDelegate today, we need to make sure that it has full support for WebAuthn. This
         * needs to be fixed in GeckoView.
         *
         * See: https://bugzilla.mozilla.org/show_bug.cgi?id=1671988
         */
        runtime.activityDelegate = GeckoActivityDelegate(WeakReference(activityDelegate))
    }

    /**
     * See [Engine.unregisterActivityDelegate].
     */
    override fun unregisterActivityDelegate() {
        runtime.activityDelegate = null
    }

    /**
     * See [Engine.registerScreenOrientationDelegate].
     */
    override fun registerScreenOrientationDelegate(
        delegate: OrientationDelegate,
    ) {
        runtime.orientationController.delegate = GeckoScreenOrientationDelegate(delegate)
    }

    /**
     * See [Engine.unregisterScreenOrientationDelegate].
     */
    override fun unregisterScreenOrientationDelegate() {
        runtime.orientationController.delegate = null
    }

    /**
     * See [BrowserPreferencesRuntime.registerPreferenceObserverDelegate].
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun registerPrefObserverDelegate(prefObserverDelegate: BrowserPrefObserverDelegate) {
    runtime.preferencesObserverDelegate = GeckoPreferenceObserverDelegate(
        delegate = prefObserverDelegate,
    )
}

    /**
     * See [BrowserPreferencesRuntime.unregisterPreferenceObserverDelegate].
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun unregisterPrefObserverDelegate() {
        runtime.preferencesObserverDelegate = null
    }

    override fun registerServiceWorkerDelegate(serviceWorkerDelegate: ServiceWorkerDelegate) {
        runtime.serviceWorkerDelegate = GeckoServiceWorkerDelegate(
            delegate = serviceWorkerDelegate,
            runtime = runtime,
            engineSettings = defaultSettings,
        )
    }

    override fun unregisterServiceWorkerDelegate() {
        runtime.serviceWorkerDelegate = null
    }

    override fun handleWebNotificationClick(webNotification: Parcelable, action: String?) {
        val notification = (webNotification as? WebNotification)
        if (action != null) {
            notification?.click(action)
        } else {
            notification?.click()
        }
    }

    /**
     * See [Engine.clearData].
     */
    override fun clearData(
        data: Engine.BrowsingData,
        host: String?,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        val flags = data.types.toLong()
        if (host != null) {
            runtime.storageController.clearDataFromBaseDomain(host, flags)
        } else {
            runtime.storageController.clearData(flags)
        }.then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.isTranslationsEngineSupported].
     */
    override fun isTranslationsEngineSupported(
        onSuccess: (Boolean) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.isTranslationsEngineSupported(
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.getTranslationsPairDownloadSize].
     */
    override fun getTranslationsPairDownloadSize(
        fromLanguage: String,
        toLanguage: String,
        onSuccess: (Long) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.getTranslationsPairDownloadSize(
            fromLanguage = fromLanguage,
            toLanguage = toLanguage,
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.getTranslationsModelDownloadStates].
     */
    override fun getTranslationsModelDownloadStates(
        onSuccess: (List<LanguageModel>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.getTranslationsModelDownloadStates(
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.getSupportedTranslationLanguages].
     */
    override fun getSupportedTranslationLanguages(
        onSuccess: (TranslationSupport) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.getSupportedTranslationLanguages(
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.manageTranslationsLanguageModel].
     */
    override fun manageTranslationsLanguageModel(
        options: ModelManagementOptions,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.manageTranslationsLanguageModel(
            options = options,
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.getUserPreferredLanguages].
     */
    override fun getUserPreferredLanguages(
        onSuccess: (List<String>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.getUserPreferredLanguages(
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.getTranslationsOfferPopup].
     */
    override fun getTranslationsOfferPopup(): Boolean {
        return runtime.settings.translationsOfferPopup
    }

    /**
     * See [Engine.setTranslationsOfferPopup].
     */
    override fun setTranslationsOfferPopup(offer: Boolean) {
        runtime.settings.translationsOfferPopup = offer
    }

    /**
     * See [Engine.getLanguageSetting].
     */
    override fun getLanguageSetting(
        languageCode: String,
        onSuccess: (LanguageSetting) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.getLanguageSetting(
            languageCode = languageCode,
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.setLanguageSetting].
     */
    override fun setLanguageSetting(
        languageCode: String,
        languageSetting: LanguageSetting,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.setLanguageSetting(
            languageCode = languageCode,
            languageSetting = languageSetting,
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.getLanguageSettings].
     */
    override fun getLanguageSettings(
        onSuccess: (Map<String, LanguageSetting>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.getLanguageSettings(
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.getNeverTranslateSiteList].
     */
    override fun getNeverTranslateSiteList(
        onSuccess: (List<String>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.getNeverTranslateSiteList(
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [Engine.setNeverTranslateSpecifiedSite].
     */
    override fun setNeverTranslateSpecifiedSite(
        origin: String,
        setting: Boolean,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        runtimeTranslationAccessor.setNeverTranslateSpecifiedSite(
            origin = origin,
            neverTranslate = setting,
            onSuccess = onSuccess,
            onError = onError,
        )
    }

    /**
     * See [BrowserPreferencesRuntime.registerPrefForObservation].
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun registerPrefForObservation(
        pref: String,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.registerGeckoPrefForObservation(pref).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [BrowserPreferencesRuntime.registerPrefsForObservation].
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun registerPrefsForObservation(
        prefs: List<String>,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.registerGeckoPrefsForObservation(prefs).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [BrowserPreferencesRuntime.unregisterPrefForObservation].
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun unregisterPrefForObservation(
        pref: String,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.unregisterGeckoPrefForObservation(pref).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [BrowserPreferencesRuntime.unregisterPrefsForObservation].
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun unregisterPrefsForObservation(
        prefs: List<String>,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.unregisterGeckoPrefsForObservation(prefs).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.getBrowserPref].
     */
    @ExperimentalAndroidComponentsApi
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun getBrowserPref(
        pref: String,
        onSuccess: (BrowserPreference<*>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.getGeckoPref(pref).then(
            { geckoPrefResult ->
                if (geckoPrefResult != null) {
                    onSuccess(
                        geckoPrefResult.intoBrowserPreference(),
                    )
                } else {
                    onError(Throwable("Browser preference was unexpectedly null!"))
                }
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.setBrowserPref].
     */
    @ExperimentalAndroidComponentsApi
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun getBrowserPrefs(
        prefs: List<String>,
        onSuccess: (List<BrowserPreference<*>>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.getGeckoPrefs(prefs).then(
            { geckoPrefsResult ->
                if (geckoPrefsResult != null) {
                    onSuccess(
                        geckoPrefsResult.map { pref -> pref.intoBrowserPreference() },
                    )
                } else {
                    onError(Throwable("The browser preferences list was unexpectedly null!"))
                }
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.setBrowserPref].
     */
    @ExperimentalAndroidComponentsApi
    override fun setBrowserPref(
        pref: String,
        value: String,
        branch: Branch,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.setGeckoPref(pref, value, branch.intoGeckoBranch()).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.setBrowserPref].
     */
    @ExperimentalAndroidComponentsApi
    override fun setBrowserPref(
        pref: String,
        value: Int,
        branch: Branch,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.setGeckoPref(pref, value, branch.intoGeckoBranch()).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.setBrowserPref].
     */
    @ExperimentalAndroidComponentsApi
    override fun setBrowserPref(
        pref: String,
        value: Boolean,
        branch: Branch,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.setGeckoPref(pref, value, branch.intoGeckoBranch()).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                GeckoResult<Void>()
            },
        )
    }

    /**
     * See [Engine.setBrowserPref].
     */
    @ExperimentalAndroidComponentsApi
    @OptIn(ExperimentalGeckoViewApi::class)
    @Suppress("TooGenericExceptionCaught")
    override fun setBrowserPrefs(
        prefs: List<SetBrowserPreference<*>>,
        onSuccess: (Map<String, Boolean>) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        try {
            val geckoPrefs = prefs.map { pref -> pref.intoSetGeckoPreference() }
            geckoPreferenceAccessor.setGeckoPrefs(geckoPrefs).then(
                { geckoPrefsResult ->
                    if (geckoPrefsResult != null) {
                        onSuccess(
                            geckoPrefsResult,
                        )
                    } else {
                        onError(Throwable("The browser preferences map was unexpectedly null!"))
                    }
                    GeckoResult<Void>()
                },
                { throwable ->
                    onError(throwable)
                    GeckoResult<Void>()
                },
            )
        } catch (throwable: Throwable) {
            onError(throwable)
        }
    }

    /**
     * See [Engine.clearBrowserUserPref].
     */
    @ExperimentalAndroidComponentsApi
    override fun clearBrowserUserPref(
        pref: String,
        onSuccess: () -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        geckoPreferenceAccessor.clearGeckoUserPref(pref).then(
            {
                onSuccess()
                GeckoResult<Void>()
            },
            { throwable ->
                onError(throwable)
                   GeckoResult<Void>()
            },
        )
    }

    override fun getAddressStructure(
        countryCode: String,
        onSuccess: (AddressStructure) -> Unit,
        onError: (Throwable) -> Unit,
    ) {
        addressStructureAccessor.getAddressStructure(countryCode, onSuccess, onError)
    }

    /**
     * See [Engine.profiler].
     */
    override val profiler: Profiler = Profiler()

    override fun name(): String = "Gecko"

    override val version: EngineVersion = EngineVersion.parse(
        org.mozilla.geckoview.BuildConfig.MOZILLA_VERSION,
        org.mozilla.geckoview.BuildConfig.MOZ_UPDATE_CHANNEL,
    ) ?: throw IllegalStateException("Could not determine engine version")

    /**
     * See [Engine.settings]
     */
    @kotlin.OptIn(ExperimentalAndroidComponentsApi::class)
    override val settings: Settings = object : Settings() {
        override var javascriptEnabled: Boolean
            get() = runtime.settings.javaScriptEnabled
            set(value) { runtime.settings.javaScriptEnabled = value }

        override var webFontsEnabled: Boolean
            get() = runtime.settings.webFontsEnabled
            set(value) { runtime.settings.webFontsEnabled = value }

        override var automaticFontSizeAdjustment: Boolean
            get() = runtime.settings.automaticFontSizeAdjustment
            set(value) { runtime.settings.automaticFontSizeAdjustment = value }

        override var automaticLanguageAdjustment: Boolean
            get() = localeUpdater.enabled
            set(value) {
                localeUpdater.enabled = value
                defaultSettings?.automaticLanguageAdjustment = value
            }

        override var safeBrowsingPolicy: Array<SafeBrowsingPolicy> =
            arrayOf(SafeBrowsingPolicy.RECOMMENDED)
            set(value) {
                val policy = value.sumOf { it.id }
                runtime.settings.contentBlocking.setSafeBrowsing(policy)
                field = value
            }

        override var trackingProtectionPolicy: TrackingProtectionPolicy? = null
            set(value) {
                value?.let { policy ->
                    with(runtime.settings.contentBlocking) {
                        if (enhancedTrackingProtectionLevel != value.getEtpLevel()) {
                            enhancedTrackingProtectionLevel = value.getEtpLevel()
                        }

                        if (getEnhancedTrackingProtectionCategory() != policy.getEtpCategory()) {
                            setEnhancedTrackingProtectionCategory(policy.getEtpCategory())
                        }

                        if (strictSocialTrackingProtection != value.getStrictSocialTrackingProtection()) {
                            strictSocialTrackingProtection = policy.getStrictSocialTrackingProtection()
                        }

                        if (antiTrackingCategories != value.getAntiTrackingPolicy()) {
                            setAntiTracking(policy.getAntiTrackingPolicy())
                        }

                        if (cookieBehavior != value.cookiePolicy.id) {
                            cookieBehavior = value.cookiePolicy.id
                        }

                        if (cookieBehaviorPrivateMode != value.cookiePolicyPrivateMode.id) {
                            cookieBehaviorPrivateMode = value.cookiePolicyPrivateMode.id
                        }

                        if (cookiePurging != value.cookiePurging) {
                            setCookiePurging(value.cookiePurging)
                        }

                        if (getBounceTrackingProtectionMode() != policy.bounceTrackingProtectionMode.mode) {
                            setBounceTrackingProtectionMode(policy.bounceTrackingProtectionMode.mode)
                        }

                        if (allowListBaselineTrackingProtection != value.allowListBaselineTrackingProtection) {
                            setAllowListBaselineTrackingProtection(value.allowListBaselineTrackingProtection)
                        }

                        if (allowListConvenienceTrackingProtection != value.allowListConvenienceTrackingProtection) {
                            setAllowListConvenienceTrackingProtection(value.allowListConvenienceTrackingProtection)
                        }
                    }

                    defaultSettings?.trackingProtectionPolicy = value
                    field = value
                }
            }

        override var cookieBannerHandlingMode: CookieBannerHandlingMode = CookieBannerHandlingMode.DISABLED
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.cookieBannerMode != value.mode) {
                        this.cookieBannerMode = value.mode
                    }
                }
                field = value
            }

        override var cookieBannerHandlingModePrivateBrowsing: CookieBannerHandlingMode =
            CookieBannerHandlingMode.REJECT_ALL
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.cookieBannerModePrivateBrowsing != value.mode) {
                        this.cookieBannerModePrivateBrowsing = value.mode
                    }
                }
                field = value
            }

        override var emailTrackerBlockingPrivateBrowsing: Boolean = false
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.emailTrackerBlockingPrivateBrowsingEnabled != value) {
                        this.setEmailTrackerBlockingPrivateBrowsing(value)
                    }
                }
                field = value
            }

        override var cookieBannerHandlingDetectOnlyMode: Boolean = false
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.cookieBannerDetectOnlyMode != value) {
                        this.cookieBannerDetectOnlyMode = value
                    }
                }
                field = value
            }

        override var cookieBannerHandlingGlobalRules: Boolean = false
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.cookieBannerGlobalRulesEnabled != value) {
                        this.cookieBannerGlobalRulesEnabled = value
                    }
                }
                field = value
            }

        override var cookieBannerHandlingGlobalRulesSubFrames: Boolean = false
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.cookieBannerGlobalRulesSubFramesEnabled != value) {
                        this.cookieBannerGlobalRulesSubFramesEnabled = value
                    }
                }
                field = value
            }

        override var queryParameterStripping: Boolean = false
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.queryParameterStrippingEnabled != value) {
                        this.queryParameterStrippingEnabled = value
                    }
                }
                field = value
            }

        override var queryParameterStrippingPrivateBrowsing: Boolean = false
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.queryParameterStrippingPrivateBrowsingEnabled != value) {
                        this.queryParameterStrippingPrivateBrowsingEnabled = value
                    }
                }
                field = value
            }

        @Suppress("SpreadOperator")
        override var queryParameterStrippingAllowList: String = ""
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.queryParameterStrippingAllowList.joinToString() != value) {
                        this.setQueryParameterStrippingAllowList(
                            *value.split(",")
                                .toTypedArray(),
                        )
                    }
                }
                field = value
            }

        @Suppress("SpreadOperator")
        override var queryParameterStrippingStripList: String = ""
            set(value) {
                with(runtime.settings.contentBlocking) {
                    if (this.queryParameterStrippingStripList.joinToString() != value) {
                        this.setQueryParameterStrippingStripList(
                            *value.split(",").toTypedArray(),
                        )
                    }
                }
                field = value
            }

        override var remoteDebuggingEnabled: Boolean
            get() = runtime.settings.remoteDebuggingEnabled
            set(value) { runtime.settings.remoteDebuggingEnabled = value }

        override var historyTrackingDelegate: HistoryTrackingDelegate?
            get() = defaultSettings?.historyTrackingDelegate
            set(value) { defaultSettings?.historyTrackingDelegate = value }

        override var downloadDelegate: DownloadDelegate?
            get() = defaultSettings?.downloadDelegate
            set(value) { defaultSettings?.downloadDelegate = value }

        override var testingModeEnabled: Boolean
            get() = defaultSettings?.testingModeEnabled ?: false
            set(value) { defaultSettings?.testingModeEnabled = value }

        override var userAgentString: String?
            get() = defaultSettings?.userAgentString ?: GeckoSession.getDefaultUserAgent()
            set(value) { defaultSettings?.userAgentString = value }

        override var preferredColorScheme: PreferredColorScheme
            get() = PreferredColorScheme.from(runtime.settings.preferredColorScheme)
            set(value) { runtime.settings.preferredColorScheme = value.toGeckoValue() }

        override var suspendMediaWhenInactive: Boolean
            get() = defaultSettings?.suspendMediaWhenInactive ?: false
            set(value) { defaultSettings?.suspendMediaWhenInactive = value }

        override var clearColor: Int?
            get() = defaultSettings?.clearColor
            set(value) { defaultSettings?.clearColor = value }

        override var fontInflationEnabled: Boolean?
            get() = runtime.settings.fontInflationEnabled
            set(value) {
                // automaticFontSizeAdjustment is set to true by default, which
                // will cause an exception if fontInflationEnabled is set
                // (to either true or false). We therefore need to be able to
                // set our built-in default value to null so that the exception
                // is only thrown if an app is configured incorrectly but not
                // if it uses default values.
                value?.let {
                    runtime.settings.fontInflationEnabled = it
                }
            }

        override var fontSizeFactor: Float?
            get() = runtime.settings.fontSizeFactor
            set(value) {
                // automaticFontSizeAdjustment is set to true by default, which
                // will cause an exception if fontSizeFactor is set as well.
                // We therefore need to be able to set our built-in default value
                // to null so that the exception is only thrown if an app is
                // configured incorrectly but not if it uses default values.
                value?.let {
                    runtime.settings.fontSizeFactor = it
                }
            }

        override var loginAutofillEnabled: Boolean
            get() = runtime.settings.loginAutofillEnabled
            set(value) { runtime.settings.loginAutofillEnabled = value }

        @ExperimentalAndroidComponentsApi
        override var firefoxRelay: Engine.FirefoxRelayMode?
            @OptIn(ExperimentalGeckoViewApi::class)
            get() = when (runtime.settings.firefoxRelay) {
                GeckoRuntimeSettings.FIREFOX_RELAY_AVAILABLE -> Engine.FirefoxRelayMode.AVAILABLE
                GeckoRuntimeSettings.FIREFOX_RELAY_OFFERED -> Engine.FirefoxRelayMode.OFFERED
                GeckoRuntimeSettings.FIREFOX_RELAY_ENABLED -> Engine.FirefoxRelayMode.ENABLED
                GeckoRuntimeSettings.FIREFOX_RELAY_DISABLED -> Engine.FirefoxRelayMode.DISABLED
                else -> null
            }

            @OptIn(ExperimentalGeckoViewApi::class)
            set(value) {
                value?.let {
                    val mode = when (it) {
                        Engine.FirefoxRelayMode.AVAILABLE -> GeckoRuntimeSettings.FIREFOX_RELAY_AVAILABLE
                        Engine.FirefoxRelayMode.OFFERED -> GeckoRuntimeSettings.FIREFOX_RELAY_OFFERED
                        Engine.FirefoxRelayMode.ENABLED -> GeckoRuntimeSettings.FIREFOX_RELAY_ENABLED
                        Engine.FirefoxRelayMode.DISABLED -> GeckoRuntimeSettings.FIREFOX_RELAY_DISABLED
                    }
                    runtime.settings.setFirefoxRelay(mode)
                }
            }

        override var forceUserScalableContent: Boolean
            get() = runtime.settings.forceUserScalableEnabled
            set(value) { runtime.settings.forceUserScalableEnabled = value }

        override var enterpriseRootsEnabled: Boolean
            get() = runtime.settings.enterpriseRootsEnabled
            set(value) { runtime.settings.enterpriseRootsEnabled = value }

        override var httpsOnlyMode: Engine.HttpsOnlyMode
            get() = when (runtime.settings.allowInsecureConnections) {
                GeckoRuntimeSettings.ALLOW_ALL -> Engine.HttpsOnlyMode.DISABLED
                GeckoRuntimeSettings.HTTPS_ONLY_PRIVATE -> Engine.HttpsOnlyMode.ENABLED_PRIVATE_ONLY
                GeckoRuntimeSettings.HTTPS_ONLY -> Engine.HttpsOnlyMode.ENABLED
                else -> throw java.lang.IllegalStateException("Unknown HTTPS-Only mode returned by GeckoView")
            }
            set(value) {
                runtime.settings.allowInsecureConnections = when (value) {
                    Engine.HttpsOnlyMode.DISABLED -> GeckoRuntimeSettings.ALLOW_ALL
                    Engine.HttpsOnlyMode.ENABLED_PRIVATE_ONLY -> GeckoRuntimeSettings.HTTPS_ONLY_PRIVATE
                    Engine.HttpsOnlyMode.ENABLED -> GeckoRuntimeSettings.HTTPS_ONLY
                }
            }

        @Suppress("TooGenericExceptionCaught")
        override var dohSettingsMode: Engine.DohSettingsMode
            get() {
                try {
                    runtime.settings.trustedRecusiveResolverMode
                } catch (npe: NullPointerException) {
                    runtime.settings.setTrustedRecursiveResolverMode(GeckoRuntimeSettings.TRR_MODE_OFF)
                }
                return when (runtime.settings.trustedRecusiveResolverMode) {
                    GeckoRuntimeSettings.TRR_MODE_OFF -> Engine.DohSettingsMode.DEFAULT
                    GeckoRuntimeSettings.TRR_MODE_FIRST -> Engine.DohSettingsMode.INCREASED
                    GeckoRuntimeSettings.TRR_MODE_ONLY -> Engine.DohSettingsMode.MAX
                    GeckoRuntimeSettings.TRR_MODE_DISABLED -> Engine.DohSettingsMode.OFF
                    else -> Engine.DohSettingsMode.DEFAULT
                }
            }
            set(value) {
                when (value) {
                    Engine.DohSettingsMode.DEFAULT ->
                        runtime.settings.setTrustedRecursiveResolverMode(GeckoRuntimeSettings.TRR_MODE_OFF)

                    Engine.DohSettingsMode.INCREASED ->
                        runtime.settings.setTrustedRecursiveResolverMode(GeckoRuntimeSettings.TRR_MODE_FIRST)

                    Engine.DohSettingsMode.MAX ->
                        runtime.settings.setTrustedRecursiveResolverMode(GeckoRuntimeSettings.TRR_MODE_ONLY)

                    Engine.DohSettingsMode.OFF ->
                        runtime.settings.setTrustedRecursiveResolverMode(GeckoRuntimeSettings.TRR_MODE_DISABLED)
                }
            }

        @Suppress("TooGenericExceptionCaught")
        override var dohProviderUrl: String
            get() {
                return try {
                    runtime.settings.trustedRecursiveResolverUri
                } catch (npe: NullPointerException) {
                    // network.trr.uri pref has not been set
                    runtime.settings.setTrustedRecursiveResolverUri("")
                    runtime.settings.trustedRecursiveResolverUri
                }
            }
            set(value) { runtime.settings.setTrustedRecursiveResolverUri(value) }

        @Suppress("TooGenericExceptionCaught")
        override var dohDefaultProviderUrl: String?
            get() {
                return try {
                    runtime.settings.defaultRecursiveResolverUri
                } catch (npe: NullPointerException) {
                    // network.trr.default_provider_uri pref has not been set
                    runtime.settings.setDefaultRecursiveResolverUri("")
                    runtime.settings.defaultRecursiveResolverUri
                }
            }
            set(value) {
                if (value != null) {
                    runtime.settings.setDefaultRecursiveResolverUri(value)
                }
            }

        @Suppress("TooGenericExceptionCaught")
        override var dohExceptionsList: List<String>
            get() {
                return try {
                    runtime.settings.trustedRecursiveResolverExcludedDomains
                } catch (npe: NullPointerException) {
                    // network.trr.excluded-domains pref has not been set
                    runtime.settings.setTrustedRecursiveResolverExcludedDomains(emptyList())
                    runtime.settings.trustedRecursiveResolverExcludedDomains
                }
            }
            set(value) { runtime.settings.setTrustedRecursiveResolverExcludedDomains(value) }

        override var globalPrivacyControlEnabled: Boolean
            get() = runtime.settings.globalPrivacyControl
            set(value) { runtime.settings.setGlobalPrivacyControl(value) }

        override var fingerprintingProtection: Boolean?
            get() = runtime.settings.fingerprintingProtection
            set(value) {
                value?.let { runtime.settings.setFingerprintingProtection(it) }
            }

        override var fingerprintingProtectionPrivateBrowsing: Boolean?
            get() = runtime.settings.fingerprintingProtectionPrivateBrowsing
            set(value) {
                value?.let { runtime.settings.setFingerprintingProtectionPrivateBrowsing(it) }
            }

        override var fingerprintingProtectionOverrides: String?
            get() = runtime.settings.fingerprintingProtectionOverrides
            set(value) {
                value?.let { runtime.settings.setFingerprintingProtectionOverrides(it) }
            }

        override var fdlibmMathEnabled: Boolean
            get() = runtime.settings.fdlibmMathEnabled
            set(value) { runtime.settings.setFdlibmMathEnabled(value) }

        override var userCharacteristicPingCurrentVersion: Int
            get() = runtime.settings.userCharacteristicPingCurrentVersion
            set(value) { runtime.settings.setUserCharacteristicPingCurrentVersion(value) }

        override var baselineFingerprintingProtection: Boolean?
            get() = runtime.settings.baselineFingerprintingProtection
            set(value) {
                value?.let { runtime.settings.setBaselineFingerprintingProtection(it) }
            }

        override var baselineFingerprintingProtectionOverrides: String?
            get() = runtime.settings.baselineFingerprintingProtectionOverrides
            set(value) {
                value?.let { runtime.settings.setBaselineFingerprintingProtectionOverrides(it) }
            }

        override var webContentIsolationStrategy: WebContentIsolationStrategy?
            get() = runtime.settings.webContentIsolationStrategy?.intoWebContentIsolationStrategy()
            set(value) {
                value?.let { runtime.settings.setWebContentIsolationStrategy(it.intoWebContentIsolationStrategy()) }
            }

        override var fetchPriorityEnabled: Boolean
            get() = runtime.settings.fetchPriorityEnabled
            set(value) { runtime.settings.setFetchPriorityEnabled(value) }

        override var parallelMarkingEnabled: Boolean
            get() = runtime.settings.parallelMarkingEnabled
            set(value) { runtime.settings.setParallelMarkingEnabled(value) }

        override var cookieBehaviorOptInPartitioning: Boolean
            get() = runtime.settings.cookieBehaviorOptInPartitioning
            set(value) { runtime.settings.setCookieBehaviorOptInPartitioning(value) }

        override var cookieBehaviorOptInPartitioningPBM: Boolean
            get() = runtime.settings.cookieBehaviorOptInPartitioningPBM
            set(value) { runtime.settings.setCookieBehaviorOptInPartitioningPBM(value) }

        override var certificateTransparencyMode: Int?
            get() = runtime.settings.certificateTransparencyMode.or(2)
            set(value) { value?.let { runtime.settings.setCertificateTransparencyMode(value) } }

        override var postQuantumKeyExchangeEnabled: Boolean?
            get() = runtime.settings.postQuantumKeyExchangeEnabled.or(false)
            set(value) { value?.let { runtime.settings.setPostQuantumKeyExchangeEnabled(value) } }

        override var dohAutoselectEnabled: Boolean
            get() = runtime.settings.dohAutoselectEnabled
            set(value) { runtime.settings.setDohAutoselectEnabled(value) }

        override var bannedPorts: String
            get() = runtime.settings.bannedPorts
            set(value) { runtime.settings.setBannedPorts(value) }

        override var lnaBlockingEnabled: Boolean
            get() = runtime.settings.lnaBlocking ?: false
            set(value) { runtime.settings.setLnaBlocking(value) }

        override var lnaTrackerBlockingEnabled: Boolean
            get() = runtime.settings.lnaBlockTrackers ?: false
            set(value) { runtime.settings.setLnaBlockTrackers(value) }

        override var lnaFeatureEnabled: Boolean
            get() = runtime.settings.lnaEnabled ?: false
            set(value) { runtime.settings.setLnaEnabled(value) }

        override var crliteChannel: String?
            get() = runtime.settings.crliteChannel
            set(value) { value?.let { runtime.settings.setCrliteChannel(value) } }

        override var safeBrowsingV5Enabled: Boolean?
            get() = runtime.settings.contentBlocking.safeBrowsingV5Enabled.or(false)
            set(value) { value?.let { runtime.settings.contentBlocking.setSafeBrowsingV5Enabled(value) } }

        override var safeBrowsingGlobalCacheEnabled: Boolean?
            get() = runtime.settings.contentBlocking.safeBrowsingGlobalCacheEnabled
            set(value) { value?.let { runtime.settings.contentBlocking.setSafeBrowsingGlobalCacheEnabled(it) } }

        override var safeBrowsingRealTimeEnabled: Boolean?
            get() = runtime.settings.contentBlocking.safeBrowsingRealTimeEnabled
            set(value) { value?.let { runtime.settings.contentBlocking.setSafeBrowsingRealTimeEnabled(it) } }

        @ExperimentalAndroidComponentsApi
        override var safeBrowsingRealTimeSimulationEnabled: Boolean?
            get() = runtime.settings.contentBlocking.safeBrowsingRealTimeSimulationEnabled
            set(value) {
                value?.let { runtime.settings.contentBlocking.setSafeBrowsingRealTimeSimulationEnabled(it) }
            }

        @ExperimentalAndroidComponentsApi
        override var safeBrowsingRealTimeSimulationHitProbability: Int?
            get() = runtime.settings.contentBlocking.safeBrowsingRealTimeSimulationHitProbability
            set(value) {
                value?.let { runtime.settings.contentBlocking.setSafeBrowsingRealTimeSimulationHitProbability(it) }
            }

        @ExperimentalAndroidComponentsApi
        override var safeBrowsingRealTimeSimulationCacheTTLSec: Int?
            get() = runtime.settings.contentBlocking.safeBrowsingRealTimeSimulationCacheTTLSec
            set(value) {
                value?.let { runtime.settings.contentBlocking.setSafeBrowsingRealTimeSimulationCacheTTLSec(it) }
            }

        @ExperimentalAndroidComponentsApi
        override var safeBrowsingRealTimeSimulationNegativeCacheEnabled: Boolean?
            get() =
              runtime.settings.contentBlocking.safeBrowsingRealTimeSimulationNegativeCacheEnabled
            set(value) {
                value?.let {
                    runtime.settings.contentBlocking.setSafeBrowsingRealTimeSimulationNegativeCacheEnabled(it)
                }
            }

        @ExperimentalAndroidComponentsApi
        override var safeBrowsingRealTimeSimulationNegativeCacheTTLSec: Int?
            get() = runtime.settings.contentBlocking.safeBrowsingRealTimeSimulationNegativeCacheTTLSec
            set(value) {
                value?.let { runtime.settings.contentBlocking.setSafeBrowsingRealTimeSimulationNegativeCacheTTLSec(it) }
            }

        override var browserML: Boolean?
            get() = runtime.settings.browserMLEnabled
            set(value) {
                value?.also { runtime.settings.setBrowserMlEnabled(it) }
            }

        override var extensionsML: Boolean?
            get() = runtime.settings.extensionsMLEnabled
            set(value) {
                value?.also { runtime.settings.setExtensionsMlEnabled(it) }
            }
    }.apply {
        defaultSettings?.let {
            this.javascriptEnabled = it.javascriptEnabled
            this.webFontsEnabled = it.webFontsEnabled
            this.automaticFontSizeAdjustment = it.automaticFontSizeAdjustment
            this.automaticLanguageAdjustment = it.automaticLanguageAdjustment
            this.trackingProtectionPolicy = it.trackingProtectionPolicy
            this.safeBrowsingPolicy = arrayOf(SafeBrowsingPolicy.RECOMMENDED)
            this.remoteDebuggingEnabled = it.remoteDebuggingEnabled
            this.testingModeEnabled = it.testingModeEnabled
            this.userAgentString = it.userAgentString
            this.preferredColorScheme = it.preferredColorScheme
            this.fontInflationEnabled = it.fontInflationEnabled
            this.fontSizeFactor = it.fontSizeFactor
            this.forceUserScalableContent = it.forceUserScalableContent
            this.clearColor = it.clearColor
            this.loginAutofillEnabled = it.loginAutofillEnabled
            this.firefoxRelay = it.firefoxRelay
            this.enterpriseRootsEnabled = it.enterpriseRootsEnabled
            this.httpsOnlyMode = it.httpsOnlyMode
            this.dohSettingsMode = it.dohSettingsMode
            this.dohProviderUrl = it.dohProviderUrl
            this.dohDefaultProviderUrl = it.dohDefaultProviderUrl
            this.dohExceptionsList = it.dohExceptionsList
            this.cookieBannerHandlingMode = it.cookieBannerHandlingMode
            this.cookieBannerHandlingModePrivateBrowsing = it.cookieBannerHandlingModePrivateBrowsing
            this.cookieBannerHandlingDetectOnlyMode = it.cookieBannerHandlingDetectOnlyMode
            this.cookieBannerHandlingGlobalRules = it.cookieBannerHandlingGlobalRules
            this.cookieBannerHandlingGlobalRulesSubFrames = it.cookieBannerHandlingGlobalRulesSubFrames
            this.globalPrivacyControlEnabled = it.globalPrivacyControlEnabled
            this.fingerprintingProtection = it.fingerprintingProtection
            this.fingerprintingProtectionPrivateBrowsing = it.fingerprintingProtectionPrivateBrowsing
            this.fingerprintingProtectionOverrides = it.fingerprintingProtectionOverrides
            this.fdlibmMathEnabled = it.fdlibmMathEnabled
            this.emailTrackerBlockingPrivateBrowsing = it.emailTrackerBlockingPrivateBrowsing
            this.userCharacteristicPingCurrentVersion = it.userCharacteristicPingCurrentVersion
            this.baselineFingerprintingProtection = it.baselineFingerprintingProtection
            this.baselineFingerprintingProtectionOverrides = it.baselineFingerprintingProtectionOverrides
            this.webContentIsolationStrategy = it.webContentIsolationStrategy
            this.fetchPriorityEnabled = it.fetchPriorityEnabled
            this.parallelMarkingEnabled = it.parallelMarkingEnabled
            this.cookieBehaviorOptInPartitioning = it.cookieBehaviorOptInPartitioning
            this.cookieBehaviorOptInPartitioningPBM = it.cookieBehaviorOptInPartitioningPBM
            this.certificateTransparencyMode = it.certificateTransparencyMode
            this.postQuantumKeyExchangeEnabled = it.postQuantumKeyExchangeEnabled
            this.dohAutoselectEnabled = it.dohAutoselectEnabled
            this.bannedPorts = it.bannedPorts
            this.lnaBlockingEnabled = it.lnaBlockingEnabled
            this.lnaFeatureEnabled = it.lnaFeatureEnabled
            this.lnaTrackerBlockingEnabled = it.lnaTrackerBlockingEnabled
            this.crliteChannel = it.crliteChannel
            this.safeBrowsingV5Enabled = it.safeBrowsingV5Enabled
            this.downloadDelegate = it.downloadDelegate
            this.safeBrowsingGlobalCacheEnabled = it.safeBrowsingGlobalCacheEnabled
            this.safeBrowsingRealTimeEnabled = it.safeBrowsingRealTimeEnabled
            this.safeBrowsingRealTimeSimulationEnabled = it.safeBrowsingRealTimeSimulationEnabled
            this.safeBrowsingRealTimeSimulationHitProbability = it.safeBrowsingRealTimeSimulationHitProbability
            this.safeBrowsingRealTimeSimulationCacheTTLSec = it.safeBrowsingRealTimeSimulationCacheTTLSec
            this.safeBrowsingRealTimeSimulationNegativeCacheEnabled =
                it.safeBrowsingRealTimeSimulationNegativeCacheEnabled
            this.safeBrowsingRealTimeSimulationNegativeCacheTTLSec =
                it.safeBrowsingRealTimeSimulationNegativeCacheTTLSec
            this.browserML = it.browserML
            this.extensionsML = it.extensionsML
        }
    }

    internal fun ContentBlockingController.LogEntry.BlockingData.getLoadedCategory(): TrackingCategory {
        val socialTrackingProtectionEnabled = settings.trackingProtectionPolicy?.strictSocialTrackingProtection
            ?: false

        return when (category) {
            Event.LOADED_FINGERPRINTING_CONTENT -> TrackingCategory.FINGERPRINTING
            Event.LOADED_CRYPTOMINING_CONTENT -> TrackingCategory.CRYPTOMINING
            Event.LOADED_SOCIALTRACKING_CONTENT -> {
                if (socialTrackingProtectionEnabled) TrackingCategory.MOZILLA_SOCIAL else TrackingCategory.NONE
            }
            Event.COOKIES_LOADED_SOCIALTRACKER -> {
                if (!socialTrackingProtectionEnabled) TrackingCategory.MOZILLA_SOCIAL else TrackingCategory.NONE
            }
            Event.LOADED_LEVEL_1_TRACKING_CONTENT -> TrackingCategory.SCRIPTS_AND_SUB_RESOURCES
            Event.LOADED_LEVEL_2_TRACKING_CONTENT -> {
                // We are making sure that we are only showing trackers that our settings are
                // taking into consideration.
                val isContentListActive =
                    settings.trackingProtectionPolicy?.contains(TrackingCategory.CONTENT)
                        ?: false
                val isStrictLevelActive =
                    runtime.settings
                        .contentBlocking
                        .getEnhancedTrackingProtectionLevel() == ContentBlocking.EtpLevel.STRICT

                if (isStrictLevelActive && isContentListActive) {
                    TrackingCategory.SCRIPTS_AND_SUB_RESOURCES
                } else {
                    TrackingCategory.NONE
                }
            }
            else -> TrackingCategory.NONE
        }
    }

    private fun isCategoryActive(category: TrackingCategory) = settings.trackingProtectionPolicy?.contains(category)
        ?: false

    /**
     * Mimics the behavior for categorizing trackers from desktop, they should be kept in sync,
     * as differences will result in improper categorization for trackers.
     * https://dxr.mozilla.org/mozilla-central/source/browser/base/content/browser-siteProtections.js
     */
    internal fun ContentBlockingController.LogEntry.toTrackerLog(): TrackerLog {
        val cookiesHasBeenBlocked = this.blockingData.any { it.hasBlockedCookies() }
        val blockedCategories = blockingData.map { it.getBlockedCategory() }
            .filterNot { it == TrackingCategory.NONE }
            .distinct()
        val loadedCategories = blockingData.map { it.getLoadedCategory() }
            .filterNot { it == TrackingCategory.NONE }
            .distinct()

        /**
         *  When a resource is shimmed we'll received a [REPLACED_TRACKING_CONTENT] event with
         *  the quantity [BlockingData.count] of categories that were shimmed, but it doesn't
         *  specify which ones, it only tells us how many. For example:
         *     {
         *      "category": REPLACED_TRACKING_CONTENT,
         *      "count": 2
         *     }
         *
         *  This indicates that there are 2 categories that were shimmed, as a result
         *  we have to infer based on the categories that are active vs the amount of
         *  shimmed categories, for example:
         *
         *     "blockData": [
         *      {
         *          "category": LOADED_LEVEL_1_TRACKING_CONTENT,
         *          "count": 1
         *      },
         *      {
         *          "category": LOADED_SOCIALTRACKING_CONTENT,
         *          "count": 1
         *      },
         *      {
         *          "category": REPLACED_TRACKING_CONTENT,
         *          "count": 2
         *      }
         *     ]
         *  This indicates that categories [LOADED_LEVEL_1_TRACKING_CONTENT] and
         *  [LOADED_SOCIALTRACKING_CONTENT] were loaded but shimmed and we should display them
         *  as blocked instead of loaded.
         */
        val shimmedCount = blockingData.find {
            it.category == Event.REPLACED_TRACKING_CONTENT
        }?.count ?: 0

        // If we find blocked categories that are loaded it means they were shimmed.
        val shimmedCategories = loadedCategories.filter { isCategoryActive(it) }
            .take(shimmedCount)

        // We have to remove the categories that are shimmed from the loaded list and
        // put them back in the blocked list.
        return TrackerLog(
            url = origin,
            loadedCategories = loadedCategories.filterNot { it in shimmedCategories },
            blockedCategories = (blockedCategories + shimmedCategories).distinct(),
            cookiesHasBeenBlocked = cookiesHasBeenBlocked,
            unBlockedBySmartBlock = this.blockingData.any { it.unBlockedBySmartBlock() },
        )
    }

    internal fun org.mozilla.geckoview.WebExtension?.toSafeWebExtension(): GeckoWebExtension? {
        return if (this != null) {
            GeckoWebExtension(
                nativeExtension = this,
                runtime = runtime,
            )
        } else {
            null
        }
    }

    private fun onExtensionInstalled(
        ext: org.mozilla.geckoview.WebExtension,
        onSuccess: ((WebExtension) -> Unit),
    ) {
        val installedExtension = GeckoWebExtension(
            nativeExtension = ext,
            runtime = runtime,
        )
        webExtensionDelegate?.onInstalled(installedExtension)
        installedExtension.registerActionHandler(webExtensionActionHandler)
        installedExtension.registerTabHandler(webExtensionTabHandler, defaultSettings)
        onSuccess(installedExtension)
    }
}

internal fun ContentBlockingController.LogEntry.BlockingData.hasBlockedCookies(): Boolean {
    return category == Event.COOKIES_BLOCKED_BY_PERMISSION ||
        category == Event.COOKIES_BLOCKED_TRACKER ||
        category == Event.COOKIES_BLOCKED_ALL ||
        category == Event.COOKIES_PARTITIONED_TRACKER ||
        category == Event.COOKIES_PARTITIONED_FOREIGN ||
        category == Event.COOKIES_BLOCKED_FOREIGN ||
        category == Event.COOKIES_BLOCKED_SOCIALTRACKER
}

internal fun ContentBlockingController.LogEntry.BlockingData.unBlockedBySmartBlock(): Boolean {
    return category == Event.ALLOWED_TRACKING_CONTENT
}

internal fun ContentBlockingController.LogEntry.BlockingData.getBlockedCategory(): TrackingCategory {
    return when (category) {
        Event.BLOCKED_FINGERPRINTING_CONTENT -> TrackingCategory.FINGERPRINTING
        Event.BLOCKED_SUSPICIOUS_FINGERPRINTING -> TrackingCategory.FINGERPRINTING
        Event.BLOCKED_CRYPTOMINING_CONTENT -> TrackingCategory.CRYPTOMINING
        Event.BLOCKED_SOCIALTRACKING_CONTENT, Event.COOKIES_BLOCKED_SOCIALTRACKER -> TrackingCategory.MOZILLA_SOCIAL
        Event.BLOCKED_TRACKING_CONTENT -> TrackingCategory.SCRIPTS_AND_SUB_RESOURCES
        else -> TrackingCategory.NONE
    }
}

internal fun InstallationMethod.toGeckoInstallationMethod(): String? {
    return when (this) {
        InstallationMethod.MANAGER -> WebExtensionController.INSTALLATION_METHOD_MANAGER
        InstallationMethod.FROM_FILE -> WebExtensionController.INSTALLATION_METHOD_FROM_FILE
        InstallationMethod.ONBOARDING -> WebExtensionController.INSTALLATION_METHOD_ONBOARDING
    }
}
