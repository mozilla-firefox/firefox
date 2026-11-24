/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.appservices.syncmanager

import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.work.testing.WorkManagerTestInitHelper
import mozilla.appservices.sync15.EngineInfo
import mozilla.appservices.sync15.FailureName
import mozilla.appservices.sync15.FailureReason
import mozilla.appservices.sync15.IncomingInfo
import mozilla.appservices.sync15.OutgoingInfo
import mozilla.appservices.sync15.ProblemInfo
import mozilla.appservices.sync15.SyncInfo
import mozilla.appservices.sync15.SyncTelemetryPing
import mozilla.appservices.sync15.ValidationInfo
import mozilla.telemetry.glean.Glean
import mozilla.telemetry.glean.config.Configuration
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Assert.fail
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.appservices.syncmanager.GleanMetrics.Pings
import org.mozilla.appservices.syncmanager.GleanMetrics.SyncSettings
import java.util.Date
import java.util.UUID
import org.mozilla.appservices.syncmanager.GleanMetrics.BookmarksSyncV2 as BookmarksSync
import org.mozilla.appservices.syncmanager.GleanMetrics.FxaTabV2 as FxaTab
import org.mozilla.appservices.syncmanager.GleanMetrics.HistorySyncV2 as HistorySync
import org.mozilla.appservices.syncmanager.GleanMetrics.LoginsSyncV2 as LoginsSync
import org.mozilla.appservices.syncmanager.GleanMetrics.SyncV2 as Sync

private fun Date.asSeconds() = time / BaseGleanSyncPing.MILLIS_PER_SEC

@RunWith(AndroidJUnit4::class)
@Suppress("LargeClass", "LongMethod")
class SyncTelemetryTest {
    private var now: Long = 0
    private var pingCount = 0

    @Before
    fun setup() {
        now = Date().asSeconds()
        pingCount = 0

        // Due to recent changes in how upload enabled works, we need to register the custom
        // Sync pings before resetting Glean manually so they can be submitted properly. This
        // replaces the use of the GleanTestRule until it can be updated to better support testing
        // custom pings in libraries.
        Glean.registerPings(Pings.sync)
        Glean.registerPings(Pings.historySync)
        Glean.registerPings(Pings.bookmarksSync)
        Glean.registerPings(Pings.loginsSync)
        Glean.registerPings(Pings.creditcardsSync)
        Glean.registerPings(Pings.addressesSync)
        Glean.registerPings(Pings.tabsSync)

        // Glean will crash in tests without this line when not using the GleanTestRule.
        WorkManagerTestInitHelper.initializeTestWorkManager(ApplicationProvider.getApplicationContext())
        Glean.resetGlean(
            context = ApplicationProvider.getApplicationContext(),
            config = Configuration(),
            clearStores = true,
        )
    }

    @After
    fun tearDown() {
        // This closes the WorkManager database to help prevent leaking it during tests.
        WorkManagerTestInitHelper.closeWorkDatabase()
    }

