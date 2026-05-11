/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.button

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.ButtonDefaults.outlinedButtonBorder
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.painter.Painter
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.privateColorPalette
import androidx.compose.material3.Button as M3Button
import androidx.compose.material3.OutlinedButton as M3OutlinedButton
import mozilla.components.ui.icons.R as iconsR

const val DEFAULT_MAX_LINES = 2

@Composable
@ReadOnlyComposable
private fun AcornTheme.buttonContentPadding(): PaddingValues {
    return PaddingValues(
        horizontal = this.layout.space.static300,
        vertical = this.layout.space.static150,
    )
}

@Composable
private fun ButtonContent(
    text: String,
    icon: Painter?,
    modifier: Modifier = Modifier,
    iconTint: Color = LocalContentColor.current,
) {
    val fontScale: Float = LocalConfiguration.current.fontScale

    icon?.let { painter ->
        Icon(
            painter = painter,
            contentDescription = null,
            modifier = modifier,
            tint = iconTint,
        )
        Spacer(modifier = Modifier.width(AcornTheme.layout.space.static100))
    }

    Text(
        text = text,
        textAlign = TextAlign.Center,
        style = AcornTheme.typography.button,
        maxLines = if (fontScale > 1.0f) Int.MAX_VALUE else DEFAULT_MAX_LINES,
    )
}

/**
 * Filled button.
 *
 * @param text The button text to be displayed.
 * @param modifier [Modifier] to be applied to the layout.
 * @param enabled Controls the enabled state of the button.
 * When false, this button will not be clickable.
 * @param contentColor The color to be used for the button's text and icon when enabled.
 * @param containerColor The background color of the button when enabled.
 * @param icon Optional [Painter] used to display an [Icon] before the button text.
 * @param iconModifier [Modifier] to be applied to the icon.
 * @param iconTint Optional [Color] used to tint the icon. When `null` the icon is tinted using
 * [contentColor].
 * @param onClick Invoked when the user clicks on the button.
 */
@Composable
fun FilledButton(
    text: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    contentColor: Color = ButtonDefaults.buttonColors().contentColor,
    containerColor: Color = ButtonDefaults.buttonColors().containerColor,
    icon: Painter? = null,
    iconModifier: Modifier = Modifier,
    iconTint: Color = contentColor,
    onClick: () -> Unit,
) {
    M3Button(
        onClick = onClick,
        modifier = modifier,
        enabled = enabled,
        contentPadding = AcornTheme.buttonContentPadding(),
        colors = ButtonDefaults.buttonColors(
            containerColor = containerColor,
            contentColor = contentColor,
        ),
    ) {
        ButtonContent(text = text, icon = icon, modifier = iconModifier, iconTint = iconTint)
    }
}

/**
 * Filled button.
 *
 * @param onClick Invoked when the user clicks on the button.
 * @param modifier [Modifier] to be applied to the layout.
 * @param enabled Controls the enabled state of the button.
 * When false, this button will not be clickable.
 * @param contentColor The color to be used for the button's text and icon when enabled.
 * @param containerColor The background color of the button when enabled.
 * @param content [Composable] content to be displayed in the button.
 */
@Composable
fun FilledButton(
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    contentColor: Color = ButtonDefaults.buttonColors().contentColor,
    containerColor: Color = ButtonDefaults.buttonColors().containerColor,
    content: @Composable () -> Unit,
) {
    M3Button(
        onClick = onClick,
        modifier = modifier,
        enabled = enabled,
        contentPadding = AcornTheme.buttonContentPadding(),
        colors = ButtonDefaults.buttonColors(
            containerColor = containerColor,
            contentColor = contentColor,
        ),
    ) {
        content()
    }
}

/**
 * Outlined button.
 *
 * @param text The button text to be displayed.
 * @param modifier [Modifier] to be applied to the layout.
 * @param enabled Controls the enabled state of the button.
 * When false, this button will not be clickable
 * @param contentColor The [Color] to be used for the button's text and icon when enabled.
 * @param containerColor The background fill [Color] of the button when enabled.
 * @param outlineColor The [Color] to be used for the button's outline when enabled.
 * @param icon Optional [Painter] used to display an [Icon] before the button text.
 * @param iconModifier [Modifier] to be applied to the icon.
 * @param onClick Invoked when the user clicks on the button.
 */
