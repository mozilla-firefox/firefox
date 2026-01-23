/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.concept.storage

import android.content.Context
import androidx.work.CoroutineWorker
import androidx.work.WorkerParameters
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * An abstract WorkManager Worker class that executes maintenance task provided via [operate].
 *
 * If there is a failure or the constraints of worker are no longer met during execution,
 * cancellation tasks are executed via [onError].
 */
abstract class StorageMaintenanceWorker(context: Context, params: WorkerParameters) :
    CoroutineWorker(context, params) {

    @Suppress("TooGenericExceptionCaught")
    final override suspend fun doWork(): Result = withContext(Dispatchers.IO) {
        try {
            operate()
            Result.success()
        } catch (exception: Exception) {
            onError(exception)
            Result.failure()
        }
    }

    /**
     * Called when [doWork] is being executed.
     * */
    abstract suspend fun operate()

    /**
     * Called when [doWork] causes an exception while being executed.
     *
     * @param exception Exception is passed to the child overriding the method.
     * */
    abstract fun onError(exception: Exception)

    companion object {
        const val WORKER_PERIOD_IN_HOURS = 12L
    }
}
