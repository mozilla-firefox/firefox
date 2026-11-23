/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.textfield

import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.setTextAndPlaceCursorAtEnd
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.runtime.snapshotFlow

/**
 * Remembers a [TextFieldState] kept in sync with a caller-owned [value] string: external changes are pushed into the
 * field, and user edits are reported back through [onValueChange]. The text is not saved to instance state: the caller
 * owns it, and it may be a password.
 *
 * @param value The text the caller currently holds for the field.
 * @param onValueChange Invoked with the new text whenever the user edits the field. The caller must store it verbatim:
 *   a rejected edit is not pushed back into the field.
 */
@Composable
fun rememberSyncedTextFieldState(
    value: String,
    onValueChange: (String) -> Unit,
): TextFieldState {
    val textFieldState = remember { TextFieldState(value) }
    var lastSynced by remember { mutableStateOf(value) }
    val latestOnValueChange by rememberUpdatedState(onValueChange)

    LaunchedEffect(value) {
        if (value != lastSynced) {
            lastSynced = value
            if (textFieldState.text.toString() != value) {
                textFieldState.setTextAndPlaceCursorAtEnd(value)
            }
        }
    }
    LaunchedEffect(textFieldState) {
        snapshotFlow { textFieldState.text.toString() }
            .collect { text ->
                if (text != lastSynced) {
                    lastSynced = text
                    latestOnValueChange(text)
                }
            }
    }

    return textFieldState
}
