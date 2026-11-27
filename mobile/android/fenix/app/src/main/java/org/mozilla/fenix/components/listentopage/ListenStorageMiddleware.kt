/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import android.annotation.SuppressLint
import android.app.usage.StorageStatsManager
import android.content.Context
import android.content.pm.PackageManager
import android.os.Process
import android.os.storage.StorageManager
import androidx.core.content.getSystemService
import java.io.IOException
import java.util.UUID
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import org.mozilla.fenix.GleanMetrics.ListenToPage

/**
 * [Middleware] that records how much room the audio cache has when a listening session starts, since the system may
 * clear the cache directory when storage runs low.
 *
 * @param readStorage Reads the storage of the cache directory, or returns `null` when it cannot. It reads the disk, so
 *   it is called on [ioDispatcher].
 * @param scope The scope the reading is launched in.
 * @param ioDispatcher The dispatcher the reading runs on.
 */
class ListenStorageMiddleware(
    private val readStorage: () -> CacheStorage?,
    private val scope: CoroutineScope,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : Middleware<ListenState, ListenAction> {

    override fun invoke(
        store: Store<ListenState, ListenAction>,
        next: (ListenAction) -> Unit,
        action: ListenAction,
    ) {
        next(action)

        if (action is ListenAction.User.Session.ListenRequested) {
            scope.launch(ioDispatcher) { readStorage()?.record() }
        }
    }
}

/**
 * The storage of the app's cache directory.
 *
 * @property quotaBytes The cache quota the system gives the app. Apps over their quota are cleared first.
 * @property freeBytes The free space on the storage that holds the cache directory.
 * @property allocatableBytes The space the system could make available there, which also counts cached data it could
 *   clear.
 * @property appCacheBytes How much the app's whole cache takes up, or `null` when the system cannot report it.
 */
data class CacheStorage(
    val quotaBytes: Long,
    val freeBytes: Long,
    val allocatableBytes: Long,
    val appCacheBytes: Long?,
) {

    /**
     * Records them in megabytes to two significant figures: precise where storage is nearly full, which is the case
     * that matters, and too coarse where it is plentiful to tell devices apart.
     */
    internal fun record() {
        ListenToPage.cacheQuota.accumulate(quotaBytes.toMegabytesToTwoSignificantFigures())
        ListenToPage.cacheFreeSpace.accumulate(freeBytes.toMegabytesToTwoSignificantFigures())
        ListenToPage.cacheAllocatableSpace.accumulate(allocatableBytes.toMegabytesToTwoSignificantFigures())
        appCacheBytes?.let { ListenToPage.appCacheSize.accumulate(it.toMegabytesToTwoSignificantFigures()) }
    }

    companion object {
        /**
         * Reads the storage of [context]'s cache directory, or returns `null` when the system cannot report it.
         *
         * Free space is what is actually free, which is what the system watches before it clears caches. Allocatable
         * space also counts cached data the system could clear, ours included, so the gap between the two is how much
         * of the headroom depends on clearing caches.
         */
        @SuppressLint("UsableSpace")
        fun from(context: Context): CacheStorage? {
            val storageManager = context.getSystemService<StorageManager>() ?: return null
            val cacheDir = context.cacheDir
            return try {
                val uuid = storageManager.getUuidForPath(cacheDir)
                CacheStorage(
                    quotaBytes = storageManager.getCacheQuotaBytes(uuid),
                    freeBytes = cacheDir.usableSpace,
                    allocatableBytes = storageManager.getAllocatableBytes(uuid),
                    appCacheBytes = context.appCacheBytes(uuid),
                )
            } catch (_: IOException) {
                null
            }
        }

        private fun Context.appCacheBytes(uuid: UUID): Long? =
            try {
                getSystemService<StorageStatsManager>()
                    ?.queryStatsForPackage(uuid, packageName, Process.myUserHandle())
                    ?.cacheBytes
            } catch (_: PackageManager.NameNotFoundException) {
                null
            }
    }
}

private fun Long.toMegabytesToTwoSignificantFigures(): Long {
    val megabytes = (this + BYTES_PER_MEGABYTE / 2) / BYTES_PER_MEGABYTE
    var step = 1L
    while (megabytes / step >= TWO_SIGNIFICANT_FIGURES_LIMIT) step *= DECIMAL_BASE
    return (megabytes + step / 2) / step * step
}

private const val BYTES_PER_MEGABYTE = 1024L * 1024L
private const val TWO_SIGNIFICANT_FIGURES_LIMIT = 100L
private const val DECIMAL_BASE = 10L
