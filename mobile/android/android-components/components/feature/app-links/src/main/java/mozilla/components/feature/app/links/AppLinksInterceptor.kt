/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.app.links

import android.annotation.SuppressLint
import android.content.Context
import android.content.Intent
import android.os.SystemClock
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import androidx.fragment.app.FragmentManager
import mozilla.components.browser.state.selector.findTabOrCustomTab
import mozilla.components.browser.state.state.SessionState
import mozilla.components.browser.state.store.BrowserStore
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags.Companion.EXTERNAL
import mozilla.components.concept.engine.EngineSession.LoadUrlFlags.Companion.LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.feature.app.links.AppLinksUseCases.Companion.ALWAYS_DENY_SCHEMES
import mozilla.components.feature.app.links.AppLinksUseCases.Companion.ENGINE_SUPPORTED_SCHEMES
import mozilla.components.feature.app.links.RedirectDialogFragment.Companion.FRAGMENT_TAG
import mozilla.components.feature.session.SessionUseCases
import mozilla.components.support.ktx.android.content.appName
import mozilla.components.support.ktx.android.net.isHttpOrHttps
import mozilla.components.support.ktx.kotlin.tryGetHostFromUrl

private const val WWW = "www."
private const val M = "m."
private const val MOBILE = "mobile."
private const val MAPS = "maps."

// List of URL schemes that are allowed to open an external application in subframe.
private val ALLOWED_SCHEMES_IN_SUBFRAME: List<String> = listOf(
    "msteams", // Microsoft Teams
)

/**
 * This feature implements use cases for detecting and handling redirects to external apps. The user
 * is asked to confirm her intention before leaving the app. These include the Android Intents,
 * custom schemes and support for [Intent.CATEGORY_BROWSABLE] `http(s)` URLs.
 *
 * In the case of Android Intents that are not installed, and with no fallback, the user is prompted
 * to search the installed market place.
 *
 * It provides use cases to detect and open links openable in third party non-browser apps.
 *
 * It requires: a [Context].
 *
 * A [Boolean] flag is provided at construction to allow the feature and use cases to be landed without
 * adjoining UI. The UI will be activated in https://github.com/mozilla-mobile/android-components/issues/2974
 * and https://github.com/mozilla-mobile/android-components/issues/2975.
 *
 * @param context Context the feature is associated with.
 * @param interceptLinkClicks If {true} then intercept link clicks.
 * @param engineSupportedSchemes List of schemes that the engine supports.
 * @param alwaysDeniedSchemes List of schemes that will never be opened in a third-party app even if
 * [interceptLinkClicks] is `true`.
 * @param launchInApp If {true} then launch app links in third party app(s). Default to false because
 * of security concerns.
 * @param useCases These use cases allow for the detection of, and opening of links that other apps
 * have registered to open.
 * @param launchFromInterceptor If {true} then the interceptor will prompt and launch the link in
 * third-party apps if available.  Do not use this in conjunction with [AppLinksFeature]
 * @param showCheckbox if {true} then the checkbox will be visible on normal tabs within [SimpleRedirectDialogFragment]
 * @param store [BrowserStore] containing the information about the currently open tabs.
 * @param shouldPrompt If {true} then we should prompt the user before redirect.
 * @param failedToLaunchAction Action to perform when failing to launch in third party app.
 * @param checkboxCheckedAction Action to perform when checkbox is ticked and positive button is clicked
 * on redirect prompt.
 */