@Composable
fun OutlinedButton(
    text: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    contentColor: Color = MaterialTheme.colorScheme.primary,
    containerColor: Color = ButtonDefaults.outlinedButtonColors().containerColor,
    outlineColor: Color = MaterialTheme.colorScheme.outline,
    icon: Painter? = null,
    iconModifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    M3OutlinedButton(
        onClick = onClick,
        modifier = modifier,
        enabled = enabled,
        colors = ButtonDefaults.outlinedButtonColors(
            contentColor = contentColor,
            containerColor = containerColor,
        ),
        contentPadding = AcornTheme.buttonContentPadding(),
        border = enabled.getBorderColor(outlineColor),
    ) {
        ButtonContent(text = text, icon = icon, modifier = iconModifier)
    }
}

/**
 * Destructive button.
 *
 * @param text The button text to be displayed.
 * @param modifier [Modifier] to be applied to the layout.
 * @param enabled Controls the enabled state of the button.
 * When false, this button will not be clickable
 * @param contentColor The color to be used for the button's text, icon, and border when enabled.
 * @param containerColor The background color of the button when enabled.
 * @param icon Optional [Painter] used to display an [Icon] before the button text.
 * @param iconModifier [Modifier] to be applied to the icon.
 * @param onClick Invoked when the user clicks on the button.
 */
@Composable
fun DestructiveButton(
    text: String,
    modifier: Modifier = Modifier,
    enabled: Boolean = true,
    contentColor: Color = MaterialTheme.colorScheme.error,
    containerColor: Color = ButtonDefaults.outlinedButtonColors().containerColor,
    icon: Painter? = null,
    iconModifier: Modifier = Modifier,
    onClick: () -> Unit,
) {
    M3OutlinedButton(
        onClick = onClick,
        modifier = modifier,
        enabled = enabled,
        colors = ButtonDefaults.outlinedButtonColors(
            contentColor = contentColor,
            containerColor = containerColor,
        ),
        contentPadding = AcornTheme.buttonContentPadding(),
        border = enabled.getBorderColor(contentColor),
    ) {
        ButtonContent(text = text, icon = icon, modifier = iconModifier)
    }
}

/**
 * Extension function to return the appropriate border color based on the enabled state.
 *
 * @param this [Boolean] indicating whether the button is enabled or not.
 * @param borderOutline The [Color] to be used for the button's border when enabled.
 * @return [BorderStroke] representing the border of the button.
 */
@Composable
private fun Boolean.getBorderColor(borderOutline: Color): BorderStroke {
    return if (this) {
        BorderStroke(
            width = 1.dp,
            color = borderOutline,
        )
    } else {
        outlinedButtonBorder(enabled = false)
    }
}

@Composable
private fun ButtonPreviewContent() {
    Surface {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            FilledButton(
                text = "Label",
                onClick = {},
            )

            FilledButton(
                text = "Label",
                icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
                onClick = {},
            )

            FilledButton(
                text = "Label",
                enabled = false,
                icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
                onClick = {},
            )

            FilledButton(
                text = "Label",
                icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
                iconTint = Color.Red,
                onClick = {},
            )

            FilledButton(
                onClick = {},
            ) {
                CircularProgressIndicator(color = MaterialTheme.colorScheme.onPrimary)
            }

            OutlinedButton(
                text = "Label",
                onClick = {},
            )

            OutlinedButton(
                text = "Label",
                icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
                onClick = {},
            )

            OutlinedButton(
                text = "Label",
                enabled = false,
                icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
                onClick = {},
            )

            DestructiveButton(
                text = "Label",
                onClick = {},
            )

            DestructiveButton(
                text = "Label",
                icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
                onClick = {},
            )

            DestructiveButton(
                text = "Label",
                enabled = false,
                icon = painterResource(iconsR.drawable.mozac_ic_collection_24),
                onClick = {},
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun ButtonPreview() {
    AcornTheme {
        ButtonPreviewContent()
    }
}

@Composable
@Preview
private fun ButtonPrivatePreview() {
    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        ButtonPreviewContent()
    }
}
