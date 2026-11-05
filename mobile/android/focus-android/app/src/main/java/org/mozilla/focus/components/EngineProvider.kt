/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.components

import android.content.Context
import androidx.annotation.VisibleForTesting
import mozilla.components.browser.engine.gecko.GeckoEngine
import mozilla.components.browser.engine.gecko.fetch.GeckoViewFetchClient
import mozilla.components.concept.engine.DefaultSettings
import mozilla.components.concept.engine.Engine
import mozilla.components.concept.engine.EngineSession
import mozilla.components.concept.engine.EngineSession.TrackingProtectionPolicy.CookiePolicy
import mozilla.components.concept.fetch.Client
import mozilla.components.lib.crash.handler.CrashHandlerService
import org.mozilla.focus.R
import org.mozilla.focus.components.EngineProvider.getCookiePolicy
import org.mozilla.focus.components.EngineProvider.getOrCreateRuntime
import org.mozilla.focus.ext.components
import org.mozilla.focus.ext.settings
import org.mozilla.focus.utils.AppConstants
import org.mozilla.focus.utils.Settings
import org.mozilla.geckoview.GeckoRuntime
import org.mozilla.geckoview.GeckoRuntimeSettings

/**
 * Object responsible for providing and managing instances of the Gecko engine and related components.
 *
 * This object handles:
 * - Initialization and caching of the shared [GeckoRuntime].
 * - Creation of [Engine] instances with customized settings.
 * - Creation of [Client] instances for network operations.
 * - Construction of [EngineSession.TrackingProtectionPolicy] based on user settings.
 * - Determination of [CookiePolicy] based on user preferences.
 * - Configuration of Safe Browsing for [Engine] instances.
 */
object EngineProvider {
    private var runtime: GeckoRuntime? = null

    // Default value is block cross site cookies.
    const val DEFAULT_COOKIE_OPTION_INDEX = 3
    const val NO_VALUE = "no value"

    /**
     * Returns the existing GeckoRuntime instance or creates a new one if it doesn't exist.
     *
     * This function ensures that only one GeckoRuntime instance is created throughout the application's lifecycle. It
     * is synchronized to handle concurrent access from multiple threads.
     *
     * The function also configures the GeckoRuntime with specific settings:
     * - A crash handler is set to use the CrashHandlerService.
     * - The "about:config" page is enabled for development, nightly, and beta builds.
     *
     * @param context The Android Context to use for creating the GeckoRuntime.
     * @param createRuntime A lambda function that creates a GeckoRuntime instance.
     * @return The existing or newly created GeckoRuntime instance.
     * @throws IllegalStateException If the runtime is null after attempting to create it.
     */
    @VisibleForTesting
    @Synchronized
    internal fun getOrCreateRuntime(
        context: Context,
        createRuntime: (GeckoRuntimeSettings) -> GeckoRuntime = { geckoRuntimeSettings ->
            GeckoRuntime.create(context, geckoRuntimeSettings)
        },
    ): GeckoRuntime {
        if (runtime == null) {
            val builder = GeckoRuntimeSettings.Builder()

            builder.proguardUuid(context.components.buildMetadata.proguardUuid)
            builder.crashHandler(CrashHandlerService::class.java)
            builder.aboutConfigEnabled(AppConstants.isDevOrNightlyBuild || AppConstants.isBetaBuild)

            runtime = createRuntime(builder.build())
        }

        return runtime!!
    }

    /**
     * Creates and returns a GeckoEngine instance.
     *
     * This function initializes and configures a Gecko engine for use within an application. It leverages a shared
     * runtime (obtained via [getOrCreateRuntime]) to reduce overhead and improve performance.
     *
     * @param context The Android context used for engine initialization. This context should be long-lived, such as the
     *   application context.
     * @param defaultSettings The default settings to apply to the engine. This object controls various aspects of
     *   engine behavior, such as whether debugging is enabled.
     * @return A configured and ready-to-use [GeckoEngine] instance.
     * @throws IllegalStateException if the underlying Gecko runtime could not be initialized.
     */
    fun createEngine(context: Context, defaultSettings: DefaultSettings): Engine {
        val runtime = getOrCreateRuntime(context)

        return GeckoEngine(context, defaultSettings, runtime)
    }

