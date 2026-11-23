/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.textfield

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.foundation.text.input.InputTransformation
import androidx.compose.foundation.text.input.TextFieldState
import androidx.compose.foundation.text.input.TextObfuscationMode
import androidx.compose.material3.OutlinedSecureTextField
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.input.KeyboardType
import mozilla.components.compose.base.theme.AcornTheme

/**
 * UI for a text field holding sensitive input such as a password.
 *
 * @param state The [TextFieldState] backing the input text.
 * @param placeholder The text displayed when the text field is empty.
 * @param errorText The message displayed when there is an error.
 * @param modifier Modifier to be applied to the text field layout.
 * @param label Optional text displayed as a header above the input field.
 * @param isError Whether there is an error with the input value. When set to true, error styling will be applied to the
 *   text field.
 * @param isEnabled When set to false, the text field cannot be edited.
 * @param trailingIcon The optional composable for adding a trailing icon at the end of the text field container.
 * @param inputTransformation Optional [InputTransformation] that filters or changes the input before it reaches
 *   [state].
 * @param keyboardOptions Software keyboard options such as the [KeyboardType].
 * @param textObfuscationMode How the input is masked. Use [TextObfuscationMode.Visible] to reveal it.
 */
@Composable
fun SecureTextField(
    state: TextFieldState,
    placeholder: String,
    errorText: String,
    modifier: Modifier = Modifier,
    label: String? = null,
    isError: Boolean = false,
    isEnabled: Boolean = true,
    trailingIcon: @Composable (TrailingIconScope.() -> Unit)? = null,
    inputTransformation: InputTransformation? = null,
    keyboardOptions: KeyboardOptions =
        KeyboardOptions(autoCorrectEnabled = false, keyboardType = KeyboardType.Password),
    textObfuscationMode: TextObfuscationMode = TextObfuscationMode.System,
) {
    OutlinedSecureTextField(
        state = state,
        modifier = modifier.fillMaxWidth(),
        enabled = isEnabled,
        textStyle = AcornTheme.typography.body1,
        label = label?.let { { TextFieldLabel(text = it) } },
        placeholder = { TextFieldPlaceholder(text = placeholder) },
        trailingIcon = { TextFieldTrailingIcon(isError = isError, trailingIcon = trailingIcon) },
        supportingText = { TextFieldSupportingText(isError = isError, errorText = errorText) },
        isError = isError,
        inputTransformation = inputTransformation,
        textObfuscationMode = textObfuscationMode,
        keyboardOptions = keyboardOptions,
    )
}
