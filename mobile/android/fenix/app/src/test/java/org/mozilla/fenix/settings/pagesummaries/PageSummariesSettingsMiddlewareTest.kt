/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.pagesummaries

import mozilla.components.feature.summarize.settings.LearnMoreClicked
import mozilla.components.feature.summarize.settings.ShakeToSummarizePreferenceToggled
import mozilla.components.feature.summarize.settings.SummarizePagesPreferenceToggled
import mozilla.components.feature.summarize.settings.SummarizeSettingsMiddleware
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.feature.summarize.settings.SummarizeSettingsStore
import mozilla.components.feature.summarize.settings.summarizeSettingsReducer
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.mozilla.fenix.settings.summarize.FakeSummarizationFeatureConfiguration

class PageSummariesSettingsMiddlewareTest {

    private lateinit var settings: FakeSummarizationFeatureConfiguration
    private var learnMoreClicked = false

    @Before
    fun setup() {
        settings = FakeSummarizationFeatureConfiguration()
        learnMoreClicked = false
    }

    @Test
    fun `WHEN summarize pages is toggled on THEN setting is updated`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = SummarizeSettingsState(
                summarizePagesEnabled = false,
                shakeToSummarizeEnabled = false,
            ),
        )

        store.dispatch(SummarizePagesPreferenceToggled)

        assertTrue(settings.summarizePagesEnabled)
    }

    @Test
    fun `WHEN summarize pages is toggled off THEN setting is updated`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = SummarizeSettingsState(
                summarizePagesEnabled = true,
                shakeToSummarizeEnabled = true,
            ),
        )

        store.dispatch(SummarizePagesPreferenceToggled)

        assertFalse(settings.summarizePagesEnabled)
    }

    @Test
    fun `WHEN shake to summarize is toggled on THEN setting is updated`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = SummarizeSettingsState(
                summarizePagesEnabled = true,
                shakeToSummarizeEnabled = false,
            ),
        )

        store.dispatch(ShakeToSummarizePreferenceToggled)

        assertTrue(settings.shakeToSummarizeEnabled)
    }

    @Test
    fun `WHEN shake to summarize is toggled off THEN setting is updated`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore(
            initialState = SummarizeSettingsState(
                summarizePagesEnabled = true,
                shakeToSummarizeEnabled = true,
            ),
        )

        store.dispatch(ShakeToSummarizePreferenceToggled)

        assertFalse(settings.shakeToSummarizeEnabled)
    }

    @Test
    fun `WHEN learn more is clicked THEN callback is invoked`() {
        val middleware = buildMiddleware()
        val store = middleware.makeStore()

        store.dispatch(LearnMoreClicked)

        assertTrue(learnMoreClicked)
    }

    private fun buildMiddleware() = SummarizeSettingsMiddleware(
        settings = settings,
        onLearnMoreClicked = { learnMoreClicked = true },
    )

    private fun SummarizeSettingsMiddleware.makeStore(
        initialState: SummarizeSettingsState = SummarizeSettingsState(
            summarizePagesEnabled = false,
            shakeToSummarizeEnabled = false,
        ),
    ) = SummarizeSettingsStore(
        initialState = initialState,
        reducer = ::summarizeSettingsReducer,
        middleware = listOf(this),
    )
}
