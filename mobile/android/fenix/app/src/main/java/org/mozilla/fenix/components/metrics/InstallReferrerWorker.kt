/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import android.content.Context
import android.os.RemoteException
import androidx.annotation.VisibleForTesting
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import com.android.installreferrer.api.InstallReferrerClient
import com.android.installreferrer.api.InstallReferrerStateListener
import java.io.UnsupportedEncodingException
import java.net.URLDecoder
import kotlin.coroutines.resume
import kotlinx.coroutines.CancellableContinuation
import kotlinx.coroutines.suspendCancellableCoroutine
import mozilla.components.support.base.log.logger.Logger
import mozilla.telemetry.glean.Glean
import mozilla.telemetry.glean.GleanTimerId
import mozilla.telemetry.glean.internal.AttributionMetrics
import org.json.JSONException
import org.json.JSONObject
import org.mozilla.fenix.GleanMetrics.MetaAttribution
import org.mozilla.fenix.GleanMetrics.Pings
import org.mozilla.fenix.GleanMetrics.PlayStoreAttribution
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.utils.Settings

/**
 * A WorkManager Worker that handles retrying install referrer attribution requests. This worker uses exponential
 * backoff for retries.
 *
 * The [UTMParams] and/or [MetaParams] are derived from the install referrer URL and stored in settings.
 *
 * @param context The application context.
 * @param workerParameters Setup parameters for a [CoroutineWorker].
 */
class InstallReferrerWorker(
    context: Context,
    workerParameters: WorkerParameters,
) : CoroutineWorker(context, workerParameters) {

    private val settings = context.components.settings

    override suspend fun doWork(): Result {
        val referrerClient = DefaultInstallReferrerClient(applicationContext)
        val (responseCode, referrerResponse) = fetchInstallReferrer(referrerClient)

        return when (responseCode) {
            InstallReferrerClient.InstallReferrerResponse.OK -> {
                handleSuccess(referrerResponse, responseCode, settings)
                Result.success()
            }

            InstallReferrerClient.InstallReferrerResponse.SERVICE_DISCONNECTED,
            InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE -> {
                if (shouldRetry(runAttemptCount)) {
                    Result.retry()
                } else {
                    markAsComplete(responseCode)
                    Result.failure()
                }
            }

            else -> {
                markAsComplete(responseCode)
                Result.failure()
            }
        }
    }

    @VisibleForTesting
    internal fun handleSuccess(
        installReferrerResponse: String?,
        responseCode: Int,
        settings: Settings,
    ) {
        if (!installReferrerResponse.isNullOrBlank()) {
            var utmParams = UTMParams.parseUTMParameters(installReferrerResponse)

            // A referral code must not reach attribution data, so it is pulled out and reported on
            // its own ping before anything else consumes utm_content. See bug 2062793.
            ReferralAttribution.referralCodeFrom(utmParams.content)?.let { referralCode ->
                ReferralAttribution.submit(referralCode, settings)
                utmParams = utmParams.copy(content = "")
            }

            PlayStoreAttribution.installReferrerResponse.set(ReferralAttribution.redact(installReferrerResponse))

            val metaParams = MetaParams.extractMetaAttribution(utmParams.content)
            if (metaParams != null) {
                settings.isUserMetaAttributed = true
                metaParams.recordMetaAttribution()
            } else {
                settings.isUserMetaAttributed = false
            }

            settings.isUserTikTokAttributed =
                InstallReferrerHandlingService.isTikTokAttribution(installReferrerResponse)

            settings.isUserRedditAttributed =
                InstallReferrerHandlingService.isRedditAttribution(installReferrerResponse)

            settings.isUserXTwitterAttributed =
                InstallReferrerHandlingService.isXTwitterAttribution(installReferrerResponse)

            settings.isUserMolocoAttributed =
                InstallReferrerHandlingService.isMolocoAttribution(installReferrerResponse)

            settings.isUserRakutenAttributed =
                InstallReferrerHandlingService.isRakutenAttribution(installReferrerResponse)

            settings.isUserSkyflagAttributed =
                InstallReferrerHandlingService.isSkyflagAttribution(installReferrerResponse)

            settings.isUserQuoraAttributed = InstallReferrerHandlingService.isQuoraAttribution(installReferrerResponse)

            PlayStoreAttribution.pairingCampaign.set(
                InstallReferrerHandlingService.isPairingCampaignAttribution(
                    installReferrerResponse,
                    FxNimbus.features.pairingSigninPrompt.value().campaignUtmContent,
                )
            )

            utmParams.recordInstallReferrer(settings)
        }

        markAsComplete(responseCode)
    }

    private fun markAsComplete(responseCode: Int) {
        settings.utmParamsKnown = true
        PlayStoreAttribution.responseCode.set(responseCode.toString())
        Pings.playStoreAttribution.submit()
    }

    companion object {
        private val logger = Logger("InstallReferrerWorker")

        @VisibleForTesting
        internal fun shouldRetry(runAttemptCount: Int): Boolean {
            return runAttemptCount < InstallReferrerMetricsService.MAX_RETRIES
        }

        @VisibleForTesting
        internal suspend fun fetchInstallReferrer(referrerClient: InstallReferrerClientWrapper): Pair<Int, String?> {
            val timerId = PlayStoreAttribution.attributionTime.start()

            return suspendCancellableCoroutine { continuation ->
                continuation.invokeOnCancellation {
                    safelyEndConnection(referrerClient)
                }

                val listener = createReferrerStateListener(timerId, referrerClient, continuation)
                referrerClient.startConnection(listener)
            }
        }

        private fun createReferrerStateListener(
            timerId: GleanTimerId,
            referrerClient: InstallReferrerClientWrapper,
            continuation: CancellableContinuation<Pair<Int, String?>>,
        ) =
            object : InstallReferrerStateListener {
                override fun onInstallReferrerSetupFinished(responseCode: Int) {
                    PlayStoreAttribution.attributionTime.stopAndAccumulate(timerId)
                    if (!continuation.isActive) {
                        safelyEndConnection(referrerClient)
                        return
                    }
                    val result =
                        try {
                            Pair(responseCode, getReferrerResponseIfOk(responseCode, referrerClient))
                        } catch (e: IllegalStateException) {
                            logger.error("Install referrer client not ready", e)
                            Pair(InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE, null)
                        }
                    safelyEndConnection(referrerClient)
                    continuation.resume(result)
                }

                override fun onInstallReferrerServiceDisconnected() {
                    safelyEndConnection(referrerClient)
                    if (!continuation.isActive) {
                        return
                    }
                    continuation.resume(
                        Pair(
                            InstallReferrerClient.InstallReferrerResponse.SERVICE_UNAVAILABLE,
                            null,
                        )
                    )
                }
            }

        private fun getReferrerResponseIfOk(
            responseCode: Int,
            referrerClient: InstallReferrerClientWrapper,
        ): String? {
            if (responseCode != InstallReferrerClient.InstallReferrerResponse.OK) {
                return null
            }

            return try {
                referrerClient.getInstallReferrer()
            } catch (e: RemoteException) {
                logger.error("Failed to retrieve install referrer", e)
                null
            } catch (e: SecurityException) {
                logger.error("Failed to retrieve install referrer", e)
                null
            }
        }

        @Suppress("TooGenericExceptionCaught")
        private fun safelyEndConnection(referrerClient: InstallReferrerClientWrapper) {
            try {
                referrerClient.endConnection()
            } catch (e: Exception) {
                logger.error("Failed to end connection", e)
            }
        }
    }
}

