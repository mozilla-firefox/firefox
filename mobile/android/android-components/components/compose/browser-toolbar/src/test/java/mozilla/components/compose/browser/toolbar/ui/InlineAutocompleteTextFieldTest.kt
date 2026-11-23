/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.content.Context
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputMethodManager
import android.widget.Magnifier
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalTextToolbar
import androidx.compose.ui.platform.SoftwareKeyboardController
import androidx.compose.ui.platform.TextToolbar
import androidx.compose.ui.platform.TextToolbarStatus
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotDisplayed
import androidx.compose.ui.test.assertTextEquals
import androidx.compose.ui.test.click
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.longClick
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performImeAction
import androidx.compose.ui.test.performTextInput
import androidx.compose.ui.test.performTextInputSelection
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.text.TextRange
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import mozilla.components.concept.toolbar.AutocompleteResult
import mozilla.components.support.test.any
import mozilla.components.support.test.eq
import mozilla.components.support.test.mock
import mozilla.components.support.test.robolectric.testContext
import mozilla.components.support.test.whenever
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith
import org.mockito.Mockito.atLeastOnce
import org.mockito.Mockito.never
import org.mockito.Mockito.verify
import org.robolectric.RobolectricTestRunner
import org.robolectric.Shadows.shadowOf
import org.robolectric.annotation.Config
import org.robolectric.annotation.Implementation
import org.robolectric.annotation.Implements

@RunWith(RobolectricTestRunner::class)
class InlineAutocompleteTextFieldTest {

    @get:Rule val composeTestRule = createComposeRule()

