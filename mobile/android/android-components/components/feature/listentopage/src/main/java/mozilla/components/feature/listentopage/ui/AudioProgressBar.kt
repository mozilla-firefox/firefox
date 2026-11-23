/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.rememberSliderState
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.slider.Slider
import mozilla.components.compose.base.theme.AcornTheme

/** Height of an [AudioProgressBarType.Mini] bar, exposed so callers can reserve room for it. */
internal val MiniAudioProgressBarHeight = 2.dp
private const val AUDIO_PROGRESS_BAR_TEST_TAG = "AudioProgressBar"
internal const val AUDIO_PROGRESS_BAR_TEST_TAG_COMPACT = "$AUDIO_PROGRESS_BAR_TEST_TAG.Compact"
internal const val AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED = "$AUDIO_PROGRESS_BAR_TEST_TAG.Expanded"

/** The visual variants of [AudioProgressBar]. */
internal enum class AudioProgressBarType {
    /** A slim track without a thumb, sized for the compact player. */
    Mini,

    /** A taller track with a thumb, sized for the expanded player. */
    Full,
}

/**
 * Playback progress for the Listen To Page players.
 *
 * @param progress Fraction of the audio that has played, from `0` to `1`. Values outside that range, and `NaN`, are
 *   clamped. This is a lambda so that the value is read while drawing rather than while composing, which keeps a
 *   position update from recomposing the player around it.
 * @param onSeek Called with the new fraction once the user finishes scrubbing. Only the [AudioProgressBarType.Full]
 *   variant supports seeking; the mini bar never invokes this.
 * @param type Which visual variant to draw.
 * @param isEnabled Whether playback is usable. When `false` the bar is dimmed and cannot be scrubbed.
 * @param modifier Optional modifier for further customisation of this bar.
 * @param isSeekable Whether the thumb can be dragged. It cannot while the audio has no length to seek in yet.
 * @param scrubFraction Where the user is dragging the thumb to, or `null` when they are not. The caller holds it, so
 *   that the thumb and anything else showing the scrub read the same value. A lambda so that a release straight after a
 *   change, as a tap makes, reads the change.
 * @param onSliderDrag Called with the fraction the thumb is at while the user is still scrubbing. The caller is
 *   expected to store it as [scrubFraction], and to clear that in [onSeek].
 */
@Composable
internal fun AudioProgressBar(
    progress: () -> Float,
    onSeek: (Float) -> Unit,
    type: AudioProgressBarType,
    modifier: Modifier = Modifier,
    isSeekable: Boolean = true,
    isEnabled: Boolean = true,
    scrubFraction: () -> Float? = { null },
    onSliderDrag: (Float) -> Unit = {},
) {
    val playedFraction = { progress().let { if (it.isNaN()) 0f else it.coerceIn(0f, 1f) } }
    when (type) {
        AudioProgressBarType.Mini -> {
            MiniBar(playedFraction = playedFraction, isEnabled = isEnabled, modifier = modifier)
        }
        AudioProgressBarType.Full -> {
            FullBar(
                playedFraction = playedFraction,
                onSeek = onSeek,
                scrubFraction = scrubFraction,
                onSliderDrag = onSliderDrag,
                isSeekable = isSeekable,
                isEnabled = isEnabled,
                modifier = modifier,
            )
        }
    }
}

@Composable
private fun FullBar(
    playedFraction: () -> Float,
    onSeek: (Float) -> Unit,
    scrubFraction: () -> Float?,
    onSliderDrag: (Float) -> Unit,
    isSeekable: Boolean,
    isEnabled: Boolean,
    modifier: Modifier = Modifier,
) {
    val sliderState = rememberSliderState()
    sliderState.value = scrubFraction() ?: playedFraction()

    Slider(
        state = sliderState,
        onValueChange = onSliderDrag,
        isEnabled = isEnabled && isSeekable,
        onValueChangeFinished = { scrubFraction()?.let(onSeek) },
        modifier =
            modifier
                .fillMaxWidth()
                .height(AcornTheme.layout.size.static150)
                .testTag(AUDIO_PROGRESS_BAR_TEST_TAG_EXPANDED),
    )
}

@Composable
private fun MiniBar(playedFraction: () -> Float, isEnabled: Boolean, modifier: Modifier = Modifier) {
    val progressColor = MaterialTheme.colorScheme.primary
    val trackColor = MaterialTheme.colorScheme.outline
    Canvas(
        modifier =
            modifier
                .fillMaxWidth()
                .height(MiniAudioProgressBarHeight)
                .alpha(if (isEnabled) 1f else DISABLED_ALPHA)
                .testTag(AUDIO_PROGRESS_BAR_TEST_TAG_COMPACT)
    ) {
        val cornerRadius = CornerRadius(size.height / 2f)

        drawRoundRect(
            color = trackColor,
            cornerRadius = cornerRadius,
        )
        drawRoundRect(
            color = progressColor,
            size = Size(playedFraction() * size.width, size.height),
            cornerRadius = cornerRadius,
        )
    }
}

private data class AudioProgressBarPreviewUiState(
    val type: AudioProgressBarType,
    val progress: () -> Float,
)

private class AudioProgressBarPreviewParameterProvider : PreviewParameterProvider<AudioProgressBarPreviewUiState> {
    val data =
        listOf(
            "0% Compact" to AudioProgressBarPreviewUiState(AudioProgressBarType.Mini, { 0f }),
            "40% Compact" to AudioProgressBarPreviewUiState(AudioProgressBarType.Mini, { 0.4f }),
            "100% Compact" to AudioProgressBarPreviewUiState(AudioProgressBarType.Mini, { 1f }),
            "0% Expanded" to AudioProgressBarPreviewUiState(AudioProgressBarType.Full, { 0f }),
            "40% Expanded" to AudioProgressBarPreviewUiState(AudioProgressBarType.Full, { 0.4f }),
            "100% Expanded" to AudioProgressBarPreviewUiState(AudioProgressBarType.Full, { 1f }),
        )
    override val values: Sequence<AudioProgressBarPreviewUiState>
        get() = data.map { it.second }.asSequence()

    override fun getDisplayName(index: Int): String {
        return data[index].first
    }
}

@PreviewLightDark
@Composable
private fun AudioProgressBarPreview(
    @PreviewParameter(AudioProgressBarPreviewParameterProvider::class) state: AudioProgressBarPreviewUiState
) {
    AcornTheme {
        Box(
            modifier = Modifier.background(MaterialTheme.colorScheme.surface).padding(AcornTheme.layout.space.static100)
        ) {
            AudioProgressBar(
                progress = state.progress,
                type = state.type,
                onSeek = {},
            )
        }
    }
}
