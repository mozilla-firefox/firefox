/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.listenReducer
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.ListenToPage
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@OptIn(ExperimentalCoroutinesApi::class)
@RunWith(RobolectricTestRunner::class)
class ListenStorageMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private var reads = 0

    @Test
    fun `WHEN listening is requested THEN the cache storage is recorded in megabytes to two significant figures`() {
        val store =
            createStore(
                CacheStorage(
                    quotaBytes = 126 * MEGABYTE,
                    freeBytes = 47 * MEGABYTE + MEGABYTE * 6 / 10,
                    allocatableBytes = 5_432 * MEGABYTE,
                    appCacheBytes = 54_321 * MEGABYTE,
                )
            )

        store.dispatch(LISTEN_REQUESTED)

        // Glean keeps memory in bytes whatever unit it was given in.
        assertEquals(130 * MEGABYTE, ListenToPage.cacheQuota.testGetValue()?.sum)
        assertEquals(48 * MEGABYTE, ListenToPage.cacheFreeSpace.testGetValue()?.sum)
        assertEquals(5_400 * MEGABYTE, ListenToPage.cacheAllocatableSpace.testGetValue()?.sum)
        assertEquals(54_000 * MEGABYTE, ListenToPage.appCacheSize.testGetValue()?.sum)
    }

    @Test
    fun `WHEN the app's cache size cannot be read THEN the rest of the storage is still recorded`() {
        val store =
            createStore(
                CacheStorage(
                    quotaBytes = MEGABYTE,
                    freeBytes = MEGABYTE,
                    allocatableBytes = MEGABYTE,
                    appCacheBytes = null,
                )
            )

        store.dispatch(LISTEN_REQUESTED)

        assertEquals(1L, ListenToPage.cacheQuota.testGetValue()?.count)
        assertNull(ListenToPage.appCacheSize.testGetValue())
    }

    @Test
    fun `WHEN the storage cannot be read THEN nothing is recorded`() {
        val store = createStore(storage = null)

        store.dispatch(LISTEN_REQUESTED)

        assertEquals(1, reads)
        assertNull(ListenToPage.cacheQuota.testGetValue())
        assertNull(ListenToPage.cacheFreeSpace.testGetValue())
    }

    @Test
    fun `WHEN other actions are dispatched THEN the storage is not read`() {
        val store =
            createStore(
                CacheStorage(
                    quotaBytes = MEGABYTE,
                    freeBytes = MEGABYTE,
                    allocatableBytes = MEGABYTE,
                    appCacheBytes = MEGABYTE,
                )
            )

        store.dispatch(ListenAction.User.Session.StopRequested())
        store.dispatch(ListenAction.System.Playback.PlaybackWaiting)

        assertEquals(0, reads)
        assertNull(ListenToPage.cacheQuota.testGetValue())
    }

    private fun createStore(storage: CacheStorage?): ListenStore {
        val dispatcher = UnconfinedTestDispatcher()
        val middleware =
            ListenStorageMiddleware(
                readStorage = {
                    reads++
                    storage
                },
                scope = CoroutineScope(dispatcher),
                ioDispatcher = dispatcher,
            )
        return ListenStore(initialState = ListenState(), reducer = ::listenReducer, middleware = listOf(middleware))
    }

    private companion object {
        const val MEGABYTE = 1024L * 1024L
        val LISTEN_REQUESTED = ListenAction.User.Session.ListenRequested(tabId = "tab", url = "https://example.com")
    }
}
