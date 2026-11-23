/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.wrapContentSize
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberSliderState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.testTag
import androidx.compose.ui.semantics.testTagsAsResourceId
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import kotlin.math.roundToInt
import mozilla.components.compose.base.theme.PreviewThemeProvider
import mozilla.components.compose.base.theme.Theme
import org.mozilla.fenix.R
import org.mozilla.fenix.theme.FirefoxTheme

private const val HALF_ALPHA = 0.5F
private const val BASE_SAMPLE_TEXT_SIZE = 14
private const val BASE_SAMPLE_HEIGHT_LINE_DIFFERENCE = 6
private const val START_VALUE = 50
private const val END_VALUE = 200
private const val INCREASE_STEP = 5

/**
 * The slider that changes websites font size.
 *
 * @param isEnabled Whether or not the slider can be used.
 * @param value The current value of the slider.
 * @param onValueChange Callback invoked continuously while the user moves the thumb.
 * @param onValueChangeFinished Callback invoked once the dragging end.
 */
@Composable
fun FontSizePreference(
    isEnabled: Boolean,
    value: Float,
    onValueChange: (Float) -> Unit,
    onValueChangeFinished: () -> Unit,
) {
    val alpha = if (isEnabled) 1f else HALF_ALPHA
    // The values used to align with the top bar
    val paddingFontSizeSection = PaddingValues(start = 72.dp, top = 16.dp, end = 16.dp, bottom = 16.dp)

    Surface {
        Column(modifier = Modifier.alpha(alpha).padding(paddingFontSizeSection)) {
            Text(
                text = stringResource(id = R.string.preference_accessibility_font_size_title),
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = "fontSizeTitle"
                    },
                style = MaterialTheme.typography.bodyLarge,
            )

            Text(
                text = stringResource(id = R.string.preference_accessibility_text_size_summary),
                modifier =
                    Modifier.semantics {
                        testTagsAsResourceId = true
                        testTag = "fontSizeSubtitle"
                    },
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            Spacer(modifier = Modifier.height(12.dp))

            FontSizeSlider(
                isEnabled = isEnabled,
                value = value,
                onValueChange = onValueChange,
                onValueChangeFinished = onValueChangeFinished,
            )

            Spacer(modifier = Modifier.height(12.dp))

            SampleText(fontSize = value)
        }
    }
}

@Composable
private fun SampleText(fontSize: Float) {
    val textSize = (BASE_SAMPLE_TEXT_SIZE * (fontSize / 100f))

    Box(
        modifier =
            Modifier.wrapContentSize()
                .background(
                    color = MaterialTheme.colorScheme.secondaryContainer,
                    shape = MaterialTheme.shapes.extraLarge,
                )
                .padding(16.dp)
    ) {
        Text(
            text = stringResource(id = R.string.accessibility_text_size_sample_text_1),
            modifier =
                Modifier.semantics {
                    testTagsAsResourceId = true
                    testTag = "sampleText"
                },
            style = MaterialTheme.typography.bodyMedium,
            fontSize = textSize.sp,
            lineHeight = (textSize + BASE_SAMPLE_HEIGHT_LINE_DIFFERENCE).sp,
        )
    }
}

@Composable
private fun FontSizeSlider(
    isEnabled: Boolean,
    value: Float,
    onValueChange: (Float) -> Unit,
    onValueChangeFinished: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // The slider input is continuous, so discrete steps keep the value changing by [INCREASE_STEP].
        val sliderState =
            rememberSliderState(
                value = value,
                steps = ((END_VALUE - START_VALUE) / INCREASE_STEP) - 1,
                trackRange = START_VALUE.toFloat()..END_VALUE.toFloat(),
            )

        sliderState.value = value

        Slider(
            state = sliderState,
            modifier =
                Modifier.weight(1f).semantics {
                    testTagsAsResourceId = true
                    testTag = "fontSizeSlider"
                },
            enabled = isEnabled,
            onValueChange = { onValueChange(it.roundToInt().toFloat()) },
            onValueChangeFinished = onValueChangeFinished,
            thumb = { Thumb(isEnabled) },
            track = { Track(it.coercedValueAsFraction, isEnabled) },
        )

        Text(
            text = "${value.toInt()} %",
            modifier =
                Modifier.padding(start = 8.dp).semantics {
                    testTagsAsResourceId = true
                    testTag = "fontSizeSliderValue"
                },
        )
    }
}

/** Thumb is the draggable handle of the slider that user moves to change the value. */
@Composable
private fun Thumb(isEnabled: Boolean) {
    if (isEnabled) {
        Box(
            modifier =
                Modifier.padding(vertical = 6.dp).size(12.dp).background(MaterialTheme.colorScheme.primary, CircleShape)
        )
    } else {
        Box(
            contentAlignment = Alignment.Center,
            modifier =
                Modifier.padding(vertical = 6.dp)
                    .size(8.dp)
                    .border(2.dp, MaterialTheme.colorScheme.primary, CircleShape)
                    .padding(6.dp),
        ) {}
    }
}

@Composable
private fun Track(fraction: Float, isEnabled: Boolean) {
    Row(
        modifier =
            Modifier.fillMaxWidth()
                .height(2.dp)
                .background(
                    MaterialTheme.colorScheme.surfaceContainerHighest,
                    MaterialTheme.shapes.medium,
                ),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        FilledTrack(fraction = fraction, isEnabled = isEnabled)
    }
}

@Composable
private fun FilledTrack(fraction: Float, isEnabled: Boolean) {
    val color = if (isEnabled) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.surfaceContainerHighest

    Row(
        modifier =
            Modifier.fillMaxWidth(fraction)
                .height(2.dp)
                .background(
                    color = color,
                    shape = MaterialTheme.shapes.medium,
                )
    ) {}
}

@Preview
@Composable
private fun FontSizePreferencePreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        FontSizePreference(
            isEnabled = true,
            value = 100f,
            onValueChange = {},
            onValueChangeFinished = {},
        )
    }
}

@Preview
@Composable
private fun FontSizePreferenceDisabledPreview(@PreviewParameter(PreviewThemeProvider::class) theme: Theme) {
    FirefoxTheme(theme) {
        FontSizePreference(
            isEnabled = false,
            value = 200f,
            onValueChange = {},
            onValueChangeFinished = {},
        )
    }
}
