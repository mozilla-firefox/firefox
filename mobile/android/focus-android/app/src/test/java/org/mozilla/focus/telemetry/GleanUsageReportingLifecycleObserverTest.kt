/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.focus.telemetry

import androidx.lifecycle.Lifecycle
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.mozilla.focus.telemetry.fake.FakeGleanUsageReporting
import org.mozilla.focus.telemetry.fake.FakeLifecycleOwner

class GleanUsageReportingLifecycleObserverTest {

    private val fakeGleanUsageReportingApi = FakeGleanUsageReporting()
    private var fakeCurrentTime = 0L
    private val fakeCurrentTimeProvider = { ++fakeCurrentTime }

    @Test
    fun `before any lifecycle state changes, no pings are submitted`() {
        createGleanUsageReportingLifecycleObserver()
        assertEquals(0, fakeGleanUsageReportingApi.pingSubmitCount)
    }

    @Test
    fun `before any lifecycle state changes, no usage reason is set`() {
        createGleanUsageReportingLifecycleObserver()
        assertNull(fakeGleanUsageReportingApi.lastUsageReason)
    }

    @Test
    fun `when state changed to start, usage reason is set to active`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals("active", fakeGleanUsageReportingApi.lastUsageReason)
    }

    @Test
    fun `when state changed to start, usage ping is sent`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeGleanUsageReportingApi.pingSubmitCount)
    }

    @Test
    fun `when state changed to stop, usage reason is set to inactive`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_STOP)
        assertEquals("inactive", fakeGleanUsageReportingApi.lastUsageReason)
    }

    @Test
    fun `when state changed to stop, usage ping is sent`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeGleanUsageReportingApi.pingSubmitCount)
    }

    @Test
    fun `when duration is not set, don't submit it`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_STOP)
        assertNull(fakeGleanUsageReportingApi.lastDurationMillis)
    }

    @Test
    fun `duration is set to the length of the last foreground session`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeCurrentTime)
        fakeCurrentTime++
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_STOP)
        assertEquals(3, fakeCurrentTime)
        assertEquals(2L, fakeGleanUsageReportingApi.lastDurationMillis)
    }

    @Test
    fun `ON_PAUSE lifecycle event doesn't affect duration`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeCurrentTime)
        fakeCurrentTime++
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_PAUSE)
        assertEquals(2, fakeCurrentTime)
        assertNull(fakeGleanUsageReportingApi.lastDurationMillis)
    }

    @Test
    fun `ON_DESTROY lifecycle event doesn't affect duration`() {
        val lifecycleObserver = createGleanUsageReportingLifecycleObserver()
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_START)
        assertEquals(1, fakeCurrentTime)
        fakeCurrentTime++
        lifecycleObserver.onStateChanged(FakeLifecycleOwner(), Lifecycle.Event.ON_DESTROY)
        assertEquals(2, fakeCurrentTime)
        assertNull(fakeGleanUsageReportingApi.lastDurationMillis)
    }

    private fun createGleanUsageReportingLifecycleObserver() =
        GleanUsageReportingLifecycleObserver(
            gleanUsageReportingApi = fakeGleanUsageReportingApi,
            currentTimeProvider = fakeCurrentTimeProvider,
        )
}
