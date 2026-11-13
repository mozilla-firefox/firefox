/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.metrics

import androidx.lifecycle.Lifecycle
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.fenix.components.metrics.fake.FakeGleanUsageReporting
import org.mozilla.fenix.components.metrics.fake.FakeLifecycleOwner

class GleanUsageReportingLifecycleObserverTest {

    private val fakeGleanUsageReporting = FakeGleanUsageReporting()

    private var fakeCurrentTime = 0L
    private val fakeCurrentTimeProvider = { ++fakeCurrentTime }

    @Test
    fun `before any lifecycle state changes, no pings are submitted`() {
        createGleanUsageReportingLifecycleObserver()
        assertEquals(0, fakeGleanUsageReporting.pingSubmitCount)
    }

    @Test
    fun `before any lifecycle state changes, no usage reason is set`() {
        createGleanUsageReportingLifecycleObserver()
        assertNull(fakeGleanUsageReporting.lastUsageReason)
    }

    @Test
    fun `when state changed to start, usage reason is set to active`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals("active", fakeGleanUsageReporting.lastUsageReason)
    }

    @Test
    fun `when state changed to start, usage ping is sent`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeGleanUsageReporting.pingSubmitCount)
    }

    @Test
    fun `when state changed to stop, usage reason is set to inactive`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_STOP)
        assertEquals("inactive", fakeGleanUsageReporting.lastUsageReason)
    }

    @Test
    fun `when state changed to stop, usage ping is sent`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeGleanUsageReporting.pingSubmitCount)
    }

    @Test
    fun `when duration is not set, don't submit it`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_STOP)
        assertNull(fakeGleanUsageReporting.lastDurationMillis)
    }

    @Test
    fun `duration is set to the length of the last foreground session`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeCurrentTime)
        fakeCurrentTime++
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_STOP)
        assertEquals(3, fakeCurrentTime)
        assertEquals(2L, fakeGleanUsageReporting.lastDurationMillis)
    }

    @Test
    fun `ON_PAUSE lifecycle event doesn't affect duration`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeCurrentTime)
        fakeCurrentTime++
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_PAUSE)
        assertEquals(2, fakeCurrentTime)
        assertNull(fakeGleanUsageReporting.lastDurationMillis)
    }

    @Test
    fun `ON_DESTROY lifecycle event doesn't affect duration`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeCurrentTime)
        fakeCurrentTime++
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_DESTROY)
        assertEquals(2, fakeCurrentTime)
        assertNull(fakeGleanUsageReporting.lastDurationMillis)
    }

    private fun createGleanUsageReportingLifecycleObserver() =
        GleanUsageReportingLifecycleObserver(
            gleanUsageReportingApi = fakeGleanUsageReporting,
            currentTimeProvider = fakeCurrentTimeProvider,
        )
}
