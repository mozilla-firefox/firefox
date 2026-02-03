/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.errorpages

import android.annotation.SuppressLint
import android.content.Context
import androidx.annotation.StringRes
import mozilla.components.support.ktx.android.content.appName
import mozilla.components.support.ktx.kotlin.urlEncode
import mozilla.components.ui.icons.R as iconsR

object ErrorPages {

    private const val HTML_RESOURCE_FILE = "error_page_js.html"

    /**
     * Provides an encoded URL for an error page. Supports displaying images
     *
     * @param titleOverride A function that can return an error page title for an error type. If not
     * provided or if `null` is returned from the function then the default page title for this
     * error type, provided by this component, will be used.
     * @param descriptionOverride  A function that can return an error page description text for an
     * error type. If not provided or if `null` is returned from the function then the default
     * description text for this error type, provided by this component, will be used.
     */
    @SuppressLint("StringFormatInvalid")
    fun createUrlEncodedErrorPage(
        context: Context,
        errorType: ErrorType,
        uri: String? = null,
        htmlResource: String = HTML_RESOURCE_FILE,
        titleOverride: (ErrorType) -> String? = { null },
        descriptionOverride: (ErrorType) -> String? = { null },
        isPrivate: Boolean = false,
    ): String {
        val title = titleOverride(errorType) ?: context.getString(errorType.titleRes)
        val button = context.getString(errorType.refreshButtonRes)
        val description = descriptionOverride(errorType) ?: context.getString(errorType.messageRes, uri)
        val imageName = if (errorType.imageNameRes != null) context.getString(errorType.imageNameRes) + ".svg" else ""
        val errorCode = if (errorType.errorCode != null) context.getString(errorType.errorCode) else ""
        val continueHttpButton = context.getString(R.string.mozac_browser_errorpages_httpsonly_button)
        val badCertAdvanced = context.getString(R.string.mozac_browser_errorpages_security_bad_cert_advanced)
        val badCertTechInfo = when (errorType) {
            ErrorType.ERROR_SECURITY_BAD_CERT ->
                context.getString(
                    R.string.mozac_browser_errorpages_security_bad_cert_techInfo,
                    context.appName,
                    uri.toString(),
                )
            ErrorType.ERROR_BAD_HSTS_CERT -> context.getString(
                R.string.mozac_browser_errorpages_security_bad_hsts_cert_techInfo2,
                uri.toString().trim('/'),
                context.appName,
            )
            else -> ""
        }

        val badCertGoBack = context.getString(R.string.mozac_browser_errorpages_security_bad_cert_back)
        val badCertAcceptTemporary = context.getString(
            R.string.mozac_browser_errorpages_security_bad_cert_accept_temporary,
        )

        val showSSLAdvanced: String = when (errorType) {
            ErrorType.ERROR_SECURITY_BAD_CERT -> true
            else -> false
        }.toString()

        val showHSTSAdvanced: String = when (errorType) {
            ErrorType.ERROR_BAD_HSTS_CERT -> true
            else -> false
        }.toString()

        val showContinueHttp: String = (errorType == ErrorType.ERROR_HTTPS_ONLY).toString()

        /**
         * Warning: When updating these params you WILL cause breaking changes that are undetected
         * by consumers. Update the README accordingly.
         */
        var urlEncodedErrorPage = "resource://android/assets/$htmlResource?" +
            "&title=${title.urlEncode()}" +
            "&button=${button.urlEncode()}" +
            "&description=${description.urlEncode()}" +
            "&image=${imageName.urlEncode()}" +
            "&showSSL=${showSSLAdvanced.urlEncode()}" +
            "&showHSTS=${showHSTSAdvanced.urlEncode()}" +
            "&badCertAdvanced=${badCertAdvanced.urlEncode()}" +
            "&badCertTechInfo=${badCertTechInfo.urlEncode()}" +
            "&badCertGoBack=${badCertGoBack.urlEncode()}" +
            "&badCertAcceptTemporary=${badCertAcceptTemporary.urlEncode()}" +
            "&showContinueHttp=${showContinueHttp.urlEncode()}" +
            "&continueHttpButton=${continueHttpButton.urlEncode()}" +
            "&errorCode=${errorCode.urlEncode()}" +
            "&isPrivate=$isPrivate"

        urlEncodedErrorPage = urlEncodedErrorPage
            .replace("<ul>".urlEncode(), "<ul role=\"presentation\">".urlEncode())
        return urlEncodedErrorPage
    }
}

