/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.content.Context
import android.text.Spanned
import android.view.KeyEvent
import android.view.inputmethod.EditorInfo
import android.view.inputmethod.InputConnection
import android.view.inputmethod.InputConnectionWrapper
import android.view.inputmethod.InputMethodManager
import androidx.annotation.DoNotInline
import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.ComposeFoundationFlags
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.awaitLongPressOrCancellation
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.InputTransformation
import androidx.compose.foundation.text.input.OutputTransformation
import androidx.compose.foundation.text.input.TextFieldBuffer
import androidx.compose.foundation.text.input.TextFieldLineLimits
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.rememberTextFieldState
import androidx.compose.foundation.text.selection.LocalTextSelectionColors
import androidx.compose.foundation.text.selection.TextSelectionColors
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.key.Key
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.key
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.Clipboard
import androidx.compose.ui.platform.InterceptPlatformTextInput
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalTextToolbar
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.PlatformTextInputInterceptor
import androidx.compose.ui.platform.PlatformTextInputMethodRequest
import androidx.compose.ui.platform.TextToolbar
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.sp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.autofillText
import mozilla.components.compose.base.theme.selectedText
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import mozilla.components.concept.toolbar.AutocompleteResult
import mozilla.components.support.base.utils.MAX_URI_LENGTH
import mozilla.components.support.ktx.kotlin.trimmed
import mozilla.components.support.utils.SafeUrl

private const val TEXT_SIZE = 15f
private const val MIN_CHAR_COUNT_FOR_MULTILINE = 10_000

/**
 * A text field composable that displays a suggestion inline with the user's input, styled differently to distinguish it
 * from the typed text.
 *
 * @param query The query to show.
 * @param hint Placeholder text tpo show if [query] is empty.
 * @param suggestion The autocomplete suggestion to display. `null` if no suggestion is active.
 * @param showQueryAsPreselected If `true`, the initial query text will be fully selected.
 * @param usePrivateModeQueries If `true`, instructs the keyboard to disable personalized learning, suitable for
 *   private/incognito modes.
 * @param modifier The [Modifier] to be applied to this text field.
 * @param onUrlEdit Callback invoked when the user types or deletes text, providing [BrowserToolbarQuery] with
 *   information about the previous and the new query.
 * @param onUrlCommitted A callback for when the user commits the text via an IME action like "Go".
 */
