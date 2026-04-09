/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.engine.gecko

import android.os.Build
import android.view.WindowManager
import androidx.annotation.OptIn
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.launch
import mozilla.components.browser.engine.gecko.ext.isExcludedForTrackingProtection
import mozilla.components.browser.engine.gecko.fetch.toResponse
import mozilla.components.browser.engine.gecko.media.GeckoMediaDelegate
import mozilla.components.browser.engine.gecko.mediasession.GeckoMediaSessionDelegate
import mozilla.components.browser.engine.gecko.pageextraction.intoPageExtractionError
import mozilla.components.browser.engine.gecko.permission.GeckoPermissionRequest
import mozilla.components.browser.engine.gecko.prompt.GeckoPromptDelegate
import mozilla.components.browser.engine.gecko.translate.GeckoTranslateSessionDelegate
import mozilla.components.browser.engine.gecko.translate.GeckoTranslationUtils.intoTranslationError
import mozilla.components.browser.engine.gecko.window.GeckoWindowRequest
import mozilla.components.browser.errorpages.ErrorType
import mozilla.components.concept.engine.DownloadDelegate
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags.Companion.ALLOW_ADDITIONAL_HEADERS
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags.Companion.ALLOW_JAVASCRIPT_URL
import mozilla.components.concept.engine.EngineSessionState
import mozilla.components.concept.engine.HitResult
import mozilla.components.concept.engine.Settings
import mozilla.components.concept.engine.content.blocking.Tracker
import mozilla.components.concept.engine.history.HistoryItem
import mozilla.components.concept.engine.history.HistoryTrackingDelegate
import mozilla.components.concept.engine.manifest.WebAppManifest
import mozilla.components.concept.engine.manifest.WebAppManifestParser
import mozilla.components.concept.engine.pageextraction.ContentParams
import mozilla.components.concept.engine.pageextraction.PageExtractionError
import mozilla.components.concept.engine.pageextraction.PageMetadata
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.concept.engine.request.RequestInterceptor.InterceptionResponse
import mozilla.components.concept.engine.translate.TranslationError
import mozilla.components.concept.engine.translate.TranslationOperation
import mozilla.components.concept.engine.translate.TranslationOptions
import mozilla.components.concept.engine.window.WindowRequest
import mozilla.components.concept.fetch.Headers.Names.CONTENT_DISPOSITION
import mozilla.components.concept.fetch.Headers.Names.CONTENT_LENGTH
import mozilla.components.concept.fetch.Headers.Names.CONTENT_TYPE
import mozilla.components.concept.fetch.MutableHeaders
import mozilla.components.concept.fetch.Response
import mozilla.components.concept.storage.PageVisit
import mozilla.components.concept.storage.RedirectSource
import mozilla.components.concept.storage.VisitType
import mozilla.components.support.base.Component
import mozilla.components.support.base.facts.Action
import mozilla.components.support.base.facts.Fact
import mozilla.components.support.base.facts.collect
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.kotlin.isEmail
import mozilla.components.support.ktx.kotlin.isExtensionUrl
import mozilla.components.support.ktx.kotlin.isGeoLocation
import mozilla.components.support.ktx.kotlin.isPhone
import mozilla.components.support.ktx.kotlin.sanitizeFileName
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl
import mozilla.components.support.utils.CertificateUtils
import mozilla.components.support.utils.DownloadUtils
import mozilla.components.support.utils.DownloadUtils.RESPONSE_CODE_SUCCESS
import mozilla.components.support.utils.DownloadUtils.makePdfContentDisposition
import org.json.JSONObject
import org.mozilla.geckoview.AllowOrDeny
import org.mozilla.geckoview.ContentBlocking
import org.mozilla.geckoview.ExperimentalGeckoViewApi
import org.mozilla.geckoview.GeckoResult
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoSession
import org.mozilla.geckoview.GeckoSession.APP_LINK_LAUNCH_TYPE_COLD
import org.mozilla.geckoview.GeckoSession.APP_LINK_LAUNCH_TYPE_HOT
import org.mozilla.geckoview.GeckoSession.APP_LINK_LAUNCH_TYPE_UNKNOWN
import org.mozilla.geckoview.GeckoSession.APP_LINK_LAUNCH_TYPE_WARM
import org.mozilla.geckoview.GeckoSession.NavigationDelegate
import org.mozilla.geckoview.GeckoSession.PermissionDelegate.ContentPermission
import org.mozilla.geckoview.GeckoSessionSettings
import org.mozilla.geckoview.PageExtractionController
import org.mozilla.geckoview.WebRequestError
import org.mozilla.geckoview.WebResponse
import java.security.cert.X509Certificate
import java.util.Locale
import kotlin.coroutines.CoroutineContext
import org.mozilla.geckoview.TranslationsController.SessionTranslation as GeckoViewTranslateSession

/**
 * Gecko-based EngineSession implementation.
 */
