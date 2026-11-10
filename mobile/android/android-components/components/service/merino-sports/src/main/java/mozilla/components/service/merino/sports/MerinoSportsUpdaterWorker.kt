/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import kotlin.coroutines.cancellation.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import mozilla.components.support.base.log.logger.Logger

/** An implementation of [CoroutineWorker] to perform Merino sports matches updates. */
internal class MerinoSportsUpdaterWorker(
    context: Context,
    params: WorkerParameters,
) : CoroutineWorker(context, params) {

    private val logger = Logger("MerinoSportsUpdaterWorker")

    @Suppress("TooGenericExceptionCaught")
    override suspend fun doWork(): Result =
        withContext(Dispatchers.IO) {
            try {
                when {
                    MerinoSportsUseCases().refreshMatches.invoke() -> Result.success()
                    runAttemptCount < MAX_RETRIES -> Result.retry()
                    else -> Result.failure()
                }
            } catch (e: CancellationException) {
                throw e
            } catch (e: Exception) {
                logger.error("Failed to refresh Merino sports matches", e)
                Result.failure()
            }
        }

    internal companion object {
        /** Maximum number of times a failed fetch is retried before waiting for the next periodic run. */
        internal const val MAX_RETRIES = 3
    }
}