@Suppress("LongMethod", "CyclomaticComplexMethod", "CognitiveComplexMethod")
@Composable
internal fun InlineAutocompleteTextField(
    query: String,
    hint: String,
    suggestion: AutocompleteResult?,
    showQueryAsPreselected: Boolean,
    usePrivateModeQueries: Boolean,
    modifier: Modifier = Modifier,
    onUrlEdit: (BrowserToolbarQuery) -> Unit = {},
    onUrlCommitted: (String) -> Unit = {},
) {
    // Bound the editable text to a safe maximum to avoid any UX instability.
    val query = query.trimmed()
    val textFieldState =
        rememberTextFieldState(
            initialText = query,
            initialSelection =
                when {
                    showQueryAsPreselected -> TextRange(0, query.length)
                    else -> TextRange(query.length)
                },
        )
    // A single line cannot be laid out by the platform above a certain length,
    // so wrap very long URLs over multiple lines instead.
    // Temporary workaround for https://issuetracker.google.com/issues/527276313.
    val lineLimits =
        remember(query) {
            when (query.length >= MIN_CHAR_COUNT_FOR_MULTILINE) {
                true -> TextFieldLineLimits.MultiLine()
                false -> TextFieldLineLimits.SingleLine
            }
        }
    var useSuggestion by remember { mutableStateOf(true) }
    // Properties referenced in long lived lambdas
    val currentSuggestion by rememberUpdatedState(suggestion)
    val currentUseSuggestion by rememberUpdatedState(useSuggestion)
    val currentOnUrlEdit by rememberUpdatedState(onUrlEdit)

    val focusRequester = remember { FocusRequester() }
    val keyboardController = LocalSoftwareKeyboardController.current

    val suggestionTextColor = MaterialTheme.colorScheme.onSurface
    val highlightBackgroundColor = MaterialTheme.colorScheme.autofillText

    // Set the text field selection colors locally so that the colors will not be overridden when
    // nested in `MaterialTheme`.
    val textSelectionColors =
        TextSelectionColors(
            handleColor = MaterialTheme.colorScheme.primary,
            backgroundColor = MaterialTheme.colorScheme.selectedText,
        )

    var suggestionBounds by remember { mutableStateOf<Rect?>(null) }
    val deviceLayoutDirection = LocalLayoutDirection.current
    val scrollState = rememberScrollState()

    val context = LocalContext.current
    val localView = LocalView.current
    val inputMethodManager =
        remember(context) {
            context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
        }
    val defaultTextToolbar = LocalTextToolbar.current
    val clipboard = LocalClipboard.current
    val coroutineScope = rememberCoroutineScope()
    val pasteInterceptorToolbar =
        remember(defaultTextToolbar, clipboard) {
            PasteSanitizerTextToolbar(context, defaultTextToolbar, clipboard, coroutineScope) {
                val originalText = textFieldState
                textFieldState.edit {
                    replace(originalText.selection.start, originalText.selection.end, it)
                }
                currentOnUrlEdit(
                    BrowserToolbarQuery(
                        previous = originalText.text.toString(),
                        current = textFieldState.text.toString(),
                    )
                )
            }
        }
    DisposableEffect(Unit) {
        onDispose {
            pasteInterceptorToolbar.hide()
            // Ensure the IME is dismissed if this field leaves composition while still focused,
            // which can otherwise race with the focus-loss handler and leave the keyboard open.
            // Using the InputMethodManager will ensure the IME is hidden regardless of whether the
            // Compose text input session is still active, which `keyboardController.hide()` relies on.
            inputMethodManager.hideSoftInputFromWindow(localView.windowToken, 0)
        }
    }

    // Dismiss the text selection/contextual menu as soon as the user edits the text.
    LaunchedEffect(Unit) {
        snapshotFlow { textFieldState.text.toString() }.collect { pasteInterceptorToolbar.hide() }
    }

    LaunchedEffect(Unit) {
        focusRequester.requestFocus()
    }

    LaunchedEffect(query) {
        if (query != textFieldState.text.toString()) {
            textFieldState.edit {
                replace(0, length, query)
                selection = TextRange(query.length)
            }
        }
    }

    LaunchedEffect(suggestion) {
        if (useSuggestion) {
            suggestion?.text?.let {
                @Suppress("DEPRECATION") localView.announceForAccessibility(it)
            }
        }
    }

    val textInputInterceptor =
        remember(usePrivateModeQueries) {
            PlatformTextInputInterceptor { request, nextHandler ->
                val modifiedRequest = PlatformTextInputMethodRequest { outAttributes ->
                    val delegate = request.createInputConnection(outAttributes)

                    if (usePrivateModeQueries) {
                        NoPersonalizedLearningHelper.addNoPersonalizedLearning(outAttributes)
                    }

                    autocompleteInputConnection(
                        delegate = delegate,
                        currentText = { textFieldState.text.toString() },
                        selection = { textFieldState.selection },
                        suggestionText = { currentSuggestion?.text },
                        handleCommitingSuggestion = accept@{
                                val suggestionText = currentSuggestion?.text ?: return@accept false
                                val previousText = textFieldState.text.toString()

                                val isCurrentSuggestionValid =
                                    isCurrentSuggestionValid(
                                        originalText = previousText,
                                        suggestion = suggestionText,
                                        shouldUseSuggestion = currentUseSuggestion,
                                    )
                                if (!isCurrentSuggestionValid) {
                                    return@accept false
                                }

                                currentOnUrlEdit(
                                    BrowserToolbarQuery(
                                        previous = previousText,
                                        current = suggestionText,
                                    )
                                )

                                textFieldState.edit {
                                    replace(0, length, suggestionText)
                                    selection = TextRange(suggestionText.length)
                                }

                                true
                            },
                    )
                }

                nextHandler.startInputMethod(modifiedRequest)
            }
        }

    // Always want the text to be entered left to right.
    CompositionLocalProvider(
        LocalLayoutDirection provides LayoutDirection.Ltr,
        LocalTextToolbar provides pasteInterceptorToolbar,
        LocalTextSelectionColors provides textSelectionColors,
    ) {
        // Set incognito mode for the keyboard when needed.
        InterceptPlatformTextInput(interceptor = textInputInterceptor) {
            BasicTextField(
                state = textFieldState,
                modifier =
                    modifier
                        .testTag(ADDRESSBAR_SEARCH_BOX)
                        .fillMaxWidth()
                        .onFocusChanged { focusState ->
                            if (focusState.isFocused) {
                                keyboardController?.show()
                            } else {
                                keyboardController?.hide()
                            }
                        }
                        .focusRequester(focusRequester)
                        .onPreviewKeyEvent { keyEvent ->
                            if (
                                keyEvent.type == KeyEventType.KeyUp &&
                                    (keyEvent.key == Key.DirectionRight || keyEvent.key == Key.MoveEnd)
                            ) {
                                val currentText = textFieldState.text.toString()
                                val suggestionText = currentSuggestion?.text

                                if (
                                    suggestionText != null &&
                                        isCurrentSuggestionValid(
                                            originalText = currentText,
                                            suggestion = suggestionText,
                                            shouldUseSuggestion = currentUseSuggestion,
                                        )
                                ) {
                                    currentOnUrlEdit(
                                        BrowserToolbarQuery(
                                            previous = currentText,
                                            current = suggestionText,
                                        )
                                    )
                                    textFieldState.edit {
                                        replace(0, length, suggestionText)
                                        selection = TextRange(suggestionText.length)
                                    }
                                    val imm =
                                        context.getSystemService(Context.INPUT_METHOD_SERVICE) as InputMethodManager
                                    imm.updateSelection(
                                        localView,
                                        suggestionText.length,
                                        suggestionText.length,
                                        -1,
                                        -1,
                                    )
                                    true
                                } else {
                                    false
                                }
                            } else {
                                false
                            }
                        },
                textStyle =
                    TextStyle(
                        fontSize = TEXT_SIZE.sp,
                        color = MaterialTheme.colorScheme.onSurface,
                        textAlign =
                            when (deviceLayoutDirection) {
                                LayoutDirection.Ltr -> TextAlign.Start
                                LayoutDirection.Rtl -> TextAlign.End
                            },
                    ),
                lineLimits = lineLimits,
                scrollState = scrollState,
                keyboardOptions =
                    KeyboardOptions(
                        showKeyboardOnFocus = true,
                        keyboardType = KeyboardType.Uri,
                        imeAction = ImeAction.Go,
                        autoCorrectEnabled = false,
                    ),
                onKeyboardAction = {
                    keyboardController?.hide()
                    val currentText = textFieldState.text.toString()
                    val finalUrl =
                        if (useSuggestion && suggestion?.text?.startsWith(currentText) == true) {
                            suggestion.text
                        } else {
                            currentText
                        }
                    onUrlCommitted(finalUrl)
                },
                inputTransformation =
                    remember(onUrlEdit) {
                        AutocompleteInputTransformation(
                            suggestion = { currentSuggestion },
                            shouldUseSuggestion = { currentUseSuggestion },
                            onSuggestionVisibilityChangeRequest = { useSuggestion = it },
                            onUrlEdit = onUrlEdit,
                        )
                    },
                outputTransformation =
                    remember(suggestionTextColor) {
                        AutocompleteOutputTransformation(
                            suggestion = { currentSuggestion },
                            shouldUseSuggestion = { currentUseSuggestion },
                            textColor = suggestionTextColor,
                            textBackground = highlightBackgroundColor,
                        )
                    },
                cursorBrush = SolidColor(MaterialTheme.colorScheme.primary),
                onTextLayout = { layoutResult ->
                    val currentInput = textFieldState.text
                    suggestionBounds =
                        when (currentInput.isEmpty()) {
                            true -> null
                            false ->
                                try {
                                    layoutResult()?.getBoundingBox(currentInput.length - 1)
                                } catch (_: IllegalArgumentException) {
                                    null
                                }
                        }
                },
                decorator = { innerTextField ->
                    AutocompleteDecorator(
                        hint = hint,
                        suggestion =
                            when {
                                useSuggestion -> currentSuggestion
                                else -> null
                            },
                        onSuggestionVisibilityChangeRequest = { useSuggestion = it },
                        suggestionBounds = suggestionBounds,
                        textFieldState = textFieldState,
                        onUrlEdit = onUrlEdit,
                        deviceLayoutDirection = deviceLayoutDirection,
                        innerTextField = innerTextField,
                    )
                },
            )
        }
    }
}

