/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.app.links

import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.pm.PackageManager
import android.content.pm.ResolveInfo
import android.os.SystemClock
import android.provider.Browser.EXTRA_APPLICATION_ID
import androidx.annotation.VisibleForTesting
import androidx.core.net.toUri
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.ktx.android.content.pm.isPackageInstalled
import mozilla.components.support.ktx.android.net.isHttpOrHttps
import mozilla.components.support.utils.Browsers
import mozilla.components.support.utils.BrowsersCache
import mozilla.components.support.utils.ext.queryIntentActivitiesCompat
import mozilla.components.support.utils.ext.resolveActivityCompat
import java.net.URISyntaxException

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal const val EXTRA_BROWSER_FALLBACK_URL = "browser_fallback_url"
internal const val PARAMS_ANDROID_FALLBACK_LINK = "afl" // From https://firebase.google.com/docs/dynamic-links/create-manually
internal const val PARAMS_FALLBACK_LINK = "link"
private const val MARKET_INTENT_URI_PACKAGE_PREFIX = "market://details?id="

@VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
internal const val APP_LINKS_CACHE_INTERVAL = 30 * 1000L // 30 seconds
private const val ANDROID_RESOLVER_PACKAGE_NAME = "android"

/**
 * These use cases allow for the detection of, and opening of links that other apps have registered
 * an [IntentFilter]s to open.
 *
 * Care is taken to:
 *  * resolve [intent://] links, including [S.browser_fallback_url]
 *  * provide a fallback to the installed marketplace app (e.g. on Google Android, the Play Store).
 *  * open HTTP(S) links with an external app.
 *
 * Since browsers are able to open HTTPS pages, existing browser apps are excluded from the list of
 * apps that trigger a redirect to an external app.
 *
 * @param context Context the feature is associated with.
 * @param launchInApp If {true} then launch app links in third party app(s). Default to false because
 * of security concerns.
 * @param alwaysDeniedSchemes List of schemes that will never be opened in a third-party app.
 * @param installedBrowsers List of all installed browsers on the device.
 */
