/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.experiments

import android.content.Context
import android.os.Build
import androidx.annotation.VisibleForTesting
import androidx.core.app.NotificationManagerCompat
import java.io.File
import kotlinx.coroutines.flow.first
import mozilla.components.support.base.ext.areNotificationsEnabledSafe
import mozilla.components.support.base.ext.isNotificationChannelEnabled
import mozilla.components.support.locale.LocaleManager
import mozilla.components.support.locale.LocaleManager.getSystemDefault
import mozilla.components.support.utils.ext.packageManagerCompatHelper
import org.json.JSONArray
import org.json.JSONObject
import org.mozilla.experiments.nimbus.NIMBUS_DATA_DIR
import org.mozilla.experiments.nimbus.NimbusDeviceInfo
import org.mozilla.experiments.nimbus.internal.JsonObject
import org.mozilla.experiments.nimbus.internal.RecordedContext
import org.mozilla.experiments.nimbus.internal.RecordedEnrollmentState
import org.mozilla.experiments.nimbus.internal.getCalculatedAttributes
import org.mozilla.fenix.GleanMetrics.NimbusSystem
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.home.pocket.ContentRecommendationsFeatureHelper
import org.mozilla.fenix.onboarding.MARKETING_CHANNEL_ID
import org.mozilla.fenix.perf.runBlockingIncrement
import org.mozilla.fenix.termsofuse.experimentation.TermsOfUseAdvancedTargetingHelper
import org.mozilla.fenix.termsofuse.experimentation.utils.DefaultTermsOfUseDataProvider
import org.mozilla.fenix.utils.Settings

/** The following constants are string constants of the keys that appear in the [EVENT_QUERIES] map. */
const val DAYS_OPENED_IN_LAST_28 = "days_opened_in_last_28"

/** [EVENT_QUERIES] is a map of keys to Nimbus SDK EventStore queries. */
private val EVENT_QUERIES = mapOf(DAYS_OPENED_IN_LAST_28 to "'app_opened'|eventCountNonZero('Days', 28, 0)")

/**
 * The RecordedNimbusContext class inherits from an internal Nimbus interface that provides methods for obtaining a JSON
 * value for the object and recording the object's value to Glean. Its JSON value is loaded into the Nimbus targeting
 * context.
 *
 * The value recorded to Glean is used to automate population sizing. Any additions to this object require a new data
 * review for the `nimbus_system.recorded_nimbus_context` metric.
 */