/**
 * Information about the current browser toolbar query.
 *
 * @property current The current query.
 * @property previous The previous query, if any.
 */
data class BrowserToolbarQuery(
    val current: String,
    val previous: String? = null,
)

/** Helper for removing the suggestion or delete from the user query when backspace is pressed. */
private class AutocompleteInputTransformation(
    private val suggestion: () -> AutocompleteResult?,
    private val shouldUseSuggestion: () -> Boolean,
    private val onSuggestionVisibilityChangeRequest: (Boolean) -> Unit,
    private val onUrlEdit: (BrowserToolbarQuery) -> Unit,
) : InputTransformation {
    override fun TextFieldBuffer.transformInput() {
        val originalText = originalText.toString()
        val newText = asCharSequence().toString()
        val suggestion = suggestion()?.text

        val isBackspace = originalText.length > newText.length && originalText.startsWith(newText)
        val isSuggestionVisible = isCurrentSuggestionValid(originalText, suggestion, shouldUseSuggestion())
        val isCursorAtQueryEnd = originalSelection.collapsed && originalSelection.end == originalText.length

        if (isBackspace) {
            onSuggestionVisibilityChangeRequest(false)

            val isBackspaceHidingSuggestion = isCursorAtQueryEnd && isSuggestionVisible
            if (isBackspaceHidingSuggestion) {
                // Avoid deleting text, just hide the suggestion.
                revertAllChanges()
            } else {
                // Actually delete text and hide the suggestion.
                onUrlEdit(BrowserToolbarQuery(previous = originalText, current = newText))
            }
        } else {
            if (originalText != newText) {
                onSuggestionVisibilityChangeRequest(true)
                onUrlEdit(BrowserToolbarQuery(previous = originalText, current = newText))
            }
        }
    }
}