class AppLinksUseCases(
    private val context: Context,
    private var launchInApp: () -> Boolean = { false },
    private val alwaysDeniedSchemes: Set<String> = ALWAYS_DENY_SCHEMES,
    private val installedBrowsers: Browsers = BrowsersCache.all(context),
) {
    @Suppress(
        "QueryPermissionsNeeded", // We expect our browsers to have the QUERY_ALL_PACKAGES permission
        "TooGenericExceptionCaught",
    )
    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal fun findActivities(intent: Intent): List<ResolveInfo> {
        return try {
            context.packageManager
                .queryIntentActivitiesCompat(intent, PackageManager.GET_RESOLVED_FILTER)
        } catch (e: RuntimeException) {
            Logger("AppLinksUseCases").error("failed to query activities", e)
            emptyList()
        }
    }

    /**
     * Update launchInApp for this instance of AppLinksUseCases
     * @param launchInApp the new value of launchInApp
     */
    fun updateLaunchInApp(launchInApp: () -> Boolean) {
        this.launchInApp = launchInApp
    }

    private fun findDefaultActivity(intent: Intent): ResolveInfo? {
        return context.packageManager.resolveActivityCompat(intent, PackageManager.MATCH_DEFAULT_ONLY)
    }

    /**
     * Parse a URL and check if it can be handled by an app elsewhere on the Android device.
     * If that app is not available, then a market place intent is also provided.
     *
     * It will also provide a fallback.
     *
     * @param includeHttpAppLinks If {true} then test URLs that start with {http} and {https}.
     * @param includeInstallAppFallback If {true} then offer an app-link to the installed market app
     * if no web fallback is available.
     */
    @Suppress("ComplexMethod")
    inner class GetAppLinkRedirect internal constructor(
        private val includeHttpAppLinks: Boolean = false,
        private val includeInstallAppFallback: Boolean = false,
    ) {
        operator fun invoke(url: String): AppLinkRedirect {
            val urlHash = (url + includeHttpAppLinks + includeHttpAppLinks).hashCode()
            val currentTimeStamp = SystemClock.elapsedRealtime()
            // since redirectCache is mutable, get the latest
            val cache = redirectCache
            if (cache != null && urlHash == cache.cachedUrlHash &&
                currentTimeStamp <= cache.cacheTimeStamp + APP_LINKS_CACHE_INTERVAL
            ) {
                return cache.cachedAppLinkRedirect
            }

            val redirectData = createBrowsableIntents(url)
            val isAppIntentHttpOrHttps = redirectData.appIntent?.data?.isHttpOrHttps ?: false
            val isEngineSupportedScheme = ENGINE_SUPPORTED_SCHEMES.contains(url.toUri().scheme)
            val isBrowserRedirect = redirectData.resolveInfo?.activityInfo?.packageName?.let { packageName ->
                installedBrowsers.isInstalled(packageName)
            } ?: false

            val appName = redirectData.resolveInfo?.let { resolveInfo ->
                getAppNameFromResolveInfo(context, resolveInfo)
            } ?: ""

            // Only set fallback URL if url is not a Google PlayStore URL
            // The reason here is we already handled that case with the market place URL
            val fallbackUrl = redirectData.fallbackUrl?.takeIf {
                !isPlayStoreURL(it) || redirectData.resolveInfo == null
            }?.toString()

            val appIntent = when {
                redirectData.resolveInfo == null -> null
                isBrowserRedirect && isEngineSupportedScheme -> null
                includeHttpAppLinks && isAppIntentHttpOrHttps -> redirectData.appIntent
                !launchInApp() && (isEngineSupportedScheme || fallbackUrl != null) -> null
                else -> redirectData.appIntent
            }

            // no need to check marketplace intent since it is only set if a package is set in the intent
            val appLinkRedirect = AppLinkRedirect(
                appIntent = appIntent,
                appName = appName,
                fallbackUrl = fallbackUrl,
                marketplaceIntent = redirectData.marketplaceIntent,
            )

            redirectCache = AppLinkRedirectCache(currentTimeStamp, urlHash, appLinkRedirect)
            return appLinkRedirect
        }

        private fun getAppNameFromResolveInfo(context: Context, resolveInfo: ResolveInfo): String {
            val packageManager: PackageManager = context.packageManager
            val applicationInfo = resolveInfo.activityInfo.applicationInfo
            return packageManager.getApplicationLabel(applicationInfo).toString()
        }

        private fun createBrowsableIntents(url: String): RedirectData {
            val intent = safeParseUri(url, Intent.URI_INTENT_SCHEME)

            val marketplaceIntent = intent?.`package`?.let {
                if (includeInstallAppFallback &&
                    !context.packageManager.isPackageInstalled(it)
                ) {
                    safeParseUri(MARKET_INTENT_URI_PACKAGE_PREFIX + it, 0)
                } else {
                    null
                }
            }

            if (marketplaceIntent != null) {
                marketplaceIntent.flags = Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK
            }

            val appIntent = when {
                intent?.data == null -> null
                alwaysDeniedSchemes.contains(intent.data?.scheme) -> null
                else -> intent
            }

            appIntent?.let {
                it.addCategory(Intent.CATEGORY_BROWSABLE)
                it.component = null
                it.flags = Intent.FLAG_ACTIVITY_NEW_TASK
                it.selector?.addCategory(Intent.CATEGORY_BROWSABLE)
                it.selector?.component = null
                it.putExtra(EXTRA_APPLICATION_ID, context.packageName)
            }

            val resolveInfo = appIntent?.let {
                findDefaultActivity(it)
            }?.let { resolveInfo ->
                when (resolveInfo.activityInfo?.packageName) {
                    // don't self target when it is an app link
                    context.packageName -> null
                    // no default app found but Android resolver shows there are multiple applications
                    // that can open this app link
                    ANDROID_RESOLVER_PACKAGE_NAME, null -> {
                        findActivities(appIntent).firstOrNull {
                            it.filter != null
                        }
                    }
                    // use default app
                    else -> {
                        appIntent.component =
                            ComponentName(resolveInfo.activityInfo.packageName, resolveInfo.activityInfo.name)
                        resolveInfo
                    }
                }
            }

            return RedirectData(
                appIntent = appIntent,
                fallbackUrl = url.getHierarchicalUrl(),
                marketplaceIntent = marketplaceIntent,
                resolveInfo = resolveInfo,
            )
        }

        private fun String.getHierarchicalUrl(): String? {
            val fallbackUrlFromUrlParams = this.toUri()
                .takeIf { it.isHierarchical }
                ?.let { uriWithParams ->
                    uriWithParams.getQueryParameter(PARAMS_ANDROID_FALLBACK_LINK)
                        ?: uriWithParams.getQueryParameter(PARAMS_FALLBACK_LINK)
                }

            return fallbackUrlFromUrlParams ?: safeParseUri(this, Intent.URI_INTENT_SCHEME)
                ?.getStringExtra(EXTRA_BROWSER_FALLBACK_URL)
        }

        private fun isPlayStoreURL(url: String): Boolean {
            val playStoreUrlRegex = Regex("https?://play\\.google\\.com/store/.*")
            return url.matches(playStoreUrlRegex)
        }
    }

    /**
     * Open an external app with the redirect created by the [GetAppLinkRedirect].
     *
     * This does not do any additional UI other than the chooser that Android may provide the user.
     */
    @Suppress("TooGenericExceptionCaught")
    inner class OpenAppLinkRedirect internal constructor(
        private val context: Context,
    ) {
        /**
         * Tries to open an external app for the provided [appIntent]. Invokes [failedToLaunchAction]
         * in case an exception is thrown opening the app.
         *
         * @param appIntent the [Intent] to open the external app for.
         * @param launchInNewTask whether or not the app should be launched in a new task.
         * @param failedToLaunchAction callback invoked in case opening the external app fails.
         */
        operator fun invoke(
            appIntent: Intent?,
            launchInNewTask: Boolean = true,
            failedToLaunchAction: (fallbackUrl: String?) -> Unit = {},
        ) {
            appIntent?.let {
                try {
                    val scheme = appIntent.data?.scheme
                    if (scheme != null && alwaysDeniedSchemes.contains(scheme)) {
                        return
                    }

                    if (launchInNewTask) {
                        it.flags = it.flags or Intent.FLAG_ACTIVITY_NEW_TASK
                    }
                    context.startActivity(it)
                } catch (e: Exception) {
                    when (e) {
                        is ActivityNotFoundException, is SecurityException, is NullPointerException -> {
                            failedToLaunchAction(it.getStringExtra(EXTRA_BROWSER_FALLBACK_URL))
                            Logger.error("failed to start third party app activity", e)
                        }
                        else -> throw e
                    }
                }
            }
        }
    }

    @VisibleForTesting
    internal fun safeParseUri(uri: String, flags: Int): Intent? {
        return try {
            val intent = Intent.parseUri(uri, flags)
            if (context.packageName != null && context.packageName == intent?.`package`) {
                // Ignore intents that would open in the browser itself
                null
            } else {
                intent
            }
        } catch (e: URISyntaxException) {
            Logger.error("failed to parse URI", e)
            null
        } catch (e: NumberFormatException) {
            Logger.error("failed to parse URI", e)
            null
        }
    }

    val openAppLink: OpenAppLinkRedirect by lazy { OpenAppLinkRedirect(context) }
    val interceptedAppLinkRedirect: GetAppLinkRedirect by lazy {
        GetAppLinkRedirect(
            includeHttpAppLinks = false,
            includeInstallAppFallback = true,
        )
    }
    val appLinkRedirect: GetAppLinkRedirect by lazy {
        GetAppLinkRedirect(
            includeHttpAppLinks = true,
            includeInstallAppFallback = false,
        )
    }
    val appLinkRedirectIncludeInstall: GetAppLinkRedirect by lazy {
        GetAppLinkRedirect(
            includeHttpAppLinks = true,
            includeInstallAppFallback = true,
        )
    }
    private data class RedirectData(
        val appIntent: Intent? = null,
        val fallbackUrl: String? = null,
        val marketplaceIntent: Intent? = null,
        val resolveInfo: ResolveInfo? = null,
    )

    @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
    internal data class AppLinkRedirectCache(
        var cacheTimeStamp: Long,
        var cachedUrlHash: Int,
        var cachedAppLinkRedirect: AppLinkRedirect,
    )

    companion object {
        @VisibleForTesting(otherwise = VisibleForTesting.PRIVATE)
        internal var redirectCache: AppLinkRedirectCache? = null

        @VisibleForTesting
        internal fun clearRedirectCache() {
            redirectCache = null
        }

        // list of scheme from https://searchfox.org/mozilla-central/source/netwerk/build/components.conf
        internal val ENGINE_SUPPORTED_SCHEMES: Set<String> = setOf(
            "about", "data", "file", "ftp", "http",
            "https", "moz-extension", "moz-safe-about", "resource", "view-source", "ws", "wss", "blob",
        )

        internal val ALWAYS_DENY_SCHEMES: Set<String> =
            setOf("jar", "file", "javascript", "data", "about", "content", "fido")
    }
}
