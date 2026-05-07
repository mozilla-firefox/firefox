/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.last
import kotlinx.coroutines.flow.take
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class PageSummariesSettingsMiddlewareTest {

    private var learnMoreClicked = false

    @Before
    fun setup() {
        learnMoreClicked = false
    }

    @Test
    fun `WHEN summarize pages is toggled on THEN feature is enabled `() = runTest {
        val settings = SummarizationSettings.inMemory(
            isFeatureEnabled = false,
            isGestureEnabled = false,
        )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore()

        store.dispatch(ViewAppeared)
        this.runCurrent()

        store.dispatch(SummarizePagesPreferenceToggled)
        this.runCurrent()

        assertTrue(settings.getFeatureEnabledUserStatus().first())
    }

    @Test
    fun `WHEN summarize pages is toggled off THEN feature is disabled`() = runTest {
        val settings = SummarizationSettings.inMemory(
            isFeatureEnabled = true,
            isGestureEnabled = false,
        )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore()

        store.dispatch(ViewAppeared)
        this.runCurrent()

        store.dispatch(SummarizePagesPreferenceToggled)
        this.runCurrent()

        assertFalse(settings.getFeatureEnabledUserStatus().first())
    }

    @Test
    fun `WHEN shake to summarize is toggled on THEN gesture is enabled`() = runTest {
        val settings = SummarizationSettings.inMemory(
            isFeatureEnabled = true,
            isGestureEnabled = false,
        )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore()

        store.dispatch(ViewAppeared)
        this.runCurrent()

        store.dispatch(ShakeToSummarizePreferenceToggled)
        this.runCurrent()

        assertTrue(settings.getFeatureEnabledUserStatus().first())
    }

    @Test
    fun `WHEN shake to summarize is toggled off THEN gesture is disabled`() = runTest {
        val settings = SummarizationSettings.inMemory(
            isFeatureEnabled = true,
            isGestureEnabled = true,
        )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore()

        store.dispatch(ViewAppeared)
        this.runCurrent()

        store.dispatch(ShakeToSummarizePreferenceToggled)
        this.runCurrent()

        assertFalse(settings.getGestureEnabledUserStatus().first())
    }

    @Test
    fun `WHEN page summaries are toggled off THEN gesture preference is preserved`() = runTest {
        val settings = SummarizationSettings.inMemory(
            isFeatureEnabled = true,
            isGestureEnabled = true,
        )
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore()

        store.dispatch(ViewAppeared)
        this.runCurrent()

        store.dispatch(SummarizePagesPreferenceToggled)
        this.runCurrent()

        assertFalse(settings.getFeatureEnabledUserStatus().first())
        assertTrue(settings.getGestureEnabledUserStatus().first())
    }

    @Test
    fun `WHEN learn more is clicked THEN callback is invoked`() = runTest {
        val settings = SummarizationSettings.inMemory()
        val middleware = buildMiddleware(settings, this)
        val store = middleware.makeStore()

        store.dispatch(ViewAppeared)
        this.runCurrent()

        store.dispatch(LearnMoreClicked)
        this.runCurrent()

        assertTrue(learnMoreClicked)
    }

    private fun buildMiddleware(
        settings: SummarizationSettings,
        scope: CoroutineScope,
    ) = SummarizeSettingsMiddleware(
        settings = settings,
        onLearnMoreClicked = { learnMoreClicked = true },
        scope = scope,
    )

    private fun SummarizeSettingsMiddleware.makeStore() = SummarizeSettingsStore(
        initialState = SummarizeSettingsState(),
        reducer = ::summarizeSettingsReducer,
        middleware = listOf(this),
    )
}