@Suppress("TooManyFunctions", "LargeClass")
class GeckoEngineSession(
    private val runtime: GeckoRuntime,
    private val privateMode: Boolean = false,
    private val defaultSettings: Settings? = null,
    contextId: String? = null,
    private val geckoSessionProvider: () -> GeckoSession = {
        val settings = GeckoSessionSettings.Builder()
            .usePrivateMode(privateMode)
            .contextId(contextId)
            .build()
        GeckoSession(settings)
    },
    private val context: CoroutineContext = Dispatchers.IO,
    openGeckoSession: Boolean = true,
) : CoroutineScope, EngineSession() {

    // This logger is temporary and parsed by FNPRMS for performance measurements. It can be
    // removed once FNPRMS is replaced: https://github.com/mozilla-mobile/android-components/issues/8662
    // It mimics GeckoView debug log statements, hence the unintuitive tag and messages.
    private val fnprmsLogger = Logger("GeckoSession")

    private val logger = Logger("GeckoEngineSession")

    internal lateinit var geckoSession: GeckoSession
    internal var currentUrl: String? = null
    internal var currentTitle: String? = null
    internal var lastLoadRequestUri: String? = null
    internal var pageLoadingUrl: String? = null
    internal var appRedirectUrl: String? = null
    internal var scrollY: Int = 0

    // The Gecko site permissions for the loaded site.
    internal var geckoPermissions: List<ContentPermission> = emptyList()

    internal var job: Job = Job()
    private var canGoBack: Boolean = false
    private var canGoForward: Boolean = false

    /**
     * See [EngineSession.settings]
     */
    override val settings: Settings = object : Settings() {
        override var requestInterceptor: RequestInterceptor? = null
        override var historyTrackingDelegate: HistoryTrackingDelegate? = null
        override var downloadDelegate: DownloadDelegate? = null

        override var userAgentString: String?
            get() = geckoSession.settings.userAgentOverride
            set(value) {
                geckoSession.settings.userAgentOverride = value
            }
        override var suspendMediaWhenInactive: Boolean
            get() = geckoSession.settings.suspendMediaWhenInactive
            set(value) {
                geckoSession.settings.suspendMediaWhenInactive = value
            }
    }

    internal var initialLoad = true

    override val coroutineContext: CoroutineContext
        get() = context + job

    init {
        createGeckoSession(shouldOpen = openGeckoSession)
    }

    /**
     * Represents a request to load a [url].
     *
     * @param url the url to load.
     * @param parent the parent (referring) [EngineSession] i.e. the session that
     * triggered creating this one.
     * @param flags the [LoadUrlFlags] to use when loading the provided url.
     * @param additionalHeaders the extra headers to use when loading the provided url.
     **/
    data class LoadRequest(
        val url: String,
        val parent: EngineSession?,
        val flags: LoadUrlFlags,
        val additionalHeaders: Map<String, String>?,
    )

    @VisibleForTesting
    internal var initialLoadRequest: LoadRequest? = null

    /**
     * See [EngineSession.loadUrl]
     */
    override fun loadUrl(
        url: String,
        parent: EngineSession?,
        flags: LoadUrlFlags,
        additionalHeaders: Map<String, String>?,
        originalInput: String?,
        textDirectiveUserActivation: Boolean,
    ) {
        notifyObservers { onLoadUrl() }

        val scheme = url.toUri().normalizeScheme().scheme
        if (BLOCKED_SCHEMES.contains(scheme) && !shouldLoadJSSchemes(scheme, flags)) {
            logger.error("URL scheme not allowed. Aborting load.")
            return
        }

        if (initialLoad) {
            initialLoadRequest = LoadRequest(url, parent, flags, additionalHeaders)
        }

        val loader = GeckoSession.Loader()
            .uri(url)
            .flags(flags.getGeckoFlags())
            .originalInput(originalInput)
            .textDirectiveUserActivation(textDirectiveUserActivation)
            .appLinkLaunchType(flags.toGeckoLaunchType())

        if (additionalHeaders != null) {
            val headerFilter = if (flags.contains(ALLOW_ADDITIONAL_HEADERS)) {
                GeckoSession.HEADER_FILTER_UNRESTRICTED_UNSAFE
            } else {
                GeckoSession.HEADER_FILTER_CORS_SAFELISTED
            }
            loader.additionalHeaders(additionalHeaders)
                .headerFilter(headerFilter)
        }

        if (parent != null) {
            loader.referrer((parent as GeckoEngineSession).geckoSession)
        }

        geckoSession.load(loader)
        Fact(
            Component.BROWSER_ENGINE_GECKO,
            Action.IMPLEMENTATION_DETAIL,
            "GeckoSession.load",
        ).collect()
    }

    private fun shouldLoadJSSchemes(
        scheme: String?,
        flags: LoadUrlFlags,
    ) = scheme?.startsWith(JS_SCHEME) == true && flags.contains(ALLOW_JAVASCRIPT_URL)

    /**
     * See [EngineSession.loadData]
     */
    override fun loadData(data: String, mimeType: String, encoding: String) {
        when (encoding) {
            "base64" -> geckoSession.load(GeckoSession.Loader().data(data.toByteArray(), mimeType))
            else -> geckoSession.load(GeckoSession.Loader().data(data, mimeType))
        }
        notifyObservers { onLoadData() }
    }

    /**
     * See [EngineSession.requestPdfToDownload]
     */
    override fun requestPdfToDownload() {
        geckoSession.saveAsPdf().then(
            { inputStream ->
                if (inputStream == null) {
                    logger.error("No input stream available for Save to PDF.")
                    return@then GeckoResult<Void>()
                }

                val url = this.currentUrl ?: ""
                val contentType = "application/pdf"
                val disposition = currentTitle?.let { makePdfContentDisposition(it) }
                // A successful status code suffices because the PDF is generated on device.
                val responseStatus = RESPONSE_CODE_SUCCESS
                // We do not know the size at this point; send 0 so consumers do not display it.
                val contentLength = 0L
                // NB: If the title is an empty string, there is a chance the PDF will not have a name.
                // See https://github.com/mozilla-mobile/android-components/issues/12276
                val fileName = settings.downloadDelegate?.guessFileName(
                    contentDisposition = disposition,
                    url = url,
                    mimeType = contentType,
                )

                val response = Response(
                    url = url,
                    status = responseStatus,
                    headers = MutableHeaders(),
                    body = Response.Body(inputStream),
                )

                notifyObservers {
                    onExternalResource(
                        url = url,
                        contentLength = contentLength,
                        contentType = contentType,
                        fileName = fileName,
                        response = response,
                        isPrivate = privateMode,
                    )
                }

                notifyObservers {
                    onSaveToPdfComplete()
                }

                GeckoResult()
            },
            { throwable ->
                // Log the error. There is nothing we can do otherwise.
                logger.error("Save to PDF failed.", throwable)
                notifyObservers {
                    onSaveToPdfException(throwable)
                }
                GeckoResult()
            },
        )
    }

    /**
     * See [EngineSession.requestPrintContent]
     */
    override fun requestPrintContent() {
        geckoSession.didPrintPageContent().then(
            { finishedPrinting ->
                if (finishedPrinting == true) {
                    notifyObservers {
                        onPrintFinish()
                    }
                }
                GeckoResult<Void>()
            },
            { throwable ->
                logger.error("Printing failed.", throwable)
                notifyObservers {
                    onPrintException(true, throwable)
                }
                GeckoResult()
            },
        )
    }

    /**
     * See [EngineSession.stopLoading]
     */
    override fun stopLoading() {
        geckoSession.stop()
    }

    /**
     * See [EngineSession.reload]
     */
    override fun reload(flags: LoadUrlFlags) {
        initialLoadRequest?.let {
            // We have a pending initial load request, which means we never
            // successfully loaded a page. Calling reload now would just reload
            // about:blank. To prevent that we trigger the initial load again.
            loadUrl(it.url, it.parent, it.flags, it.additionalHeaders)
        } ?: geckoSession.reload(flags.getGeckoFlags())
    }

    /**
     * See [EngineSession.goBack]
     */
    override fun goBack(userInteraction: Boolean) {
        geckoSession.goBack(userInteraction)
        if (canGoBack) {
            notifyObservers { onNavigateBack() }
        }
    }

    /**
     * See [EngineSession.goForward]
     */
    override fun goForward(userInteraction: Boolean) {
        geckoSession.goForward(userInteraction)
        if (canGoForward) {
            notifyObservers { onNavigateForward() }
        }
    }

    /**
     * See [EngineSession.goToHistoryIndex]
     */
    override fun goToHistoryIndex(index: Int) {
        geckoSession.gotoHistoryIndex(index)
        notifyObservers { onGotoHistoryIndex() }
    }

    /**
     * See [EngineSession.restoreState]
     */
    override fun restoreState(state: EngineSessionState): Boolean {
        if (state !is GeckoEngineSessionState) {
            throw IllegalStateException("Can only restore from GeckoEngineSessionState")
        }
        // Also checking if SessionState is empty as a workaround for:
        // https://bugzilla.mozilla.org/show_bug.cgi?id=1687523
        if (state.actualState.isNullOrEmpty()) {
            return false
        }

        geckoSession.restoreState(state.actualState)
        return true
    }

    /**
     * See [EngineSession.flushSessionState]
     */
    override fun flushSessionState() {
        geckoSession.flushSessionState()
    }

    /**
     * See [EngineSession.updateTrackingProtection]
     */
    override fun updateTrackingProtection(policy: TrackingProtectionPolicy) {
        updateContentBlocking(policy)
        val enabled = policy != TrackingProtectionPolicy.none()
        etpEnabled = enabled
        notifyObservers {
            onTrackerBlockingEnabledChange(this, enabled)
        }
    }

    @VisibleForTesting
    internal fun updateContentBlocking(policy: TrackingProtectionPolicy) {
        /**
         * As described on https://bugzilla.mozilla.org/show_bug.cgi?id=1579264,useTrackingProtection
         * is a misleading setting. When is set to true is blocking content (scripts/sub-resources).
         * Instead of just turn on/off tracking protection. Until, this issue is fixed consumers need
         * a way to indicate, if they want to block content or not, this is why we use
         * [TrackingProtectionPolicy.TrackingCategory.SCRIPTS_AND_SUB_RESOURCES].
         */
        val shouldBlockContent =
            policy.contains(TrackingProtectionPolicy.TrackingCategory.SCRIPTS_AND_SUB_RESOURCES)

        val enabledInBrowsingMode = if (privateMode) {
            policy.useForPrivateSessions
        } else {
            policy.useForRegularSessions
        }
        geckoSession.settings.useTrackingProtection = enabledInBrowsingMode && shouldBlockContent
    }

    // This is a temporary solution to address
    // https://github.com/mozilla-mobile/android-components/issues/8431
    // until we eventually delete [EngineObserver] then this will not be needed.
    @VisibleForTesting
    internal var etpEnabled: Boolean? = null

    override fun register(observer: Observer) {
        super.register(observer)
        etpEnabled?.let { enabled ->
            onTrackerBlockingEnabledChange(observer, enabled)
        }
    }

    private fun onTrackerBlockingEnabledChange(observer: Observer, enabled: Boolean) {
        // We now register engine observers in a middleware using a dedicated
        // store thread. Since this notification can be delayed until an observer
        // is registered we switch to the main scope to make sure we're not notifying
        // on the store thread.
        MainScope().launch {
            observer.onTrackerBlockingEnabledChange(enabled)
        }
    }

    /**
     * Indicates if this [EngineSession] should be ignored the tracking protection policies.
     * @return if this [EngineSession] is in
     * the exception list, true if it is in, otherwise false.
     */
    internal fun isIgnoredForTrackingProtection(): Boolean {
        return geckoPermissions.any { it.isExcludedForTrackingProtection }
    }

    /**
     * See [EngineSession.settings]
     */
    override fun toggleDesktopMode(enable: Boolean, reload: Boolean) {
        val currentMode = geckoSession.settings.userAgentMode
        val currentViewPortMode = geckoSession.settings.viewportMode
        var overrideUrl: String? = null

        val newMode = if (enable) {
            GeckoSessionSettings.USER_AGENT_MODE_DESKTOP
        } else {
            GeckoSessionSettings.USER_AGENT_MODE_MOBILE
        }

        val newViewportMode = if (enable) {
            overrideUrl = currentUrl?.let { checkForMobileSite(it) }
            if (overrideUrl == null && pageLoadingUrl != currentUrl) {
                overrideUrl = pageLoadingUrl
            }
            GeckoSessionSettings.VIEWPORT_MODE_DESKTOP
        } else {
            GeckoSessionSettings.VIEWPORT_MODE_MOBILE
        }

        if (newMode != currentMode || newViewportMode != currentViewPortMode) {
            geckoSession.settings.userAgentMode = newMode
            geckoSession.settings.viewportMode = newViewportMode
            notifyObservers { onDesktopModeChange(enable) }
        }

        if (reload) {
            if (overrideUrl == null) {
                this.reload()
            } else {
                loadUrl(overrideUrl, flags = LoadUrlFlags.select(LoadUrlFlags.LOAD_FLAGS_REPLACE_HISTORY))
            }
        }
    }

    /**
     * See [EngineSession.hasCookieBannerRuleForSession]
     */
    override fun hasCookieBannerRuleForSession(
        onResult: (Boolean) -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        geckoSession.hasCookieBannerRuleForBrowsingContextTree().then(
            { response ->
                if (response == null) {
                    logger.error(
                        "Invalid value: unable to get response from hasCookieBannerRuleForBrowsingContextTree.",
                    )
                    onException(
                        java.lang.IllegalStateException(
                            "Invalid value: unable to get response from hasCookieBannerRuleForBrowsingContextTree.",
                        ),
                    )
                    return@then GeckoResult()
                }
                onResult(response)
                GeckoResult<Boolean>()
            },
            { throwable ->
                logger.error("Checking for cookie banner rule failed.", throwable)
                onException(throwable)
                GeckoResult()
            },
        )
    }

    /**
     * Checks and returns a non-mobile version of the url.
     */
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun checkForMobileSite(url: String): String? {
        var overrideUrl: String? = null
        val mPrefix = "m."
        val mobilePrefix = "mobile."

        val uri = url.toUri()
        val authority = uri.authority?.lowercase(Locale.ROOT) ?: return null

        val foundPrefix = when {
            authority.startsWith(mPrefix) -> mPrefix
            authority.startsWith(mobilePrefix) -> mobilePrefix
            else -> null
        }

        foundPrefix?.let {
            val mobileUri = url.toUri().buildUpon().authority(authority.substring(it.length))
            overrideUrl = mobileUri.toString()
        }

        return overrideUrl
    }

    /**
     * See [EngineSession.findAll]
     */
    override fun findAll(text: String) {
        notifyObservers { onFind(text) }
        geckoSession.finder.find(text, 0).then { result: GeckoSession.FinderResult? ->
            result?.let {
                val activeMatchOrdinal = if (it.current > 0) it.current - 1 else it.current
                notifyObservers { onFindResult(activeMatchOrdinal, it.total, true) }
            }
            GeckoResult<Void>()
        }
    }

    /**
     * See [EngineSession.findNext]
     */
    override fun findNext(forward: Boolean) {
        val findFlags = if (forward) GeckoSession.FINDER_FIND_FORWARD else GeckoSession.FINDER_FIND_BACKWARDS
        geckoSession.finder.find(null, findFlags).then { result: GeckoSession.FinderResult? ->
            result?.let {
                val activeMatchOrdinal = if (it.current > 0) it.current - 1 else it.current
                notifyObservers { onFindResult(activeMatchOrdinal, it.total, true) }
            }
            GeckoResult<Void>()
        }
    }

    /**
     * See [EngineSession.clearFindMatches]
     */
    override fun clearFindMatches() {
        geckoSession.finder.clear()
    }

    /**
     * See [EngineSession.exitFullScreenMode]
     */
    override fun exitFullScreenMode() {
        geckoSession.exitFullScreen()
    }

    /**
     * See [EngineSession.markActiveForWebExtensions].
     */
    override fun markActiveForWebExtensions(active: Boolean) {
        runtime.webExtensionController.setTabActive(geckoSession, active)
    }

    /**
     * See [EngineSession.updateSessionPriority].
     */
    override fun updateSessionPriority(priority: SessionPriority) {
        geckoSession.setPriorityHint(priority.id)
    }

    /**
     * See [EngineSession.setDisplayMode].
     */
    override fun setDisplayMode(displayMode: WebAppManifest.DisplayMode) {
        geckoSession.settings.displayMode = when (displayMode) {
            WebAppManifest.DisplayMode.MINIMAL_UI -> GeckoSessionSettings.DISPLAY_MODE_MINIMAL_UI
            WebAppManifest.DisplayMode.FULLSCREEN -> GeckoSessionSettings.DISPLAY_MODE_FULLSCREEN
            WebAppManifest.DisplayMode.STANDALONE -> GeckoSessionSettings.DISPLAY_MODE_STANDALONE
            else -> GeckoSessionSettings.DISPLAY_MODE_BROWSER
        }
    }

    /**
     * See [EngineSession.onPipModeChanged].
     */
    override fun onPipModeChanged(enabled: Boolean) {
        geckoSession.compositorController.onPipModeChanged(enabled)
    }

    /**
     * See [EngineSession.checkForFormData].
     */
    override fun checkForFormData(adjustPriority: Boolean) {
        geckoSession.containsFormData().then(
            { result ->
                if (result == null) {
                    logger.error("No result from GeckoView containsFormData.")
                    return@then GeckoResult<Boolean>()
                }
                notifyObservers { onCheckForFormData(result, adjustPriority) }
                GeckoResult<Boolean>()
            },
            { throwable ->
                notifyObservers {
                    onCheckForFormDataException(throwable)
                }
                GeckoResult<Boolean>()
            },
        )
    }

    /**
     * Checks if a PDF viewer is being used on the current page or not via GeckoView session.
     */
    override fun checkForPdfViewer(
        onResult: (Boolean) -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        geckoSession.isPdfJs.then(
            { response ->
                if (response == null) {
                    logger.error(
                        "Invalid value: No result from GeckoView if a PDF viewer is used.",
                    )
                    onException(
                        IllegalStateException(
                            "Invalid value: No result from GeckoView if a PDF viewer is used.",
                        ),
                    )
                    return@then GeckoResult()
                }
                onResult(response)
                GeckoResult<Boolean>()
            },
            { throwable ->
                logger.error("Checking for PDF viewer failed.", throwable)
                onException(throwable)
                GeckoResult()
            },
        )
    }

    /**
     * See [EngineSession.getWebCompatInfo].
     */
    override fun getWebCompatInfo(
        onResult: (JSONObject) -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        geckoSession.webCompatInfo.then(
            { result ->
                if (result == null) {
                    logger.error("No result from GeckoView getWebCompatInfo.")
                    return@then GeckoResult<JSONObject>()
                }
                onResult(result)
                GeckoResult()
            },
            { throwable ->
                logger.error("Getting web compat info failed.", throwable)
                onException(throwable)
                GeckoResult()
            },
        )
    }

    /**
     * See [EngineSession.sendMoreWebCompatInfo].
     */
    override fun sendMoreWebCompatInfo(
        info: JSONObject,
        onResult: () -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        geckoSession.sendMoreWebCompatInfo(info).then(
            {
                onResult()
                GeckoResult<Void>()
            },
            { throwable ->
                logger.error("Sending more web compat info failed.", throwable)
                onException(throwable)
                GeckoResult()
            },
        )
    }

    /**
     * See [EngineSession.requestTranslate]
     */
    override fun requestTranslate(
        fromLanguage: String,
        toLanguage: String,
        options: TranslationOptions?,
    ) {
        if (geckoSession.sessionTranslation == null) {
            notifyObservers {
                onTranslateException(
                    TranslationOperation.TRANSLATE,
                    TranslationError.MissingSessionCoordinator(),
                )
            }
            return
        }

        var geckoOptions: GeckoViewTranslateSession.TranslationOptions? = null
        if (options != null) {
            geckoOptions =
                GeckoViewTranslateSession.TranslationOptions.Builder()
                    .downloadModel(options.downloadModel).build()
        }

        geckoSession.sessionTranslation!!.translate(fromLanguage, toLanguage, geckoOptions).then({
            notifyObservers {
                onTranslateComplete(TranslationOperation.TRANSLATE)
            }
            GeckoResult<Void>()
        }, { throwable ->
            logger.error("Request for translation failed: ", throwable)
            notifyObservers {
                onTranslateException(
                    TranslationOperation.TRANSLATE,
                    throwable.intoTranslationError(),
                )
            }
            GeckoResult()
        })
    }

    /**
     * See [EngineSession.requestTranslationRestore]
     */
    override fun requestTranslationRestore() {
        if (geckoSession.sessionTranslation == null) {
            notifyObservers {
                onTranslateException(
                    TranslationOperation.RESTORE,
                    TranslationError.MissingSessionCoordinator(),
                )
            }
            return
        }

        geckoSession.sessionTranslation!!.restoreOriginalPage().then({
            notifyObservers {
                onTranslateComplete(TranslationOperation.RESTORE)
            }
            GeckoResult<Void>()
        }, { throwable ->
            logger.error("Request for translation failed: ", throwable)
            notifyObservers {
                onTranslateException(TranslationOperation.RESTORE, throwable.intoTranslationError())
            }
            GeckoResult()
        })
    }

    /**
     * See [EngineSession.getNeverTranslateSiteSetting]
     */
    override fun getNeverTranslateSiteSetting(
        onResult: (Boolean) -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        if (geckoSession.sessionTranslation == null) {
            onException(TranslationError.MissingSessionCoordinator())
            return
        }

        geckoSession.sessionTranslation!!.neverTranslateSiteSetting.then({ response ->
            if (response == null) {
                logger.error("Did not receive a site setting response.")
                onException(
                    TranslationError.UnexpectedNull(),
                )
                return@then GeckoResult()
            }
            onResult(response)
            GeckoResult<Boolean>()
        }, { throwable ->
            logger.error("Request for site translation preference failed: ", throwable)
            onException(throwable.intoTranslationError())
            GeckoResult()
        })
    }

    /**
     * See [EngineSession.setNeverTranslateSiteSetting]
     */
    override fun setNeverTranslateSiteSetting(
        setting: Boolean,
        onResult: () -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        if (geckoSession.sessionTranslation == null) {
            onException(TranslationError.MissingSessionCoordinator())
            return
        }

        geckoSession.sessionTranslation!!.setNeverTranslateSiteSetting(setting).then({
            onResult()
            GeckoResult<Boolean>()
        }, { throwable ->
            logger.error("Request for setting site translation preference failed: ", throwable)
            onException(throwable.intoTranslationError())
            GeckoResult()
        })
    }

    /**
     * See [EngineSession.getPageContent]
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun getPageContent(
        options: ContentParams,
        onResult: (String) -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        val geckoViewOptions = PageExtractionController.ContentParams(
            options.removeBoilerplate,
        )
        geckoSession.sessionPageExtractor.getPageContent(geckoViewOptions)
            .then(
                { content ->
                    if (content == null) {
                        onException(PageExtractionError.UnexpectedNull())
                        return@then GeckoResult()
                    }
                    onResult(content)
                    GeckoResult<Unit>()
                },
                { error ->
                    onException(error.intoPageExtractionError())
                    GeckoResult()
                },
            )
    }

    /**
     * See [EngineSession.getPageMetadata]
     */
    @OptIn(ExperimentalGeckoViewApi::class)
    override fun getPageMetadata(
        onResult: (PageMetadata) -> Unit,
        onException: (Throwable) -> Unit,
    ) {
        geckoSession.sessionPageExtractor.pageMetadata
            .then(
                { metadata ->
                    if (metadata == null) {
                        onException(PageExtractionError.UnexpectedNull())
                        return@then GeckoResult()
                    }
                    onResult(
                        PageMetadata(
                            structuredDataTypes = metadata.structuredDataTypes.toList(),
                            wordCount = metadata.wordCount,
                            language = metadata.language,
                        ),
                    )
                    GeckoResult<Unit>()
                },
                { error ->
                    onException(error.intoPageExtractionError())
                    GeckoResult()
                },
            )
    }

    /**
     * Purges the history for the session (back and forward history).
     */
    override fun purgeHistory() {
        geckoSession.purgeHistory()
    }

    /**
     * See [EngineSession.close].
     */
    override fun close() {
        super.close()
        job.cancel()
        geckoSession.close()
    }

    override fun getBlockedSchemes(): List<String> {
        return BLOCKED_SCHEMES
    }

    /**
     * See [EngineSession.processBackPressed].
     */
    override fun processBackPressed(
          onResult: (Boolean) -> Unit,
    ) {
        geckoSession.processBackPressed().then(
            { response ->
                if (response == null) {
                    logger.error("Did not receive a back key pressed.")
                    onResult(false)
                    return@then GeckoResult<Void>()
                }
                onResult(response)
                GeckoResult()
            },
            { throwable ->
                onResult(false)
                GeckoResult()
            },
        )
    }

    /**
     * NavigationDelegate implementation for forwarding callbacks to observers of the session.
     */
    @Suppress("CognitiveComplexMethod")
    private fun createNavigationDelegate() = object : GeckoSession.NavigationDelegate {
        override fun onLocationChange(
            session: GeckoSession,
            url: String?,
            geckoPermissions: List<ContentPermission>,
            hasUserGesture: Boolean,
        ) {
            this@GeckoEngineSession.geckoPermissions = geckoPermissions
            if (url == null) {
                return // ¯\_(ツ)_/¯
            }

            // Ignore initial loads of about:blank, see:
            // https://github.com/mozilla-mobile/android-components/issues/403
            // https://github.com/mozilla-mobile/android-components/issues/6832
            if (initialLoad && url == ABOUT_BLANK) {
                return
            }

            // if it is an initial load then we can't go back. We should update the URL.
            if (!initialLoad) {
                appRedirectUrl?.let {
                    if (url == appRedirectUrl) {
                        goBack(false)
                        return
                    }
                }
            }

            if (hasUserGesture) {
                pageLoadingUrl = url
            }
            currentUrl = url
            initialLoad = false
            initialLoadRequest = null

            notifyObservers {
                onExcludedOnTrackingProtectionChange(isIgnoredForTrackingProtection())
            }
            // Re-set the status of cookie banner handling when the user navigates to another site.
            notifyObservers {
                onCookieBannerChange(CookieBannerHandlingStatus.NO_DETECTED)
            }
            // Reset the status of the translation state for the page
            notifyObservers { onTranslatePageChange() }
            notifyObservers { onLocationChange(url, hasUserGesture) }
        }

        override fun onLoadRequest(
            session: GeckoSession,
            request: NavigationDelegate.LoadRequest,
        ): GeckoResult<AllowOrDeny> {
            // The process switch involved when loading extension pages will
            // trigger an initial load of about:blank which we want to
            // avoid:
            // https://github.com/mozilla-mobile/android-components/issues/6832
            // https://github.com/mozilla-mobile/android-components/issues/403
            if (currentUrl?.isExtensionUrl() != request.uri.isExtensionUrl()) {
                initialLoad = true
            }

            return when {
                maybeInterceptRequest(request, false) != null ->
                    GeckoResult.deny()
                request.target == NavigationDelegate.TARGET_WINDOW_NEW ->
                    GeckoResult.allow()
                else -> {
                    notifyObservers {
                        onLoadRequest(
                            url = request.uri,
                            triggeredByRedirect = request.isRedirect,
                            triggeredByWebContent = request.hasUserGesture,
                        )
                    }

                    GeckoResult.allow()
                }
            }
        }

        override fun onSubframeLoadRequest(
            session: GeckoSession,
            request: NavigationDelegate.LoadRequest,
        ): GeckoResult<AllowOrDeny> {
            if (request.target == NavigationDelegate.TARGET_WINDOW_NEW) {
                return GeckoResult.allow()
            }

            return if (maybeInterceptRequest(request, true) != null) {
                GeckoResult.deny()
            } else {
                // Not notifying session observer because of performance concern and currently there
                // is no use case.
                GeckoResult.allow()
            }
        }

        override fun onCanGoForward(session: GeckoSession, canGoForward: Boolean) {
            notifyObservers { onNavigationStateChange(canGoForward = canGoForward) }
            this@GeckoEngineSession.canGoForward = canGoForward
        }

        override fun onCanGoBack(session: GeckoSession, canGoBack: Boolean) {
            notifyObservers { onNavigationStateChange(canGoBack = canGoBack) }
            this@GeckoEngineSession.canGoBack = canGoBack
        }

        override fun onNewSession(
            session: GeckoSession,
            uri: String,
        ): GeckoResult<GeckoSession> {
            val newEngineSession =
                GeckoEngineSession(
                    runtime = runtime,
                    privateMode = privateMode,
                    defaultSettings = defaultSettings,
                    openGeckoSession = false,
                )
            notifyObservers {
                onWindowRequest(GeckoWindowRequest(uri, newEngineSession))
            }
            return GeckoResult.fromValue(newEngineSession.geckoSession)
        }

        override fun onLoadError(
            session: GeckoSession,
            uri: String?,
            error: WebRequestError,
        ): GeckoResult<String> {
            val response = settings.requestInterceptor?.onErrorRequest(
                this@GeckoEngineSession,
                geckoErrorToErrorType(error.code),
                uri,
            )
            return GeckoResult.fromValue(response?.uri)
        }

        private fun maybeInterceptRequest(
            request: NavigationDelegate.LoadRequest,
            isSubframeRequest: Boolean,
        ): InterceptionResponse? {
            if (request.hasUserGesture) {
                lastLoadRequestUri = ""
            }

            val interceptor = settings.requestInterceptor
            val interceptionResponse = if (
                interceptor == null || (request.isDirectNavigation && !interceptor.interceptsAppInitiatedRequests())
            ) {
                null
            } else {
                val engineSession = this@GeckoEngineSession
                val isSameDomain =
                    engineSession.currentUrl?.tryGetHostFromUrl() == request.uri.tryGetHostFromUrl()
                interceptor.onLoadRequest(
                    engineSession,
                    request.uri,
                    lastLoadRequestUri,
                    request.hasUserGesture,
                    isSameDomain,
                    request.isRedirect,
                    request.isDirectNavigation,
                    isSubframeRequest,
                )?.apply {
                    when (this) {
                        is InterceptionResponse.AppIntent -> {
                            appRedirectUrl = lastLoadRequestUri
                            notifyObservers {
                                onLaunchIntentRequest(
                                    url = url,
                                    appIntent = appIntent,
                                    fallbackUrl = fallbackUrl,
                                    appName = appName,
                                )
                            }
                        }

                        is InterceptionResponse.Content -> loadData(data, mimeType, encoding)
                        is InterceptionResponse.Url -> loadUrl(
                            url = url,
                            flags = flags,
                            additionalHeaders = additionalHeaders,
                        )
                        else -> {
                            // no-op
                        }
                    }
                }
            }

            if (interceptionResponse !is InterceptionResponse.AppIntent) {
                appRedirectUrl = ""
            }

            lastLoadRequestUri = request.uri
            return interceptionResponse
        }
    }

    /**
     * ProgressDelegate implementation for forwarding callbacks to observers of the session.
     */
    private fun createProgressDelegate() = object : GeckoSession.ProgressDelegate {
        override fun onProgressChange(session: GeckoSession, progress: Int) {
            notifyObservers { onProgress(progress) }
        }

        override fun onSecurityChange(
            session: GeckoSession,
            securityInfo: GeckoSession.ProgressDelegate.SecurityInformation,
        ) {
            // Ignore initial load of about:blank (see https://github.com/mozilla-mobile/android-components/issues/403)
            if (initialLoad && securityInfo.origin?.startsWith(MOZ_NULL_PRINCIPAL) == true) {
                return
            }

            notifyObservers {
                onSecurityChange(
                    securityInfo.isSecure,
                    securityInfo.host,
                    CertificateUtils.issuerOrganization(securityInfo.certificate),
                    securityInfo.certificate,
                )
            }
        }

        override fun onPageStart(session: GeckoSession, url: String) {
            // This log statement is temporary and parsed by FNPRMS for performance measurements. It can be
            // removed once FNPRMS is replaced: https://github.com/mozilla-mobile/android-components/issues/8662
            fnprmsLogger.info("handleMessage GeckoView:PageStart uri=") // uri intentionally blank

            pageLoadingUrl = url

            // Ignore initial load of about:blank (see https://github.com/mozilla-mobile/android-components/issues/403)
            if (initialLoad && url == ABOUT_BLANK) {
                return
            }

            notifyObservers {
                onProgress(PROGRESS_START)
                onLoadingStateChange(true)
            }
        }

        override fun onPageStop(session: GeckoSession, success: Boolean) {
            // This log statement is temporary and parsed by FNPRMS for performance measurements. It can be
            // removed once FNPRMS is replaced: https://github.com/mozilla-mobile/android-components/issues/8662
            fnprmsLogger.info("handleMessage GeckoView:PageStop uri=null") // uri intentionally hard-coded to null
            // by the time we reach here, any new request will come from web content.
            // If it comes from the chrome, loadUrl(url) or loadData(string) will set it to
            // false.

            // Ignore initial load of about:blank (see https://github.com/mozilla-mobile/android-components/issues/403)
            if (initialLoad && pageLoadingUrl == ABOUT_BLANK) {
                return
            }

            notifyObservers {
                onProgress(PROGRESS_STOP)
                onLoadingStateChange(false)
            }
        }

        override fun onSessionStateChange(session: GeckoSession, sessionState: GeckoSession.SessionState) {
            notifyObservers {
                onStateUpdated(GeckoEngineSessionState(sessionState))
            }
        }
    }

    internal fun createHistoryDelegate() = object : GeckoSession.HistoryDelegate {
        @SuppressWarnings("ReturnCount")
        override fun onVisited(
            session: GeckoSession,
            url: String,
            lastVisitedURL: String?,
            flags: Int,
        ): GeckoResult<Boolean>? {
            // Don't track:
            // - private visits
            // - error pages
            // - non-top level visits (i.e. iframes).
            if (privateMode ||
                (flags and GeckoSession.HistoryDelegate.VISIT_TOP_LEVEL) == 0 ||
                (flags and GeckoSession.HistoryDelegate.VISIT_UNRECOVERABLE_ERROR) != 0
            ) {
                return GeckoResult.fromValue(false)
            }

            appRedirectUrl?.let {
                if (url == appRedirectUrl) {
                    return GeckoResult.fromValue(false)
                }
            }

            val delegate = settings.historyTrackingDelegate ?: return GeckoResult.fromValue(false)

            // Check if the delegate wants this type of url.
            if (!delegate.shouldStoreUri(url)) {
                return GeckoResult.fromValue(false)
            }

            val isReload = lastVisitedURL?.let { it == url } ?: false

            // Note the difference between `VISIT_REDIRECT_PERMANENT`,
            // `VISIT_REDIRECT_TEMPORARY`, `VISIT_REDIRECT_SOURCE`, and
            // `VISIT_REDIRECT_SOURCE_PERMANENT`.
            //
            // The former two indicate if the visited page is the *target*
            // of a redirect; that is, another page redirected to it.
            //
            // The latter two indicate if the visited page is the *source*
            // of a redirect: it's redirecting to another page, because the
            // server returned an HTTP 3xy status code.
            //
            // So, we mark the **source** redirects as actual redirects, while treating **target**
            // redirects as normal visits.
            val visitType = when {
                isReload -> VisitType.RELOAD
                flags and GeckoSession.HistoryDelegate.VISIT_REDIRECT_SOURCE_PERMANENT != 0 ->
                    VisitType.REDIRECT_PERMANENT
                flags and GeckoSession.HistoryDelegate.VISIT_REDIRECT_SOURCE != 0 ->
                    VisitType.REDIRECT_TEMPORARY
                else -> VisitType.LINK
            }
            val redirectSource = when {
                flags and GeckoSession.HistoryDelegate.VISIT_REDIRECT_SOURCE_PERMANENT != 0 ->
                    RedirectSource.PERMANENT
                flags and GeckoSession.HistoryDelegate.VISIT_REDIRECT_SOURCE != 0 ->
                    RedirectSource.TEMPORARY
                else -> null
            }

            return launchGeckoResult {
                delegate.onVisited(url, PageVisit(visitType, redirectSource))
                true
            }
        }

        override fun getVisited(
            session: GeckoSession,
            urls: Array<out String>,
        ): GeckoResult<BooleanArray>? {
            if (privateMode) {
                return GeckoResult.fromValue(null)
            }

            val delegate = settings.historyTrackingDelegate ?: return GeckoResult.fromValue(null)

            return launchGeckoResult {
                val visits = delegate.getVisited(urls.toList())
                visits.toBooleanArray()
            }
        }

        override fun onHistoryStateChange(
            session: GeckoSession,
            historyList: GeckoSession.HistoryDelegate.HistoryList,
        ) {
            val items = historyList.map {
                // title is sometimes null despite the @NotNull annotation
                // https://bugzilla.mozilla.org/show_bug.cgi?id=1660286
                val title: String? = it.title
                HistoryItem(
                    title = title ?: it.uri,
                    uri = it.uri,
                )
            }
            notifyObservers { onHistoryStateChanged(items, historyList.currentIndex) }
        }
    }

    @Suppress("NestedBlockDepth", "CognitiveComplexMethod")
    internal fun createContentDelegate() = object : GeckoSession.ContentDelegate {
        override fun onCookieBannerDetected(session: GeckoSession) {
            notifyObservers { onCookieBannerChange(CookieBannerHandlingStatus.DETECTED) }
        }

        override fun onCookieBannerHandled(session: GeckoSession) {
            notifyObservers { onCookieBannerChange(CookieBannerHandlingStatus.HANDLED) }
        }

        override fun onFirstComposite(session: GeckoSession) = Unit

        override fun onFirstContentfulPaint(session: GeckoSession) {
            notifyObservers { onFirstContentfulPaint() }
        }

        override fun onPaintStatusReset(session: GeckoSession) {
            notifyObservers { onPaintStatusReset() }
        }

        override fun onContextMenu(
            session: GeckoSession,
            screenX: Int,
            screenY: Int,
            element: GeckoSession.ContentDelegate.ContextElement,
        ) {
            val hitResult = handleLongClick(
                elementSrc = element.srcUri,
                elementType = element.type,
                uri = element.linkUri,
                title = element.title,
                linkText = element.linkText,
            )
            hitResult?.let {
                notifyObservers { onLongPress(it) }
            }
        }

        override fun onCrash(session: GeckoSession) {
            notifyObservers { onCrash() }
        }

        override fun onKill(session: GeckoSession) {
            notifyObservers {
                onProcessKilled()
            }
        }

        override fun onFullScreen(session: GeckoSession, fullScreen: Boolean) {
            notifyObservers { onFullScreenChange(fullScreen) }
        }

        override fun onExternalResponse(session: GeckoSession, webResponse: WebResponse) {
            with(webResponse) {
                val contentType = headers[CONTENT_TYPE]?.trim()
                val contentLength = headers[CONTENT_LENGTH]?.trim()?.toLongOrNull()
                val contentDisposition = headers[CONTENT_DISPOSITION]?.trim()
                val url = uri

                val fileName = settings.downloadDelegate?.guessFileName(
                    contentDisposition = contentDisposition,
                    url = url,
                    mimeType = contentType,
                )
                val response = webResponse.toResponse()
                notifyObservers {
                    onExternalResource(
                        url = url,
                        contentLength = contentLength,
                        contentType = DownloadUtils.sanitizeMimeType(contentType),
                        fileName = fileName?.sanitizeFileName(),
                        response = response,
                        isPrivate = privateMode,
                        openInApp = webResponse.requestExternalApp,
                        skipConfirmation = webResponse.skipConfirmation,
                    )
                }
            }
        }

        override fun onCloseRequest(session: GeckoSession) {
            notifyObservers {
                onWindowRequest(
                    GeckoWindowRequest(
                        engineSession = this@GeckoEngineSession,
                        type = WindowRequest.Type.CLOSE,
                    ),
                )
            }
        }

        override fun onTitleChange(session: GeckoSession, title: String?) {
            if (appRedirectUrl.isNullOrEmpty()) {
                if (!privateMode) {
                    currentUrl?.let { url ->
                        settings.historyTrackingDelegate?.let { delegate ->
                            if (delegate.shouldStoreUri(url)) {
                                // NB: There's no guarantee that the title change will be processed by the
                                // delegate before the session is closed (and the corresponding coroutine
                                // job is cancelled). Observers will always be notified of the title
                                // change though.
                                launch(coroutineContext) {
                                    delegate.onTitleChanged(url, title ?: "")
                                }
                            }
                        }
                    }
                }
                this@GeckoEngineSession.currentTitle = title
                notifyObservers { onTitleChange(title ?: "") }
            }
        }

        override fun onPreviewImage(session: GeckoSession, previewImageUrl: String) {
            if (!privateMode) {
                currentUrl?.let { url ->
                    settings.historyTrackingDelegate?.let { delegate ->
                        if (delegate.shouldStoreUri(url)) {
                            launch(coroutineContext) {
                                delegate.onPreviewImageChange(url, previewImageUrl)
                            }
                        }
                    }
                }
            }
            notifyObservers { onPreviewImageChange(previewImageUrl) }
        }

        override fun onFocusRequest(session: GeckoSession) = Unit

        override fun onWebAppManifest(session: GeckoSession, manifest: JSONObject) {
            val parsed = WebAppManifestParser().parse(manifest)
            if (parsed is WebAppManifestParser.Result.Success) {
                notifyObservers { onWebAppManifestLoaded(parsed.manifest) }
            }
        }

        override fun onMetaViewportFitChange(session: GeckoSession, viewportFit: String) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
                val layoutInDisplayCutoutMode = when (viewportFit) {
                    "cover" -> WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES
                    "contain" -> WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER
                    else -> WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_DEFAULT
                }

                notifyObservers { onMetaViewportFitChanged(layoutInDisplayCutoutMode) }
            }
        }

        override fun onShowDynamicToolbar(geckoSession: GeckoSession) {
            notifyObservers { onShowDynamicToolbar() }
        }
    }

    private fun createContentBlockingDelegate() = object : ContentBlocking.Delegate {
        override fun onContentBlocked(session: GeckoSession, event: ContentBlocking.BlockEvent) {
            notifyObservers {
                onTrackerBlocked(event.toTracker())
            }
        }

        override fun onContentLoaded(session: GeckoSession, event: ContentBlocking.BlockEvent) {
            notifyObservers {
                onTrackerLoaded(event.toTracker())
            }
        }
    }

    private fun ContentBlocking.BlockEvent.toTracker(): Tracker {
        val blockedContentCategories = mutableListOf<TrackingProtectionPolicy.TrackingCategory>()

        if (antiTrackingCategory.contains(ContentBlocking.AntiTracking.AD)) {
            blockedContentCategories.add(TrackingProtectionPolicy.TrackingCategory.AD)
        }

        if (antiTrackingCategory.contains(ContentBlocking.AntiTracking.ANALYTIC)) {
            blockedContentCategories.add(TrackingProtectionPolicy.TrackingCategory.ANALYTICS)
        }

        if (antiTrackingCategory.contains(ContentBlocking.AntiTracking.SOCIAL)) {
            blockedContentCategories.add(TrackingProtectionPolicy.TrackingCategory.SOCIAL)
        }

        if (antiTrackingCategory.contains(ContentBlocking.AntiTracking.FINGERPRINTING)) {
            blockedContentCategories.add(TrackingProtectionPolicy.TrackingCategory.FINGERPRINTING)
        }

        if (antiTrackingCategory.contains(ContentBlocking.AntiTracking.CRYPTOMINING)) {
            blockedContentCategories.add(TrackingProtectionPolicy.TrackingCategory.CRYPTOMINING)
        }

        if (antiTrackingCategory.contains(ContentBlocking.AntiTracking.CONTENT)) {
            blockedContentCategories.add(TrackingProtectionPolicy.TrackingCategory.CONTENT)
        }

        if (antiTrackingCategory.contains(ContentBlocking.AntiTracking.TEST)) {
            blockedContentCategories.add(TrackingProtectionPolicy.TrackingCategory.TEST)
        }

        return Tracker(
            url = uri,
            trackingCategories = blockedContentCategories,
            cookiePolicies = getCookiePolicies(),
        )
    }

    private fun ContentBlocking.BlockEvent.getCookiePolicies(): List<TrackingProtectionPolicy.CookiePolicy> {
        val cookiesPolicies = mutableListOf<TrackingProtectionPolicy.CookiePolicy>()

        if (cookieBehaviorCategory == ContentBlocking.CookieBehavior.ACCEPT_ALL) {
            cookiesPolicies.add(TrackingProtectionPolicy.CookiePolicy.ACCEPT_ALL)
        }

        if (cookieBehaviorCategory.contains(ContentBlocking.CookieBehavior.ACCEPT_FIRST_PARTY)) {
            cookiesPolicies.add(TrackingProtectionPolicy.CookiePolicy.ACCEPT_ONLY_FIRST_PARTY)
        }

        if (cookieBehaviorCategory.contains(ContentBlocking.CookieBehavior.ACCEPT_NONE)) {
            cookiesPolicies.add(TrackingProtectionPolicy.CookiePolicy.ACCEPT_NONE)
        }

        if (cookieBehaviorCategory.contains(ContentBlocking.CookieBehavior.ACCEPT_NON_TRACKERS)) {
            cookiesPolicies.add(TrackingProtectionPolicy.CookiePolicy.ACCEPT_NON_TRACKERS)
        }

        if (cookieBehaviorCategory.contains(ContentBlocking.CookieBehavior.ACCEPT_VISITED)) {
            cookiesPolicies.add(TrackingProtectionPolicy.CookiePolicy.ACCEPT_VISITED)
        }

        return cookiesPolicies
    }

    private operator fun Int.contains(mask: Int): Boolean {
        return (this and mask) != 0
    }

    private fun createPermissionDelegate() = object : GeckoSession.PermissionDelegate {
        override fun onContentPermissionRequest(
            session: GeckoSession,
            geckoContentPermission: ContentPermission,
        ): GeckoResult<Int> {
            val geckoResult = GeckoResult<Int>()
            val uri = geckoContentPermission.uri
            val type = geckoContentPermission.permission
            val request = GeckoPermissionRequest.Content(uri, type, geckoContentPermission, mutableListOf(geckoResult))
            notifyObservers { onContentPermissionRequest(request) }
            return geckoResult
        }

        override fun onMediaPermissionRequest(
            session: GeckoSession,
            uri: String,
            video: Array<out GeckoSession.PermissionDelegate.MediaSource>?,
            audio: Array<out GeckoSession.PermissionDelegate.MediaSource>?,
            callback: GeckoSession.PermissionDelegate.MediaCallback,
        ) {
            val request = GeckoPermissionRequest.Media(
                uri,
                video?.toList() ?: emptyList(),
                audio?.toList() ?: emptyList(),
                callback,
            )
            notifyObservers { onContentPermissionRequest(request) }
        }

        override fun onAndroidPermissionsRequest(
            session: GeckoSession,
            permissions: Array<out String>?,
            callback: GeckoSession.PermissionDelegate.Callback,
        ) {
            val request = GeckoPermissionRequest.App(
                permissions?.toList() ?: emptyList(),
                mutableListOf(callback),
            )
            notifyObservers { onAppPermissionRequest(request) }
        }
    }

    private fun createScrollDelegate() = object : GeckoSession.ScrollDelegate {
        override fun onScrollChanged(session: GeckoSession, scrollX: Int, scrollY: Int) {
            this@GeckoEngineSession.scrollY = scrollY
            notifyObservers { onScrollChange(scrollX, scrollY) }
        }
    }

    /**
     * Handles long click events.
     *
     * @param elementSrc The source of the element.
     * @param elementType The type of the element.
     * @param uri The (optional) URI of the element.
     * @param title The (optional) title of the element.
     * @param linkText The (optional) link text of the element.
     */
    fun handleLongClick(
        elementSrc: String?,
        elementType: Int,
        uri: String? = null,
        title: String? = null,
        linkText: String? = null,
    ): HitResult? {
        return when (elementType) {
            GeckoSession.ContentDelegate.ContextElement.TYPE_AUDIO ->
                elementSrc?.let {
                    HitResult.AUDIO(it, title)
                }
            GeckoSession.ContentDelegate.ContextElement.TYPE_VIDEO ->
                elementSrc?.let {
                    HitResult.VIDEO(it, title)
                }
            GeckoSession.ContentDelegate.ContextElement.TYPE_IMAGE -> {
                when {
                    elementSrc != null && uri != null ->
                        HitResult.IMAGE_SRC(elementSrc, uri)
                    elementSrc != null ->
                        HitResult.IMAGE(elementSrc, title)
                    else -> HitResult.UNKNOWN("")
                }
            }
            GeckoSession.ContentDelegate.ContextElement.TYPE_NONE -> {
                elementSrc?.let {
                    when {
                        it.isPhone() -> HitResult.PHONE(it)
                        it.isEmail() -> HitResult.EMAIL(it)
                        it.isGeoLocation() -> HitResult.GEO(it)
                        else -> HitResult.UNKNOWN(it)
                    }
                } ?: uri?.let {
                    HitResult.UNKNOWN(src = it, linkText = linkText)
                }
            }
            else -> HitResult.UNKNOWN("")
        }
    }

    override fun qwacStatus(onResult: (X509Certificate?) -> Unit) {
      geckoSession.qwacStatus().then({ qwac ->
        onResult(qwac)
        GeckoResult<Void>()
      })
    }

    private fun createGeckoSession(shouldOpen: Boolean = true) {
        this.geckoSession = geckoSessionProvider()

        defaultSettings?.trackingProtectionPolicy?.let { updateTrackingProtection(it) }
        defaultSettings?.requestInterceptor?.let { settings.requestInterceptor = it }
        defaultSettings?.historyTrackingDelegate?.let { settings.historyTrackingDelegate = it }
        defaultSettings?.downloadDelegate?.let { settings.downloadDelegate = it }
        defaultSettings?.testingModeEnabled?.let {
            geckoSession.settings.fullAccessibilityTree = it
        }
        defaultSettings?.userAgentString?.let { geckoSession.settings.userAgentOverride = it }
        defaultSettings?.suspendMediaWhenInactive?.let {
            geckoSession.settings.suspendMediaWhenInactive = it
        }
        defaultSettings?.clearColor?.let { geckoSession.compositorController.clearColor = it }

        if (shouldOpen) {
            runtime.warmUp()
            geckoSession.open(runtime)
        }

        geckoSession.navigationDelegate = createNavigationDelegate()
        geckoSession.progressDelegate = createProgressDelegate()
        geckoSession.contentDelegate = createContentDelegate()
        geckoSession.contentBlockingDelegate = createContentBlockingDelegate()
        geckoSession.permissionDelegate = createPermissionDelegate()
        geckoSession.promptDelegate = GeckoPromptDelegate(this)
        geckoSession.mediaDelegate = GeckoMediaDelegate(this)
        geckoSession.historyDelegate = createHistoryDelegate()
        geckoSession.mediaSessionDelegate = GeckoMediaSessionDelegate(this)
        geckoSession.scrollDelegate = createScrollDelegate()
        geckoSession.translationsSessionDelegate = GeckoTranslateSessionDelegate(this)
    }

    companion object {
        internal const val PROGRESS_START = 25
        internal const val PROGRESS_STOP = 100
        internal const val MOZ_NULL_PRINCIPAL = "moz-nullprincipal:"
        internal const val ABOUT_BLANK = "about:blank"
        internal const val JS_SCHEME = "javascript"
        internal val BLOCKED_SCHEMES =
            listOf("file", "resource", "fido", JS_SCHEME) // See 1684761 and 1684947

        /**
         * Provides an ErrorType corresponding to the error code provided.
         */
        internal fun geckoErrorToErrorType(errorCode: Int) =
            when (errorCode) {
                WebRequestError.ERROR_UNKNOWN -> ErrorType.UNKNOWN
                WebRequestError.ERROR_SECURITY_SSL -> ErrorType.ERROR_SECURITY_SSL
                WebRequestError.ERROR_SECURITY_BAD_CERT -> ErrorType.ERROR_SECURITY_BAD_CERT
                WebRequestError.ERROR_NET_INTERRUPT -> ErrorType.ERROR_NET_INTERRUPT
                WebRequestError.ERROR_NET_TIMEOUT -> ErrorType.ERROR_NET_TIMEOUT
                WebRequestError.ERROR_CONNECTION_REFUSED -> ErrorType.ERROR_CONNECTION_REFUSED
                WebRequestError.ERROR_UNKNOWN_SOCKET_TYPE -> ErrorType.ERROR_UNKNOWN_SOCKET_TYPE
                WebRequestError.ERROR_REDIRECT_LOOP -> ErrorType.ERROR_REDIRECT_LOOP
                WebRequestError.ERROR_OFFLINE -> ErrorType.ERROR_OFFLINE
                WebRequestError.ERROR_PORT_BLOCKED -> ErrorType.ERROR_PORT_BLOCKED
                WebRequestError.ERROR_NET_RESET -> ErrorType.ERROR_NET_RESET
                WebRequestError.ERROR_UNSAFE_CONTENT_TYPE -> ErrorType.ERROR_UNSAFE_CONTENT_TYPE
                WebRequestError.ERROR_CORRUPTED_CONTENT -> ErrorType.ERROR_CORRUPTED_CONTENT
                WebRequestError.ERROR_CONTENT_CRASHED -> ErrorType.ERROR_CONTENT_CRASHED
                WebRequestError.ERROR_INVALID_CONTENT_ENCODING -> ErrorType.ERROR_INVALID_CONTENT_ENCODING
                WebRequestError.ERROR_UNKNOWN_HOST -> ErrorType.ERROR_UNKNOWN_HOST
                WebRequestError.ERROR_MALFORMED_URI -> ErrorType.ERROR_MALFORMED_URI
                WebRequestError.ERROR_UNKNOWN_PROTOCOL -> ErrorType.ERROR_UNKNOWN_PROTOCOL
                WebRequestError.ERROR_FILE_NOT_FOUND -> ErrorType.ERROR_FILE_NOT_FOUND
                WebRequestError.ERROR_FILE_ACCESS_DENIED -> ErrorType.ERROR_FILE_ACCESS_DENIED
                WebRequestError.ERROR_PROXY_CONNECTION_REFUSED -> ErrorType.ERROR_PROXY_CONNECTION_REFUSED
                WebRequestError.ERROR_UNKNOWN_PROXY_HOST -> ErrorType.ERROR_UNKNOWN_PROXY_HOST
                WebRequestError.ERROR_SAFEBROWSING_MALWARE_URI -> ErrorType.ERROR_SAFEBROWSING_MALWARE_URI
                WebRequestError.ERROR_SAFEBROWSING_UNWANTED_URI -> ErrorType.ERROR_SAFEBROWSING_UNWANTED_URI
                WebRequestError.ERROR_SAFEBROWSING_HARMFUL_URI -> ErrorType.ERROR_SAFEBROWSING_HARMFUL_URI
                WebRequestError.ERROR_SAFEBROWSING_PHISHING_URI -> ErrorType.ERROR_SAFEBROWSING_PHISHING_URI
                WebRequestError.ERROR_HARMFULADDON_URI -> ErrorType.ERROR_HARMFULADDON_URI
                WebRequestError.ERROR_HTTPS_ONLY -> ErrorType.ERROR_HTTPS_ONLY
                WebRequestError.ERROR_BAD_HSTS_CERT -> ErrorType.ERROR_BAD_HSTS_CERT
                else -> ErrorType.UNKNOWN
            }
    }
}

