/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base

import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalTextStyle
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.button.FilledButton
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.utils.parseHtml
import mozilla.components.ui.icons.R as iconsR

/**
 * Card for presenting promotional messages.
 *
 * @param description The optional description text shown in the body of the card.
 * @param modifier The [Modifier] to be applied to the card.
 * @param title The optional header text shown above the [description].
 * @param footer An optional piece of text with a clickable link.
 * @param illustration Composable slot displayed at the end of the card. Commonly used for illustrations.
 * @param contentSpacing The vertical spacing between the title, message, and actions slots.
 * @param colors Defines the color styling for the card. Defaults to
 * [PromoCardColors.promoCardColors].
 * @param closeButtonContentDescription The content description for the close button. Ignored
 * when [onDismiss] is null.
 * @param onDismiss Callback invoked when the close button is clicked. When null, the close
 * button is not rendered.
 */
@Composable
fun PromoCard(
    description: String?,
    modifier: Modifier = Modifier,
    title: String? = null,
    footer: Pair<String, LinkTextState>? = null,
    illustration: (@Composable () -> Unit)? = null,
    contentSpacing: Dp = AcornTheme.layout.space.static50,
    colors: PromoCardColors = PromoCardColors.promoCardColors(),
    closeButtonContentDescription: String? = null,
    onDismiss: (() -> Unit)? = null,
) {
    PromoCard(
        modifier = modifier,
        title = title?.let { titleText -> { Text(text = titleText) } },
        message = { description?.let { Text(text = remember(description) { parseHtml(description) }) } },
        actions = footer?.let { (footerText, linkState) ->
            {
                LinkText(
                    text = footerText,
                    linkTextStates = listOf(linkState),
                    style = AcornTheme.typography.body2.copy(
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    ),
                    linkTextColor = colors.actionsTextColor,
                    linkTextDecoration = TextDecoration.Underline,
                )
            }
        },
        illustration = illustration,
        contentSpacing = contentSpacing,
        colors = colors,
        closeButtonContentDescription = closeButtonContentDescription,
        onDismiss = onDismiss,
    )
}

/**
 * Card for presenting promotional messages with slotted content for title, message, actions and
 * illustration.
 *
 * @param modifier The [Modifier] to be applied to the card.
 * @param title Composable slot for the card's heading.
 * @param message Composable slot displayed below the title. Intended for descriptive or supporting content.
 * @param actions Composable slot below the message, intended for actions such as a link or buttons.
 * @param illustration Composable slot displayed at the end of the card.
 * @param contentSpacing The vertical spacing between the title, message, and actions slots.
 * @param colors Defines the color styling for the card. Defaults to [PromoCardColors.promoCardColors].
 * @param closeButtonContentDescription The content description for the close button. Ignored
 * when [onDismiss] is null.
 * @param onDismiss Callback invoked when the close button is clicked. When null, the close
 * button is not rendered.
 */
@Composable
fun PromoCard(
    modifier: Modifier = Modifier,
    title: (@Composable () -> Unit)? = null,
    message: (@Composable () -> Unit)? = null,
    actions: (@Composable () -> Unit)? = null,
    illustration: (@Composable () -> Unit)? = null,
    contentSpacing: Dp = AcornTheme.layout.space.static50,
    colors: PromoCardColors = PromoCardColors.promoCardColors(),
    closeButtonContentDescription: String? = null,
    onDismiss: (() -> Unit)? = null,
) {
    InfoCardContainer(
        modifier = modifier,
        backgroundColor = colors.backgroundColor,
        elevation = 0.dp,
        contentPadding = PaddingValues(0.dp),
    ) {
        Box {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(horizontal = AcornTheme.layout.space.static200),
                horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static200),
                verticalAlignment = Alignment.Bottom,
            ) {
                Column(
                    modifier = Modifier
                        .weight(1f)
                        .padding(vertical = AcornTheme.layout.space.static150),
                    verticalArrangement = Arrangement.spacedBy(contentSpacing),
                ) {
                    CompositionLocalProvider(
                        LocalTextStyle provides AcornTheme.typography.headline8.copy(
                            color = colors.titleTextColor,
                        ),
                    ) {
                        title?.invoke()
                    }

                    CompositionLocalProvider(
                        LocalTextStyle provides AcornTheme.typography.body2.copy(
                            color = colors.messageTextColor,
                        ),
                    ) {
                        message?.invoke()
                    }

                    CompositionLocalProvider(
                        LocalTextStyle provides AcornTheme.typography.body2.copy(
                            color = colors.actionsTextColor,
                        ),
                    ) {
                        actions?.invoke()
                    }
                }

                illustration?.invoke()
            }

            if (onDismiss != null) {
                CloseButton(
                    modifier = Modifier.align(Alignment.TopEnd),
                    color = colors.iconColor,
                    contentDescription = closeButtonContentDescription,
                    onCloseButtonClick = onDismiss,
                )
            }
        }
    }
}

