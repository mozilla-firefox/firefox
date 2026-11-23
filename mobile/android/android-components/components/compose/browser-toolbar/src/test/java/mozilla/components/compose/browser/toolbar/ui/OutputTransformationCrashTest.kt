/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import androidx.compose.ui.platform.InterceptPlatformTextInput
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performClick
import java.util.concurrent.atomic.AtomicReference
import kotlin.random.Random
import kotlinx.coroutines.awaitCancellation
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import mozilla.components.concept.toolbar.AutocompleteResult
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner

@RunWith(RobolectricTestRunner::class)
class OutputTransformationCrashTest {

    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `WHEN deleteSurroundingText follows commitText in the same batch THEN it does not crash`() {
        val ic = setUpInlineAutocompleteField()
        composeTestRule.runOnUiThread {
            ic.beginBatchEdit()
            ic.commitText("o", 1)
            ic.deleteSurroundingText(0, SUGGESTION_SUFFIX_LENGTH)
            ic.endBatchEdit()
        }
    }

    @Test
    fun `WHEN commitText follows deleteSurroundingText in the same batch THEN it does not crash`() {
        val ic = setUpInlineAutocompleteField()
        composeTestRule.runOnUiThread {
            ic.beginBatchEdit()
            ic.deleteSurroundingText(1, 0)
            ic.commitText("o", 1)
            ic.endBatchEdit()
        }
    }

    @Test
    fun `WHEN IME edits run on an empty autocomplete field THEN they do not crash`() {
        val ic = setUpInlineAutocompleteField(query = "")
        composeTestRule.runOnUiThread {
            ic.beginBatchEdit()
            ic.commitText("m", 1)
            ic.deleteSurroundingText(0, SUGGESTION_SUFFIX_LENGTH)
            ic.endBatchEdit()
        }
    }

    @Test
    fun `WHEN random IME batch edits stress the autocomplete field THEN they do not crash`() {
        val ic = setUpInlineAutocompleteField()
        composeTestRule.runOnUiThread {
            val rng = Random(seed = 0xC0FFEEL)
            repeat(STRESS_ITERATIONS) {
                ic.beginBatchEdit()
                when (rng.nextInt(0, 6)) {
                    0 -> ic.commitText(randomText(rng), rng.nextInt(-2, 3))
                    1 -> ic.setComposingText(randomText(rng), rng.nextInt(-2, 3))
                    2 -> ic.setComposingRegion(rng.nextInt(0, 32), rng.nextInt(0, 32))
                    3 -> ic.setSelection(rng.nextInt(0, 32), rng.nextInt(0, 32))
                    4 -> ic.deleteSurroundingText(rng.nextInt(0, 32), rng.nextInt(0, 32))
                    5 -> ic.deleteSurroundingTextInCodePoints(rng.nextInt(0, 32), rng.nextInt(0, 32))
                }
                ic.endBatchEdit()
            }
        }
    }

    private fun setUpInlineAutocompleteField(query: String = INITIAL_QUERY): InputConnection {
        val captured = AtomicReference<InputConnection?>(null)
        val suggestion =
            AutocompleteResult(
                input = query,
                text = query + "x".repeat(SUGGESTION_SUFFIX_LENGTH),
                url = "",
                source = "test",
                totalItems = 1,
            )
        composeTestRule.setContent {
            InterceptPlatformTextInput(
                interceptor = { request, _ ->
                    // Robolectric does not attach an IME, so call createInputConnection
                    // ourselves and hold the interceptor scope open via awaitCancellation
                    // to keep the input session live; it cancels cleanly at test teardown.
                    captured.set(request.createInputConnection(EditorInfo()))
                    awaitCancellation()
                }
            ) {
                InlineAutocompleteTextField(
                    query = query,
                    hint = "",
                    suggestion = suggestion,
                    showQueryAsPreselected = false,
                    usePrivateModeQueries = false,
                )
            }
        }
        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performClick()
        composeTestRule.waitUntil(timeoutMillis = AWAIT_IC_TIMEOUT_MS) {
            captured.get() != null
        }
        return captured.get()!!
    }

    private fun randomText(rng: Random): CharSequence = buildString {
        repeat(rng.nextInt(0, 8)) {
            append(('a'.code + rng.nextInt(26)).toChar())
        }
    }

    companion object {
        const val INITIAL_QUERY = "m"
        const val SUGGESTION_SUFFIX_LENGTH = 200
        const val STRESS_ITERATIONS = 200
        const val AWAIT_IC_TIMEOUT_MS = 5_000L
    }
}
