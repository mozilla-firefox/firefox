/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.perf

import android.app.usage.StorageStats
import android.app.usage.StorageStatsManager
import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.annotation.VisibleForTesting.Companion.PRIVATE
import androidx.annotation.WorkerThread
import androidx.core.content.getSystemService
import kotlinx.coroutines.DelicateCoroutinesApi
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.GlobalScope
import kotlinx.coroutines.launch
import mozilla.components.support.base.log.logger.Logger
import org.mozilla.fenix.GleanMetrics.StorageStats as Metrics

/**
 * A collection of functions related to measuring the [StorageStats] of the application such as data
 * dir size.
 *
 * Unfortunately, this API is only available on API 26+ so the data will only be reported for those
 * platforms.
 */
object StorageStatsMetrics {

    private val logger = Logger("StorageStatsMetrics")

    @OptIn(DelicateCoroutinesApi::class) // GlobalScope usage
    fun report(context: Context) {
        GlobalScope.launch(Dispatchers.IO) {
            reportSync(context)
        }
    }

    // I couldn't get runBlockingTest to work correctly so I moved the functionality under test to
    // a synchronous function.
    @VisibleForTesting(otherwise = PRIVATE)
    @WorkerThread // queryStatsForUid
    fun reportSync(context: Context) {
        // I don't expect this to ever be null so we don't report if so.
        context.getSystemService<StorageStatsManager>()?.let { storageStatsManager ->
            val appInfo = context.applicationInfo
            val storageStats = Metrics.queryStatsDuration.measure {
                // The docs say queryStatsForPackage may be slower if the app uses
                // android:sharedUserId so we the suggested alternative.
                //
                // The docs say this may be slow:
                // > This method may take several seconds to complete, so it should only be called
                // > from a worker thread.
                //
                // So we call from a worker thread and measure the duration to make sure it's not
                // too slow.

                // Bug 2008785/2061436 - we are getting unusual crashes that seem to be related to the
                // sharedUserId causing a RuntimeException/IOException when querying storage stats.
                // We catch and swallow the exception since this information is used only for telemetry
                // and not used for anything functional, we should not crash if we get into that situation.
                // That unfortunately means we have to try-catch generic exception types.
                @Suppress("TooGenericExceptionCaught")
                try {
                    storageStatsManager.queryStatsForUid(appInfo.storageUuid, appInfo.uid)
                } catch (exception: RuntimeException) {
                    logger.error("Failed to query storage stats", exception)
                    null
                } catch (exception: java.io.IOException) {
                    logger.error("Failed to query storage stats", exception)
                    null
                }
            } ?: return@let

            // dataBytes includes the cache so we subtract it.
            val justDataDirBytes = storageStats.dataBytes - storageStats.cacheBytes

            Metrics.dataDirBytes.accumulate(justDataDirBytes)
            Metrics.appBytes.accumulate(storageStats.appBytes)
            Metrics.cacheBytes.accumulate(storageStats.cacheBytes)
        }
    }
}
