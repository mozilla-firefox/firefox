/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import mozilla.components.lib.shake.ShakeSensitivity
import org.junit.Assert.assertEquals
import org.junit.Test

class PageSummariesSettingsReducerTest {

    @Test
    fun `WHEN summarize pages is toggled on THEN it is enabled in the state`() {
        val state =
            SummarizeSettingsState(
                isFeatureEnabled = false,
                isGestureEnabled = false,
            )
        val result = summarizeSettingsReducer(state, SummarizePagesPreferenceToggled)

        assertEquals(state.copy(isFeatureEnabled = true), result)
    }

    @Test
    fun `WHEN summarize pages is toggled off THEN it is disabled in the state`() {
        val state =
            SummarizeSettingsState(
                isFeatureEnabled = true,
                isGestureEnabled = true,
            )
        val result = summarizeSettingsReducer(state, SummarizePagesPreferenceToggled)

        assertEquals(state.copy(isFeatureEnabled = false), result)
    }

    @Test
    fun `WHEN shake to summarize is toggled on THEN it is enabled in the state`() {
        val state =
            SummarizeSettingsState(
                isFeatureEnabled = true,
                isGestureEnabled = false,
            )
        val result = summarizeSettingsReducer(state, ShakeToSummarizePreferenceToggled)

        assertEquals(state.copy(isGestureEnabled = true), result)
    }

    @Test
    fun `WHEN shake to summarize is toggled off THEN it is disabled in the state`() {
        val state =
            SummarizeSettingsState(
                isFeatureEnabled = true,
                isGestureEnabled = true,
            )
        val result = summarizeSettingsReducer(state, ShakeToSummarizePreferenceToggled)

        assertEquals(state.copy(isGestureEnabled = false), result)
    }

    @Test
    fun `WHEN shake sensitivity is changed THEN it is updated in the state`() {
        val state =
            SummarizeSettingsState(
                isFeatureEnabled = true,
                isGestureEnabled = true,
                shakeSensitivity = ShakeSensitivity.Medium,
            )
        val result =
            summarizeSettingsReducer(
                state,
                ShakeSensitivityChanged(ShakeSensitivity.High),
            )

        assertEquals(state.copy(shakeSensitivity = ShakeSensitivity.High), result)
    }

    @Test
    fun `WHEN learn more is clicked THEN the link is requested`() {
        val state =
            SummarizeSettingsState(
                isFeatureEnabled = true,
                isGestureEnabled = true,
                shakeSensitivity = ShakeSensitivity.High,
            )
        val result = summarizeSettingsReducer(state, LearnMoreClicked)

        assertEquals(state.copy(isLearnMoreRequested = true), result)
    }

    @Test
    fun `WHEN learn more is handled THEN the request is cleared`() {
        val state =
            SummarizeSettingsState(
                isFeatureEnabled = true,
                isGestureEnabled = true,
                isLearnMoreRequested = true,
            )
        val result = summarizeSettingsReducer(state, LearnMoreHandled)

        assertEquals(state.copy(isLearnMoreRequested = false), result)
    }

    @Test
    fun `WHEN summarize pages is toggled THEN sensitivity is the same`() {
        val state = SummarizeSettingsState(shakeSensitivity = ShakeSensitivity.High)
        val result =
            summarizeSettingsReducer(
                state,
                SummarizePagesPreferenceToggled,
            )
        assertEquals(ShakeSensitivity.High, result.shakeSensitivity)
    }
}
