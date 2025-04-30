/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.content.Context
import android.content.Intent
import androidx.browser.customtabs.CustomTabColorSchemeParams
import androidx.browser.customtabs.CustomTabsIntent
import androidx.core.net.toUri
import mozilla.components.support.ktx.android.content.appVersionName
import mozilla.components.support.ktx.android.content.getColorFromAttr
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.IntentReceiverActivity
import org.mozilla.fenix.R
import org.mozilla.fenix.customtabs.EXTRA_IS_SANDBOX_CUSTOM_TAB
import org.mozilla.fenix.settings.account.AuthIntentReceiverActivity
import java.io.UnsupportedEncodingException
import java.net.URLEncoder
import java.util.Locale

object SupportUtils {
    const val RATE_APP_URL = "market://details?id=" + BuildConfig.APPLICATION_ID
    const val POCKET_TRENDING_URL = "https://getpocket.com/fenix-top-articles"
    const val WIKIPEDIA_URL = "https://www.wikipedia.org/"
    const val FENIX_PLAY_STORE_URL = "https://play.google.com/store/apps/details?id=${BuildConfig.APPLICATION_ID}"
    const val GOOGLE_URL = "https://www.google.com/"
    const val GOOGLE_US_URL = "https://www.google.com/webhp?client=firefox-b-1-m&channel=ts"
    const val GOOGLE_XX_URL = "https://www.google.com/webhp?client=firefox-b-m&channel=ts"
    const val WHATS_NEW_URL = "https://www.mozilla.org/firefox/android/notes"
    const val FXACCOUNT_SUMO_URL = "https://support.mozilla.org/kb/access-mozilla-services-firefox-account"

    // This is locale-less on purpose so that the content negotiation happens on the AMO side because the current
    // user language might not be supported by AMO and/or the language might not be exactly what AMO is expecting
    // (e.g. `en` instead of `en-US`).
    const val AMO_HOMEPAGE_FOR_ANDROID = "${BuildConfig.AMO_BASE_URL}/android/"

    enum class SumoTopic(internal val topicStr: String) {
        HELP("faq-android"),
        PRIVATE_BROWSING_MYTHS("common-myths-about-private-browsing"),
        YOUR_RIGHTS("your-rights"),
        TRACKING_PROTECTION("tracking-protection-firefox-android"),
        TOTAL_COOKIE_PROTECTION("enhanced-tracking-protection-android"),
        OPT_OUT_STUDIES("how-opt-out-studies-firefox-android"),
        SEND_TABS("send-tab-preview"),
        SET_AS_DEFAULT_BROWSER("make-firefox-default-browser-android"),
        SEARCH_SUGGESTION("how-search-firefox-preview"),
        CUSTOM_SEARCH_ENGINES("custom-search-engines"),
        SYNC_SETUP("how-set-firefox-sync-firefox-android"),
        QR_CAMERA_ACCESS("qr-camera-access"),
        SMARTBLOCK("smartblock-enhanced-tracking-protection"),
        SPONSOR_PRIVACY("sponsor-privacy"),
        HTTPS_ONLY_MODE("https-only-mode-firefox-android"),
        DNS_OVER_HTTPS("https-only-mode-firefox-android"), // FIXME
        DNS_OVER_HTTPS_LOCAL_PROVIDER("https-only-mode-firefox-android"), // FIXME
        DNS_OVER_HTTPS_NETWORK("https-only-mode-firefox-android"), // FIXME
        UNSIGNED_ADDONS("unsigned-addons"),
        REVIEW_QUALITY_CHECK("review_checker_mobile"),
        FX_SUGGEST("search-suggestions-firefox"),
        TRANSLATIONS("android-translation"),
        MANAGE_OPTIONAL_EXTENSION_PERMISSIONS("manage-optional-permissions-android-extensions"),
        EXTENSION_PERMISSIONS("extension-permissions"),
        FIND_INSTALL_ADDONS("add-ons-firefox-android"),
        CRASH_REPORTS("mobile-crash-reports"),
        TECHNICAL_AND_INTERACTION_DATA("mobile-technical-and-interaction-data"),
        USAGE_PING_SETTINGS("usage-ping-settings-mobile"),
        MARKETING_DATA("mobile-marketing-data"),
    }

    enum class MozillaPage(internal val path: String) {
        PRIVATE_NOTICE("privacy/firefox/"),
        MANIFESTO("about/manifesto/"),
        TERMS_OF_SERVICE("about/legal/terms/firefox/"),
    }

    /**
     * Gets a support page URL for the corresponding topic.
     */
    fun getSumoURLForTopic(
        context: Context,
        topic: SumoTopic,
        locale: Locale = Locale.getDefault(),
    ): String {
        val escapedTopic = getEncodedTopicUTF8(topic.topicStr)
        // Remove the whitespace so a search is not triggered:
        val appVersion = context.appVersionName.replace(" ", "")
        val osTarget = "Android"
        val langTag = getLanguageTag(locale)
        return "https://support.mozilla.org/1/mobile/$appVersion/$osTarget/$langTag/$escapedTopic"
    }

    /**
     * Gets a support page URL for the corresponding topic.
     * Used when the app version and os are not part of the URL.
     */
    fun getGenericSumoURLForTopic(topic: SumoTopic, locale: Locale = Locale.getDefault()): String {
        val escapedTopic = getEncodedTopicUTF8(topic.topicStr)
        val langTag = getLanguageTag(locale)
        return "https://support.mozilla.org/$langTag/kb/$escapedTopic"
    }

    fun getMozillaPageUrl(page: MozillaPage, locale: Locale = Locale.getDefault()): String {
        val path = page.path
        val langTag = getLanguageTag(locale)
        return "https://www.mozilla.org/$langTag/$path"
    }

    fun createCustomTabIntent(context: Context, url: String): Intent = CustomTabsIntent.Builder()
        .setInstantAppsEnabled(false)
        .setDefaultColorSchemeParams(
            CustomTabColorSchemeParams.Builder().setToolbarColor(context.getColorFromAttr(R.attr.layer1)).build(),
        )
        .build()
        .intent
        .setData(url.toUri())
        .setClassName(context, IntentReceiverActivity::class.java.name)
        .setPackage(context.packageName)

    fun createAuthCustomTabIntent(context: Context, url: String): Intent =
        createCustomTabIntent(context, url).setClassName(context, AuthIntentReceiverActivity::class.java.name)

    /**
     * Custom tab that cannot open the content in Firefox directly.
     * This ensures the content is contained to this custom tab only.
     */
    private fun createSandboxCustomTabIntent(context: Context, url: String): Intent =
        createCustomTabIntent(context, url).putExtra(EXTRA_IS_SANDBOX_CUSTOM_TAB, true)

    /**
     * Launches a new sandboxed custom tab Activity.
     *
     * @param context The context to launch the Activity from.
     * @param url The URL to load in the custom tab.
     */
    fun launchSandboxCustomTab(context: Context, url: String) {
        val intent = createSandboxCustomTabIntent(context, url)
        context.startActivity(intent)
    }

    private fun getEncodedTopicUTF8(topic: String): String {
        try {
            return URLEncoder.encode(topic, "UTF-8")
        } catch (e: UnsupportedEncodingException) {
            throw IllegalStateException("utf-8 should always be available", e)
        }
    }

    private fun getLanguageTag(locale: Locale): String {
        val language = locale.language
        val country = locale.country // Can be an empty string.
        return if (country.isEmpty()) language else "$language-$country"
    }
}
