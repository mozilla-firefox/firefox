/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.base.text

import androidx.compose.foundation.layout.width
import androidx.compose.material.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.SubcomposeLayout
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.rememberTextMeasurer
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import mozilla.components.compose.base.modifier.FadeDirection
import mozilla.components.compose.base.modifier.horizontalFadeGradient

/**
 * A [Text] Composable that will have a horizontal fade and truncate the string.
 *
 * @param text The [String] for the [Text].
 * @param modifier The [Modifier] for the [Text].
 * @param style The [TextStyle] for the [Text].
 * @param truncationDirection The [TruncationDirection] that represents which side the truncation and fade are on.
 * @param fadeLength The [Dp] length of how much of the view to fade.
 */
@Composable
fun FadedText(
    text: String,
    modifier: Modifier = Modifier,
    style: TextStyle = TextStyle.Default,
    truncationDirection: TruncationDirection = TruncationDirection.START,
    fadeLength: Dp,
) {
    val textMeasurer = rememberTextMeasurer()

    val fadeDirection = if (
        (truncationDirection == TruncationDirection.START) ==
        (LocalLayoutDirection.current == LayoutDirection.Ltr)
    ) {
        FadeDirection.LEFT
    } else {
        FadeDirection.RIGHT
    }

    SubcomposeLayout(
        modifier = modifier,
    ) { constraints ->
        val textLayoutResult = textMeasurer.measure(
            text = text,
            maxLines = 1,
            style = style,
            constraints = constraints,
        )

        val visibleCharacters = textLayoutResult.getLineEnd(0, true)
        val isTextOverflowing = visibleCharacters < text.length

        val placeable = when (isTextOverflowing) {
            true -> {
                val truncatedText = if (fadeDirection == FadeDirection.LEFT) {
                    text.takeLast(visibleCharacters)
                } else {
                    text.take(visibleCharacters)
                }

                subcompose("truncated") {
                    Text(
                        text = truncatedText,
                        modifier = modifier.horizontalFadeGradient(
                            fadeLength = fadeLength,
                            fadeDirection = fadeDirection,
                        ),
                        style = style,
                        maxLines = 1,
                    )
                }.first().measure(constraints)
            }

            false -> subcompose("text") {
                Text(
                    text = text,
                    style = style,
                    maxLines = 1,
                )
            }.first().measure(constraints)
        }

        layout(placeable.width, placeable.height) {
            placeable.place(0, 0)
        }
    }
}

/**
 * Describes the direction of truncation and fade for [FadedText].
 */
enum class TruncationDirection {
    START,
    END,
}

/**
 * Preview of a short[FadedText] with start truncation and fade.
 */
@Preview(showBackground = true)
@Composable
fun ShortStartFadedTextPreview() {
    FadedText(
        text = "127.0.0.1",
        modifier = Modifier.width(200.dp),
        style = TextStyle(fontSize = 16.sp),
        truncationDirection = TruncationDirection.START,
        fadeLength = 60.dp,
    )
}

/**
 * Preview of [FadedText] with start truncation and fade.
 */
@Preview(showBackground = true)
@Composable
fun StartFadedTextPreview() {
    FadedText(
        text = "https://data.stackexchange.com/stackoverflow/query/58883/test-long-url",
        modifier = Modifier.width(200.dp),
        style = TextStyle(fontSize = 16.sp),
        truncationDirection = TruncationDirection.START,
        fadeLength = 60.dp,
    )
}

/**
 * Preview of a short [FadedText] with end truncation and fade
 */
@Preview(showBackground = true)
@Composable
fun ShortEndFadedTextPreview() {
    FadedText(
        text = "127.0.0.1",
        modifier = Modifier.width(200.dp),
        style = TextStyle(fontSize = 16.sp),
        truncationDirection = TruncationDirection.END,
        fadeLength = 60.dp,
    )
}

/**
 * Preview of [FadedText] with end truncation and fade
 */
@Preview(showBackground = true)
@Composable
fun EndFadedTextPreview() {
    FadedText(
        text = "https://data.stackexchange.com/stackoverflow/query/58883/test-long-url/",
        modifier = Modifier.width(200.dp),
        style = TextStyle(fontSize = 16.sp),
        truncationDirection = TruncationDirection.END,
        fadeLength = 60.dp,
    )
}

/**
 * Preview of a short [FadedText] with start truncation and fade and RTL language
 */
@Preview(showBackground = true)
@Composable
fun ShortRTLStartFadedTextPreview() {
    CompositionLocalProvider(
        LocalLayoutDirection provides LayoutDirection.Rtl,
    ) {
        FadedText(
            text = "127.0.0.1",
            modifier = Modifier.width(200.dp),
            style = TextStyle(fontSize = 16.sp),
            truncationDirection = TruncationDirection.START,
            fadeLength = 60.dp,
        )
    }
}

/**
 * Preview of [FadedText] with start truncation and fade and RTL language
 */
@Preview(showBackground = true)
@Composable
fun RTLStartFadedTextPreview() {
    CompositionLocalProvider(
        LocalLayoutDirection provides LayoutDirection.Rtl,
    ) {
        FadedText(
            text = "https://data.stackexchange.com/stackoverflow/query/58883/test-long-url/",
            modifier = Modifier.width(200.dp),
            style = TextStyle(fontSize = 16.sp),
            truncationDirection = TruncationDirection.START,
            fadeLength = 60.dp,
        )
    }
}

/**
 * Preview of a short [FadedText] with end truncation and fade and RTL language
 */
@Preview(showBackground = true)
@Composable
fun ShortRTLEndFadedTextPreview() {
    CompositionLocalProvider(
        LocalLayoutDirection provides LayoutDirection.Rtl,
    ) {
        FadedText(
            text = "127.0.0.1",
            modifier = Modifier.width(200.dp),
            style = TextStyle(fontSize = 16.sp),
            truncationDirection = TruncationDirection.END,
            fadeLength = 60.dp,
        )
    }
}

/**
 * Preview of [FadedText] with end truncation and fade and RTL language
 */
@Preview(showBackground = true)
@Composable
fun RTLEndFadedTextPreview() {
    CompositionLocalProvider(
        LocalLayoutDirection provides LayoutDirection.Rtl,
    ) {
        FadedText(
            text = "https://data.stackexchange.com/stackoverflow/query/58883/test-long-url/",
            modifier = Modifier.width(200.dp),
            style = TextStyle(fontSize = 16.sp),
            truncationDirection = TruncationDirection.END,
            fadeLength = 60.dp,
        )
    }
}
