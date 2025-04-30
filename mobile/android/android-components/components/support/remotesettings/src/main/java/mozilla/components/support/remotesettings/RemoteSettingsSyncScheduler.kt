/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.remotesettings

import android.content.Context
import android.os.Build
import androidx.annotation.VisibleForTesting
import androidx.work.Constraints
import androidx.work.ExistingPeriodicWorkPolicy
import androidx.work.NetworkType
import androidx.work.PeriodicWorkRequest
import androidx.work.PeriodicWorkRequestBuilder
import androidx.work.WorkManager
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.base.worker.Frequency
import java.util.concurrent.TimeUnit

/**
 * Defines behavior for scheduling periodic sync for Remote Settings.
 */
interface RemoteSettingsSyncScheduler {
    /**
     * Registers for periodic sync for new Remote Settings.
     */
    fun registerForSync()

    /**
     * Unregisters for periodic sync for new Remote Settings.
     */
    fun unregisterForSync()
}

/**
 * An implementation of [RemoteSettingsSyncScheduler] that uses the [WorkManager] for scheduling sync.
 *
 * @param context The application context.
 * @param frequency The [Frequency] used by the periodic worker.
 */
@SuppressWarnings("MagicNumber")
class DefaultRemoteSettingsSyncScheduler(
    private val context: Context,
    private val frequency: Frequency = Frequency(24, TimeUnit.HOURS),
) : RemoteSettingsSyncScheduler {
    private val logger = Logger("DefaultRemoteSettingsChecker")

    override fun registerForSync() {
        WorkManager.getInstance(context).enqueueUniquePeriodicWork(
            RemoteSettingsSyncWorker.UNIQUE_NAME,
            ExistingPeriodicWorkPolicy.KEEP,
            createPeriodicWorkerRequest(),
        )
        logger.info("Register sync work for Remote Settings")
    }

    override fun unregisterForSync() {
        WorkManager.getInstance(context)
            .cancelUniqueWork(RemoteSettingsSyncWorker.UNIQUE_NAME)
        logger.info("Unregister sync work for Remote Settings")
    }

    /**
     * Creates the [PeriodicWorkRequest] for the DefaultRemoteSettingsChecker.
     */
    @VisibleForTesting
    fun createPeriodicWorkerRequest(): PeriodicWorkRequest {
        return PeriodicWorkRequestBuilder<RemoteSettingsSyncWorker>(
            frequency.repeatInterval,
            frequency.repeatIntervalTimeUnit,
        ).apply {
            setConstraints(getWorkerConstraints())
        }.build()
    }

    private fun getWorkerConstraints() = Constraints.Builder().apply {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            setRequiresDeviceIdle(true)
        }
    }.setRequiresBatteryNotLow(true)
        .setRequiredNetworkType(NetworkType.CONNECTED)
        .build()
}
