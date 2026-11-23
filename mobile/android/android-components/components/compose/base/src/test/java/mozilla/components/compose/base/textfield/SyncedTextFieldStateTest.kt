/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.textfield

import androidx.compose.foundation.text.BasicTextField
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.LocalSaveableStateRegistry
import androidx.compose.runtime.saveable.SaveableStateRegistry
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.performTextClearance
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextInputSelection
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.text.TextRange
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

private const val FIELD_TAG = "field"

@RunWith(AndroidJUnit4::class)
class SyncedTextFieldStateTest {

    @get:Rule val composeTestRule = createComposeRule()

    private var value by mutableStateOf("")
    private val reported = mutableListOf<String>()

    private fun setContent(initialValue: String) {
        value = initialValue
        composeTestRule.setContent {
            val state =
                rememberSyncedTextFieldState(value) {
                    reported.add(it)
                    value = it
                }
            BasicTextField(state = state, modifier = Modifier.testTag(FIELD_TAG))
        }
    }

    @Test
    fun `WHEN the caller changes the value THEN the field shows it without reporting it back`() {
        setContent("mozilla")
        composeTestRule.onNodeWithTag(FIELD_TAG).assertTextEquals("mozilla")

        value = "firefox"

        composeTestRule.onNodeWithTag(FIELD_TAG).assertTextEquals("firefox")
        assertEquals(emptyList<String>(), reported)
    }

    @Test
    fun `WHEN the user edits the field THEN each edit is reported once`() {
        setContent("")

        composeTestRule.onNodeWithTag(FIELD_TAG).performTextReplacement("fire")
        composeTestRule.onNodeWithTag(FIELD_TAG).performTextReplacement("firefox")

        composeTestRule.onNodeWithTag(FIELD_TAG).assertTextEquals("firefox")
        assertEquals(listOf("fire", "firefox"), reported)
    }

    @Test
    fun `WHEN the user undoes an edit before the caller stores it THEN both edits are reported and the undo is kept`() {
        composeTestRule.mainClock.autoAdvance = false
        composeTestRule.setContent {
            val scope = rememberCoroutineScope()
            val state =
                rememberSyncedTextFieldState(value) { text ->
                    reported.add(text)
                    scope.launch {
                        delay(100)
                        value = text
                    }
                }
            BasicTextField(state = state, modifier = Modifier.testTag(FIELD_TAG))
        }
        val field = composeTestRule.onNodeWithTag(FIELD_TAG)

        field.performTextInput("a")
        field.performTextClearance()
        composeTestRule.mainClock.advanceTimeBy(1000)

        field.assertTextEquals("")
        assertEquals(listOf("a", ""), reported)
    }

    @Test
    fun `WHEN the user types in the middle of the text THEN the cursor stays after the typed character`() {
        setContent("ac")
        val field = composeTestRule.onNodeWithTag(FIELD_TAG)

        field.performTextInputSelection(TextRange(1))
        field.performTextInput("b")
        composeTestRule.waitForIdle()

        field.assertTextEquals("abc")
        assertEquals(TextRange(2), field.fetchSemanticsNode().config[SemanticsProperties.TextSelectionRange])
        assertEquals(listOf("abc"), reported)
    }

    @Test
    fun `WHEN the field holds text THEN the text is not saved to instance state`() {
        val registry = SaveableStateRegistry(restoredValues = null) { true }
        composeTestRule.setContent {
            CompositionLocalProvider(LocalSaveableStateRegistry provides registry) {
                BasicTextField(state = rememberSyncedTextFieldState("hunter2") {})
            }
        }
        composeTestRule.waitForIdle()

        assertFalse(registry.performSave().values.flatten().toString().contains("hunter2"))
    }
}
