/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.test.ext.junit.runners.AndroidJUnit4
import io.mockk.mockk
import io.mockk.verify
import io.mockk.verifyOrder
import java.io.IOException
import java.time.Instant
import java.time.ZoneId
import java.util.Locale
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.service.merino.sports.MerinoCompetition
import mozilla.components.service.merino.sports.MerinoMatch
import mozilla.components.service.merino.sports.MerinoMatchStatus
import mozilla.components.service.merino.sports.MerinoMatches
import mozilla.components.service.merino.sports.MerinoMatchesStorage
import mozilla.components.service.merino.sports.MerinoSportsUpdater
import mozilla.components.service.merino.sports.MerinoTeam
import mozilla.components.support.utils.RunWhenReadyQueue
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SportsWidgetAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.sports.SportsWidgetState

@RunWith(AndroidJUnit4::class)
class SportsWidgetRefreshMiddlewareTest {

    private val updater: MerinoSportsUpdater = mockk(relaxed = true)
    private val storage = MerinoMatchesStorage.inMemory()

    @Test
    fun `GIVEN stored matches and the widget is enabled and visible WHEN StartAction is dispatched THEN matchCards is set from the stored matches`() =
        runTest {
            storage.setMatches(MERINO_MATCHES)
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            assertEquals(MATCH_CARDS, store.state.sportsWidgetState.matchCards)
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN new matches are stored THEN matchCards is updated`() = runTest {
        val store = createStore(isFeatureEnabled = true, isVisible = true)
        store.dispatch(AppAction.AppLifecycleAction.StartAction)
        testScheduler.runCurrent()

        assertTrue(store.state.sportsWidgetState.matchCards.isEmpty())

        storage.setMatches(MERINO_MATCHES)
        testScheduler.runCurrent()

        assertEquals(MATCH_CARDS, store.state.sportsWidgetState.matchCards)
    }

    @Test
    fun `GIVEN errorState is set WHEN new matches are stored THEN errorState is cleared`() = runTest {
        val store = createStore(isFeatureEnabled = true, isVisible = true)
        store.dispatch(AppAction.AppLifecycleAction.StartAction)
        store.dispatch(SportsWidgetAction.ErrorStateChanged(SportCardErrorState.LoadFailed))
        testScheduler.runCurrent()

        assertEquals(SportCardErrorState.LoadFailed, store.state.sportsWidgetState.errorState)

        storage.setMatches(MERINO_MATCHES)
        testScheduler.runCurrent()

        assertNull(store.state.sportsWidgetState.errorState)
    }

    @Test
    fun `GIVEN stored matches and the feature is disabled WHEN StartAction is dispatched THEN matchCards is not set`() =
        runTest {
            storage.setMatches(MERINO_MATCHES)
            val store = createStore(isFeatureEnabled = false, isVisible = true)

            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            assertTrue(store.state.sportsWidgetState.matchCards.isEmpty())
        }

    @Test
    fun `GIVEN the widget is hidden WHEN new matches are stored THEN matchCards is not updated`() = runTest {
        storage.setMatches(MERINO_MATCHES)
        val store = createStore(isFeatureEnabled = true, isVisible = true)
        store.dispatch(AppAction.AppLifecycleAction.StartAction)
        testScheduler.runCurrent()

        assertEquals(MATCH_CARDS, store.state.sportsWidgetState.matchCards)

        store.dispatch(SportsWidgetAction.VisibilityChanged(isVisible = false))
        testScheduler.runCurrent()

        storage.setMatches(OTHER_MERINO_MATCHES)
        testScheduler.runCurrent()

        assertEquals(MATCH_CARDS, store.state.sportsWidgetState.matchCards)
    }

    @Test
    fun `GIVEN new matches were stored while the widget was hidden WHEN the widget is shown THEN matchCards is updated`() =
        runTest {
            storage.setMatches(MERINO_MATCHES)
            val store = createStore(isFeatureEnabled = true, isVisible = true)
            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            store.dispatch(SportsWidgetAction.VisibilityChanged(isVisible = false))
            testScheduler.runCurrent()

            storage.setMatches(OTHER_MERINO_MATCHES)
            testScheduler.runCurrent()

            assertEquals(MATCH_CARDS, store.state.sportsWidgetState.matchCards)

            store.dispatch(SportsWidgetAction.VisibilityChanged(isVisible = true))
            testScheduler.runCurrent()

            assertEquals(OTHER_MATCH_CARDS, store.state.sportsWidgetState.matchCards)
        }

    @Test
    fun `GIVEN the stored matches are shown WHEN the time zone changes and StartAction is dispatched THEN matchCards uses the new time zone`() =
        runTest {
            storage.setMatches(MERINO_MATCHES)
            var zoneId = ZoneId.of("America/New_York")
            val store = createStore(isFeatureEnabled = true, isVisible = true, zoneId = { zoneId })
            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            assertEquals(MATCH_CARDS, store.state.sportsWidgetState.matchCards)

            zoneId = ZoneId.of("Europe/London")
            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            assertEquals(
                listOf(MATCH_CARD.copy(matches = listOf(MATCH.copy(time = "15:00")))),
                store.state.sportsWidgetState.matchCards,
            )
        }

    @Test
    fun `GIVEN reading the stored matches fails WHEN StartAction is dispatched THEN matchCards is not set and the periodic work is started`() =
        runTest {
            val failingStorage =
                object : MerinoMatchesStorage {
                    override val matches = flow<MerinoMatches?> { throw IOException("read failed") }

                    override suspend fun setMatches(matches: MerinoMatches?) = Unit
                }
            val store = createStore(isFeatureEnabled = true, isVisible = true, matchesStorage = failingStorage)

            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            assertTrue(store.state.sportsWidgetState.matchCards.isEmpty())
            verify { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN StartAction is dispatched THEN start the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            verify { updater.startPeriodicWork() }
            verify(exactly = 0) { updater.stopPeriodicWork() }
        }

    @Test
    fun `GIVEN the feature is disabled WHEN StartAction is dispatched THEN stop the periodic work`() = runTest {
        val store = createStore(isFeatureEnabled = false, isVisible = true)

        store.dispatch(AppAction.AppLifecycleAction.StartAction)
        testScheduler.runCurrent()

        verify { updater.stopPeriodicWork() }
        verify(exactly = 0) { updater.startPeriodicWork() }
    }

    @Test
    fun `GIVEN the widget is hidden WHEN StartAction is dispatched THEN stop the periodic work`() = runTest {
        val store = createStore(isFeatureEnabled = true, isVisible = false)

        store.dispatch(AppAction.AppLifecycleAction.StartAction)
        testScheduler.runCurrent()

        verify { updater.stopPeriodicWork() }
        verify(exactly = 0) { updater.startPeriodicWork() }
    }

    @Test
    fun `GIVEN the app is not visually complete WHEN StartAction is dispatched THEN wait before starting the periodic work`() =
        runTest {
            val queue = RunWhenReadyQueue(this)
            val store = createStore(isFeatureEnabled = true, isVisible = true, queue = queue)

            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            verify(exactly = 0) { updater.startPeriodicWork() }

            queue.ready()
            testScheduler.runCurrent()

            verify { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the feature is enabled and not visible WHEN VisibilityChanged is dispatched with true THEN start the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = false)

            store.dispatch(SportsWidgetAction.VisibilityChanged(isVisible = true))
            testScheduler.runCurrent()

            verify { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN VisibilityChanged is dispatched with false THEN stop the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(SportsWidgetAction.VisibilityChanged(isVisible = false))
            testScheduler.runCurrent()

            verify { updater.stopPeriodicWork() }
            verify(exactly = 0) { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is visible WHEN FeatureEnabledChanged is dispatched with true THEN start the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = false, isVisible = true)

            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = true))
            testScheduler.runCurrent()

            verify { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN FeatureEnabledChanged is dispatched with false THEN stop the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = false))
            testScheduler.runCurrent()

            verify { updater.stopPeriodicWork() }
            verify(exactly = 0) { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN the feature is disabled and enabled again THEN stop and start the periodic work in order`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = false))
            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = true))
            testScheduler.runCurrent()

            verifyOrder {
                updater.stopPeriodicWork()
                updater.startPeriodicWork()
            }
        }

    @Test
    fun `GIVEN the stored matches are shown WHEN the locale and clock change and StartAction is dispatched THEN matchCards uses the new format`() =
        runTest {
            storage.setMatches(MERINO_MATCHES)
            var locale = Locale.US
            var is24HourFormat = false
            val store =
                createStore(
                    isFeatureEnabled = true,
                    isVisible = true,
                    zoneId = { ZoneId.of("Europe/Paris") },
                    locale = { locale },
                    is24HourFormat = { is24HourFormat },
                )
            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            assertEquals("Oct 17", store.state.sportsWidgetState.matchCards.single().matches.single().date)

            locale = Locale.FRANCE
            is24HourFormat = true
            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.runCurrent()

            val match = store.state.sportsWidgetState.matchCards.single().matches.single()
            assertEquals("17 oct.", match.date)
            assertEquals("16:00", match.time)
        }

    private fun TestScope.createStore(
        isFeatureEnabled: Boolean,
        isVisible: Boolean,
        queue: RunWhenReadyQueue = RunWhenReadyQueue(this).also { it.ready() },
        matchesStorage: MerinoMatchesStorage = storage,
        zoneId: () -> ZoneId = { ZoneId.of("America/New_York") },
        locale: () -> Locale = { Locale.US },
        is24HourFormat: () -> Boolean = { true },
    ) =
        AppStore(
            initialState =
                AppState(
                    sportsWidgetState = SportsWidgetState(isFeatureEnabled = isFeatureEnabled, isVisible = isVisible)
                ),
            middlewares =
                listOf(
                    SportsWidgetRefreshMiddleware(
                        merinoSportsUpdater = lazy { updater },
                        merinoMatchesStorage = lazy { matchesStorage },
                        is24HourFormat = is24HourFormat,
                        visualCompletenessQueue = queue,
                        zoneId = zoneId,
                        locale = locale,
                        coroutineScope = backgroundScope,
                    )
                ),
        )

    private companion object {
        val MERINO_MATCHES =
            MerinoMatches(
                previous = emptyList(),
                current = emptyList(),
                upcoming =
                    listOf(
                        MerinoMatch(
                            id = 1L,
                            competition = MerinoCompetition(id = 34L, name = "Test League"),
                            kickoff = "2026-10-17T14:00:00Z",
                            homeTeam = MerinoTeam(id = 1L, name = "Team A", logoUrl = "https://example.com/a.png"),
                            awayTeam = MerinoTeam(id = 2L, name = "Team B", logoUrl = "https://example.com/b.png"),
                            status = MerinoMatchStatus.SCHEDULED,
                            updatedAt = "2026-10-10T09:00:00Z",
                        )
                    ),
            )

        val OTHER_MERINO_MATCHES =
            MERINO_MATCHES.copy(upcoming = listOf(MERINO_MATCHES.upcoming.single().copy(id = 2L)))

        val MATCH =
            Match(
                globalEventId = 1L,
                date = "Oct 17",
                time = "10:00",
                home = Team(name = "Team A", id = 1L, iconUrl = "https://example.com/a.png"),
                away = Team(name = "Team B", id = 2L, iconUrl = "https://example.com/b.png"),
                matchStatus = MatchStatus.Scheduled,
                updatedAt = Instant.ofEpochSecond(1791622800),
            )

        val MATCH_CARD = MatchCard(matches = listOf(MATCH), label = "Test League")

        val MATCH_CARDS = listOf(MATCH_CARD)

        val OTHER_MATCH_CARDS = listOf(MATCH_CARD.copy(matches = listOf(MATCH.copy(globalEventId = 2L))))
    }
}
