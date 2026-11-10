/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.work.Constraints
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.NetworkType
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import java.util.concurrent.TimeUnit
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.worker.Frequency

/**
 * Provides functionality to schedule updates of the Merino sports matches.
 *
 * @property context A reference to the application context.
 * @property provider An instance of [MerinoSportsProvider] which will fetch the matches.
 * @property storage An instance of [MerinoMatchesStorage] which will store the fetched matches.
 * @property frequency Optional [Frequency] that specifies how often the match updates should happen.
 */
class MerinoSportsUpdater(
    private val context: Context,
    private val provider: MerinoSportsProvider,
    private val storage: MerinoMatchesStorage,
    private val frequency: Frequency = Frequency(1, TimeUnit.HOURS),
) {

    private val logger = Logger("MerinoSportsUpdater")

    /**
     * Starts a work request in the background to periodically update the Merino sports matches.
     *
     * This must be called on every app start, such as from `Application.onCreate`, because the dependencies used by the
     * scheduled work do not survive process death while the scheduled work does.
     */
    fun startPeriodicWork() {
        MerinoSportsUseCases.initialize(provider, storage)

        WorkManager.getInstance(context)
            .enqueueUniquePeriodicWork(
                PERIODIC_WORK_TAG,
                ExistingPeriodicWorkPolicy.KEEP,
                createPeriodicWorkRequest(),
            )

        logger.info("Started periodic work to update Merino sports matches")
    }

    /** Stops the work request to periodically update the Merino sports matches. */
    fun stopPeriodicWork() {
        MerinoSportsUseCases.destroy()

        WorkManager.getInstance(context).cancelUniqueWork(PERIODIC_WORK_TAG)

        logger.info("Stopped periodic work to update Merino sports matches")
    }

    @VisibleForTesting
    internal fun createPeriodicWorkRequest() =
        PeriodicWorkRequestBuilder<MerinoSportsUpdaterWorker>(
                repeatInterval = frequency.repeatInterval,
                repeatIntervalTimeUnit = frequency.repeatIntervalTimeUnit,
            )
            .apply {
                setConstraints(getWorkerConstraints())
                addTag(PERIODIC_WORK_TAG)
            }
            .build()

    @VisibleForTesting
    internal fun getWorkerConstraints() =
        Constraints.Builder().setRequiresStorageNotLow(true).setRequiredNetworkType(NetworkType.CONNECTED).build()

    companion object {
        internal const val PERIODIC_WORK_TAG = "mozilla.components.service.merino.sports.periodicWork"
    }
}