/**
 * Descriptions of utm parameters comes from https://support.google.com/analytics/answer/1033863
 * - utm_source Identify the advertiser, site, publication, etc. that is sending traffic to your property, for example:
 *   google, newsletter4, billboard.
 * - utm_medium The advertising or marketing medium, for example: cpc, banner, email newsletter. utm_campaign The
 *   individual campaign name, slogan, promo code, etc. for a product.
 * - utm_term Identify paid search keywords. If you're manually tagging paid keyword campaigns, you should also use
 *   utm_term to specify the keyword.
 * - utm_content Used to differentiate similar content, or links within the same ad. For example, if you have two
 *   call-to-action links within the same email message, you can use utm_content and set different values for each so
 *   you can tell which version is more effective.
 */
data class UTMParams(
    val source: String,
    val medium: String,
    val campaign: String,
    val content: String,
    val term: String,
) {

    companion object {
        const val UTM_SOURCE = "utm_source"
        const val UTM_MEDIUM = "utm_medium"
        const val UTM_CAMPAIGN = "utm_campaign"
        const val UTM_CONTENT = "utm_content"
        const val UTM_TERM = "utm_term"

        /** Try and unpack the install referrer response. */
        fun parseInstallReferrer(installReferrerResponse: String): Map<String, String> {
            val params = mutableMapOf<String, String>()
            for (param in installReferrerResponse.split("&")) {
                val keyValue = param.split("=", limit = 2)
                if (keyValue.size == 2) {
                    val key = keyValue[0]
                    val value = keyValue[1]
                    params[key] = value
                }
            }
            return params
        }

        /** Extract the [UTMParams] from the install referrer response. */
        fun parseUTMParameters(installReferrerResponse: String): UTMParams {
            val utmParams = parseInstallReferrer(installReferrerResponse)
            return UTMParams(
                source = utmParams[UTM_SOURCE] ?: "",
                medium = utmParams[UTM_MEDIUM] ?: "",
                campaign = utmParams[UTM_CAMPAIGN] ?: "",
                content = utmParams[UTM_CONTENT] ?: "",
                term = utmParams[UTM_TERM] ?: "",
            )
        }

        /** Derive the set of UTM parameters stored in Settings. */
        fun fromSettings(settings: Settings): UTMParams =
            with(settings) {
                UTMParams(
                    source = utmSource,
                    medium = utmMedium,
                    campaign = utmCampaign,
                    content = utmContent,
                    term = utmTerm,
                )
            }
    }

    /** Persist the UTM params into Settings. */
    fun intoSettings(settings: Settings) {
        with(settings) {
            utmSource = source
            utmMedium = medium
            utmCampaign = campaign
            utmTerm = term
            utmContent = content
        }
    }

    /**
     * Check if this UTM param is empty
     *
     * @Return [Boolean] true if none of the utm params are set.
     */
    fun isEmpty(): Boolean {
        return source.isBlank() && medium.isBlank() && campaign.isBlank() && term.isBlank() && content.isBlank()
    }

    /**
     * record UTM params into settings and telemetry
     *
     * @param settings [Settings] application settings.
     */
    fun recordInstallReferrer(settings: Settings) {
        if (isEmpty()) {
            return
        }
        intoSettings(settings)

        PlayStoreAttribution.source.set(source)
        PlayStoreAttribution.medium.set(medium)
        PlayStoreAttribution.campaign.set(campaign)
        PlayStoreAttribution.content.set(content)
        PlayStoreAttribution.term.set(term)

        Glean.updateAttribution(
            AttributionMetrics(
                source = source,
                medium = medium,
                campaign = campaign,
                term = term,
                content = content,
            )
        )
    }
}

