/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.listentopage

import mozilla.components.feature.listentopage.ChunkState
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenState
import mozilla.components.feature.listentopage.ListenStore
import mozilla.components.feature.listentopage.PlaybackState
import mozilla.components.feature.listentopage.listenReducer
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.support.test.robolectric.testContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.GleanMetrics.ListenToPage
import org.mozilla.fenix.helpers.FenixGleanTestRule
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class ListenThermalMiddlewareTest {

    @get:Rule val gleanTestRule = FenixGleanTestRule(testContext)

    private val monitor = FakeThermalStatusMonitor()
    private val store =
        ListenStore(
            initialState = ListenState(),
            reducer = ::listenReducer,
            middleware = listOf(ListenThermalMiddleware(monitor)),
        )

    @Test
    fun `WHEN a session that played audio stops THEN its hottest status is counted`() {
        startReading()
        monitor.report(ThermalStatus.Severe)
        monitor.report(ThermalStatus.Light)

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["severe"].testGetValue())
        assertNull(ListenToPage.sessionMaxThermalStatus["light"].testGetValue())
    }

    @Test
    fun `WHEN the device stays cool THEN the session is counted as none`() {
        startReading()

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["none"].testGetValue())
        assertNull(ListenToPage.thermalThrottlingEntered.testGetValue())
    }

    @Test
    fun `WHEN the status rises into throttling THEN each entry is counted`() {
        startReading()
        monitor.report(ThermalStatus.Moderate)
        monitor.report(ThermalStatus.Severe)
        monitor.report(ThermalStatus.Light)
        monitor.report(ThermalStatus.Moderate)

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(2, ListenToPage.thermalThrottlingEntered.testGetValue())
    }

    @Test
    fun `WHEN the device is already throttling at the first audio THEN it is not counted as entering throttling`() {
        monitor.currentStatus = ThermalStatus.Moderate
        startReading()
        monitor.report(ThermalStatus.Moderate)

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["moderate"].testGetValue())
        assertNull(ListenToPage.thermalThrottlingEntered.testGetValue())
    }

    @Test
    fun `WHEN the device cannot report its status THEN nothing is observed or recorded`() {
        monitor.supported = false
        startReading()

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertFalse(monitor.observing)
        assertNull(ListenToPage.sessionMaxThermalStatus["none"].testGetValue())
    }

    @Test
    fun `WHEN the device reports a status this code does not know THEN the session is not recorded`() {
        monitor.currentStatus = ThermalStatus.Unknown
        startReading()

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertFalse(monitor.observing)
        listOf("none", "light", "moderate", "severe", "critical", "emergency", "shutdown").forEach {
            assertNull(ListenToPage.sessionMaxThermalStatus[it].testGetValue())
        }
    }

    @Test
    fun `WHEN the device reports a status this code does not know during a session THEN it is ignored`() {
        startReading()
        monitor.report(ThermalStatus.Moderate)
        monitor.report(ThermalStatus.Unknown)
        monitor.report(ThermalStatus.Moderate)

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["moderate"].testGetValue())
        assertEquals(1, ListenToPage.thermalThrottlingEntered.testGetValue())
    }

    @Test
    fun `WHEN a session stops before any audio plays THEN nothing is observed or recorded`() {
        store.dispatch(ListenAction.User.Session.ListenRequested(tabId = "tab", url = "https://example.com"))

        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(0, monitor.supportChecks)
        assertNull(ListenToPage.sessionMaxThermalStatus["none"].testGetValue())
    }

    @Test
    fun `WHEN later chunks play THEN support is only checked once per session`() {
        startReading()

        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 1), positionMs = 0))
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 2), positionMs = 0))

        assertEquals(1, monitor.supportChecks)
    }

    @Test
    fun `WHEN an unsupported session plays more chunks THEN support is not checked again`() {
        monitor.supported = false
        startReading()

        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 1), positionMs = 0))

        assertEquals(1, monitor.supportChecks)
    }

    @Test
    fun `WHEN a session stops THEN observing stops and later reports are ignored`() {
        startReading()
        store.dispatch(ListenAction.User.Session.StopRequested())

        assertFalse(monitor.observing)
        monitor.report(ThermalStatus.Severe)
        assertNull(ListenToPage.sessionMaxThermalStatus["severe"].testGetValue())
    }

    @Test
    fun `WHEN another session is requested THEN the previous one is recorded`() {
        startReading()
        monitor.report(ThermalStatus.Severe)

        store.dispatch(ListenAction.User.Session.ListenRequested(tabId = "other", url = "https://example.org"))

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["severe"].testGetValue())
    }

    @Test
    fun `WHEN the article is read to its end THEN the session is recorded and observing stops`() {
        startReading()
        monitor.report(ThermalStatus.Severe)

        store.dispatch(ListenAction.System.Playback.PlaybackEnded)

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["severe"].testGetValue())
        assertFalse(monitor.observing)
    }

    @Test
    fun `WHEN reading fails THEN the session is recorded and observing stops`() {
        startReading()
        monitor.report(ThermalStatus.Severe)

        store.dispatch(ListenAction.System.Playback.PlaybackFailed)

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["severe"].testGetValue())
        assertFalse(monitor.observing)
    }

    @Test
    fun `WHEN the audio is paused THEN the status is not observed until it plays again`() {
        startReading()

        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Paused)))
        assertFalse(monitor.observing)
        monitor.report(ThermalStatus.Severe)
        monitor.report(ThermalStatus.None)
        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Playing)))
        assertTrue(monitor.observing)
        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(1, ListenToPage.sessionMaxThermalStatus["none"].testGetValue())
        assertNull(ListenToPage.sessionMaxThermalStatus["severe"].testGetValue())
        assertNull(ListenToPage.thermalThrottlingEntered.testGetValue())
    }

    @Test
    fun `WHEN an article that ended is played again THEN a new session is recorded`() {
        startReading()
        store.dispatch(ListenAction.System.Playback.PlaybackEnded)

        store.dispatch(ListenAction.System.Playback.StateChangeObserved(PlaybackState(phase = PlaybackPhase.Playing)))
        assertTrue(monitor.observing)
        store.dispatch(ListenAction.User.Session.StopRequested())

        assertEquals(2, ListenToPage.sessionMaxThermalStatus["none"].testGetValue())
    }

    private fun startReading() {
        store.dispatch(ListenAction.User.Session.ListenRequested(tabId = "tab", url = "https://example.com"))
        store.dispatch(ListenAction.System.Playback.PlaybackStarted(ChunkState(index = 0), positionMs = 0))
        assertTrue(!monitor.supported || monitor.currentStatus == ThermalStatus.Unknown || monitor.observing)
    }

    private class FakeThermalStatusMonitor : ThermalStatusMonitor {
        var supported = true
        var supportChecks = 0
        override var currentStatus = ThermalStatus.None
        private var onStatusChanged: ((ThermalStatus) -> Unit)? = null

        val observing: Boolean
            get() = onStatusChanged != null

        override fun isSupported(): Boolean {
            supportChecks += 1
            return supported
        }

        override fun startObserving(onStatusChanged: (status: ThermalStatus) -> Unit) {
            this.onStatusChanged = onStatusChanged
        }

        override fun stopObserving() {
            onStatusChanged = null
        }

        /** Reports [status], whether or not anyone is observing, as the platform does. */
        fun report(status: ThermalStatus) {
            currentStatus = status
            onStatusChanged?.invoke(status)
        }
    }
}