/** Helper for showing the autocomplete suggestion inline with user's input. */
private class AutocompleteOutputTransformation(
    private val suggestion: () -> AutocompleteResult?,
    private val shouldUseSuggestion: () -> Boolean,
    private val textColor: Color,
    private val textBackground: Color,
) : OutputTransformation {
    override fun TextFieldBuffer.transformOutput() {
        val userInput = asCharSequence()
        val suggestion = suggestion()
        if (
            !shouldUseSuggestion() ||
                suggestion?.text?.isEmpty() == true ||
                suggestion?.text?.startsWith(userInput) == false
        ) {
            return
        }

        val suffix = suggestion?.text?.removePrefix(userInput) ?: return
        if (suffix.isNotEmpty()) {
            val originalLength = length
            append(suffix)
            addStyle(
                SpanStyle(
                    color = textColor,
                    background = textBackground,
                ),
                originalLength,
                length,
            )
        }
    }
}

/**
 * Helper for handling the text shown to the user:
 * - show the current query or hint if query is empty.
 * - dismisses the suggestion if cursor is placed in query.
 * - commits the suggestion if cursor is placed in the suggestion or after it.
 */
@Composable
@Suppress("LongParameterList", "CognitiveComplexMethod")
private fun AutocompleteDecorator(
    hint: String,
    suggestion: AutocompleteResult?,
    onSuggestionVisibilityChangeRequest: (Boolean) -> Unit,
    suggestionBounds: Rect?,
    textFieldState: TextFieldState,
    onUrlEdit: (BrowserToolbarQuery) -> Unit,
    deviceLayoutDirection: LayoutDirection,
    innerTextField: @Composable () -> Unit,
) {
    // Stop using the suggestion if cursor is moved manually away from the end.
    LaunchedEffect(textFieldState) {
        snapshotFlow { textFieldState.selection }
            .collectLatest {
                if (it.end != textFieldState.text.length) {
                    onSuggestionVisibilityChangeRequest(false)
                }
            }
    }

    Box(
        modifier =
            Modifier.fillMaxWidth()
                // Commit the suggestion when users tap on the outside of the typed in text.
                .pointerInput(suggestion, suggestionBounds) {
                    awaitEachGesture {
                        val downEvent = awaitFirstDown(requireUnconsumed = false)
                        val suggestionText = suggestion?.text
                        val currentText = textFieldState.text.toString()
                        when {
                            suggestionText == null -> {
                                // No suggestion shown, nothing to commit.
                            }

                            // Tapping outside the typed text, to the right, commits the suggestion
                            // and places the cursor at the end.
                            suggestionBounds != null && suggestionBounds.right < downEvent.position.x -> {
                                onUrlEdit(BrowserToolbarQuery(previous = currentText, current = suggestionText))
                                textFieldState.edit {
                                    replace(0, length, suggestionText)
                                    selection = TextRange(suggestionText.length)
                                }
                            }

                            // A long press over the typed text commits the suggestion and selects the whole URL
                            // so the contextual menu operates on the full text.
                            suggestionText.startsWith(currentText) && suggestionText.length > currentText.length -> {
                                if (awaitLongPressOrCancellation(downEvent.id) != null) {
                                    onUrlEdit(BrowserToolbarQuery(previous = currentText, current = suggestionText))
                                    textFieldState.edit {
                                        replace(0, length, suggestionText)
                                        selection = TextRange(0, suggestionText.length)
                                    }
                                }
                            }
                        }
                    }
                },
        contentAlignment =
            when (deviceLayoutDirection) {
                LayoutDirection.Ltr -> Alignment.CenterStart
                LayoutDirection.Rtl -> Alignment.CenterEnd
            },
    ) {
        if (textFieldState.text.isEmpty()) {
            Text(
                text = hint,
                style =
                    LocalTextStyle.current.merge(
                        fontSize = TEXT_SIZE.sp,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    ),
                maxLines = 1,
                overflow = TextOverflow.Clip,
            )
        }
        innerTextField()
    }
}