class AppLinksInterceptor(
    private val context: Context,
    private val interceptLinkClicks: Boolean = false,
    private val engineSupportedSchemes: Set<String> = ENGINE_SUPPORTED_SCHEMES,
    private val alwaysDeniedSchemes: Set<String> = ALWAYS_DENY_SCHEMES,
    private var launchInApp: () -> Boolean = { false },
    private val useCases: AppLinksUseCases = AppLinksUseCases(
        context,
        launchInApp,
        alwaysDeniedSchemes = alwaysDeniedSchemes,
    ),
    private val launchFromInterceptor: Boolean = false,
    private val showCheckbox: Boolean = false,
    private val store: BrowserStore? = null,
    private val shouldPrompt: () -> Boolean = { true },
    private val failedToLaunchAction: (fallbackUrl: String?) -> Unit = {},
    private val loadUrlUseCase: SessionUseCases.DefaultLoadUrlUseCase? = null,
    private val checkboxCheckedAction: () -> Unit = {},
) : RequestInterceptor {
    private var fragmentManager: FragmentManager? = null
    private val dialog: RedirectDialogFragment? = null

    /**
     * Update [FragmentManager] for this instance of AppLinksInterceptor
     * @param fragmentManager the new value of [FragmentManager]
     */
    fun updateFragmentManger(fragmentManager: FragmentManager?) {
        this.fragmentManager = fragmentManager
    }

    /**
     * Update launchInApp for this instance of AppLinksInterceptor
     * @param launchInApp the new value of launchInApp
     */
    fun updateLaunchInApp(launchInApp: () -> Boolean) {
        this.launchInApp = launchInApp
        useCases.updateLaunchInApp(launchInApp)
    }

    @Suppress("ComplexMethod", "ReturnCount")
    override fun onLoadRequest(
        engineSession: EngineSession,
        uri: String,
        lastUri: String?,
        hasUserGesture: Boolean,
        isSameDomain: Boolean,
        isRedirect: Boolean,
        isDirectNavigation: Boolean,
        isSubframeRequest: Boolean,
    ): RequestInterceptor.InterceptionResponse? {
        val encodedUri = uri.toUri()
        val uriScheme = encodedUri.scheme
        val engineSupportsScheme = engineSupportedSchemes.contains(uriScheme)
        val isAllowedRedirect = (isRedirect && !isSubframeRequest)
        val tabSessionState = store?.state?.findTabOrCustomTab(engineSession)

        val doNotIntercept = when {
            uriScheme == null -> true
            // A subframe request not triggered by the user and not in allow list should not go to
            // an external app.
            (!hasUserGesture && isSubframeRequest && !isSubframeAllowed(uriScheme)) -> true
            // If request not from an user gesture, allowed redirect and direct navigation
            // or if we're already on the site then let's not go to an external app.
            (
                (!hasUserGesture && !isAllowedRedirect && !isDirectNavigation) ||
                    isSameDomain(lastUri, uri)
                ) && engineSupportsScheme -> true
            // If scheme not in safelist then follow user preference
            (!interceptLinkClicks || !launchInApp()) && engineSupportsScheme -> true
            // Never go to an external app when scheme is in blocklist
            alwaysDeniedSchemes.contains(uriScheme) -> true
            else -> false
        }

        if (doNotIntercept) {
            return null
        }

        val tabId = tabSessionState?.id ?: ""
        val redirect = useCases.interceptedAppLinkRedirect(uri)
        val result = handleRedirect(redirect, uri, tabId)

        if (redirect.hasExternalApp()) {
            val packageName = redirect.appIntent?.component?.packageName

            if (
                lastApplinksPackageWithTimestamp.first == packageName && lastApplinksPackageWithTimestamp.second +
                APP_LINKS_DO_NOT_INTERCEPT_INTERVAL > SystemClock.elapsedRealtime()
            ) {
                return null
            }

            lastApplinksPackageWithTimestamp = Pair(packageName, SystemClock.elapsedRealtime())
        }

        if (redirect.isRedirect()) {
            if (
                launchFromInterceptor &&
                result is RequestInterceptor.InterceptionResponse.AppIntent
            ) {
                handleIntent(
                    sessionState = tabSessionState,
                    url = uri,
                    appIntent = redirect.appIntent,
                    fallbackUrl = redirect.fallbackUrl,
                    marketingIntent = redirect.marketplaceIntent,
                    appName = redirect.appName,
                )
                // We can avoid loading the page only if openInApp settings is set to Always
                return if (shouldPrompt()) null else result
            }

            return result
        }

        return null
    }

    @SuppressWarnings("ReturnCount")
    @SuppressLint("MissingPermission")
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun handleRedirect(
        redirect: AppLinkRedirect,
        uri: String,
        tabId: String,
    ): RequestInterceptor.InterceptionResponse? {
        if (!launchInApp()) {
            redirect.fallbackUrl?.let {
                return RequestInterceptor.InterceptionResponse.Url(it)
            }
        }

        if (inUserDoNotIntercept(uri, redirect.appIntent, tabId)) {
            redirect.fallbackUrl?.let {
                return RequestInterceptor.InterceptionResponse.Url(it)
            }

            return null
        }

        if (!redirect.hasExternalApp()) {
            redirect.marketplaceIntent?.let {
                return RequestInterceptor.InterceptionResponse.AppIntent(it, uri)
            }

            redirect.fallbackUrl?.let {
                return RequestInterceptor.InterceptionResponse.Url(it)
            }

            return null
        }

        redirect.appIntent?.let {
            return RequestInterceptor.InterceptionResponse.AppIntent(it, uri)
        }

        return null
    }

    // Determines if the transition between the two URLs is related.  If the two URLs
    // are from the same website then the app links interceptor will not try to find an application to open it.
    @VisibleForTesting
    internal fun isSameDomain(url1: String?, url2: String?): Boolean {
        return stripCommonSubDomains(url1?.tryGetHostFromUrl()) == stripCommonSubDomains(url2?.tryGetHostFromUrl())
    }

    // Remove subdomains that are ignored when determining if two URLs are from the same website.
    private fun stripCommonSubDomains(url: String?): String? {
        return when {
            url == null -> return null
            url.startsWith(WWW) -> url.replaceFirst(WWW, "")
            url.startsWith(M) -> url.replaceFirst(M, "")
            url.startsWith(MOBILE) -> url.replaceFirst(MOBILE, "")
            url.startsWith(MAPS) -> url.replaceFirst(MAPS, "")
            else -> url
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun handleIntent(
        sessionState: SessionState?,
        url: String,
        appIntent: Intent?,
        fallbackUrl: String?,
        marketingIntent: Intent?,
        appName: String,
    ) {
        var isAuthenticationFlow = false

        val targetIntent = when {
            appIntent != null -> {
                // Without fragment manager we are unable to prompt
                // Only non private tabs can be redirected to external app without prompt
                // Authentication flow should not prompt
                isAuthenticationFlow =
                    sessionState?.let { isAuthentication(sessionState, appIntent) } == true
                appIntent
            }
            marketingIntent != null -> marketingIntent
            else -> return
        }

        val fragmentManager = fragmentManager

        val isPrivate = sessionState?.content?.private == true
        val doNotOpenApp = {
            addUserDoNotIntercept(url, targetIntent, sessionState?.id)
            if (sessionState != null && fallbackUrl != null) {
                loadUrlIfSchemeSupported(sessionState, fallbackUrl)
            }
        }

        val doOpenApp = {
            useCases.openAppLink(
                targetIntent,
                failedToLaunchAction = failedToLaunchAction,
            )
        }

        val shouldShowPrompt = isPrivate || shouldPrompt()
        if (fragmentManager == null || !shouldShowPrompt || isAuthenticationFlow) {
            doOpenApp()
            return
        }

        if (isADialogAlreadyCreated()) {
            return
        }

        if (!fragmentManager.isStateSaved) {
            getOrCreateDialog(isPrivate, url, appName).apply {
                onConfirmRedirect = { isCheckboxTicked ->
                    if (isCheckboxTicked) {
                        checkboxCheckedAction()
                    }
                    doOpenApp()
                }
                onCancelRedirect = doNotOpenApp
            }.showNow(fragmentManager, FRAGMENT_TAG)
        }
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun getOrCreateDialog(
        isPrivate: Boolean,
        url: String,
        targetAppName: String,
    ): RedirectDialogFragment {
        if (dialog != null) {
            return dialog
        }

        val dialogTitle = when {
            isPrivate && targetAppName.isNotBlank() -> {
                context.getString(R.string.mozac_feature_applinks_confirm_dialog_title_with_app_name, targetAppName)
            }
            isPrivate -> {
                context.getString(R.string.mozac_feature_applinks_confirm_dialog_title)
            }
            targetAppName.isNotBlank() -> {
                context.getString(
                    R.string.mozac_feature_applinks_normal_confirm_dialog_title_with_app_name,
                    targetAppName,
                )
            }
            else -> {
                context.getString(R.string.mozac_feature_applinks_normal_confirm_dialog_title)
            }
        }

        val dialogMessage = if (isPrivate) {
            url
        } else {
            context.getString(
                R.string.mozac_feature_applinks_normal_confirm_dialog_message,
                context.appName,
            )
        }

        return SimpleRedirectDialogFragment.newInstance(
            dialogTitleString = dialogTitle,
            dialogMessageString = dialogMessage,
            showCheckbox = if (isPrivate) false else showCheckbox,
            maxSuccessiveDialogMillisLimit = MAX_SUCCESSIVE_DIALOG_MILLIS_LIMIT,
        )
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun loadUrlIfSchemeSupported(tab: SessionState, url: String) {
        val schemeSupported = engineSupportedSchemes.contains(url.toUri().scheme)
        if (schemeSupported) {
            loadUrlUseCase?.invoke(
                url = url,
                sessionId = tab.id,
                flags = EngineSession.LoadUrlFlags.select(EXTERNAL, LOAD_FLAGS_BYPASS_LOAD_URI_DELEGATE),
            )
        }
    }

    private fun isADialogAlreadyCreated(): Boolean {
        return fragmentManager?.findFragmentByTag(FRAGMENT_TAG) as? RedirectDialogFragment != null
    }

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun isAuthentication(sessionState: SessionState, appIntent: Intent): Boolean {
        return when (sessionState.source) {
            is SessionState.Source.External.ActionSend,
            is SessionState.Source.External.ActionSearch,
            -> false
            // CustomTab and ActionView can be used for authentication
            is SessionState.Source.External.CustomTab,
            is SessionState.Source.External.ActionView,
            -> {
                (sessionState.source as? SessionState.Source.External)?.let { externalSource ->
                    when (externalSource.caller?.packageId) {
                        null -> false
                        appIntent.component?.packageName -> true
                        else -> false
                    }
                } ?: false
            }
            else -> false
        }
    }

    companion object {
        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal var userDoNotInterceptCache: MutableMap<Int, Long> = mutableMapOf()

        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal var lastApplinksPackageWithTimestamp: Pair<String?, Long> = Pair(null, 0L)

        @VisibleForTesting
        internal fun getCacheKey(url: String, appIntent: Intent?, tabId: String?): Int? {
            return url.toUri().let { uri ->
                when {
                    appIntent?.component?.packageName != null -> appIntent.component?.packageName
                    !uri.isHttpOrHttps -> uri.scheme
                    else -> uri.host // worst case we do not prompt again on this host
                }?.let {
                    (it + (tabId.orEmpty())).hashCode() // do not open cache should only apply to this tab
                }
            }
        }

        @VisibleForTesting
        internal fun inUserDoNotIntercept(url: String, appIntent: Intent?, tabId: String?): Boolean {
            val cacheKey = getCacheKey(url, appIntent, tabId)
            val cacheTimeStamp = userDoNotInterceptCache[cacheKey]
            val currentTimeStamp = SystemClock.elapsedRealtime()

            return cacheTimeStamp != null &&
                currentTimeStamp <= (cacheTimeStamp + APP_LINKS_DO_NOT_OPEN_CACHE_INTERVAL)
        }

        internal fun addUserDoNotIntercept(url: String, appIntent: Intent?, tabId: String?) {
            val cacheKey = getCacheKey(url, appIntent, tabId)
            cacheKey?.let {
                userDoNotInterceptCache[it] = SystemClock.elapsedRealtime()
            }
        }

        @VisibleForTesting
        internal fun isSubframeAllowed(uriScheme: String): Boolean {
            return ALLOWED_SCHEMES_IN_SUBFRAME.contains(uriScheme)
        }

        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal const val APP_LINKS_DO_NOT_OPEN_CACHE_INTERVAL = 60 * 60 * 1000L // 1 hour

        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal const val APP_LINKS_DO_NOT_INTERCEPT_INTERVAL = 2000L // 2 second

        // Minimum time for dialog to settle before accepting user interactions.
        internal const val MAX_SUCCESSIVE_DIALOG_MILLIS_LIMIT: Int = 500 // 0.5 seconds
    }
}