    /**
     * Creates a new [GeckoViewFetchClient] instance.
     *
     * @param context The application [Context] used to access shared preferences and other Android resources.
     * @param runtime The [GeckoRuntime] to use for the client. If not provided, a shared runtime will be obtained or
     *   created using [getOrCreateRuntime].
     * @return A new instance of GeckoViewFetchClient.
     */
    fun createClient(
        context: Context,
        runtime: GeckoRuntime = getOrCreateRuntime(context),
    ): Client {
        return GeckoViewFetchClient(context, runtime)
    }

    /**
     * Creates a tracking protection policy based on the user's settings.
     *
     * This function determines which tracking categories should be blocked and what cookie policy should be applied
     * based on the user's preferences stored in the application settings. It then constructs and returns an
     * [EngineSession.TrackingProtectionPolicy] object representing the desired level of tracking protection.
     *
     * @param context The application context, used to access the application's settings.
     * @param settings The application settings.
     * @param newCookieValue The new value for the cookie blocking preference.
     * @return An [EngineSession.TrackingProtectionPolicy] object that defines the tracking protection rules.
     *
     * The tracking protection policy is built based on these settings:
     * - **Scripts and Sub-resources**: Always blocked.
     * - **Social Trackers**: Blocked if [Settings.shouldBlockSocialTrackers] returns true.
     * - **Ad Trackers**: Blocked if [Settings.shouldBlockAdTrackers] returns true.
     * - **Analytics Trackers**: Blocked if [Settings.shouldBlockAnalyticTrackers] returns true.
     * - **Other Trackers**: Blocked if [Settings.shouldBlockOtherTrackers] returns true.
     *
     * The cookie policy is determined by the [getCookiePolicy] function.
     *
     * The generated policy will also enforce `strictSocialTrackingProtection` if social trackers are blocked.
     */
    fun createTrackingProtectionPolicy(
        context: Context,
        settings: Settings = context.settings,
        newCookieValue: String? = null,
    ): EngineSession.TrackingProtectionPolicy {
        val trackingCategories: MutableList<EngineSession.TrackingProtectionPolicy.TrackingCategory> =
            mutableListOf(EngineSession.TrackingProtectionPolicy.TrackingCategory.SCRIPTS_AND_SUB_RESOURCES)

        if (settings.shouldBlockSocialTrackers()) {
            trackingCategories.add(EngineSession.TrackingProtectionPolicy.TrackingCategory.SOCIAL)
        }
        if (settings.shouldBlockAdTrackers()) {
            trackingCategories.add(EngineSession.TrackingProtectionPolicy.TrackingCategory.AD)
        }
        if (settings.shouldBlockAnalyticTrackers()) {
            trackingCategories.add(EngineSession.TrackingProtectionPolicy.TrackingCategory.ANALYTICS)
        }
        if (settings.shouldBlockOtherTrackers()) {
            trackingCategories.add(EngineSession.TrackingProtectionPolicy.TrackingCategory.CONTENT)
        }

        val cookiePolicy = getCookiePolicy(context, settings, newCookieValue)

        return EngineSession.TrackingProtectionPolicy.select(
            cookiePolicy = cookiePolicy,
            trackingCategories = trackingCategories.toTypedArray(),
            strictSocialTrackingProtection = settings.shouldBlockSocialTrackers(),
        )
    }