/** Temporary helper for putting the toolbar in incognito mode. See https://issuetracker.google.com/issues/359257538. */
@VisibleForTesting
internal object NoPersonalizedLearningHelper {
    @DoNotInline
    fun addNoPersonalizedLearning(info: EditorInfo) {
        info.imeOptions = info.imeOptions or EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING
    }
}

/**
 * Returns whether the visible [suggestion] is a valid completion of [originalText] that could be committed right now.
 *
 * A suggestion can be accepted when the user has not dismissed it ([shouldUseSuggestion]), when it extends the current
 * text (starts with what is already typed), and when it actually adds new characters beyond what was typed.
 */
private fun isCurrentSuggestionValid(
    originalText: String,
    suggestion: String?,
    shouldUseSuggestion: Boolean,
) = shouldUseSuggestion && suggestion?.startsWith(originalText) == true && suggestion.length > originalText.length

/**
 * Returns whether an [InputConnection.setSelection] request should be interpreted as the user asking to commit the
 * visible autocomplete suggestion.
 *
 * This recognizes keyboard cursor-control gestures aimed at the suggestion suffix. The suffix is rendered through an
 * OutputTransformation and is not real editable text, so IMEs cannot move the cursor into it directly and will instead
 * emit one of three patterns that this function detects:
 * - the IME collapses the cursor to the end of the real query from a position inside it,
 * - the IME repeats setSelection(textLength, textLength) while the cursor is already at the end and the text has not
 *   changed (eg: Gboard's spacebar swipe),
 * - the IME requests a collapsed cursor position past the real text but within the visible suffix (eg: HeliBoard's
 *   "move into the suggestion" functionality).
 *
 * [hasTextChangedSinceLastSelection] guards against accepting on selection updates that are merely IME/editor
 * synchronizations after typing or deleting.
 *
 * Callers must independently confirm via [isCurrentSuggestionValid] that there is a suggestion to apply while this
 * function answers only if the user asked to apply the suggestion.
 */