    @Test
    fun `WHEN the query is updated THEN inform callbacks`() {
        val onUrlEdit: (BrowserToolbarQuery) -> Unit = mock()

        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "",
                hint = "Search or enter address",
                suggestion = null,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = onUrlEdit,
            )
        }

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTextReplacement("hello")
        verify(onUrlEdit).invoke(BrowserToolbarQuery(current = "hello", previous = ""))

        composeTestRule.onNodeWithText("hello").performTextInput(" world")
        verify(onUrlEdit).invoke(BrowserToolbarQuery(current = "hello world", previous = "hello"))
    }

    @Test
    fun `GIVEN a query WHEN an autocomplete suggestion is available THEN display the autocompleted query`() {
        val suggestion =
            AutocompleteResult(
                input = "moz",
                text = "mozilla.org",
                url = "https://mozilla.org",
                source = "test",
                totalItems = 1,
            )

        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "moz",
                hint = "",
                suggestion = suggestion,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
            )
        }

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).assertTextEquals("mozilla.org")
    }

    @Test
    fun `GIVEN an autocomplete suggestion is shown WHEN the query is tapped, THEN don't do anything`() {
        val onUrlEdit: (BrowserToolbarQuery) -> Unit = mock()
        val suggestion =
            AutocompleteResult(
                input = "w",
                text = "wikipedia.org",
                url = "https://wikipedia.org",
                source = "test",
                totalItems = 1,
            )

        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "w",
                hint = "",
                suggestion = suggestion,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = onUrlEdit,
            )
        }

        // Tapping on the very left is where the query is shown.
        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTouchInput { click(position = centerLeft) }

        verify(onUrlEdit, never()).invoke(any())
    }

    @Test
    fun `GIVEN an autocomplete suggestion is shown WHEN tapping outside the query, THEN commit the autocomplete suggestion`() {
        val onUrlEdit: (BrowserToolbarQuery) -> Unit = mock()
        val suggestion =
            AutocompleteResult(
                input = "w",
                text = "wikipedia.org",
                url = "https://wikipedia.org",
                source = "test",
                totalItems = 1,
            )

        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "w",
                hint = "",
                suggestion = suggestion,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = onUrlEdit,
            )
        }

        // Tapping on the very right is to the outside of the current query.
        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTouchInput { click(position = centerRight) }

        verify(onUrlEdit).invoke(BrowserToolbarQuery(previous = "w", current = "wikipedia.org"))
    }

    @Test
    fun `GIVEN a query and suggestion are shown WHEN backspace is first pressed THEN only clear the suggestion`() {
        val onUrlEdit: (BrowserToolbarQuery) -> Unit = mock()
        val suggestion =
            AutocompleteResult(
                input = "moz",
                text = "mozilla.org",
                url = "https://mozilla.org",
                source = "test",
                totalItems = 1,
            )

        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "moz",
                hint = "",
                suggestion = suggestion,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = onUrlEdit,
            )
        }

        composeTestRule.onNodeWithText("mozilla.org").assertIsDisplayed()

        // Simulate a backspace normally deleting text.
        composeTestRule.onNodeWithText("mozilla.org").performTextReplacement("mo")
        // The text should now be the original query, without the suggestion.
        composeTestRule.onNodeWithText("moz").assertIsDisplayed()
        composeTestRule.onNodeWithText("mozilla.org").assertDoesNotExist()
        verify(onUrlEdit, never()).invoke(any())

        // Simulate a second backspace.
        composeTestRule.onNodeWithText("moz").performTextReplacement("mo")
        // Now the last character of the query should be deleted and the callback notified.
        composeTestRule.onNodeWithText("mo").assertIsDisplayed()
        composeTestRule.onNodeWithText("moz").assertDoesNotExist()
        verify(onUrlEdit).invoke(BrowserToolbarQuery(previous = "moz", current = "mo"))
    }

    @Test
    fun `GIVEN the contextual menu is shown WHEN the query is edited THEN hide the contextual menu`() {
        val contextualMenuToolbar: TextToolbar = mock()
        whenever(contextualMenuToolbar.status).thenReturn(TextToolbarStatus.Shown)
        composeTestRule.setContent {
            CompositionLocalProvider(LocalTextToolbar provides contextualMenuToolbar) {
                InlineAutocompleteTextField(
                    query = "moz",
                    hint = "",
                    suggestion = null,
                    showQueryAsPreselected = false,
                    usePrivateModeQueries = false,
                )
            }
        }

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTextInput("a")
        composeTestRule.waitForIdle()

        verify(contextualMenuToolbar, atLeastOnce()).hide()
    }

    @Test
    @Config(shadows = [ShadowMagnifier::class])
    fun `GIVEN an autocomplete suggestion is shown WHEN a selection is started THEN the typed query and the suggestion are selected`() {
        val suggestion =
            AutocompleteResult(
                input = "wiki",
                text = "wikipedia.org",
                url = "https://wikipedia.org",
                source = "test",
                totalItems = 1,
            )

        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "wiki",
                hint = "",
                suggestion = suggestion,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = {},
            )
        }

        // Simulate a long press on the currently typed text to select it + its shown suggestion
        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTouchInput { longClick(position = centerLeft) }
        composeTestRule.waitForIdle()

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).assertTextEquals("wikipedia.org")
    }

    @Test
    fun `GIVEN an autocomplete suggestion is shown WHEN the text is selected and deleted via the IME THEN delete the typed text without committing the suggestion`() {
        val onUrlEdit: (BrowserToolbarQuery) -> Unit = mock()
        val suggestion =
            AutocompleteResult(
                input = "wiki",
                text = "wikipedia.org",
                url = "https://wikipedia.org",
                source = "test",
                totalItems = 1,
            )

        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "wiki",
                hint = "",
                suggestion = suggestion,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = onUrlEdit,
            )
        }

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTextInputSelection(TextRange(0, "wiki".length))
        composeTestRule.waitForIdle()
        verify(onUrlEdit, never()).invoke(any())

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performTextReplacement("")
        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).assertTextEquals("")
        verify(onUrlEdit).invoke(BrowserToolbarQuery(previous = "wiki", current = ""))
    }

    @Test
    fun `WHEN query is empty THE the hint is displayed`() {
        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "",
                hint = "Search or enter address",
                suggestion = null,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = {},
            )
        }

        composeTestRule.onNodeWithText("Search or enter address").assertIsDisplayed()
    }

    @Test
    fun `WHEN query is not empty THE the hint is not displayed`() {
        composeTestRule.setContent {
            InlineAutocompleteTextField(
                query = "test",
                hint = "Search or enter address",
                suggestion = null,
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                onUrlEdit = {},
            )
        }

        composeTestRule.onNodeWithText("Search or enter address").assertIsNotDisplayed()
    }

    @Test
    fun `WHEN disabling personalized learning for the IME THEN set the right ime option`() {
        val editorInfo = EditorInfo()

        NoPersonalizedLearningHelper.addNoPersonalizedLearning(editorInfo)

        assertTrue(editorInfo.imeOptions and EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING != 0)
    }

    @Test
    fun `GIVEN a query but no suggestion WHEN the IME action button is tapped THEN hide the IME and inform callbacks of the query accepted`() {
        val userQuery = "mozilla"
        val keyboardController: SoftwareKeyboardController = mock()
        val urlCommitedCallback: (String) -> Unit = mock()
        composeTestRule.setContent {
            CompositionLocalProvider(LocalSoftwareKeyboardController provides keyboardController) {
                InlineAutocompleteTextField(
                    query = userQuery,
                    hint = "test",
                    suggestion = null, // No suggestion
                    showQueryAsPreselected = false,
                    usePrivateModeQueries = false,
                    onUrlCommitted = urlCommitedCallback,
                )
            }
        }

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performImeAction()

        verify(keyboardController, atLeastOnce()).hide()
        verify(urlCommitedCallback).invoke(userQuery)
    }

    @Test
    fun `GIVEN a query and suggestion WHEN the IME action button is tapped THEN hide the IME and inform callbacks of the suggestion accepted`() {
        val userQuery = "wiki"
        val suggestion =
            AutocompleteResult(
                input = "wiki",
                text = "wikipedia.org",
                url = "https://wikipedia.org",
                source = "test",
                totalItems = 1,
            )
        val keyboardController: SoftwareKeyboardController = mock()
        val urlCommitedCallback: (String) -> Unit = mock()
        composeTestRule.setContent {
            CompositionLocalProvider(LocalSoftwareKeyboardController provides keyboardController) {
                InlineAutocompleteTextField(
                    query = userQuery,
                    hint = "test",
                    suggestion = suggestion,
                    showQueryAsPreselected = false,
                    usePrivateModeQueries = false,
                    onUrlCommitted = urlCommitedCallback,
                )
            }
        }

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performImeAction()

        verify(keyboardController, atLeastOnce()).hide()
        verify(urlCommitedCallback).invoke(suggestion.text)
    }

    @Test
    fun `GIVEN no query and no suggestion WHEN the IME action button is tapped THEN hide keyboard and inform callbacks`() {
        val userQuery = ""
        val keyboardController: SoftwareKeyboardController = mock()
        val urlCommitedCallback: (String) -> Unit = mock()
        composeTestRule.setContent {
            CompositionLocalProvider(LocalSoftwareKeyboardController provides keyboardController) {
                InlineAutocompleteTextField(
                    query = userQuery,
                    hint = "test",
                    suggestion = null, // No suggestion
                    showQueryAsPreselected = false,
                    usePrivateModeQueries = false,
                    onUrlCommitted = urlCommitedCallback,
                )
            }
        }

        composeTestRule.onNodeWithTag(ADDRESSBAR_SEARCH_BOX).performImeAction()

        verify(keyboardController, atLeastOnce()).hide()
        verify(urlCommitedCallback).invoke(userQuery)
    }

    // Unit tests for the IME-driven accept heuristic used by autocompleteInputConnection.
    // The function translates three distinct keyboard cursor-control gestures into a
    // suggestion accept signal:
    //   - the cursor moves from inside the typed query to its end,
    //   - the redundant setSelection-at-end pattern emitted by Gboard's spacebar swipe,
    //   - a position inside the visible suggestion suffix emitted by IMEs that read
    //     the OutputTransformation-rendered text.
    // Each branch is covered along with the main negative cases that must not accept.

    @Test
    fun `GIVEN cursor was inside the real text WHEN setSelection moves it to the end of real text THEN should accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(2),
                requestedSelectionStart = 4,
                requestedSelectionEnd = 4,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertTrue(result)
    }

    @Test
    fun `GIVEN cursor was already at the end WHEN setSelection redundantly requests cursor at end THEN should accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(4),
                requestedSelectionStart = 4,
                requestedSelectionEnd = 4,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertTrue(result)
    }

    @Test
    fun `GIVEN cursor was at the end WHEN setSelection moves cursor into the visible suggestion suffix THEN should accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(4),
                requestedSelectionStart = 8,
                requestedSelectionEnd = 8,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertTrue(result)
    }

    @Test
    fun `GIVEN cursor was at the end WHEN setSelection moves cursor to the very end of the visible suggestion THEN should accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(4),
                requestedSelectionStart = 13,
                requestedSelectionEnd = 13,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertTrue(result)
    }

    @Test
    fun `GIVEN text changed since last selection WHEN setSelection requests cursor at end THEN should not accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(4),
                requestedSelectionStart = 4,
                requestedSelectionEnd = 4,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = true,
            )

        assertFalse(result)
    }

    @Test
    fun `GIVEN setSelection requests a selection range instead of a collapsed cursor THEN should not accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(2),
                requestedSelectionStart = 4,
                requestedSelectionEnd = 5,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertFalse(result)
    }

    @Test
    fun `GIVEN setSelection requests a position past the suggestion end THEN should not accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(4),
                requestedSelectionStart = 14,
                requestedSelectionEnd = 14,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertFalse(result)
    }

    @Test
    fun `GIVEN no active suggestion WHEN setSelection requests a position past real text end THEN should not accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(4),
                requestedSelectionStart = 8,
                requestedSelectionEnd = 8,
                suggestionLength = null,
                hasTextChangedSinceLastSelection = false,
            )

        assertFalse(result)
    }

    @Test
    fun `GIVEN setSelection moves cursor within real text but not to its end THEN should not accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(4),
                requestedSelectionStart = 2,
                requestedSelectionEnd = 2,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertFalse(result)
    }

    @Test
    fun `GIVEN oldSelection was a range WHEN setSelection collapses cursor to end of real text THEN should not accept`() {
        val result =
            shouldAcceptSuggestionOnSelectionUpdate(
                currentTextLength = 4,
                oldSelection = TextRange(2, 3),
                requestedSelectionStart = 4,
                requestedSelectionEnd = 4,
                suggestionLength = 13,
                hasTextChangedSinceLastSelection = false,
            )

        assertFalse(result)
    }

    // End of autocompleteInputConnection specific tests

    @Test
    fun `WHEN the text field is removed from composition THEN the keyboard is hidden`() {
        val imm = shadowOf(testContext.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager)
        var isTextFieldShown by mutableStateOf(true)

        // `setContent` can only be called once, so toggle the field in/out of composition with state.
        composeTestRule.setContent {
            if (isTextFieldShown) {
                InlineAutocompleteTextField(
                    query = "",
                    hint = "test",
                    suggestion = null, // No suggestion
                    showQueryAsPreselected = false,
                    usePrivateModeQueries = false,
                    onUrlCommitted = {},
                )
            }
        }
        composeTestRule.waitForIdle()
        assertTrue(imm.isSoftInputVisible)

        // Remove the text field from composition.
        composeTestRule.runOnIdle { isTextFieldShown = false }
        composeTestRule.waitForIdle()
        assertFalse(imm.isSoftInputVisible)
    }

    @Test
    fun `GIVEN all actions are possible WHEN showing the contextual menu THEN all are made available`() {
        val delegate: TextToolbar = mock()
        val copyAction = {}
        val pasteAction = {}
        val cutAction = {}
        val selectAllAction = {}
        val autofillAction = {}

        val toolbar =
            PasteSanitizerTextToolbar(
                context = testContext,
                delegate = delegate,
                clipboard = mock(),
                scope = mock(),
                handlePaste = {},
            )

        toolbar.showMenu(
            rect = Rect.Zero,
            onCopyRequested = copyAction,
            onPasteRequested = pasteAction,
            onCutRequested = cutAction,
            onSelectAllRequested = selectAllAction,
            onAutofillRequested = autofillAction,
        )

        verify(delegate)
            .showMenu(
                rect = eq(Rect.Zero),
                onCopyRequested = eq(copyAction),
                onPasteRequested = any(),
                onCutRequested = eq(cutAction),
                onSelectAllRequested = eq(selectAllAction),
                onAutofillRequested = eq(autofillAction),
            )
    }

    @Test
    fun `GIVEN no action is possible WHEN showing the contextual menu THEN the no actions are shown`() {
        val delegate: TextToolbar = mock()
        val toolbar =
            PasteSanitizerTextToolbar(
                context = testContext,
                delegate = delegate,
                clipboard = mock(),
                scope = mock(),
                handlePaste = {},
            )

        toolbar.showMenu(
            rect = Rect.Zero,
            onCopyRequested = null,
            onPasteRequested = null,
            onCutRequested = null,
            onSelectAllRequested = null,
            onAutofillRequested = null,
        )

        verify(delegate)
            .showMenu(
                rect = Rect.Zero,
                onCopyRequested = null,
                onPasteRequested = null,
                onCutRequested = null,
                onSelectAllRequested = null,
                onAutofillRequested = null,
            )
    }
}

@Implements(Magnifier::class)
internal class ShadowMagnifier {
    @Implementation
    fun show(
        @Suppress("UNUSED_PARAMETER") sourceCenterX: Float,
        @Suppress("UNUSED_PARAMETER") sourceCenterY: Float,
    ) {}

    @Implementation fun dismiss() {}

    @Implementation fun update() {}
}
