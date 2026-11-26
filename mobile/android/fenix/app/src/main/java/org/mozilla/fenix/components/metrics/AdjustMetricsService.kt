/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import android.app.Application
import android.os.SystemClock
import androidx.annotation.VisibleForTesting
import com.adjust.sdk.AdjustConfig
import com.adjust.sdk.AdjustEvent
import com.adjust.sdk.Constants.ADJUST_PREINSTALL_SYSTEM_PROPERTY_PATH
import com.adjust.sdk.LogLevel
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import mozilla.components.lib.crash.CrashReporter
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.BuildConfig
import org.mozilla.fenix.Config
import org.mozilla.fenix.GleanMetrics.AdjustAttribution
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.AURA_PARTNER_ID
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.DYNAMIC_CALLBACK_ID
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.GOOGLE_PARTNER_ID
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.META_PARTNER_ID
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.MOLOCO_PARTNER_ID
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.REDDIT_PARTNER_ID
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.TIKTOK_PARTNER_ID
import org.mozilla.fenix.components.metrics.AdjustThirdPartySharingController.Companion.X_TWITTER_PARTNER_ID
import org.mozilla.fenix.distributions.DistributionAdjustStartupStrategy
import org.mozilla.fenix.distributions.DistributionIdManager
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.utils.Settings