@Composable
private fun CloseButton(
    color: Color,
    contentDescription: String?,
    onCloseButtonClick: () -> Unit,
    modifier: Modifier = Modifier,
) {
    IconButton(
        modifier = modifier,
        contentDescription = contentDescription,
        onClick = onCloseButtonClick,
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_cross_20),
            contentDescription = null,
            tint = color,
        )
    }
}

/**
 * Wrapper for the color parameters of [PromoCard].
 *
 * @property backgroundColor The background [Color] of the card.
 * @property titleTextColor [Color] applied to the title slot.
 * @property messageTextColor [Color] applied to the message slot.
 * @property actionsTextColor [Color] applied to the actions slot.
 * @property iconColor [Color] applied to the close button icon.
 */
data class PromoCardColors(
    val backgroundColor: Color,
    val titleTextColor: Color,
    val messageTextColor: Color,
    val actionsTextColor: Color,
    val iconColor: Color,
) {
    companion object {

        /**
         * Builder function used to construct an instance of [PromoCardColors].
         */
        @Composable
        fun promoCardColors(
            backgroundColor: Color = MaterialTheme.colorScheme.primaryContainer,
            titleTextColor: Color = MaterialTheme.colorScheme.onSurface,
            messageTextColor: Color = MaterialTheme.colorScheme.onSurfaceVariant,
            actionsTextColor: Color = MaterialTheme.colorScheme.tertiary,
            iconColor: Color = MaterialTheme.colorScheme.onSurface,
        ): PromoCardColors {
            return PromoCardColors(
                backgroundColor = backgroundColor,
                titleTextColor = titleTextColor,
                messageTextColor = messageTextColor,
                actionsTextColor = actionsTextColor,
                iconColor = iconColor,
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun PromoCardWithSlotsPreview() {
    AcornTheme {
        PromoCard(
            title = { Text("Title") },
            message = { Text("Description") },
            actions = {
                Text(
                    text = "Link",
                    textDecoration = TextDecoration.Underline,
                )
            },
            illustration = {
                Image(
                    painter = painterResource(iconsR.drawable.mozac_ic_fox_ai_on_state),
                    contentDescription = null,
                )
            },
            closeButtonContentDescription = null,
            onDismiss = {},
        )
    }
}

@Composable
@PreviewLightDark
private fun PromoCardWithSlotsAndNoCloseButtonPreview() {
    AcornTheme {
        PromoCard(
            title = { Text("Title") },
            message = { Text("Description") },
            actions = {
                Text(
                    text = "Link",
                    textDecoration = TextDecoration.Underline,
                )
            },
            illustration = {
                Image(
                    painter = painterResource(iconsR.drawable.mozac_ic_fox_ai_on_state),
                    contentDescription = null,
                )
            },
        )
    }
}

@Composable
@PreviewLightDark
private fun PromoCardWithoutTitlePreview() {
    AcornTheme {
        PromoCard(
            message = { Text("Description") },
            actions = {
                Text(
                    text = "Link",
                    textDecoration = TextDecoration.Underline,
                )
            },
            closeButtonContentDescription = null,
            onDismiss = {},
        )
    }
}

@Composable
@PreviewLightDark
private fun PromoCardFromStringsPreview() {
    AcornTheme {
        PromoCard(
            description = "Description",
            closeButtonContentDescription = null,
            title = "Title",
            footer = "Link" to LinkTextState(
                text = "Link",
                url = "https://www.mozilla.org",
                onClick = {},
            ),
            illustration = {
                Image(
                    painter = painterResource(iconsR.drawable.mozac_ic_fox_ai_on_state),
                    contentDescription = null,
                )
            },
            onDismiss = {},
        )
    }
}

@Composable
@PreviewLightDark
private fun PromoCardWithFilledButtonActionPreview() {
    AcornTheme {
        PromoCard(
            closeButtonContentDescription = null,
            title = { Text("Title") },
            message = { Text("Description") },
            actions = {
                FilledButton(
                    text = "Action",
                    onClick = {},
                )
            },
            illustration = {
                Image(
                    painter = painterResource(iconsR.drawable.mozac_ic_fox_ai_on_state),
                    contentDescription = null,
                )
            },
            onDismiss = {},
        )
    }
}
