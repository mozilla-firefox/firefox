/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import io.mockk.mockk
import io.mockk.verify
import io.mockk.verifyOrder
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.runTest
import mozilla.components.service.merino.sports.MerinoSportsUpdater
import mozilla.components.support.utils.RunWhenReadyQueue
import org.junit.Test
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppAction.SportsWidgetAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.components.appstate.sports.SportsWidgetState

class SportsWidgetRefreshMiddlewareTest {

    private val updater: MerinoSportsUpdater = mockk(relaxed = true)

    @Test
    fun `GIVEN the widget is enabled and visible WHEN StartAction is dispatched THEN start the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.advanceUntilIdle()

            verify { updater.startPeriodicWork() }
            verify(exactly = 0) { updater.stopPeriodicWork() }
        }

    @Test
    fun `GIVEN the feature is disabled WHEN StartAction is dispatched THEN stop the periodic work`() = runTest {
        val store = createStore(isFeatureEnabled = false, isVisible = true)

        store.dispatch(AppAction.AppLifecycleAction.StartAction)
        testScheduler.advanceUntilIdle()

        verify { updater.stopPeriodicWork() }
        verify(exactly = 0) { updater.startPeriodicWork() }
    }

    @Test
    fun `GIVEN the widget is hidden WHEN StartAction is dispatched THEN stop the periodic work`() = runTest {
        val store = createStore(isFeatureEnabled = true, isVisible = false)

        store.dispatch(AppAction.AppLifecycleAction.StartAction)
        testScheduler.advanceUntilIdle()

        verify { updater.stopPeriodicWork() }
        verify(exactly = 0) { updater.startPeriodicWork() }
    }

    @Test
    fun `GIVEN the app is not visually complete WHEN StartAction is dispatched THEN wait before starting the periodic work`() =
        runTest {
            val queue = RunWhenReadyQueue(this)
            val store = createStore(isFeatureEnabled = true, isVisible = true, queue = queue)

            store.dispatch(AppAction.AppLifecycleAction.StartAction)
            testScheduler.advanceUntilIdle()

            verify(exactly = 0) { updater.startPeriodicWork() }

            queue.ready()
            testScheduler.advanceUntilIdle()

            verify { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the feature is enabled and not visible WHEN VisibilityChanged is dispatched with true THEN start the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = false)

            store.dispatch(SportsWidgetAction.VisibilityChanged(isVisible = true))
            testScheduler.advanceUntilIdle()

            verify { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN VisibilityChanged is dispatched with false THEN stop the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(SportsWidgetAction.VisibilityChanged(isVisible = false))
            testScheduler.advanceUntilIdle()

            verify { updater.stopPeriodicWork() }
            verify(exactly = 0) { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is visible WHEN FeatureEnabledChanged is dispatched with true THEN start the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = false, isVisible = true)

            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = true))
            testScheduler.advanceUntilIdle()

            verify { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN FeatureEnabledChanged is dispatched with false THEN stop the periodic work`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = false))
            testScheduler.advanceUntilIdle()

            verify { updater.stopPeriodicWork() }
            verify(exactly = 0) { updater.startPeriodicWork() }
        }

    @Test
    fun `GIVEN the widget is enabled and visible WHEN the feature is disabled and enabled again THEN stop and start the periodic work in order`() =
        runTest {
            val store = createStore(isFeatureEnabled = true, isVisible = true)

            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = false))
            store.dispatch(SportsWidgetAction.FeatureEnabledChanged(isEnabled = true))
            testScheduler.advanceUntilIdle()

            verifyOrder {
                updater.stopPeriodicWork()
                updater.startPeriodicWork()
            }
        }

    private fun TestScope.createStore(
        isFeatureEnabled: Boolean,
        isVisible: Boolean,
        queue: RunWhenReadyQueue = RunWhenReadyQueue(this).also { it.ready() },
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
                        visualCompletenessQueue = queue,
                        coroutineScope = this,
                    )
                ),
        )
}
