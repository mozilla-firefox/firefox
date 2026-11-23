/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.textfield

import androidx.annotation.DrawableRes
import androidx.compose.foundation.layout.RowScope
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
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.R
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.text.Text
import mozilla.components.compose.base.text.value
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.privateColorPalette
import mozilla.components.ui.icons.R as iconsR

/** Scope for [TextField] trailing icons. */
class TrailingIconScope(rowScope: RowScope) : RowScope by rowScope {

    /** An eye [TextField] trailing icon. */
    @Composable
    fun EyeTextFieldButton(
        contentDescription: Text? = Text.Resource(R.string.text_field_eye_trailing_icon_default_content_description),
        onTrailingIconClick: () -> Unit,
    ) =
        TrailingIconButton(
            iconId = iconsR.drawable.mozac_ic_eye_24,
            contentDescription = contentDescription,
            onTrailingIconClick = onTrailingIconClick,
        )

    /** A cross [TextField] trailing icon. */
    @Composable
    fun CrossTextFieldButton(
        contentDescription: Text? = Text.Resource(R.string.text_field_cross_trailing_icon_default_content_description),
        onTrailingIconClick: () -> Unit,
    ) =
        TrailingIconButton(
            iconId = iconsR.drawable.mozac_ic_cross_circle_fill_24,
            contentDescription = contentDescription,
            onTrailingIconClick = onTrailingIconClick,
        )

    @Composable
    private fun TrailingIconButton(
        @DrawableRes iconId: Int,
        contentDescription: Text?,
        onTrailingIconClick: () -> Unit,
    ) {
        IconButton(
            onClick = onTrailingIconClick,
            contentDescription = contentDescription?.value ?: "",
        ) {
            Icon(
                painter = painterResource(id = iconId),
                contentDescription = null,
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun EyeTextFieldButtonPreview() {
    var isPasswordVisible by remember { mutableStateOf(false) }

    AcornTheme {
        Surface {
            SecureTextField(
                state = rememberTextFieldState("password"),
                placeholder = "",
                errorText = "",
                modifier = Modifier.fillMaxWidth().padding(8.dp),
                label = "Eye",
                trailingIcon = { EyeTextFieldButton { isPasswordVisible = !isPasswordVisible } },
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

@Preview
@Composable
private fun EyeTextFieldButtonPrivatePreview() {
    var isPasswordVisible by remember { mutableStateOf(false) }

    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        Surface {
            SecureTextField(
                state = rememberTextFieldState("password"),
                placeholder = "",
                errorText = "",
                modifier = Modifier.fillMaxWidth().padding(8.dp),
                label = "Eye",
                trailingIcon = { EyeTextFieldButton { isPasswordVisible = !isPasswordVisible } },
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

@PreviewLightDark
@Composable
private fun CrossTextFieldButtonPreview() {
    var textFieldInput by remember { mutableStateOf("Delete me") }

    AcornTheme {
        Surface {
            TextField(
                value = textFieldInput,
                onValueChange = {
                    textFieldInput = it
                },
                placeholder = "",
                errorText = "",
                modifier = Modifier.fillMaxWidth().padding(8.dp),
                label = "Cross",
                trailingIcon = { CrossTextFieldButton { textFieldInput = "" } },
            )
        }
    }
}

@Preview
@Composable
private fun CrossTextFieldButtonPrivatePreview() {
    var textFieldInput by remember { mutableStateOf("Delete me") }

    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        Surface {
            TextField(
                value = textFieldInput,
                onValueChange = {
                    textFieldInput = it
                },
                placeholder = "",
                errorText = "",
                modifier = Modifier.fillMaxWidth().padding(8.dp),
                label = "Cross",
                trailingIcon = { CrossTextFieldButton { textFieldInput = "" } },
            )
        }
    }
}
