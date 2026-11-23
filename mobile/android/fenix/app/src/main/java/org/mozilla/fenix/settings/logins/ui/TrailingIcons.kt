/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.logins.ui

import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.text.input.TextObfuscationMode
import androidx.compose.foundation.text.input.rememberTextFieldState
import androidx.compose.material3.Icon
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.textfield.SecureTextField
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * An eye trailing icon for a [SecureTextField] that contains a password.
 *
 * @param isPasswordVisible true when the password is revealed.
 * @param contentDescription the content description.
 * @param onTrailingIconClick invoked when pressing the eye icon
 */
@Composable
fun EyePasswordIconButton(
    isPasswordVisible: Boolean = false,
    contentDescription: Text? = null,
    onTrailingIconClick: () -> Unit,
) {
    IconButton(
        onClick = onTrailingIconClick,
        contentDescription = contentDescription?.value,
    ) {
        Icon(
            painter =
                if (!isPasswordVisible) {
                    painterResource(id = iconsR.drawable.mozac_ic_eye_24)
                } else {
                    painterResource(id = iconsR.drawable.mozac_ic_eye_slash_24)
                },
            contentDescription = null,
        )
    }
}

@Preview
@Composable
private fun EyePasswordIconButtonPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    var isPasswordVisible by remember { mutableStateOf(false) }

    FirefoxTheme(theme) {
        Surface {
            SecureTextField(
                state = rememberTextFieldState("password"),
                placeholder = "",
                errorText = "",
                modifier = Modifier.fillMaxWidth().padding(8.dp),
                label = "",
                trailingIcon = {
                    EyePasswordIconButton(
                        isPasswordVisible = isPasswordVisible,
                        onTrailingIconClick = { isPasswordVisible = !isPasswordVisible },
                    )
                },
                textObfuscationMode =
                    if (isPasswordVisible) {
                        TextObfuscationMode.Visible
                    } else {
                        TextObfuscationMode.System
                    },
            )
        }
    }
}
