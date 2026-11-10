/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.collections.migration

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.work.BackoffPolicy
import androidx.work.Constraints
import androidx.work.CoroutineWorker
import androidx.work.ExistingWorkPolicy
import androidx.work.OneTimeWorkRequestBuilder
import androidx.work.WorkManager
import androidx.work.WorkerParameters
import java.util.concurrent.TimeUnit
import kotlin.time.Duration.Companion.minutes
import kotlin.time.Duration.Companion.seconds
import org.mozilla.fenix.ext.components

@VisibleForTesting internal const val MIGRATION_WORK_NAME = "org.mozilla.fenix.collections.migration.work"

@VisibleForTesting internal const val MAX_RUN_ATTEMPTS = 5
private val INITIAL_DELAY_MILLIS = 15.seconds.inWholeMilliseconds
private val BACKOFF_DELAY_MINUTES = 30.minutes.inWholeMinutes

/** [CoroutineWorker] that migrates the stored Collections into Tab Groups. */
class CollectionsToTabGroupsMigrationWorker(
    context: Context,
    workerParameters: WorkerParameters,
) : CoroutineWorker(context, workerParameters) {

    override suspend fun doWork(): Result {
        val components = applicationContext.components

        val migrated =
            CollectionsToTabGroupsMigration(
                    tabCollectionStorage = components.core.tabCollectionStorage,
                    restoreUseCase = components.useCases.tabsUseCases.restore,
                    tabGroupRepository = components.core.tabGroupRepository,
                    collectionsMigrationRepository = components.collectionsMigrationRepository,
                    engine = components.core.engine,
                    filesDir = applicationContext.filesDir,
                    crashReporter = components.analytics.crashReporter,
                )
                .migrateIfNeeded()

        return when {
            migrated -> Result.success()
            // Give up on this work request once the attempts are exhausted to avoid retrying a migration
            // that keeps failing. A subsequent launch will enqueue a new work request.
            runAttemptCount + 1 >= MAX_RUN_ATTEMPTS -> Result.failure()
            else -> Result.retry()
        }
    }

    companion object {

        /**
         * Schedules the migration as a background task.
         *
         * @param context The [Context] used to enqueue the work.
         * @param collectionsMigrationRepository [CollectionsMigrationRepository] for reading the migration state.
         */
        fun enqueueIfNeeded(context: Context, collectionsMigrationRepository: CollectionsMigrationRepository) {
            if (!collectionsMigrationRepository.shouldMigrate()) {
                return
            }

            val migrationWork =
                OneTimeWorkRequestBuilder<CollectionsToTabGroupsMigrationWorker>()
                    .setConstraints(getWorkerConstraints())
                    .setInitialDelay(INITIAL_DELAY_MILLIS, TimeUnit.MILLISECONDS)
                    .setBackoffCriteria(BackoffPolicy.EXPONENTIAL, BACKOFF_DELAY_MINUTES, TimeUnit.MINUTES)
                    .build()

            WorkManager.getInstance(context)
                .enqueueUniqueWork(
                    MIGRATION_WORK_NAME,
                    ExistingWorkPolicy.KEEP,
                    migrationWork,
                )
        }

        private fun getWorkerConstraints() =
            Constraints.Builder().setRequiresBatteryNotLow(true).setRequiresStorageNotLow(true).build()
    }
}