@VisibleForTesting
internal fun shouldAcceptSuggestionOnSelectionUpdate(
    currentTextLength: Int,
    oldSelection: TextRange,
    requestedSelectionStart: Int,
    requestedSelectionEnd: Int,
    suggestionLength: Int?,
    hasTextChangedSinceLastSelection: Boolean,
): Boolean {
    val requestedCollapsedCursor = requestedSelectionStart == requestedSelectionEnd

    val requestedCollapsedCursorAtRealTextEnd = requestedCollapsedCursor && requestedSelectionStart == currentTextLength

    // Normal cursor-move case: the cursor was inside the real query and the
    // IME moved it to the end of the real query.
    val movedFromInsideRealTextToEnd =
        requestedCollapsedCursorAtRealTextEnd &&
            oldSelection.collapsed &&
            oldSelection.end < currentTextLength &&
            !hasTextChangedSinceLastSelection

    // Gboard-specific behavior:
    // When the cursor is already at the end, Gboard's spacebar cursor-control
    // can emit a redundant setSelection(text.length, text.length).
    // Since the autocomplete suffix is visual-only, Gboard cannot move into it
    // as real editable text. Treat this repeated unchanged selection-at-end as
    // intent to move right into the visible suffix.
    val redundantMoveToEnd =
        requestedCollapsedCursorAtRealTextEnd &&
            oldSelection.collapsed &&
            oldSelection.end == currentTextLength &&
            !hasTextChangedSinceLastSelection

    // Other keyboard behavior:
    // Some keyboards request cursor positions inside the visually displayed
    // OutputTransformation text. Those positions are outside the real
    // TextFieldState text but inside the visible autocomplete suggestion.
    // Example:
    //   real text:        "wiki"           length = 4
    //   visible text:     "wikipedia.org"  length = 13
    //   IME request:      setSelection(9, 9)
    // Treat that as intent to move right into the visible suffix.
    val movedIntoVisibleSuggestion =
        requestedCollapsedCursor &&
            oldSelection.collapsed &&
            oldSelection.end == currentTextLength &&
            requestedSelectionStart > currentTextLength &&
            suggestionLength != null &&
            requestedSelectionStart <= suggestionLength &&
            !hasTextChangedSinceLastSelection

    return movedFromInsideRealTextToEnd || redundantMoveToEnd || movedIntoVisibleSuggestion
}

@Suppress("CognitiveComplexMethod", "ReturnCount")
private fun autocompleteInputConnection(
    delegate: InputConnection,
    currentText: () -> String,
    selection: () -> TextRange,
    suggestionText: () -> String?,
    handleCommitingSuggestion: () -> Boolean,
): InputConnection =
    object : InputConnectionWrapper(delegate, false) {
        // Some IMEs send DPAD_RIGHT through InputConnection.sendKeyEvent.
        // If we consume ACTION_DOWN to accept the suggestion, we need to also consume
        // the matching ACTION_UP when it arrives on the same InputConnection instance.
        private var consumedRightDpad = false
        private var lastSeenTextForSelection: String? = null

        override fun sendKeyEvent(event: KeyEvent): Boolean {
            // This method allows knowing when keyboards like Heliboard want to move the cursor
            // following a swipe right on the space bar.

            if (event.keyCode != KeyEvent.KEYCODE_DPAD_RIGHT) {
                return super.sendKeyEvent(event)
            }

            if (consumedRightDpad && event.action == KeyEvent.ACTION_UP) {
                consumedRightDpad = false
                return true
            }

            if (event.action == KeyEvent.ACTION_DOWN) {
                val text = currentText()
                val sel = selection()
                val cursorAtRealTextEnd = sel.collapsed && sel.end == text.length

                if (cursorAtRealTextEnd && handleCommitingSuggestion()) {
                    consumedRightDpad = true
                    return true
                }
            }

            return super.sendKeyEvent(event)
        }

        override fun setSelection(start: Int, end: Int): Boolean {
            // This method allows knowing when keyboards like Gboard want to move the cursor
            // following a swipe right on the space bar.

            val text = currentText()
            val oldSelection = selection()
            val suggestion = suggestionText()

            val hasTextChangedSinceLastSelection = lastSeenTextForSelection == null || lastSeenTextForSelection != text
            lastSeenTextForSelection = text

            val shouldAcceptOnSelectionUpdate =
                shouldAcceptSuggestionOnSelectionUpdate(
                    currentTextLength = text.length,
                    oldSelection = oldSelection,
                    requestedSelectionStart = start,
                    requestedSelectionEnd = end,
                    suggestionLength = suggestion?.length,
                    hasTextChangedSinceLastSelection = hasTextChangedSinceLastSelection,
                )

            if (shouldAcceptOnSelectionUpdate && handleCommitingSuggestion()) {
                return true
            }

            return super.setSelection(start, end)
        }
    }

