/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar.ui

import android.content.Context
import android.text.Spanned
import android.view.inputmethod.EditorInfo
import androidx.annotation.DoNotInline
import androidx.annotation.VisibleForTesting
import androidx.compose.foundation.ComposeFoundationFlags
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
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
import androidx.compose.ui.ExperimentalComposeUiApi
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.SolidColor
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.Clipboard
import androidx.compose.ui.platform.InterceptPlatformTextInput
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.platform.LocalTextToolbar
import androidx.compose.ui.platform.LocalView
import androidx.compose.ui.platform.PlatformTextInputMethodRequest
import androidx.compose.ui.platform.TextToolbar
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.text.SpanStyle
import androidx.compose.ui.text.TextRange
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.font.Font
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.input.KeyboardType
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.sp
import androidx.core.graphics.toColorInt
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.launch
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.browser.toolbar.concept.BrowserToolbarTestTags.ADDRESSBAR_SEARCH_BOX
import mozilla.components.concept.toolbar.AutocompleteResult
import mozilla.components.support.utils.SafeUrl

private const val TEXT_SIZE = 32f
private const val TEXT_HIGHLIGHT_COLOR = "#5CF5671E"
private const val MAX_TEXT_LENGTH_TO_PASTE = 2_000

/**
 * A text field composable that displays a suggestion inline with the user's input,
 * styled differently to distinguish it from the typed text.
 *
 * @param query The query to show.
 * @param hint Placeholder text tpo show if [query] is empty.
 * @param suggestion The autocomplete suggestion to display. `null` if no suggestion is active.
 * @param showQueryAsPreselected If `true`, the initial query text will be fully selected.
 * @param usePrivateModeQueries If `true`, instructs the keyboard to disable personalized learning,
 * suitable for private/incognito modes.
 * @param modifier The [Modifier] to be applied to this text field.
 * @param onUrlEdit Callback invoked when the user types or deletes text, providing [BrowserToolbarQuery]
 * with information about the previous and the new query.
 * @param onUrlCommitted A callback for when the user commits the text via an IME action like "Go".
 */
