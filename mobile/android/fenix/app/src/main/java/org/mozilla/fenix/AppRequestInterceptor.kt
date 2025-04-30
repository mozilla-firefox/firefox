/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix

import android.content.Context
import android.net.ConnectivityManager
import androidx.annotation.VisibleForTesting
import androidx.core.content.getSystemService
import androidx.navigation.NavController
import mozilla.components.browser.errorpages.ErrorPages
import mozilla.components.browser.errorpages.ErrorType
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.request.RequestInterceptor
import mozilla.components.support.utils.ext.isContentUrl
import org.mozilla.fenix.GleanMetrics.ErrorPage
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.isOnline
import java.lang.ref.WeakReference

class AppRequestInterceptor(
    private val context: Context,
) : RequestInterceptor {

    private var navController: WeakReference<NavController>? = null

    fun setNavigationController(navController: NavController) {
        this.navController = WeakReference(navController)
    }

    override fun interceptsAppInitiatedRequests() = true

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
        val services = context.components.services

        return services.appLinksInterceptor.onLoadRequest(
            engineSession,
            uri,
            lastUri,
            hasUserGesture,
            isSameDomain,
            isRedirect,
            isDirectNavigation,
            isSubframeRequest,
        )
    }

    override fun onErrorRequest(
        session: EngineSession,
        errorType: ErrorType,
        uri: String?,
    ): RequestInterceptor.ErrorResponse {
        val improvedErrorType = improveErrorType(errorType)
        val riskLevel = getRiskLevel(improvedErrorType)

        ErrorPage.visitedError.record(ErrorPage.VisitedErrorExtra(improvedErrorType.name))

        // Record additional telemetry for content URI not found
        if (uri?.isContentUrl() == true && improvedErrorType == ErrorType.ERROR_FILE_NOT_FOUND) {
            ErrorPage.visitedError.record(ErrorPage.VisitedErrorExtra(errorType = "ERROR_CONTENT_URI_NOT_FOUND"))
        }

        val errorPageUri = ErrorPages.createUrlEncodedErrorPage(
            context = context,
            errorType = improvedErrorType,
            uri = uri,
            htmlResource = riskLevel.htmlRes,
            titleOverride = { type -> getErrorPageTitle(context, type) },
            descriptionOverride = { type -> getErrorPageDescription(context, type) },
        )

        return RequestInterceptor.ErrorResponse(errorPageUri)
    }

    /**
     * Where possible, this will make the error type more accurate by including information not
     * available to AC.
     */
    private fun improveErrorType(errorType: ErrorType): ErrorType {
        // This is not an ideal solution. For context, see:
        // https://github.com/mozilla-mobile/android-components/pull/5068#issuecomment-558415367

        return when {
            errorType == ErrorType.ERROR_UNKNOWN_HOST && !isConnected() -> ErrorType.ERROR_NO_INTERNET
            errorType == ErrorType.ERROR_HTTPS_ONLY -> ErrorType.ERROR_HTTPS_ONLY
            else -> errorType
        }
    }

    /**
     * Checks for network availability.
     *
     * */
    @VisibleForTesting
    internal fun isConnected(): Boolean =
        context.getSystemService<ConnectivityManager>()!!.isOnline()

    private fun getRiskLevel(errorType: ErrorType): RiskLevel = when (errorType) {
        ErrorType.UNKNOWN,
        ErrorType.ERROR_NET_INTERRUPT,
        ErrorType.ERROR_NET_TIMEOUT,
        ErrorType.ERROR_CONNECTION_REFUSED,
        ErrorType.ERROR_UNKNOWN_SOCKET_TYPE,
        ErrorType.ERROR_REDIRECT_LOOP,
        ErrorType.ERROR_OFFLINE,
        ErrorType.ERROR_NET_RESET,
        ErrorType.ERROR_UNSAFE_CONTENT_TYPE,
        ErrorType.ERROR_CORRUPTED_CONTENT,
        ErrorType.ERROR_CONTENT_CRASHED,
        ErrorType.ERROR_INVALID_CONTENT_ENCODING,
        ErrorType.ERROR_UNKNOWN_HOST,
        ErrorType.ERROR_MALFORMED_URI,
        ErrorType.ERROR_FILE_NOT_FOUND,
        ErrorType.ERROR_FILE_ACCESS_DENIED,
        ErrorType.ERROR_PROXY_CONNECTION_REFUSED,
        ErrorType.ERROR_UNKNOWN_PROXY_HOST,
        ErrorType.ERROR_NO_INTERNET,
        ErrorType.ERROR_HTTPS_ONLY,
        ErrorType.ERROR_BAD_HSTS_CERT,
        ErrorType.ERROR_UNKNOWN_PROTOCOL,
        -> RiskLevel.Low

        ErrorType.ERROR_SECURITY_BAD_CERT,
        ErrorType.ERROR_SECURITY_SSL,
        ErrorType.ERROR_PORT_BLOCKED,
        -> RiskLevel.Medium

        ErrorType.ERROR_SAFEBROWSING_HARMFUL_URI,
        ErrorType.ERROR_SAFEBROWSING_MALWARE_URI,
        ErrorType.ERROR_SAFEBROWSING_PHISHING_URI,
        ErrorType.ERROR_SAFEBROWSING_UNWANTED_URI,
        -> RiskLevel.High
    }

    private fun getErrorPageTitle(context: Context, type: ErrorType): String? {
        return when (type) {
            ErrorType.ERROR_HTTPS_ONLY -> context.getString(R.string.errorpage_httpsonly_title)
            // Returning `null` will let the component use its default title for this error type
            else -> null
        }
    }

    private fun getErrorPageDescription(context: Context, type: ErrorType): String? {
        return when (type) {
            ErrorType.ERROR_HTTPS_ONLY ->
                context.getString(R.string.errorpage_httpsonly_message_title) +
                    "<br><br>" +
                    context.getString(R.string.errorpage_httpsonly_message_summary)
            // Returning `null` will let the component use its default description for this error type
            else -> null
        }
    }

    internal enum class RiskLevel(val htmlRes: String) {
        Low(LOW_AND_MEDIUM_RISK_ERROR_PAGES),
        Medium(LOW_AND_MEDIUM_RISK_ERROR_PAGES),
        High(HIGH_RISK_ERROR_PAGES),
    }

    companion object {
        internal const val LOW_AND_MEDIUM_RISK_ERROR_PAGES = "low_and_medium_risk_error_pages.html"
        internal const val HIGH_RISK_ERROR_PAGES = "high_risk_error_pages.html"
    }
}
