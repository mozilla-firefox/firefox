/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import kotlin.test.assertEquals
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import mozilla.components.lib.shake.ShakeSensitivity
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class PageSummariesSettingsMiddlewareTest {

    @Test
    fun `WHEN summarize pages is toggled on THEN feature is enabled `() = runTest {
        val settings =
            SummarizationSettings.inMemory(
                isFeatureEnabled = false,
                isGestureEnabled = false,
            )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore()

        store.dispatch(SummarizePagesPreferenceToggled)
        this.runCurrent()

        assertTrue(settings.getFeatureEnabledUserStatus().first() == true)
    }

    @Test
    fun `WHEN summarize pages is toggled off THEN feature is disabled`() = runTest {
        val settings =
            SummarizationSettings.inMemory(
                isFeatureEnabled = true,
                isGestureEnabled = false,
            )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore(SummarizeSettingsState(isFeatureEnabled = true))

        store.dispatch(SummarizePagesPreferenceToggled)
        this.runCurrent()

        assertFalse(settings.getFeatureEnabledUserStatus().first() == true)
    }

    @Test
    fun `WHEN shake to summarize is toggled on THEN gesture is enabled`() = runTest {
        val settings =
            SummarizationSettings.inMemory(
                isFeatureEnabled = true,
                isGestureEnabled = false,
            )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore(SummarizeSettingsState(isFeatureEnabled = true))

        store.dispatch(ShakeToSummarizePreferenceToggled)
        this.runCurrent()

        assertTrue(settings.getFeatureEnabledUserStatus().first() == true)
    }

    @Test
    fun `WHEN shake to summarize is toggled off THEN gesture is disabled`() = runTest {
        val settings =
            SummarizationSettings.inMemory(
                isFeatureEnabled = true,
                isGestureEnabled = true,
            )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore(SummarizeSettingsState(isFeatureEnabled = true, isGestureEnabled = true))

        store.dispatch(ShakeToSummarizePreferenceToggled)
        this.runCurrent()

        assertFalse(settings.getGestureEnabledUserStatus().first())
    }

    @Test
    fun `WHEN page summaries are toggled off THEN gesture preference is preserved`() = runTest {
        val settings =
            SummarizationSettings.inMemory(
                isFeatureEnabled = true,
                isGestureEnabled = true,
            )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore(SummarizeSettingsState(isFeatureEnabled = true, isGestureEnabled = true))

        store.dispatch(SummarizePagesPreferenceToggled)
        this.runCurrent()

        assertFalse(settings.getFeatureEnabledUserStatus().first() == true)
        assertTrue(settings.getGestureEnabledUserStatus().first())
    }

    @Test
    fun `WHEN shake sensitivity is changed THEN it is persisted`() = runTest {
        val settings =
            SummarizationSettings.inMemory(
                isFeatureEnabled = true,
                isGestureEnabled = true,
            )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore(SummarizeSettingsState(isFeatureEnabled = true, isGestureEnabled = true))

        store.dispatch(ShakeSensitivityChanged(ShakeSensitivity.Low))
        this.runCurrent()

        assertEquals(ShakeSensitivity.Low, settings.getShakeSensitivity().first())
    }

    @Test
    fun `WHEN learn more is requested and handled THEN no preference is persisted`() = runTest {
        val settings =
            SummarizationSettings.inMemory(
                isFeatureEnabled = true,
                isGestureEnabled = true,
                shakeSensitivity = ShakeSensitivity.High,
            )
        val middleware = buildMiddleware(settings, this)
        val store =
            middleware.makeStore(
                SummarizeSettingsState(
                    isFeatureEnabled = true,
                    isGestureEnabled = true,
                    shakeSensitivity = ShakeSensitivity.High,
                )
            )

        store.dispatch(LearnMoreClicked)
        this.runCurrent()
        assertTrue(store.state.isLearnMoreRequested)

        store.dispatch(LearnMoreHandled)
        this.runCurrent()
        assertFalse(store.state.isLearnMoreRequested)

        assertTrue(settings.getFeatureEnabledUserStatus().first() == true)
        assertTrue(settings.getGestureEnabledUserStatus().first())
        assertEquals(ShakeSensitivity.High, settings.getShakeSensitivity().first())
    }

    private fun buildMiddleware(
        settings: SummarizationSettings,
        scope: CoroutineScope,
    ) =
        SummarizeSettingsMiddleware(
            settings = settings,
            scope = scope,
        )

    private fun SummarizeSettingsMiddleware.makeStore(initialState: SummarizeSettingsState = SummarizeSettingsState()) =
        SummarizeSettingsStore(
            initialState = initialState,
            reducer = ::summarizeSettingsReducer,
            middleware = listOf(asMiddleware()),
        )
}