/**
 * Descriptions of Meta attribution parameters comes from
 * https://developers.facebook.com/docs/marketing-api/reference/ad-campaign#fields
 *
 * @property app the ID of application in the referrer response.
 * @property t the value of user interaction in the referrer response.
 * @property data the encrypted data in the referrer response.
 * @property nonce the nonce for decrypting [data] in the referrer response.
 */
data class MetaParams(
    val app: String,
    val t: String,
    val data: String,
    val nonce: String,
) {
    companion object {
        private val logger = Logger("MetaParams")
        private const val APP = "app"
        private const val T = "t"
        private const val SOURCE = "source"
        private const val DATA = "data"
        private const val NONCE = "nonce"

        @Suppress("ReturnCount")
        internal fun extractMetaAttribution(contentString: String?): MetaParams? {
            if (contentString.isNullOrBlank()) {
                return null
            }
            val decodedContentString =
                try {
                    // content string can be in percent format
                    URLDecoder.decode(contentString, "UTF-8")
                } catch (e: UnsupportedEncodingException) {
                    logger.error("failed to decode content string", e)
                    // can't recover from this
                    return null
                }

            val data: String
            val nonce: String

            val contentJson =
                try {
                    JSONObject(decodedContentString)
                } catch (e: JSONException) {
                    logger.error("content is not JSON", e)
                    // can't recover from this
                    return null
                }

            val app =
                try {
                    contentJson.optString(APP) ?: ""
                } catch (e: JSONException) {
                    logger.error("failed to extract app", e)
                    // this is an acceptable outcome
                    ""
                }

            val t =
                try {
                    contentJson.optString(T) ?: ""
                } catch (e: JSONException) {
                    logger.error("failed to extract t", e)
                    // this is an acceptable outcome
                    ""
                }

            try {
                val source = contentJson.optJSONObject(SOURCE)
                data = source?.optString(DATA) ?: ""
                nonce = source?.optString(NONCE) ?: ""
            } catch (e: JSONException) {
                logger.error("failed to extract data or nonce", e)
                // can't recover from this
                return null
            }

            if (data.isBlank() || nonce.isBlank()) {
                return null
            }

            return MetaParams(
                app = app,
                t = t,
                data = data,
                nonce = nonce,
            )
        }
    }

    /** record META attribution params to telemetry */
    fun recordMetaAttribution() {
        MetaAttribution.app.set(app)
        MetaAttribution.t.set(t)
        MetaAttribution.data.set(data)
        MetaAttribution.nonce.set(nonce)
    }
}
