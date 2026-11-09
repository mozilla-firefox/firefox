/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import androidx.annotation.WorkerThread
import androidx.core.net.toUri
import mozilla.components.concept.base.crash.Breadcrumb
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.concept.fetch.Client
import mozilla.components.concept.fetch.Request
import mozilla.components.support.base.ext.fetchBodyOrNull
import mozilla.components.support.base.log.logger.Logger

internal const val MATCHES_ENDPOINT_URL = "https://merino.services.mozilla.com/api/v1/widgets/wrexham/matches"

/**
 * Provides access to the Merino sports API.
 *
 * @property client [Client] used for interacting with the Merino HTTP API.
 * @property matchesEndPointUrl The url of the matches endpoint to fetch from. Defaults to [MATCHES_ENDPOINT_URL].
 * @property crashReporter [CrashReporting] instance used for recording caught exceptions.
 */
class MerinoSportsProvider(
    private val client: Client,
    private val matchesEndPointUrl: String = MATCHES_ENDPOINT_URL,
    private val crashReporter: CrashReporting? = null,
) {

    private val logger = Logger("MerinoSportsProvider")

    /**
     * Fetches the previous, current and upcoming matches from [matchesEndPointUrl].
     *
     * @param limit Optional maximum number of previous and upcoming matches to return. The Merino service defaults to 5
     *   when not provided.
     * @return The [MerinoMatches] or null if the request or deserialization failed.
     */
    @WorkerThread
    fun getMatches(limit: Int? = null): MerinoMatches? {
        val request = Request(url = buildMatchesUrl(limit), conservative = true)

        return client.fetchBodyOrNull(request = request, logger = logger)?.let { response ->
            try {
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

    private fun buildMatchesUrl(limit: Int?): String {
        if (limit == null) {
            return matchesEndPointUrl
        }

        return matchesEndPointUrl
            .toUri()
            .buildUpon()
            .appendQueryParameter(LIMIT_QUERY_PARAM, limit.toString())
            .build()
            .toString()
    }

    private companion object {
        const val LIMIT_QUERY_PARAM = "limit"
    }
}