/**
 * Enum containing all supported error types that we can display an error page for.
 */
enum class ErrorType(
    @param:StringRes val titleRes: Int,
    @param:StringRes val messageRes: Int,
    @param:StringRes val refreshButtonRes: Int = R.string.mozac_browser_errorpages_page_refresh,
    @param:StringRes val imageNameRes: Int? = null,
    @param:StringRes val errorCode: Int? = null,
) {
    UNKNOWN(
        R.string.mozac_browser_errorpages_generic_title,
        R.string.mozac_browser_errorpages_generic_message,
    ),
    ERROR_SECURITY_SSL(
        R.string.mozac_browser_errorpages_security_ssl_title,
        R.string.mozac_browser_errorpages_security_ssl_message,
        imageNameRes = iconsR.string.mozac_error_lock,
    ),
    ERROR_SECURITY_BAD_CERT(
        R.string.mozac_browser_errorpages_security_bad_cert_title,
        R.string.mozac_browser_errorpages_security_bad_cert_message,
        imageNameRes = iconsR.string.mozac_error_lock,
    ),
    ERROR_NET_INTERRUPT(
        R.string.mozac_browser_errorpages_net_interrupt_title,
        R.string.mozac_browser_errorpages_net_interrupt_message,
        imageNameRes = iconsR.string.mozac_error_eye_roll,
    ),
    ERROR_NET_TIMEOUT(
        R.string.mozac_browser_errorpages_net_timeout_title,
        R.string.mozac_browser_errorpages_net_timeout_message,
        imageNameRes = iconsR.string.mozac_error_asleep,
    ),
    ERROR_CONNECTION_REFUSED(
        R.string.mozac_browser_errorpages_connection_failure_title,
        R.string.mozac_browser_errorpages_connection_failure_message,
        imageNameRes = iconsR.string.mozac_error_confused,
    ),
    ERROR_UNKNOWN_SOCKET_TYPE(
        R.string.mozac_browser_errorpages_unknown_socket_type_title,
        R.string.mozac_browser_errorpages_unknown_socket_type_message,
        imageNameRes = iconsR.string.mozac_error_confused,
    ),
    ERROR_REDIRECT_LOOP(
        R.string.mozac_browser_errorpages_redirect_loop_title,
        R.string.mozac_browser_errorpages_redirect_loop_message,
        imageNameRes = iconsR.string.mozac_error_surprised,
    ),
    ERROR_OFFLINE(
        R.string.mozac_browser_errorpages_offline_title,
        R.string.mozac_browser_errorpages_offline_message,
        imageNameRes = iconsR.string.mozac_error_no_internet,
    ),
    ERROR_PORT_BLOCKED(
        R.string.mozac_browser_errorpages_port_blocked_title,
        R.string.mozac_browser_errorpages_port_blocked_message,
        imageNameRes = iconsR.string.mozac_error_lock,
    ),
    ERROR_NET_RESET(
        R.string.mozac_browser_errorpages_net_reset_title,
        R.string.mozac_browser_errorpages_net_reset_message,
        imageNameRes = iconsR.string.mozac_error_unplugged,
    ),
    ERROR_UNSAFE_CONTENT_TYPE(
        R.string.mozac_browser_errorpages_unsafe_content_type_title,
        R.string.mozac_browser_errorpages_unsafe_content_type_message,
        imageNameRes = iconsR.string.mozac_error_inspect,
    ),
    ERROR_CORRUPTED_CONTENT(
        R.string.mozac_browser_errorpages_corrupted_content_title,
        R.string.mozac_browser_errorpages_corrupted_content_message,
        imageNameRes = iconsR.string.mozac_error_shred_file,
    ),
    ERROR_CONTENT_CRASHED(
        R.string.mozac_browser_errorpages_content_crashed_title,
        R.string.mozac_browser_errorpages_content_crashed_message,
        imageNameRes = iconsR.string.mozac_error_surprised,
    ),
    ERROR_INVALID_CONTENT_ENCODING(
        R.string.mozac_browser_errorpages_invalid_content_encoding_title,
        R.string.mozac_browser_errorpages_invalid_content_encoding_message,
        imageNameRes = iconsR.string.mozac_error_surprised,
    ),
    ERROR_UNKNOWN_HOST(
        R.string.mozac_browser_errorpages_unknown_host_title,
        R.string.mozac_browser_errorpages_unknown_host_message,
        imageNameRes = iconsR.string.mozac_error_confused,
    ),
    ERROR_NO_INTERNET(
        R.string.mozac_browser_errorpages_no_internet_title_2,
        R.string.mozac_browser_errorpages_no_internet_message_2,
        R.string.mozac_browser_errorpages_no_internet_refresh_button,
        imageNameRes = iconsR.string.mozac_error_no_internet_connection,
        errorCode = R.string.mozac_browser_errorpages_no_internet_error_code,
    ),
    ERROR_MALFORMED_URI(
        R.string.mozac_browser_errorpages_malformed_uri_title,
        R.string.mozac_browser_errorpages_malformed_uri_message,
        imageNameRes = iconsR.string.mozac_error_confused,
    ),
    ERROR_UNKNOWN_PROTOCOL(
        R.string.mozac_browser_errorpages_unknown_protocol_title,
        R.string.mozac_browser_errorpages_unknown_protocol_message,
        imageNameRes = iconsR.string.mozac_error_confused,
    ),
    ERROR_FILE_NOT_FOUND(
        R.string.mozac_browser_errorpages_file_not_found_title,
        R.string.mozac_browser_errorpages_file_not_found_message,
        imageNameRes = iconsR.string.mozac_error_confused,
    ),
    ERROR_FILE_ACCESS_DENIED(
        R.string.mozac_browser_errorpages_file_access_denied_title,
        R.string.mozac_browser_errorpages_file_access_denied_message,
        imageNameRes = iconsR.string.mozac_error_question_file,
    ),
    ERROR_PROXY_CONNECTION_REFUSED(
        R.string.mozac_browser_errorpages_proxy_connection_refused_title,
        R.string.mozac_browser_errorpages_proxy_connection_refused_message,
        imageNameRes = iconsR.string.mozac_error_confused,
    ),
    ERROR_UNKNOWN_PROXY_HOST(
        R.string.mozac_browser_errorpages_unknown_proxy_host_title,
        R.string.mozac_browser_errorpages_unknown_proxy_host_message,
        imageNameRes = iconsR.string.mozac_error_unplugged,
    ),
    ERROR_SAFEBROWSING_MALWARE_URI(
        R.string.mozac_browser_errorpages_safe_browsing_malware_uri_title,
        R.string.mozac_browser_errorpages_safe_browsing_malware_uri_message,
    ),
    ERROR_SAFEBROWSING_UNWANTED_URI(
        R.string.mozac_browser_errorpages_safe_browsing_unwanted_uri_title,
        R.string.mozac_browser_errorpages_safe_browsing_unwanted_uri_message,
    ),
    ERROR_SAFEBROWSING_HARMFUL_URI(
        R.string.mozac_browser_errorpages_safe_harmful_uri_title,
        R.string.mozac_browser_errorpages_safe_harmful_uri_message,
    ),
    ERROR_SAFEBROWSING_PHISHING_URI(
        R.string.mozac_browser_errorpages_safe_phishing_uri_title,
        R.string.mozac_browser_errorpages_safe_phishing_uri_message,
    ),
    ERROR_HARMFULADDON_URI(
        R.string.mozac_browser_errorpages_harmful_addon_uri_title,
        R.string.mozac_browser_errorpages_harmful_addon_uri_message,
    ),
    ERROR_HTTPS_ONLY(
        R.string.mozac_browser_errorpages_httpsonly_title,
        R.string.mozac_browser_errorpages_httpsonly_message,
        imageNameRes = iconsR.string.mozac_error_lock,
    ),
    ERROR_BAD_HSTS_CERT(
        R.string.mozac_browser_errorpages_security_bad_hsts_cert_title,
        R.string.mozac_browser_errorpages_security_bad_hsts_cert_message,
        imageNameRes = iconsR.string.mozac_error_lock,
    ),
}