/** Helper for sanitizing what gets pasted through the contextual menu. */
@OptIn(ExperimentalFoundationApi::class) // for ComposeFoundationFlags
@VisibleForTesting
internal class PasteSanitizerTextToolbar(
    private val context: Context,
    private val delegate: TextToolbar,
    private val clipboard: Clipboard,
    private val scope: CoroutineScope,
    private val handlePaste: (String) -> Unit,
) : TextToolbar {
    init {
        // Temporary workaround for https://issuetracker.google.com/issues/447192728
        ComposeFoundationFlags.isNewContextMenuEnabled = false
    }

    override val status = delegate.status

    override fun hide() = delegate.hide()

    override fun showMenu(
        rect: Rect,
        onCopyRequested: (() -> Unit)?,
        onPasteRequested: (() -> Unit)?,
        onCutRequested: (() -> Unit)?,
        onSelectAllRequested: (() -> Unit)?,
        onAutofillRequested: (() -> Unit)?,
    ) {
        delegate.showMenu(
            rect = rect,
            onCopyRequested = onCopyRequested,
            onPasteRequested = sanitizedPasteAction(onPasteRequested),
            onCutRequested = onCutRequested,
            onSelectAllRequested = onSelectAllRequested,
            onAutofillRequested = onAutofillRequested,
        )
    }

    override fun showMenu(
        rect: Rect,
        onCopyRequested: (() -> Unit)?,
        onPasteRequested: (() -> Unit)?,
        onCutRequested: (() -> Unit)?,
        onSelectAllRequested: (() -> Unit)?,
    ) {
        delegate.showMenu(
            rect = rect,
            onCopyRequested = onCopyRequested,
            onPasteRequested = sanitizedPasteAction(onPasteRequested),
            onCutRequested = onCutRequested,
            onSelectAllRequested = onSelectAllRequested,
        )
    }

    /** Returns a sanitized paste action only when paste is available. */
    private fun sanitizedPasteAction(onPasteRequested: (() -> Unit)?): (() -> Unit)? = onPasteRequested?.let {
        {
            scope.launch {
                handlePaste(sanitizeAvailableTextClip())
            }
        }
    }

    private suspend fun sanitizeAvailableTextClip(): String {
        val originalClip = clipboard.getClipEntry() ?: return ""

        val sb = StringBuilder()
        for (i in 0 until originalClip.clipData.itemCount) {
            val text = originalClip.clipData.getItemAt(i).coerceToText(context)
            val textToBePasted = (text as? Spanned)?.toString() ?: text

            val safeTextToBePasted = SafeUrl.stripUnsafeUrlSchemes(context, textToBePasted)

            if (i >= 1) {
                sb.append("\n")
            }
            sb.append(safeTextToBePasted)
        }

        return sb.toString().take(MAX_URI_LENGTH)
    }
}

@PreviewLightDark
@Composable
private fun InlineAutocompleteTextFieldWithSuggestion() {
    AcornTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surfaceContainerHighest)) {
            InlineAutocompleteTextField(
                query = "wiki",
                hint = "hint",
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                suggestion =
                    AutocompleteResult(
                        "wiki",
                        "wikipedia.org",
                        "https://wikipedia.org",
                        "test",
                        1,
                    ),
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun InlineAutocompleteTextFieldWithNoQuery() {
    AcornTheme {
        Box(Modifier.background(MaterialTheme.colorScheme.surfaceContainerHighest)) {
            InlineAutocompleteTextField(
                query = "",
                hint = "hint",
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                suggestion = null,
            )
        }
    }
}