/**
 * Provides all gecko app link intent launch types ignoring the types that only exists on AC.
 * Ensures AC app-link launch types map to GeckoView.
 **/
private fun EngineSession.LoadUrlFlags.toGeckoLaunchType(): Int {
    return when (getGeckoAppLinkLaunchType()) {
        EngineSession.LoadUrlFlags.APP_LINK_LAUNCH_TYPE_COLD ->
            APP_LINK_LAUNCH_TYPE_COLD

        EngineSession.LoadUrlFlags.APP_LINK_LAUNCH_TYPE_WARM ->
            APP_LINK_LAUNCH_TYPE_WARM

        EngineSession.LoadUrlFlags.APP_LINK_LAUNCH_TYPE_HOT ->
            APP_LINK_LAUNCH_TYPE_HOT

        else -> APP_LINK_LAUNCH_TYPE_UNKNOWN
    }
}

private fun EngineSession.LoadUrlFlags.getGeckoAppLinkLaunchType(): Int {
    val launchTypes = listOf(
        EngineSession.LoadUrlFlags.APP_LINK_LAUNCH_TYPE_COLD,
        EngineSession.LoadUrlFlags.APP_LINK_LAUNCH_TYPE_WARM,
        EngineSession.LoadUrlFlags.APP_LINK_LAUNCH_TYPE_HOT,
    )

    return launchTypes.firstOrNull { contains(it) }
        ?: EngineSession.LoadUrlFlags.APP_LINK_LAUNCH_TYPE_UNKNOWN
}

/**
 * Provides all gecko flags ignoring flags that only exists on AC.
 **/
@VisibleForTesting
internal fun EngineSession.LoadUrlFlags.getGeckoFlags(): Int {
    var newValue = value

    if (contains(ALLOW_ADDITIONAL_HEADERS)) {
        newValue -= ALLOW_ADDITIONAL_HEADERS
    }

    if (contains(ALLOW_JAVASCRIPT_URL)) {
        newValue -= ALLOW_JAVASCRIPT_URL
    }

    return newValue
}
