/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.remotesettings

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import mozilla.components.support.base.log.logger.Logger

/**
 * Worker that uses [WorkManager] APIs to sync the Remote Settings.
 *
 * @param context The application context.
 * @param workerParameters The [WorkerParameters] for the RemoteSettingsSyncWorker.
 */
class RemoteSettingsSyncWorker(
    context: Context,
    workerParameters: WorkerParameters,
) : CoroutineWorker(context, workerParameters) {

    val logger = Logger("RemoteSettingsSyncWorker")

    @SuppressWarnings("TooGenericExceptionCaught")
    override suspend fun doWork(): Result {
        try {
            val remoteSettingsService = GlobalRemoteSettingsDependencyProvider
                .requireRemoteSettingsService().remoteSettingsService
            remoteSettingsService.sync()
            return Result.success()
        } catch (exception: Exception) {
            return Result.failure()
        }
    }

    /**
     * Companion object for holding important const strings.
     */
    companion object {
        private const val IDENTIFIER_PREFIX = "mozilla.components.support.remotesettings"

        /**
         * Identifies all the workers that periodically sync with Remote Settings.
         */
        internal const val UNIQUE_NAME = "$IDENTIFIER_PREFIX.RemoteSettingsSyncWorker"

        /**
         * Testing tag for worker.
         */
        @VisibleForTesting
        internal const val REMOTE_SETTINGS_SYNC_WORKER_TAG = UNIQUE_NAME
    }
}
