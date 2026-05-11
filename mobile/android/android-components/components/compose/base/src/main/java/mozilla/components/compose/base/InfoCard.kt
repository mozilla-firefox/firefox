/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base

import androidx.annotation.DrawableRes
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.material3.Icon
import androidx.compose.material3.LocalContentColor
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.Immutable
import androidx.compose.runtime.ReadOnlyComposable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextDecoration
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.acornPrivateColorScheme
import mozilla.components.compose.base.theme.informationContainer
import mozilla.components.compose.base.theme.onInformationContainer
import mozilla.components.compose.base.theme.onWarningContainer
import mozilla.components.compose.base.theme.privateColorPalette
import mozilla.components.compose.base.theme.warningContainer
import mozilla.components.compose.base.utils.parseHtml
import mozilla.components.ui.icons.R as iconsR

/**
 * Card for presenting informational messages or errors.
 *
 * @param description The primary piece of text.
 * @param type The [InfoType] of message to display.
 * @param modifier [Modifier] to be applied to the card.
 * @param title The optional header text shown above the description.
 * @param verticalRowAlignment An optional adjustment of how the row of text aligns.
 * @param footer An optional piece of text with a clickable link.
 * @param colors [InfoCardColors] that will be used to resolve the container and content colors
 * for this card. Defaults to the palette for [type] via [InfoCardDefaults.colors].
 */
@Composable
fun InfoCard(
    description: String,
    type: InfoType,
    modifier: Modifier = Modifier,
    title: String? = null,
    verticalRowAlignment: Alignment.Vertical = Alignment.Top,
    footer: Pair<String, LinkTextState>? = null,
    colors: InfoCardColors = InfoCardDefaults.colors(type),
) {
    InfoCardContainer(
        modifier = modifier,
        backgroundColor = colors.container,
        shape = MaterialTheme.shapes.large,
        contentPadding = PaddingValues(
            start = AcornTheme.layout.space.static150,
            top = AcornTheme.layout.space.static150,
            end = AcornTheme.layout.space.static200,
            bottom = AcornTheme.layout.space.static150,
        ),
        elevation = 0.dp,
    ) {
        CompositionLocalProvider(LocalContentColor provides colors.content) {
            Row(
                verticalAlignment = verticalRowAlignment,
            ) {
                Icon(
                    painter = painterResource(id = type.iconId),
                    contentDescription = null,
                )

                Spacer(modifier = Modifier.width(AcornTheme.layout.space.static150))

                Column {
                    title?.let { titleText ->
                        Text(
                            text = titleText,
                            style = AcornTheme.typography.headline8,
                            modifier = Modifier.semantics { heading() },
                        )

                        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static50))
                    }

                    Text(
                        text = remember(description) { parseHtml(description) },
                        style = AcornTheme.typography.body2,
                    )

                    footer?.let {
                        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static50))

                        LinkText(
                            text = it.first,
                            linkTextStates = listOf(it.second),
                            style = AcornTheme.typography.body2,
                            linkTextColor = colors.content,
                            linkTextDecoration = TextDecoration.Underline,
                        )
                    }
                }
            }
        }
    }
}

/**
 * The possible types of a [InfoCard].
 */
enum class InfoType {
    /**
     * Stylizes the card to indicate a non-permanent or minor issue has occurred.
     */
    Warning,

    /**
     * Stylizes the card to indicate a serious error has occurred.
     */
    Error,

    /**
     * Stylizes the card for informative messages in colorful tones.
     */
    Info,

    /**
     * Stylizes the card for subtle informational messages using the surface palette.
     */
    Neutral,

    ;

    @get:DrawableRes
    internal val iconId: Int
        get() = when (this) {
            Warning -> iconsR.drawable.mozac_ic_warning_24
            Error -> iconsR.drawable.mozac_ic_critical_24
            Info, Neutral -> iconsR.drawable.mozac_ic_information_24
        }
}

/**
 * Container and content colors used by an [InfoCard].
 *
 * @property container The background color of the card.
 * @property content The color applied to the icon, text, and link inside the card.
 */
@Immutable
data class InfoCardColors(
    val container: Color,
    val content: Color,
)

/**
 * Default values used by [InfoCard].
 */
object InfoCardDefaults {
    /**
     * Creates the [InfoCardColors] that represent the default container and content colors for
     * the given [type].
     */
    @Composable
    @ReadOnlyComposable
    fun colors(type: InfoType): InfoCardColors = when (type) {
        InfoType.Warning -> InfoCardColors(
            container = MaterialTheme.colorScheme.warningContainer,
            content = MaterialTheme.colorScheme.onWarningContainer,
        )
        InfoType.Error -> InfoCardColors(
            container = MaterialTheme.colorScheme.errorContainer,
            content = MaterialTheme.colorScheme.onErrorContainer,
        )
        InfoType.Info -> InfoCardColors(
            container = MaterialTheme.colorScheme.informationContainer,
            content = MaterialTheme.colorScheme.onInformationContainer,
        )
        InfoType.Neutral -> InfoCardColors(
            container = MaterialTheme.colorScheme.surfaceContainerHighest,
            content = MaterialTheme.colorScheme.onSurface,
        )
    }
}

private class InfoTypeProvider : PreviewParameterProvider<InfoType> {
    private val types = InfoType.entries

    override val values: Sequence<InfoType> = types.asSequence()

    override fun getDisplayName(index: Int): String = types[index].name
}

@Composable
private fun InfoCardPreviewContent(type: InfoType) {
    Surface {
        Column(modifier = Modifier.padding(all = 16.dp)) {
            InfoCard(
                description = "Description text",
                type = type,
                modifier = Modifier.fillMaxWidth(),
                title = "Title text",
                footer = "Primary link text with an underlined hyperlink." to LinkTextState(
                    text = "underlined hyperlink",
                    url = "https://www.mozilla.org",
                    onClick = {},
                ),
            )

            Spacer(modifier = Modifier.height(16.dp))

            InfoCard(
                description = "Description-only variant without a title or link.",
                type = type,
                modifier = Modifier.fillMaxWidth(),
            )
        }
    }
}

@Composable
@PreviewLightDark
private fun InfoCardPreview(
    @PreviewParameter(InfoTypeProvider::class) type: InfoType,
) {
    AcornTheme {
        InfoCardPreviewContent(type = type)
    }
}

@Composable
@Preview
private fun InfoCardPrivatePreview(
    @PreviewParameter(InfoTypeProvider::class) type: InfoType,
) {
    AcornTheme(
        colors = privateColorPalette,
        colorScheme = acornPrivateColorScheme(),
    ) {
        InfoCardPreviewContent(type = type)
    }
}