@Suppress("complexity:LongParameterList")
class RecordedNimbusContext(
    val isFirstRun: Boolean,
    private val eventQueries: Map<String, String> = mapOf(),
    private var eventQueryValues: Map<String, Double> = mapOf(),
    val utmSource: String,
    val utmMedium: String,
    val utmCampaign: String,
    val utmTerm: String,
    val utmContent: String,
    val androidSdkVersion: String = Build.VERSION.SDK_INT.toString(),
    val appVersion: String?,
    val locale: String,
    val daysSinceInstall: Int?,
    val daysSinceUpdate: Int?,
    val language: String?,
    val region: String?,
    val deviceManufacturer: String = Build.MANUFACTURER,
    val deviceModel: String = Build.MODEL,
    val userAcceptedTou: Boolean,
    val noShortcutsOrStoriesOptOuts: Boolean,
    val addonIds: List<String>,
    val touPoints: Int?,
    val userDisabledAi: Boolean,
    val areNotificationsEnabled: Boolean,
    val areMarketingNotificationsEnabled: Boolean,
) : RecordedContext {
    private var enrollmentState: RecordedEnrollmentState? = null

    /**
     * [getEventQueries] is called by the Nimbus SDK Rust code to retrieve the map of event queries. The are then
     * executed against the Nimbus SDK's EventStore to retrieve their values.
     *
     * @return Map<String, String>
     */
    override fun getEventQueries(): Map<String, String> {
        return eventQueries
    }

    /**
     * [record] is called when experiment enrollments are evolved. It should apply the [RecordedNimbusContext]'s values
     * to a [NimbusSystem.RecordedNimbusContextObject] instance, and use that instance to record the values to Glean.
     */
    override fun record() {
        val eventQueryValuesObject =
            NimbusSystem.RecordedNimbusContextObjectItemEventQueryValuesObject(
                daysOpenedInLast28 = eventQueryValues[DAYS_OPENED_IN_LAST_28]?.toInt()
            )
        NimbusSystem.recordedNimbusContext.set(
            NimbusSystem.RecordedNimbusContextObject(
                isFirstRun = isFirstRun,
                eventQueryValues = eventQueryValuesObject,
                installReferrerResponseUtmSource = utmSource,
                installReferrerResponseUtmMedium = utmMedium,
                installReferrerResponseUtmCampaign = utmCampaign,
                installReferrerResponseUtmTerm = utmTerm,
                installReferrerResponseUtmContent = utmContent,
                androidSdkVersion = androidSdkVersion,
                appVersion = appVersion,
                locale = locale,
                daysSinceInstall = daysSinceInstall,
                daysSinceUpdate = daysSinceUpdate,
                language = language,
                region = region,
                deviceManufacturer = deviceManufacturer,
                deviceModel = deviceModel,
                userAcceptedTou = userAcceptedTou,
                noShortcutsOrStoriesOptOuts = noShortcutsOrStoriesOptOuts,
                addonIds = NimbusSystem.RecordedNimbusContextObjectAddonIds(addonIds.toMutableList()),
                touPoints = touPoints,
                userDisabledAi = userDisabledAi,
                areNotificationsEnabled = areNotificationsEnabled,
                areMarketingNotificationsEnabled = areMarketingNotificationsEnabled,
                activeExperiments = NimbusSystem.RecordedNimbusContextObjectActiveExperiments(
                    enrollmentState?.activeExperiments.orEmpty().sorted().toMutableList(),
                ),
                enrollments = NimbusSystem.RecordedNimbusContextObjectEnrollments(
                    enrollmentState?.enrollments.orEmpty().sorted().toMutableList(),
                ),
                enrollmentsMap = NimbusSystem.RecordedNimbusContextObjectEnrollmentsMap(
                    enrollmentState?.enrollmentsMap.orEmpty().entries.sortedBy { it.key }.map { (slug, branch) ->
                        NimbusSystem.RecordedNimbusContextObjectEnrollmentsMapItem(
                            experimentslug = slug,
                            branchslug = branch,
                        )
                    }.toMutableList(),
                ),
            )
        )
    }

    /**
     * [setEventQueryValues] is called by the Nimbus SDK Rust code after the event queries have been executed. The
     * [eventQueryValues] should be written back to the Kotlin object.
     *
     * @param [eventQueryValues] The values for each query after they have been executed in the Nimbus SDK Rust
     *   environment.
     */
    override fun setEventQueryValues(eventQueryValues: Map<String, Double>) {
        this.eventQueryValues = eventQueryValues
    }

    /**
     * [setEnrollmentState] is called by the Nimbus SDK Rust code just before [record]. The [enrollmentState] is
     * recorded to Glean. It is not added to [toJson], because the SDK already supplies it to the targeting context.
     *
     * @param [enrollmentState] Which experiments the client is enrolled in, and the branch for each.
     */
    override fun setEnrollmentState(enrollmentState: RecordedEnrollmentState) {
        this.enrollmentState = enrollmentState
    }

    /**
     * [toJson] is called by the Nimbus SDK Rust code after the event queries have been executed, and before experiment
     * enrollments have been evolved. The value returned from this method will be applied directly to the Nimbus
     * targeting context, and its keys/values take precedence over those in the main Nimbus targeting context.
     *
     * @return JSONObject
     */
    override fun toJson(): JsonObject {
        val obj =
            JSONObject(
                mapOf(
                    "is_first_run" to isFirstRun,
                    "events" to JSONObject(eventQueryValues),
                    "install_referrer_response_utm_source" to utmSource,
                    "install_referrer_response_utm_medium" to utmMedium,
                    "install_referrer_response_utm_campaign" to utmCampaign,
                    "install_referrer_response_utm_term" to utmTerm,
                    "install_referrer_response_utm_content" to utmContent,
                    "android_sdk_version" to androidSdkVersion,
                    "app_version" to appVersion,
                    "locale" to locale,
                    "days_since_install" to daysSinceInstall,
                    "days_since_update" to daysSinceUpdate,
                    "language" to language,
                    "region" to region,
                    "device_manufacturer" to deviceManufacturer,
                    "device_model" to deviceModel,
                    "user_accepted_tou" to userAcceptedTou,
                    "no_shortcuts_or_stories_opt_outs" to noShortcutsOrStoriesOptOuts,
                    "addon_ids" to JSONArray(addonIds),
                    "tou_points" to touPoints,
                    "user_disabled_ai" to userDisabledAi,
                    "are_notifications_enabled" to areNotificationsEnabled,
                    "are_marketing_notifications_enabled" to areMarketingNotificationsEnabled,
                )
            )
        return obj
    }

    /** Companion object for RecordedNimbusContext */
    companion object {

        /**
         * Creates a RecordedNimbusContext instance, populated with the application-defined eventQueries
         *
         * @return RecordedNimbusContext
         */
        fun create(
            context: Context,
            isFirstRun: Boolean,
        ): RecordedNimbusContext {
            val settings = context.components.settings
            val langTag = LocaleManager.getCurrentLocale(context)?.toLanguageTag() ?: getSystemDefault().toLanguageTag()
            val termsOfUseAdvancedTargetingHelper =
                TermsOfUseAdvancedTargetingHelper(
                    DefaultTermsOfUseDataProvider(settings),
                    langTag,
                )

            val packageInfo = context.packageManagerCompatHelper.getPackageInfoCompat(context.packageName, 0)
            val deviceInfo = NimbusDeviceInfo.default()
            val db = File(context.applicationInfo.dataDir, NIMBUS_DATA_DIR)
            val calculatedAttributes =
                getCalculatedAttributes(
                    packageInfo.firstInstallTime,
                    db.path,
                    deviceInfo.localeTag,
                )

            val isAiBlocked = runBlockingIncrement { context.components.aiFeatureBlockStorage.isBlocked.first() }
            val notificationManager = NotificationManagerCompat.from(context)

            return RecordedNimbusContext(
                isFirstRun = isFirstRun,
                eventQueries = EVENT_QUERIES,
                utmSource = settings.utmSource,
                utmMedium = settings.utmMedium,
                utmCampaign = settings.utmCampaign,
                utmTerm = settings.utmTerm,
                utmContent = settings.utmContent,
                appVersion = packageInfo.versionName,
                locale = deviceInfo.localeTag,
                daysSinceInstall = calculatedAttributes.daysSinceInstall,
                daysSinceUpdate = calculatedAttributes.daysSinceUpdate,
                language = calculatedAttributes.language,
                region = calculatedAttributes.region,
                userAcceptedTou = settings.hasAcceptedTermsOfService,
                noShortcutsOrStoriesOptOuts = settings.noShortcutsOrStoriesOptOuts(context),
                addonIds = getFormattedAddons(settings),
                touPoints = termsOfUseAdvancedTargetingHelper.getTouPoints(),
                userDisabledAi = isAiBlocked,
                areNotificationsEnabled = notificationManager.areNotificationsEnabledSafe(),
                areMarketingNotificationsEnabled =
                    notificationManager.isNotificationChannelEnabled(MARKETING_CHANNEL_ID),
            )
        }

        @VisibleForTesting
        internal fun getFormattedAddons(settings: Settings): List<String> =
            settings.installedAddonsList.split(",").map { it.trim() }

        /**
         * Checks whether an eligible user has opted out of any sponsored top sites or stories.
         *
         * @return `true` if the user has opted out of any sponsored top sites or stories, `false` otherwise.
         */
        private fun Settings.noShortcutsOrStoriesOptOuts(context: Context) =
            !optedOutOfSponsoredTopSites() && !optedOutOfSponsoredStories(context)

        /**
         * Checks whether an eligible user has opted out of the sponsored top sites feature.
         *
         * This is not entirely self evident from the API descriptions, please note: [Settings.showContileFeature]
         * indicates whether the sponsored shortcuts are shown. [Settings.showTopSitesFeature] indicates whether the
         * feature should be shown at all.
         */
        private fun Settings.optedOutOfSponsoredTopSites() = !showContileFeature || !showTopSitesFeature

        private fun Settings.optedOutOfSponsoredStories(context: Context) =
            isEligibleForStories(context) && (!showPocketSponsoredStories || !showPocketRecommendationsFeature)

        private fun isEligibleForStories(context: Context): Boolean =
            ContentRecommendationsFeatureHelper.isContentRecommendationsFeatureEnabled(context)

        /**
         * Creates a RecordedNimbusContext instance for test purposes
         *
         * @return RecordedNimbusContext
         */
        @VisibleForTesting
        internal fun createForTest(
            isFirstRun: Boolean = false,
            eventQueries: Map<String, String> = EVENT_QUERIES,
            eventQueryValues: Map<String, Double> = mapOf(),
            addonIds: List<String> = emptyList(),
        ): RecordedNimbusContext {
            return RecordedNimbusContext(
                isFirstRun = isFirstRun,
                eventQueries = eventQueries,
                eventQueryValues = eventQueryValues,
                utmSource = "",
                utmMedium = "",
                utmCampaign = "",
                utmTerm = "",
                utmContent = "",
                appVersion = "",
                locale = "",
                daysSinceInstall = 5,
                daysSinceUpdate = 0,
                language = "en",
                region = "US",
                userAcceptedTou = true,
                noShortcutsOrStoriesOptOuts = true,
                addonIds = addonIds,
                touPoints = 3,
                userDisabledAi = true,
                areNotificationsEnabled = false,
                areMarketingNotificationsEnabled = false,
            )
        }
    }
}
