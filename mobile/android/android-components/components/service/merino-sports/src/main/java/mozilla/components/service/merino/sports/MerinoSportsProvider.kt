/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.annotation.WorkerThread
import mozilla.appservices.merino.MerinoWrexhamApiException
import mozilla.appservices.merino.WrexhamClient
import mozilla.appservices.merino.WrexhamClientInterface
import mozilla.appservices.merino.WrexhamConfig
import mozilla.appservices.merino.WrexhamMatchesOptions
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.support.base.log.logger.Logger

/**
 * Provides access to the Merino sports API through the Application Services [WrexhamClient].
 *
 * @property client The [WrexhamClientInterface] used to request the matches.
 * @property crashReporter [CrashReporting] instance used for recording caught exceptions.
 */
class MerinoSportsProvider(
    private val client: Lazy<WrexhamClientInterface> = lazy { WrexhamClient(WrexhamConfig(baseHost = null)) },
    private val crashReporter: CrashReporting? = null,
) {

    private val logger = Logger("MerinoSportsProvider")

    /**
     * Fetches the previous, current and upcoming matches.
     *
     * @param limit Optional maximum number of previous and upcoming matches to return. The Merino service defaults to 5
     *   when not provided or not positive.
     * @return The [MerinoMatches] or null if the request or deserialization failed.
     */
    @WorkerThread
    fun getMatches(limit: Int? = null): MerinoMatches? {
        val response =
            try {
                client.value.getMatches(WrexhamMatchesOptions(limit = limit?.takeIf { it > 0 }?.toUInt()))
            } catch (e: MerinoWrexhamApiException) {
                val message = "MerinoSportsProvider - Failed to fetch matches"
                logger.error(message = message, throwable = e)
                crashReporter?.recordCrashBreadcrumb(Breadcrumb(message = message))

                // Only report the unexpected errors to avoid reports of network-level failure (e.g. connection
                // timeout, offline) or of Merino having no data to serve.
                if (e is MerinoWrexhamApiException.Other) {
                    crashReporter?.submitCaughtException(e)
                }

                return null
            }

        return try {
            merinoSportsJson.decodeFromString<MerinoMatches>(response)
        } catch (e: IllegalArgumentException) {
            val message = "MerinoSportsProvider - Failed to deserialize matches"
            logger.error(message = message, throwable = e)
            crashReporter?.recordCrashBreadcrumb(Breadcrumb(message = message))
            crashReporter?.submitCaughtException(e)

            null
        }
    }
}