@OptIn(ExperimentalComposeUiApi::class) // for InterceptPlatformTextInput
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
    val textFieldState = rememberTextFieldState(
        initialText = query,
        initialSelection = when {
            showQueryAsPreselected -> TextRange(0, query.length)
            else -> TextRange(query.length)
        },
    )
    var useSuggestion by remember { mutableStateOf(true) }
    // Properties referenced in long lived lambdas
    val currentSuggestion by rememberUpdatedState(suggestion)
    val currentUseSuggestion by rememberUpdatedState(useSuggestion)

    val focusRequester = remember { FocusRequester() }
    val keyboardController = LocalSoftwareKeyboardController.current

    val suggestionTextColor = AcornTheme.colors.textNormal
    val highlightBackgroundColor = Color(TEXT_HIGHLIGHT_COLOR.toColorInt())

    var suggestionBounds by remember { mutableStateOf<Rect?>(null) }
    val deviceLayoutDirection = LocalLayoutDirection.current
    val scrollState = rememberScrollState()

    val context = LocalContext.current
    val defaultTextToolbar = LocalTextToolbar.current
    val clipboard = LocalClipboard.current
    val coroutineScope = rememberCoroutineScope()
    val pasteInterceptorToolbar = remember(defaultTextToolbar, clipboard) {
        PasteSanitizerTextToolbar(context, defaultTextToolbar, clipboard, coroutineScope) {
            val originalText = textFieldState
            textFieldState.edit {
                replace(originalText.selection.start, originalText.selection.end, it)
            }
            onUrlEdit(
                BrowserToolbarQuery(
                    previous = originalText.text.toString(),
                    current = textFieldState.text.toString(),
                ),
            )
        }
    }
    DisposableEffect(Unit) {
        onDispose { pasteInterceptorToolbar.hide() }
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

    val localView = LocalView.current
    LaunchedEffect(suggestion) {
        if (useSuggestion) {
            suggestion?.text?.let {
                @Suppress("DEPRECATION")
                localView.announceForAccessibility(it)
            }
        }
    }

    // Always want the text to be entered left to right.
    CompositionLocalProvider(
        LocalLayoutDirection provides LayoutDirection.Ltr,
        LocalTextToolbar provides pasteInterceptorToolbar,
    ) {
        // Set incognito mode for the keyboard when needed.
        InterceptPlatformTextInput(
            interceptor = { request, nextHandler ->
                val modifiedRequest = PlatformTextInputMethodRequest { outAttributes ->
                    request.createInputConnection(outAttributes).also {
                        if (usePrivateModeQueries) {
                            NoPersonalizedLearningHelper.addNoPersonalizedLearning(outAttributes)
                        }
                    }
                }
                nextHandler.startInputMethod(modifiedRequest)
            },
        ) {
            BasicTextField(
                state = textFieldState,
                modifier = modifier
                    .testTag(ADDRESSBAR_SEARCH_BOX)
                    .fillMaxWidth()
                    .onFocusChanged { focusState ->
                        if (focusState.isFocused) {
                            keyboardController?.show()
                        }
                    }
                    .focusRequester(focusRequester),
                textStyle = TextStyle(
                    fontSize = TEXT_SIZE.sp,
                    fontFamily = FontFamily(Font(com.android.car.navioui.R.font.vesta2_bold)),
                    color = AcornTheme.colors.textSecondary,
                    textAlign = when (deviceLayoutDirection) {
                        LayoutDirection.Ltr -> TextAlign.Start
                        LayoutDirection.Rtl -> TextAlign.End
                    },
                ),
                lineLimits = TextFieldLineLimits.SingleLine,
                scrollState = scrollState,
                keyboardOptions = KeyboardOptions(
                    showKeyboardOnFocus = true,
                    keyboardType = KeyboardType.Uri,
                    imeAction = ImeAction.Go,
                    autoCorrectEnabled = false,
                ),
                onKeyboardAction = {
                    keyboardController?.hide()
                    val currentText = textFieldState.text.toString()
                    val finalUrl = if (useSuggestion && suggestion?.text?.startsWith(currentText) == true) {
                        suggestion.text
                    } else {
                        currentText
                    }
                    onUrlCommitted(finalUrl)
                },
                inputTransformation = remember(onUrlEdit) {
                    AutocompleteInputTransformation(
                        suggestion = { currentSuggestion },
                        shouldUseSuggestion = { currentUseSuggestion },
                        onSuggestionVisibilityChangeRequest = { useSuggestion = it },
                        onUrlEdit = onUrlEdit,
                    )
                },
                outputTransformation = remember(suggestionTextColor) {
                    AutocompleteOutputTransformation(
                        suggestion = { currentSuggestion },
                        shouldUseSuggestion = { currentUseSuggestion },
                        textColor = suggestionTextColor,
                        textBackground = highlightBackgroundColor,
                    )
                },
                cursorBrush = SolidColor(AcornTheme.colors.textOnColorPrimary),
                onTextLayout = { layoutResult ->
                    val currentInput = textFieldState.text
                    suggestionBounds = when (currentInput.isEmpty()) {
                        true -> null
                        false -> try {
                            layoutResult()?.getBoundingBox(currentInput.length - 1)
                        } catch (_: IllegalArgumentException) {
                            null
                        }
                    }
                },
                decorator = { innerTextField ->
                    AutocompleteDecorator(
                        hint = hint,
                        suggestion = when {
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

/**
 * Helper for removing the suggestion or delete from the user query when backspace is pressed.
 */
@OptIn(ExperimentalFoundationApi::class)
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
        val isSuggestionVisible = shouldUseSuggestion() &&
            suggestion?.startsWith(originalText) == true && suggestion.length > originalText.length
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

/**
 * Helper for showing the autocomplete suggestion inline with user's input.
 */
@OptIn(ExperimentalFoundationApi::class)
private class AutocompleteOutputTransformation(
    private val suggestion: () -> AutocompleteResult?,
    private val shouldUseSuggestion: () -> Boolean,
    private val textColor: Color,
    private val textBackground: Color,
) : OutputTransformation {
    override fun TextFieldBuffer.transformOutput() {
        val userInput = asCharSequence()
        val suggestion = suggestion()
        if (!shouldUseSuggestion() ||
            suggestion?.text?.isEmpty() == true ||
            suggestion?.text?.startsWith(userInput) == false
        ) { return }

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
@Suppress("LongParameterList")
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
        modifier = Modifier
            .fillMaxWidth()
            // Commit the suggestion when users tap on the outside of the typed in text.
            .pointerInput(suggestion, suggestionBounds) {
                awaitEachGesture {
                    val downEvent = awaitFirstDown(requireUnconsumed = false)
                    val suggestionText = suggestion?.text
                    if (suggestionBounds != null && suggestionText != null &&
                        suggestionBounds.right < downEvent.position.x
                    ) {
                        onUrlEdit(
                            BrowserToolbarQuery(
                                previous = textFieldState.text.toString(),
                                current = suggestionText,
                            ),
                        )
                        textFieldState.edit {
                            replace(0, length, suggestionText)
                            selection = TextRange(suggestionText.length)
                        }
                    }
                }
            },
        contentAlignment = when (deviceLayoutDirection) {
            LayoutDirection.Ltr -> Alignment.CenterStart
            LayoutDirection.Rtl -> Alignment.CenterEnd
        },
    ) {
        if (textFieldState.text.isEmpty()) {
            Text(
                text = hint,
                style = LocalTextStyle.current.merge(
                    fontFamily = FontFamily(Font(com.android.car.navioui.R.font.vesta2_bold)),
                    fontSize = TEXT_SIZE.sp,
                    color = AcornTheme.colors.textSecondary,
                ),
                maxLines = 1,
                overflow = TextOverflow.Clip,
            )
        }
        innerTextField()
    }
}

/**
 * Temporary helper for putting the toolbar in incognito mode.
 * See https://issuetracker.google.com/issues/359257538.
 */
@VisibleForTesting
internal object NoPersonalizedLearningHelper {
    @DoNotInline
    fun addNoPersonalizedLearning(info: EditorInfo) {
        info.imeOptions = info.imeOptions or EditorInfo.IME_FLAG_NO_PERSONALIZED_LEARNING
    }
}

/**
 * Helper for sanitizing what gets pasted through the contextual menu.
 */
@OptIn(ExperimentalFoundationApi::class) // for ComposeFoundationFlags
private class PasteSanitizerTextToolbar(
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
            onPasteRequested = {
                scope.launch {
                    handlePaste(sanitizeAvailableTextClip())
                }
            },
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
            onPasteRequested = {
                scope.launch {
                    handlePaste(sanitizeAvailableTextClip())
                }
            },
            onCutRequested = onCutRequested,
            onSelectAllRequested = onSelectAllRequested,
        )
    }

    private suspend fun sanitizeAvailableTextClip(): String {
        val originalClip = clipboard.getClipEntry() ?: return ""

        val sb = StringBuilder()
        for (i in 0 until originalClip.clipData.itemCount) {
            val text = originalClip.clipData.getItemAt(i).coerceToText(context)
            val textToBePasted = (text as? Spanned)?.toString() ?: text

            val safeTextToBePasted = SafeUrl.stripUnsafeUrlSchemes(context, textToBePasted)

            if (i >= 1) { sb.append("\n") }
            sb.append(safeTextToBePasted)
        }

        return sb.toString().take(MAX_TEXT_LENGTH_TO_PASTE)
    }
}

@PreviewLightDark
@Composable
private fun InlineAutocompleteTextFieldWithSuggestion() {
    AcornTheme {
        Box(
            Modifier.background(MaterialTheme.colorScheme.surfaceDim),
        ) {
            InlineAutocompleteTextField(
                query = "wiki",
                hint = "hint",
                showQueryAsPreselected = false,
                usePrivateModeQueries = false,
                suggestion = AutocompleteResult(
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
        Box(
            Modifier.background(MaterialTheme.colorScheme.surfaceDim),
        ) {
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