class AdjustMetricsService(
    private val application: Application,
    private val storage: MetricsStorage,
    private val crashReporter: CrashReporter,
    private val dispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val adjustSdk: AdjustSdkController = DefaultAdjustSdkController(),
    private val adjustToken: String? = BuildConfig.ADJUST_TOKEN,
    private val thirdPartySharingController: ThirdPartySharingController = AdjustThirdPartySharingController(),
) : MetricsService {
    override val type = MetricServiceType.Marketing
    private val logger = Logger("AdjustMetricsService")

    private val initMutex = Mutex()

    @Volatile private var initialized = false

    override fun start() {
        logger.info("Started")

        CoroutineScope(dispatcher).launch {
            val settings = application.components.settings

            // Start Adjust to report attribution the first time the user accepts marketing.
            // Once attribution is known there is nothing to report, so leave Adjust dormant.
            if (!alreadyKnown(settings)) {
                ensureInitialized(settings)
            }
        }
    }

    override fun stop() {
        logger.info("Stopped")

        // Persist the forget-me synchronously (durable), then init Adjust to flush it.
        // No attribution listener: opting out must not record attribution.
        adjustSdk.gdprForgetMe(application.applicationContext)
        CoroutineScope(dispatcher).launch {
            ensureInitialized(application.components.settings, shouldRegisterAttributionListener = false)
        }
    }

    @Suppress("TooGenericExceptionCaught")
    override fun track(event: Event) {
        logger.info("Track")

        CoroutineScope(dispatcher).launch {
            try {
                val tokenName =
                    when (event) {
                        is Event.GrowthData -> event.tokenName
                        is Event.FirstWeekPostInstall -> event.tokenName
                    }

                if ((event is Event.GrowthData || event is Event.FirstWeekPostInstall) && storage.shouldTrack(event)) {
                    ensureInitialized(application.components.settings)
                    adjustSdk.trackEvent(AdjustEvent(tokenName))
                    storage.updateSentState(event)
                    sendGleanEventAndPing(event)
                    logger.info("Update sent state $event")
                }
            } catch (e: Exception) {
                crashReporter.submitCaughtException(e)
                logger.info("Track threw an exception for $event")
            }
        }
    }

    // Initializes Adjust once per process (initMutex guards concurrent callers). Pass
    // shouldRegisterAttributionListener = false on opt-out so init never records attribution.
    private suspend fun ensureInitialized(settings: Settings, shouldRegisterAttributionListener: Boolean = true) {
        if (initialized) {
            return
        }

        initMutex.withLock {
            if (initialized) {
                return
            }

            val token = adjustToken
            if (token.isNullOrBlank()) {
                logger.info("No adjust token defined")

                if (Config.channel.isReleased) {
                    throw IllegalStateException("No adjust token defined for release build")
                }

                return
            }

            val config = buildAdjustConfig(token, settings, shouldRegisterAttributionListener)

            // All configuration have to be done before this.
            adjustSdk.initSdk(config)
            adjustSdk.enable()
            initialized = true
            logger.info("Adjust SDK enabled")
        }
    }

    private suspend fun buildAdjustConfig(
        token: String,
        settings: Settings,
        shouldRegisterAttributionListener: Boolean,
    ): AdjustConfig {
        System.setProperty(ADJUST_PREINSTALL_SYSTEM_PROPERTY_PATH, "/preload/etc/adjust.preinstall")

        val config =
            AdjustConfig(
                application,
                token,
                AdjustConfig.ENVIRONMENT_PRODUCTION,
                true,
            )
        config.enablePreinstallTracking()

        val distributionIdManager = application.components.distributionIdManager

        // If we skipped the marketing consent screen, enable COPPA compliance to prevent
        // personal identifiers from being shared with Adjust.
        when (distributionIdManager.getDistributionAdjustStartupStrategy()) {
            DistributionAdjustStartupStrategy.IMMEDIATE_WITH_COPPA -> config.enableCoppaCompliance()

            DistributionAdjustStartupStrategy.IMMEDIATE_WITH_PLAY_STORE_KIDS -> config.enablePlayStoreKidsCompliance()

            else -> {}
        }

        if (shouldRegisterAttributionListener && !alreadyKnown(settings)) {
            setAttributionChangedListener(config, settings)
        }

        config.setLogLevel(LogLevel.SUPPRESS)

        config.disableFbIdReading()
        applyThirdPartySharingSettings(
            distribution = distributionIdManager.getDistribution(),
            isUserMetaAttributed = settings.isUserMetaAttributed,
            isUserTikTokAttributed = settings.isUserTikTokAttributed,
            isUserRedditAttributed = settings.isUserRedditAttributed,
            isUserXTwitterAttributed = settings.isUserXTwitterAttributed,
            isUserMolocoAttributed = settings.isUserMolocoAttributed,
            isUserRakutenAttributed = settings.isUserRakutenAttributed,
            isUserSkyflagAttributed = settings.isUserSkyflagAttributed,
            isUserQuoraAttributed = settings.isUserQuoraAttributed,
            controller = thirdPartySharingController,
        )

        return config
    }

    private fun setAttributionChangedListener(config: AdjustConfig, settings: Settings) {
        // TODO(bug 2090806): We have to avoid the start/stopAndAccumulate API here at the moment due to crashes when
        // this runs before Glean init.
        // That's fixed in Glean upstream, but has not made it into a release.
        // Once that's out we can switch it back.
        val startTime = SystemClock.elapsedRealtime()

        config.setOnAttributionChangedListener {
            AdjustAttribution.adjustAttributionTime.accumulateSingleSample(SystemClock.elapsedRealtime() - startTime)

            if (!it.network.isNullOrEmpty()) {
                settings.adjustNetwork = it.network
                AdjustAttribution.network.set(it.network)
            }
            if (!it.adgroup.isNullOrEmpty()) {
                settings.adjustAdGroup = it.adgroup
                AdjustAttribution.adgroup.set(it.adgroup)
            }
            if (!it.creative.isNullOrEmpty()) {
                settings.adjustCreative = it.creative
                AdjustAttribution.creative.set(it.creative)
            }
            if (!it.campaign.isNullOrEmpty()) {
                settings.adjustCampaignId = it.campaign
                AdjustAttribution.campaign.set(it.campaign)
            }

            triggerPing()
            logger.info("Trigger ping")
        }
    }

    override fun shouldTrack(event: Event): Boolean = event is Event.GrowthData || event is Event.FirstWeekPostInstall

    companion object {
        const val CONVERSION_EVENT_1 = 1
        const val CONVERSION_EVENT_2 = 2
        const val CONVERSION_EVENT_3 = 3
        const val CONVERSION_EVENT_4 = 4
        const val CONVERSION_EVENT_5 = 5
        const val CONVERSION_EVENT_6 = 6
        const val CONVERSION_EVENT_7 = 7
        const val CONVERSION_EVENT_8 = 8
        const val CONVERSION_EVENT_9 = 9
        const val CONVERSION_EVENT_10 = 10

        /** Records a glean event matching the Adjust conversion event, and sends the Adjust attribution ping. */
        @VisibleForTesting
        internal fun sendGleanEventAndPing(
            event: Event,
            conversionEventRecorder: ConversionEventRecorder = GleanConversionEventRecorder(),
        ) {
            when (event) {
                is Event.GrowthData.ConversionEvent1 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_1)
                is Event.GrowthData.ConversionEvent2 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_2)
                is Event.GrowthData.ConversionEvent3 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_3)
                is Event.GrowthData.ConversionEvent4 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_4)
                is Event.GrowthData.ConversionEvent5 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_5)
                is Event.GrowthData.ConversionEvent6 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_6)
                is Event.GrowthData.ConversionEvent7 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_7)
                is Event.FirstWeekPostInstall.ConversionEvent8 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_8)
                is Event.FirstWeekPostInstall.ConversionEvent9 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_9)
                is Event.FirstWeekPostInstall.ConversionEvent10 ->
                    conversionEventRecorder.recordConversionEvent(CONVERSION_EVENT_10)
            }
        }

        /** Sets third party sharing settings based on distribution and attribution. */
        @Suppress("LongParameterList")
        @VisibleForTesting
        internal fun applyThirdPartySharingSettings(
            distribution: DistributionIdManager.Distribution,
            isUserMetaAttributed: Boolean,
            isUserTikTokAttributed: Boolean,
            isUserRedditAttributed: Boolean,
            isUserXTwitterAttributed: Boolean,
            isUserMolocoAttributed: Boolean,
            isUserRakutenAttributed: Boolean,
            isUserSkyflagAttributed: Boolean,
            isUserQuoraAttributed: Boolean,
            controller: ThirdPartySharingController = AdjustThirdPartySharingController(),
        ) {
            when (distribution) {
                DistributionIdManager.Distribution.DEFAULT -> {
                    controller.disableAllThirdPartySharing()
                    // Listed in priority order. Multiple flags can be true at once, so the order
                    // is load-bearing. Insert new partners at the position matching their priority.
                    when {
                        isUserMetaAttributed -> controller.enableThirdPartySharingForPartner(META_PARTNER_ID)
                        isUserTikTokAttributed -> controller.enableThirdPartySharingForPartner(TIKTOK_PARTNER_ID)
                        isUserRedditAttributed -> controller.enableThirdPartySharingForPartner(REDDIT_PARTNER_ID)
                        isUserXTwitterAttributed -> controller.enableThirdPartySharingForPartner(X_TWITTER_PARTNER_ID)
                        isUserMolocoAttributed -> controller.enableThirdPartySharingForPartner(MOLOCO_PARTNER_ID)
                        isUserRakutenAttributed || isUserSkyflagAttributed || isUserQuoraAttributed ->
                            controller.enableThirdPartySharingForPartner(DYNAMIC_CALLBACK_ID)
                        else -> controller.enableThirdPartySharingForPartner(GOOGLE_PARTNER_ID)
                    }
                }

                DistributionIdManager.Distribution.AURA_001 -> {
                    controller.enableThirdPartySharingForPartner(AURA_PARTNER_ID)
                }

                DistributionIdManager.Distribution.VIVO_001,
                DistributionIdManager.Distribution.DT_001,
                DistributionIdManager.Distribution.DT_002,
                DistributionIdManager.Distribution.DT_003,
                DistributionIdManager.Distribution.XIAOMI_001 -> {
                    controller.disableAllThirdPartySharing()
                }
            // Do not add an else branch here. All distributions should be handled deliberately.
            }
        }

        @VisibleForTesting
        internal fun alreadyKnown(settings: Settings): Boolean {
            return settings.adjustCampaignId.isNotEmpty() ||
                settings.adjustNetwork.isNotEmpty() ||
                settings.adjustCreative.isNotEmpty() ||
                settings.adjustAdGroup.isNotEmpty()
        }

        private fun triggerPing() {
            CoroutineScope(Dispatchers.IO).launch {
                Pings.adjustAttribution.submit()
            }
        }
    }
}