    /**
     * Retrieves the appropriate [CookiePolicy] based on the user's cookie blocking preference.
     *
     * This function reads the user's cookie blocking preference from the application settings. It then maps this
     * preference to a corresponding [CookiePolicy] value that can be used by GeckoView to control cookie behavior.
     *
     * The function handles the following scenarios:
     *
     * 1. **Explicitly Set Preferences:** If the user has explicitly selected a cookie blocking preference (e.g., "Block
     *    All", "Block Third-Party Trackers"), it maps the string representation of the preference to the corresponding
     *    [CookiePolicy].
     *
     * 2. **Unset Preferences:** If the user has not yet set a cookie preference (represented by [NO_VALUE]), it sets
     *    the default preference ("cross_site") and returns [CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS].
     *
     * 3. **Locale Changes/Unknown Values:** If the stored preference value is not recognized (e.g., due to a locale
     *    change and previously saved string is not in current locale), it attempts to find the closest matching option
     *    based on the index in the `cookies_options_entries` array. If no match is found, it defaults to "cross_site".
     *    Finally it determines and returns the [CookiePolicy] based on the now properly assigned value.
     *
     * @param context The application context.
     * @param settings The application settings, defaults to `context.settings`.
     * @param newCookieValue The new value for the cookie blocking preference.
     * @return The [CookiePolicy] corresponding to the user's cookie blocking preference.
     * @see CookiePolicy
     */
    @VisibleForTesting
    internal fun getCookiePolicy(
        context: Context,
        settings: Settings = context.settings,
        newCookieValue: String? = null,
    ): CookiePolicy {
        val cookieValue = newCookieValue ?: settings.shouldBlockCookiesValue
        return when (cookieValue) {
            context.getString(R.string.yes) -> CookiePolicy.ACCEPT_NONE

            context.getString(R.string.third_party_tracker) -> CookiePolicy.ACCEPT_NON_TRACKERS

            context.getString(R.string.third_party_only) -> CookiePolicy.ACCEPT_ONLY_FIRST_PARTY

            context.getString(R.string.cross_site) -> CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS

            context.getString(R.string.no) -> CookiePolicy.ACCEPT_ALL

            NO_VALUE -> {
                // Ending up here means that the cookie preference has not been yet modified.
                // We should set it to the default value.
                settings.shouldBlockCookiesValue =
                    context.resources.getStringArray(R.array.cookies_options_entry_values)[DEFAULT_COOKIE_OPTION_INDEX]

                CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS
            }

            else -> {
                // Ending up here means that the cookie preference has already been stored in another locale.
                // We will have identify the existing option and set the preference to the corresponding value.
                // See https://github.com/mozilla-mobile/focus-android/issues/5996.

                val cookieOptionIndex =
                    context.resources.getStringArray(R.array.cookies_options_entries).asList().indexOf(cookieValue)

                val correspondingValue =
                    context.resources.getStringArray(R.array.cookies_options_entry_values).getOrNull(cookieOptionIndex)
                        ?: context.resources
                            .getStringArray(R.array.cookies_options_entry_values)[DEFAULT_COOKIE_OPTION_INDEX]

                settings.shouldBlockCookiesValue = correspondingValue

                // Get the updated cookie policy for the corresponding value
                when (settings.shouldBlockCookiesValue) {
                    context.getString(R.string.yes) -> CookiePolicy.ACCEPT_NONE

                    context.getString(R.string.third_party_tracker) -> CookiePolicy.ACCEPT_NON_TRACKERS

                    context.getString(R.string.third_party_only) -> CookiePolicy.ACCEPT_ONLY_FIRST_PARTY

                    context.getString(R.string.cross_site) -> CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS

                    context.getString(R.string.no) -> CookiePolicy.ACCEPT_ALL

                    else -> {
                        // Fallback to the default value.
                        CookiePolicy.ACCEPT_FIRST_PARTY_AND_ISOLATE_OTHERS
                    }
                }
            }
        }
    }

    /**
     * Configures the Safe Browsing policy for the given [Engine].
     *
     * @param engine The [Engine] instance for which to configure Safe Browsing.
     * @param shouldUseSafeBrowsing `true` if Safe Browsing should be enabled using the `RECOMMENDED` policy, `false` if
     *   Safe Browsing should be disabled using the `NONE` policy.
     */
    fun setupSafeBrowsing(engine: Engine, shouldUseSafeBrowsing: Boolean) {
        if (shouldUseSafeBrowsing) {
            engine.settings.safeBrowsingPolicy = arrayOf(EngineSession.SafeBrowsingPolicy.RECOMMENDED)
        } else {
            engine.settings.safeBrowsingPolicy = arrayOf(EngineSession.SafeBrowsingPolicy.NONE)
        }
    }
}