    @Test
    fun `sends history telemetry pings on success`() {
        val noGlobalError = SyncTelemetry.processHistoryPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 10000,
                        engines = listOf(
                            EngineInfo(
                                name = "logins",
                                at = now + 5,
                                took = 5000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = null,
                                validation = null,
                            ),
                            EngineInfo(
                                name = "history",
                                at = now,
                                took = 5000,
                                incoming = IncomingInfo(
                                    applied = 5,
                                    failed = 4,
                                    newFailed = 3,
                                    reconciled = 2,
                                ),
                                outgoing = listOf(
                                    OutgoingInfo(
                                        sent = 10,
                                        failed = 5,
                                    ),
                                    OutgoingInfo(
                                        sent = 4,
                                        failed = 2,
                                    ),
                                ),
                                failureReason = null,
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                    SyncInfo(
                        at = now + 10,
                        took = 5000,
                        engines = listOf(
                            EngineInfo(
                                name = "history",
                                at = now + 10,
                                took = 5000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = null,
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> {
                    HistorySync.apply {
                        assertEquals("abc123", uid.testGetValue())
                        assertEquals(now, startedAt.testGetValue()!!.asSeconds())
                        assertEquals(now + 5, finishedAt.testGetValue()!!.asSeconds())
                        assertEquals(5, incoming["applied"].testGetValue())
                        assertEquals(7, incoming["failed_to_apply"].testGetValue())
                        assertEquals(2, incoming["reconciled"].testGetValue())
                        assertEquals(14, outgoing["uploaded"].testGetValue())
                        assertEquals(7, outgoing["failed_to_upload"].testGetValue())
                        assertEquals(2, outgoingBatches.testGetValue())
                        assertNull(Sync.syncUuid.testGetValue("history-sync"))
                    }
                }
                1 -> {
                    HistorySync.apply {
                        assertEquals("abc123", uid.testGetValue())
                        assertEquals(now + 10, startedAt.testGetValue()!!.asSeconds())
                        assertEquals(now + 15, finishedAt.testGetValue()!!.asSeconds())
                        assertTrue(
                            listOf(
                                incoming["applied"],
                                incoming["failed_to_apply"],
                                incoming["reconciled"],
                                outgoing["uploaded"],
                                outgoing["failed_to_upload"],
                                outgoingBatches,
                            ).none { it.testGetValue() != null },
                        )
                        assertNull(Sync.syncUuid.testGetValue("history-sync"))
                    }
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendHistoryPing`.
            Pings.historySync.submit()
            pingCount++
        }

        assertEquals(2, pingCount)
        assertTrue(noGlobalError)
    }

    @Test
    fun `sends history telemetry pings on engine failure`() {
        val noGlobalError = SyncTelemetry.processHistoryPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 5000,
                        engines = listOf(
                            // We should ignore any engines that aren't
                            // history.
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 1,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unknown, "Boxes not locked"),
                                validation = null,
                            ),
                            // Multiple history engine syncs per sync isn't
                            // expected, but it's easier to test the
                            // different failure types this way, instead of
                            // creating a top-level `SyncInfo` for each
                            // one.
                            EngineInfo(
                                name = "history",
                                at = now + 2,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Shutdown),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "history",
                                at = now + 3,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unknown, "Synergies not aligned"),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "history",
                                at = now + 4,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Http, code = 418),
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                    // ...But, just in case, we also test multiple top-level
                    // syncs.
                    SyncInfo(
                        at = now + 5,
                        took = 4000,
                        engines = listOf(
                            EngineInfo(
                                name = "history",
                                at = now + 6,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Auth, "Splines not reticulated", 999),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "history",
                                at = now + 7,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unexpected, "Kaboom!"),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "history",
                                at = now + 8,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Other, "Qualia unsynchronized"), // other
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> {
                    // Shutdown errors shouldn't be reported at all.
                    assertTrue(
                        listOf(
                            "other",
                            "unexpected",
                            "auth",
                        ).none { HistorySync.failureReason[it].testGetValue() != null },
                    )
                }
                1 -> HistorySync.apply {
                    assertEquals("Synergies not aligned", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("history-sync"))
                }
                2 -> HistorySync.apply {
                    assertEquals("Unexpected error: 418", failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("history-sync"))
                }
                3 -> HistorySync.apply {
                    assertEquals("Splines not reticulated", failureReason["auth"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("history-sync"))
                }
                4 -> HistorySync.apply {
                    assertEquals("Kaboom!", failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("history-sync"))
                }
                5 -> HistorySync.apply {
                    assertEquals("Qualia unsynchronized", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("history-sync"))
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendHistoryPing`.
            Pings.historySync.submit()
            pingCount++
        }

        assertEquals(6, pingCount)
        assertTrue(noGlobalError)
    }

    @Test
    fun `sends history telemetry pings on sync failure`() {
        val noGlobalError = SyncTelemetry.processHistoryPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 5000,
                        engines = emptyList(),
                        failureReason = FailureReason(FailureName.Unknown, "Synergies not aligned"),
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> HistorySync.apply {
                    assertEquals("Synergies not aligned", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("history-sync"))
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendHistoryPing`.
            Pings.historySync.submit()
            pingCount++
        }

        assertEquals(1, pingCount)
        assertFalse(noGlobalError)
    }

    @Test
    fun `sends passwords telemetry pings on success`() {
        val noGlobalError = SyncTelemetry.processLoginsPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 10000,
                        engines = listOf(
                            EngineInfo(
                                name = "history",
                                at = now + 5,
                                took = 5000,
                                incoming = IncomingInfo(
                                    applied = 10,
                                    failed = 2,
                                    newFailed = 3,
                                    reconciled = 2,
                                ),
                                outgoing = emptyList(),
                                failureReason = null,
                                validation = null,
                            ),
                            EngineInfo(
                                name = "passwords",
                                at = now,
                                took = 5000,
                                incoming = IncomingInfo(
                                    applied = 5,
                                    failed = 4,
                                    newFailed = 3,
                                    reconciled = 2,
                                ),
                                outgoing = listOf(
                                    OutgoingInfo(
                                        sent = 10,
                                        failed = 5,
                                    ),
                                    OutgoingInfo(
                                        sent = 4,
                                        failed = 2,
                                    ),
                                ),
                                failureReason = null,
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                    SyncInfo(
                        at = now + 10,
                        took = 5000,
                        engines = listOf(
                            EngineInfo(
                                name = "passwords",
                                at = now + 10,
                                took = 5000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = null,
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> {
                    LoginsSync.apply {
                        assertEquals("abc123", uid.testGetValue())
                        assertEquals(now, startedAt.testGetValue()!!.asSeconds())
                        assertEquals(now + 5, finishedAt.testGetValue()!!.asSeconds())
                        assertEquals(5, incoming["applied"].testGetValue())
                        assertEquals(7, incoming["failed_to_apply"].testGetValue())
                        assertEquals(2, incoming["reconciled"].testGetValue())
                        assertEquals(14, outgoing["uploaded"].testGetValue())
                        assertEquals(7, outgoing["failed_to_upload"].testGetValue())
                        assertEquals(2, outgoingBatches.testGetValue())
                        assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                    }
                }
                1 -> {
                    LoginsSync.apply {
                        assertEquals("abc123", uid.testGetValue())
                        assertEquals(now + 10, startedAt.testGetValue()!!.asSeconds())
                        assertEquals(now + 15, finishedAt.testGetValue()!!.asSeconds())
                        assertTrue(
                            listOf(
                                incoming["applied"],
                                incoming["failed_to_apply"],
                                incoming["reconciled"],
                                outgoing["uploaded"],
                                outgoing["failed_to_upload"],
                                outgoingBatches,
                            ).none { it.testGetValue() != null },
                        )
                        assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                    }
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendPasswordsPing`.
            Pings.loginsSync.submit()
            pingCount++
        }

        assertEquals(2, pingCount)
        assertTrue(noGlobalError)
    }

    @Test
    fun `sends passwords telemetry pings on engine failure`() {
        val noGlobalError = SyncTelemetry.processLoginsPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 5000,
                        engines = listOf(
                            // We should ignore any engines that aren't
                            // passwords.
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 1,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unknown, "Boxes not locked"),
                                validation = null,
                            ),
                            // Multiple passwords engine syncs per sync isn't
                            // expected, but it's easier to test the
                            // different failure types this way, instead of
                            // creating a top-level `SyncInfo` for each
                            // one.
                            EngineInfo(
                                name = "passwords",
                                at = now + 2,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Shutdown),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "passwords",
                                at = now + 3,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unknown, "Synergies not aligned"),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "passwords",
                                at = now + 4,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Http, code = 418),
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                    // ...But, just in case, we also test multiple top-level
                    // syncs.
                    SyncInfo(
                        at = now + 5,
                        took = 4000,
                        engines = listOf(
                            EngineInfo(
                                name = "passwords",
                                at = now + 6,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Auth, "Splines not reticulated", 999),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "passwords",
                                at = now + 7,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unexpected, "Kaboom!"),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "passwords",
                                at = now + 8,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Other, "Qualia unsynchronized"), // other
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> {
                    // Shutdown errors shouldn't be reported at all.
                    assertTrue(
                        listOf(
                            "other",
                            "unexpected",
                            "auth",
                        ).none { LoginsSync.failureReason[it].testGetValue() != null },
                    )
                }
                1 -> LoginsSync.apply {
                    assertEquals("Synergies not aligned", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                }
                2 -> LoginsSync.apply {
                    assertEquals("Unexpected error: 418", failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                }
                3 -> LoginsSync.apply {
                    assertEquals("Splines not reticulated", failureReason["auth"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                }
                4 -> LoginsSync.apply {
                    assertEquals("Kaboom!", failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                }
                5 -> LoginsSync.apply {
                    assertEquals("Qualia unsynchronized", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendPasswordsPing`.
            Pings.loginsSync.submit()
            pingCount++
        }

        assertEquals(6, pingCount)
        assertTrue(noGlobalError)
    }

    @Test
    fun `sends passwords telemetry pings on sync failure`() {
        val noGlobalError = SyncTelemetry.processLoginsPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 5000,
                        engines = emptyList(),
                        failureReason = FailureReason(FailureName.Unknown, "Synergies not aligned"),
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> LoginsSync.apply {
                    assertEquals("Synergies not aligned", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("logins-sync"))
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendHistoryPing`.
            Pings.loginsSync.submit()
            pingCount++
        }

        assertEquals(1, pingCount)
        assertFalse(noGlobalError)
    }

    @Test
    fun `sends bookmarks telemetry pings on success`() {
        val noGlobalError = SyncTelemetry.processBookmarksPing(
            SyncTelemetryPing(
                version = 1,
                uid = "xyz789",
                syncs = listOf(
                    SyncInfo(
                        at = now + 20,
                        took = 8000,
                        engines = listOf(
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 25,
                                took = 6000,
                                incoming = null,
                                outgoing = listOf(
                                    OutgoingInfo(
                                        sent = 10,
                                        failed = 5,
                                    ),
                                ),
                                failureReason = null,
                                validation = ValidationInfo(
                                    version = 2,
                                    problems = listOf(
                                        ProblemInfo(
                                            name = "missingParents",
                                            count = 5,
                                        ),
                                        ProblemInfo(
                                            name = "missingChildren",
                                            count = 7,
                                        ),
                                    ),
                                    failureReason = null,
                                ),
                            ),
                        ),
                        failureReason = null,
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> {
                    BookmarksSync.apply {
                        assertEquals("xyz789", uid.testGetValue())
                        assertEquals(now + 25, startedAt.testGetValue()!!.asSeconds())
                        assertEquals(now + 31, finishedAt.testGetValue()!!.asSeconds())
                        assertNull(incoming["applied"].testGetValue())
                        assertNull(incoming["failed_to_apply"].testGetValue())
                        assertNull(incoming["reconciled"].testGetValue())
                        assertEquals(10, outgoing["uploaded"].testGetValue())
                        assertEquals(5, outgoing["failed_to_upload"].testGetValue())
                        assertEquals(1, outgoingBatches.testGetValue())
                        assertNull(Sync.syncUuid.testGetValue("bookmarks-sync"))
                    }
                }
                else -> fail()
            }
            Pings.bookmarksSync.submit()
            pingCount++
        }

        assertEquals(pingCount, 1)
        assertTrue(noGlobalError)
    }

    @Test
    fun `sends bookmarks telemetry pings on engine failure`() {
        val noGlobalError = SyncTelemetry.processBookmarksPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 5000,
                        engines = listOf(
                            EngineInfo(
                                name = "history",
                                at = now + 1,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unknown, "Boxes not locked"),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 2,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Shutdown),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 3,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unknown, "Synergies not aligned"),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 4,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Http, code = 418),
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                    SyncInfo(
                        at = now + 5,
                        took = 4000,
                        engines = listOf(
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 6,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Auth, "Splines not reticulated", 999),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 7,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Unexpected, "Kaboom!"),
                                validation = null,
                            ),
                            EngineInfo(
                                name = "bookmarks",
                                at = now + 8,
                                took = 1000,
                                incoming = null,
                                outgoing = emptyList(),
                                failureReason = FailureReason(FailureName.Other, "Qualia unsynchronized"), // other
                                validation = null,
                            ),
                        ),
                        failureReason = null,
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> {
                    // Shutdown errors shouldn't be reported.
                    assertTrue(
                        listOf(
                            "other",
                            "unexpected",
                            "auth",
                        ).none { BookmarksSync.failureReason[it].testGetValue() != null },
                    )
                }
                1 -> BookmarksSync.apply {
                    assertEquals("Synergies not aligned", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("bookmarks-sync"))
                }
                2 -> BookmarksSync.apply {
                    assertEquals("Unexpected error: 418", failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("bookmarks-sync"))
                }
                3 -> BookmarksSync.apply {
                    assertEquals("Splines not reticulated", failureReason["auth"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("bookmarks-sync"))
                }
                4 -> BookmarksSync.apply {
                    assertEquals("Kaboom!", failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["other"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("bookmarks-sync"))
                }
                5 -> BookmarksSync.apply {
                    assertEquals("Qualia unsynchronized", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("bookmarks-sync"))
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendBookmarksPing`.
            Pings.bookmarksSync.submit()
            pingCount++
        }

        assertEquals(6, pingCount)
        assertTrue(noGlobalError)
    }

    @Test
    fun `sends bookmarks telemetry pings on sync failure`() {
        val noGlobalError = SyncTelemetry.processBookmarksPing(
            SyncTelemetryPing(
                version = 1,
                uid = "abc123",
                syncs = listOf(
                    SyncInfo(
                        at = now,
                        took = 5000,
                        engines = emptyList(),
                        failureReason = FailureReason(FailureName.Unknown, "Synergies not aligned"),
                    ),
                ),
                events = emptyList(),
            ),
        ) {
            when (pingCount) {
                0 -> BookmarksSync.apply {
                    assertEquals("Synergies not aligned", failureReason["other"].testGetValue())
                    assertNull(failureReason["unexpected"].testGetValue())
                    assertNull(failureReason["auth"].testGetValue())
                    assertNull(Sync.syncUuid.testGetValue("bookmarks-sync"))
                }
                else -> fail()
            }
            // We still need to send the ping, so that the counters are
            // cleared out between calls to `sendHistoryPing`.
            Pings.bookmarksSync.submit()
            pingCount++
        }

        assertEquals(1, pingCount)
        assertFalse(noGlobalError)
    }

    @Test
    @Suppress("ComplexMethod")
    fun `sends a global sync ping alongside individual data type pings`() {
        val pings = mutableListOf<MutableMap<String, Int>>(HashMap())
        var globalPingCount = 0
        val globalSyncUuids = mutableListOf<UUID>()

        val syncTelemetry = SyncTelemetryPing(
            version = 1,
            uid = "abc123",
            syncs = listOf(
                SyncInfo(
                    at = now,
                    took = 10000,
                    engines = listOf(
                        EngineInfo(
                            name = "passwords",
                            at = now,
                            took = 5000,
                            incoming = IncomingInfo(
                                applied = 5,
                                failed = 4,
                                newFailed = 3,
                                reconciled = 2,
                            ),
                            outgoing = listOf(
                                OutgoingInfo(
                                    sent = 10,
                                    failed = 5,
                                ),
                                OutgoingInfo(
                                    sent = 4,
                                    failed = 2,
                                ),
                            ),
                            failureReason = null,
                            validation = null,
                        ),
                        EngineInfo(
                            name = "history",
                            at = now,
                            took = 5000,
                            incoming = IncomingInfo(
                                applied = 5,
                                failed = 4,
                                newFailed = 3,
                                reconciled = 2,
                            ),
                            outgoing = listOf(
                                OutgoingInfo(
                                    sent = 10,
                                    failed = 5,
                                ),
                                OutgoingInfo(
                                    sent = 4,
                                    failed = 2,
                                ),
                            ),
                            failureReason = null,
                            validation = null,
                        ),
                    ),
                    failureReason = FailureReason(FailureName.Unknown, "Synergies not aligned"),
                ),
                SyncInfo(
                    at = now + 10,
                    took = 5000,
                    engines = listOf(
                        EngineInfo(
                            name = "history",
                            at = now + 10,
                            took = 5000,
                            incoming = null,
                            outgoing = emptyList(),
                            failureReason = null,
                            validation = null,
                        ),
                    ),
                    failureReason = null,
                ),
                SyncInfo(
                    at = now + 20,
                    took = 8000,
                    engines = listOf(
                        EngineInfo(
                            name = "bookmarks",
                            at = now + 25,
                            took = 6000,
                            incoming = null,
                            outgoing = listOf(
                                OutgoingInfo(
                                    sent = 10,
                                    failed = 5,
                                ),
                            ),
                            failureReason = null,
                            validation = ValidationInfo(
                                version = 2,
                                problems = listOf(
                                    ProblemInfo(
                                        name = "missingParents",
                                        count = 5,
                                    ),
                                    ProblemInfo(
                                        name = "missingChildren",
                                        count = 7,
                                    ),
                                ),
                                failureReason = null,
                            ),
                        ),
                    ),
                    failureReason = null,
                ),
            ),
            events = emptyList(),
        )

        fun setOrAssertGlobalSyncUuid(currentPingIndex: Int, pingName: String) {
            if (globalSyncUuids.elementAtOrNull(currentPingIndex) == null) {
                globalSyncUuids.add(Sync.syncUuid.testGetValue(pingName)!!)
            } else {
                assertEquals(globalSyncUuids[currentPingIndex], Sync.syncUuid.testGetValue(pingName))
            }
        }

        fun setOrIncrementPingCount(currentPingIndex: Int, pingName: String) {
            if (pings.elementAtOrNull(currentPingIndex) == null) {
                pings.add(mutableMapOf(pingName to 1))
            } else {
                pings[currentPingIndex].incrementForKey(pingName)
            }
        }

        SyncTelemetry.processSyncTelemetry(
            syncTelemetry,
            submitGlobalPing = {
                assertNotNull(globalSyncUuids.elementAtOrNull(globalPingCount))
                assertEquals(globalSyncUuids[globalPingCount], Sync.syncUuid.testGetValue("sync"))

                // Assertions above already assert syncUuid; below, let's make sure that 'failureReason' is processed.
                when (globalPingCount) {
                    0 -> {
                        assertEquals("Synergies not aligned", Sync.failureReason["other"].testGetValue())
                    }
                    1 -> {
                        assertNull(Sync.failureReason["other"].testGetValue())
                    }
                    2 -> {
                        assertNull(Sync.failureReason["other"].testGetValue())
                    }
                    else -> fail()
                }

                Pings.sync.submit()
                globalPingCount++
            },
            submitHistoryPing = {
                when (val currentPingIndex = globalPingCount) {
                    0 -> {
                        setOrAssertGlobalSyncUuid(currentPingIndex, "history-sync")
                        setOrIncrementPingCount(currentPingIndex, "history")
                        HistorySync.apply {
                            assertEquals("abc123", uid.testGetValue())
                            assertEquals(now, startedAt.testGetValue()!!.asSeconds())
                            assertEquals(now + 5, finishedAt.testGetValue()!!.asSeconds())
                            assertEquals(5, incoming["applied"].testGetValue())
                            assertEquals(7, incoming["failed_to_apply"].testGetValue())
                            assertEquals(2, incoming["reconciled"].testGetValue())
                            assertEquals(14, outgoing["uploaded"].testGetValue())
                            assertEquals(7, outgoing["failed_to_upload"].testGetValue())
                            assertEquals(2, outgoingBatches.testGetValue())
                        }
                        Pings.historySync.submit()
                    }
                    1 -> {
                        setOrAssertGlobalSyncUuid(currentPingIndex, "history-sync")
                        setOrIncrementPingCount(currentPingIndex, "history")
                        HistorySync.apply {
                            assertEquals("abc123", uid.testGetValue())
                            assertEquals(now + 10, startedAt.testGetValue()!!.asSeconds())
                            assertEquals(now + 15, finishedAt.testGetValue()!!.asSeconds())
                            assertTrue(
                                listOf(
                                    incoming["applied"],
                                    incoming["failed_to_apply"],
                                    incoming["reconciled"],
                                    outgoing["uploaded"],
                                    outgoing["failed_to_upload"],
                                    outgoingBatches,
                                ).none { it.testGetValue() != null },
                            )
                        }
                        Pings.historySync.submit()
                    }
                    else -> fail()
                }
            },
            submitLoginsPing = {
                when (val currentPingIndex = globalPingCount) {
                    0 -> {
                        setOrAssertGlobalSyncUuid(currentPingIndex, "logins-sync")
                        setOrIncrementPingCount(currentPingIndex, "passwords")
                        LoginsSync.apply {
                            assertEquals("abc123", uid.testGetValue())
                            assertEquals(now, startedAt.testGetValue()!!.asSeconds())
                            assertEquals(now + 5, finishedAt.testGetValue()!!.asSeconds())
                            assertEquals(5, incoming["applied"].testGetValue())
                            assertEquals(7, incoming["failed_to_apply"].testGetValue())
                            assertEquals(2, incoming["reconciled"].testGetValue())
                            assertEquals(14, outgoing["uploaded"].testGetValue())
                            assertEquals(7, outgoing["failed_to_upload"].testGetValue())
                            assertEquals(2, outgoingBatches.testGetValue())
                        }
                        Pings.loginsSync.submit()
                    }
                    else -> fail()
                }
            },
            submitBookmarksPing = {
                when (val currentPingIndex = globalPingCount) {
                    2 -> {
                        setOrAssertGlobalSyncUuid(currentPingIndex, "bookmarks-sync")
                        setOrIncrementPingCount(currentPingIndex, "bookmarks")
                        BookmarksSync.apply {
                            assertEquals("abc123", uid.testGetValue())
                            assertEquals(now + 25, startedAt.testGetValue()!!.asSeconds())
                            assertEquals(now + 31, finishedAt.testGetValue()!!.asSeconds())
                            assertNull(incoming["applied"].testGetValue())
                            assertNull(incoming["failed_to_apply"].testGetValue())
                            assertNull(incoming["reconciled"].testGetValue())
                            assertEquals(10, outgoing["uploaded"].testGetValue())
                            assertEquals(5, outgoing["failed_to_upload"].testGetValue())
                            assertEquals(1, outgoingBatches.testGetValue())
                        }
                        Pings.bookmarksSync.submit()
                    }
                }
            },
        )

        assertEquals(
            listOf(
                mapOf("history" to 1, "passwords" to 1),
                mapOf("history" to 1),
                mapOf("bookmarks" to 1),
            ),
            pings,
        )
    }

    @Test
    fun `checks sent tab telemetry records what it should`() {
        val json = """
            {
                "commands_sent":[{
                    "command":"send_tab",
                    "flow_id":"test-flow-id",
                    "stream_id":"test-stream-id"
                }],
                "commands_received":[]
            }
        """
        SyncTelemetry.processFxaTelemetry(json)
        val events = FxaTab.sent.testGetValue()!!
        assertEquals(1, events.size)
        assertEquals("test-flow-id", events.elementAt(0).extra!!["flow_id"])
        assertEquals("test-stream-id", events.elementAt(0).extra!!["stream_id"])

        assertNull(FxaTab.received.testGetValue())
    }

    @Test
    fun `checks received tab telemetry records what it should`() {
        val json = """
            {
                "commands_received":[{
                    "command":"send_tab",
                    "flow_id":"test-flow-id",
                    "stream_id":"test-stream-id",
                    "reason":"test-reason"
                }]
            }
        """
        SyncTelemetry.processFxaTelemetry(json)
        val events = FxaTab.received.testGetValue()!!
        assertEquals(1, events.size)
        assertEquals("test-flow-id", events.elementAt(0).extra!!["flow_id"])
        assertEquals("test-stream-id", events.elementAt(0).extra!!["stream_id"])
        assertEquals("test-reason", events.elementAt(0).extra!!["reason"])

        assertNull(FxaTab.sent.testGetValue())
    }

    @Test
    fun `checks invalid tab telemetry doesn't record anything and doesn't crash`() {
        // commands_sent is missing the stream_id, command_received is missing a reason
        val json = """
            {
                "commands_sent":[{
                    "command":"send_tab",
                    "flow_id":"test-flow-id"
                }],
                "commands_received":[{
                    "command":"send_tab",
                    "flow_id":"test-flow-id",
                    "stream_id":"test-stream-id"
                }]
            }
        """
        val sendReceiveExceptions: List<Throwable> = SyncTelemetry.processFxaTelemetry(json)
        // one exception for each of 'send' and 'received'
        assertEquals(sendReceiveExceptions.count(), 2)

        // completely invalid json
        val topLevelExceptions: List<Throwable> = SyncTelemetry.processFxaTelemetry(""" foo bar """)
        assertNull(FxaTab.sent.testGetValue())
        assertNull(FxaTab.received.testGetValue())
        // processFxaTelemetry should report only one error
        assertEquals(topLevelExceptions.count(), 1)
    }

    @Test
    fun `checks telemetry for unknown commands doesn't record anything and doesn't crash`() {
        val json = """
            {
                "commands_sent":[{
                    "command":"test-unknown-command",
                    "flow_id":"test-flow-id",
                    "stream_id":"test-stream-id"
                }],
                "commands_received":[{
                    "command":"test-unknown-command",
                    "flow_id":"test-flow-id",
                    "stream_id":"test-stream-id",
                    "reason":"test-reason"
                }]
            }
        """
        val sendReceiveExceptions: List<Throwable> = SyncTelemetry.processFxaTelemetry(json)

        // one exception for each of 'send' and 'received'
        assertEquals(sendReceiveExceptions.count(), 2)
        assertNull(FxaTab.sent.testGetValue())
        assertNull(FxaTab.received.testGetValue())
    }

    private fun MutableMap<String, Int>.incrementForKey(key: String) {
        this[key] = 1 + this.getOrElse(key, { 0 })
    }

    @Test
    fun `checks received open sync settings menu telemetry when it should`() {
        SyncTelemetry.processOpenSyncSettingsMenuTelemetry()
        val events = SyncSettings.openMenu.testGetValue()!!
        assertEquals(1, events.size)
        assertEquals("sync_settings", events.elementAt(0).category)
        assertEquals("open_menu", events.elementAt(0).name)
    }

    @Test
    fun `checks received save sync settings telemetry when it should`() {
        val enabledEngines = listOf<String>("bookmarks", "tabs")
        val disabledEngines = listOf<String>("logins")
        SyncTelemetry.processSaveSyncSettingsTelemetry(enabledEngines, disabledEngines)
        val events = SyncSettings.save.testGetValue()!!
        assertEquals(1, events.size)
        assertEquals("sync_settings", events.elementAt(0).category)
        assertEquals("save", events.elementAt(0).name)
        assertEquals("bookmarks,tabs", events.elementAt(0).extra!!["enabled_engines"])
        assertEquals("logins", events.elementAt(0).extra!!["disabled_engines"])
    }

    @Test
    fun `checks received engine toggled telemetry when it should`() {
        SyncTelemetry.processEngineToggledTelemetry("bookmarks", false, 3)
        SyncTelemetry.processEngineToggledTelemetry("logins", true, 4)
        val events = SyncSettings.engineToggled.testGetValue()!!
        assertEquals(2, events.size)

        assertEquals("sync_settings", events.elementAt(0).category)
        assertEquals("engine_toggled", events.elementAt(0).name)
        assertEquals("bookmarks", events.elementAt(0).extra!!["engine"])
        assertEquals("false", events.elementAt(0).extra!!["enabled"])
        assertEquals("3", events.elementAt(0).extra!!["enabled_count"])

        assertEquals("engine_toggled", events.elementAt(1).name)
        assertEquals("logins", events.elementAt(1).extra!!["engine"])
        assertEquals("true", events.elementAt(1).extra!!["enabled"])
        assertEquals("4", events.elementAt(1).extra!!["enabled_count"])
    }
}
