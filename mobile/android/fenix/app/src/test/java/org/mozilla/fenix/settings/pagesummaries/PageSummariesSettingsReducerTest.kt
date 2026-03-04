/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.pagesummaries

import mozilla.components.feature.summarize.settings.LearnMoreClicked
import mozilla.components.feature.summarize.settings.ShakeToSummarizePreferenceToggled
import mozilla.components.feature.summarize.settings.SummarizePagesPreferenceToggled
import mozilla.components.feature.summarize.settings.SummarizeSettingsState
import mozilla.components.feature.summarize.settings.summarizeSettingsReducer
import org.junit.Assert.assertEquals
import org.junit.Test

class PageSummariesSettingsReducerTest {

    @Test
    fun `WHEN summarize pages is toggled on THEN it is enabled in the state`() {
        val state = SummarizeSettingsState(
            summarizePagesEnabled = false,
            shakeToSummarizeEnabled = false,
        )
        val result = summarizeSettingsReducer(state, SummarizePagesPreferenceToggled)

        assertEquals(state.copy(summarizePagesEnabled = true), result)
    }

    @Test
    fun `WHEN summarize pages is toggled off THEN it is disabled in the state`() {
        val state = SummarizeSettingsState(
            summarizePagesEnabled = true,
            shakeToSummarizeEnabled = true,
        )
        val result = summarizeSettingsReducer(state, SummarizePagesPreferenceToggled)

        assertEquals(state.copy(summarizePagesEnabled = false), result)
    }

    @Test
    fun `WHEN shake to summarize is toggled on THEN it is enabled in the state`() {
        val state = SummarizeSettingsState(
            summarizePagesEnabled = true,
            shakeToSummarizeEnabled = false,
        )
        val result = summarizeSettingsReducer(state, ShakeToSummarizePreferenceToggled)

        assertEquals(state.copy(shakeToSummarizeEnabled = true), result)
    }

    @Test
    fun `WHEN shake to summarize is toggled off THEN it is disabled in the state`() {
        val state = SummarizeSettingsState(
            summarizePagesEnabled = true,
            shakeToSummarizeEnabled = true,
        )
        val result = summarizeSettingsReducer(state, ShakeToSummarizePreferenceToggled)

        assertEquals(state.copy(shakeToSummarizeEnabled = false), result)
    }

    @Test
    fun `WHEN learn more is clicked THEN state is unchanged`() {
        val state = SummarizeSettingsState(
            summarizePagesEnabled = true,
            shakeToSummarizeEnabled = true,
        )
        val result = summarizeSettingsReducer(state, LearnMoreClicked)

        assertEquals(state, result)
    }
}
