/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import kotlinx.coroutines.delay
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.compose.base.theme.surfaceContainerSelected
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.R.string
import mozilla.components.ui.icons.R as iconsR

internal const val PLAY_PAUSE_BUTTON_TEST_TAG = "listenPlayPauseButton"
internal const val PLAY_PAUSE_BUTTON_LOADING_TEST_TAG = "$PLAY_PAUSE_BUTTON_TEST_TAG.Loading"

/** How long buffering lasts before the button shows it, so that the short buffering after most seeks does not flash. */
internal const val LOADING_INDICATOR_DELAY_MS = 300L

@Composable
internal fun PlayPauseButton(
    playbackUiState: PlaybackUiState,
    onButtonClicked: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val playButtonContentDescription =
        when (playbackUiState) {
            PlaybackUiState.Playing,
            PlaybackUiState.Buffering -> stringResource(string.mozac_feature_listentopage_pause)
            PlaybackUiState.Paused -> stringResource(string.mozac_feature_listentopage_play)
            is PlaybackUiState.Error -> stringResource(playbackUiState.listenError.userFacingMessage)
        }
    val buttonBackgroundColor =
        when (playbackUiState) {
            PlaybackUiState.Playing,
            PlaybackUiState.Buffering -> MaterialTheme.colorScheme.surfaceContainerSelected
            PlaybackUiState.Paused -> MaterialTheme.colorScheme.primary
            is PlaybackUiState.Error -> MaterialTheme.colorScheme.errorContainer
        }
    val icon =
        when (playbackUiState) {
            PlaybackUiState.Playing,
            PlaybackUiState.Buffering -> painterResource(iconsR.drawable.mozac_ic_pause_fill_24)
            PlaybackUiState.Paused -> painterResource(iconsR.drawable.mozac_ic_play_fill_24)
            is PlaybackUiState.Error -> painterResource(playbackUiState.listenError.displayIcon)
        }
    val iconTint =
        when (playbackUiState) {
            PlaybackUiState.Playing,
            PlaybackUiState.Buffering -> MaterialTheme.colorScheme.onSurface
            PlaybackUiState.Paused -> MaterialTheme.colorScheme.onPrimary
            is PlaybackUiState.Error -> MaterialTheme.colorScheme.onSurfaceVariant
        }

    var shouldShowLoading by remember { mutableStateOf(false) }
    LaunchedEffect(playbackUiState) {
        shouldShowLoading = false
        if (playbackUiState == PlaybackUiState.Buffering) {
            delay(LOADING_INDICATOR_DELAY_MS)
            shouldShowLoading = true
        }
    }

    IconButton(
        onClick = onButtonClicked,
        // todo-bug-2079989: Support retries for certain errors
        enabled = playbackUiState !is PlaybackUiState.Error,
        contentDescription = playButtonContentDescription,
        modifier =
            modifier
                .background(
                    color = buttonBackgroundColor,
                    shape = CircleShape,
                )
                .testTag(PLAY_PAUSE_BUTTON_TEST_TAG),
    ) {
        if (shouldShowLoading) {
            CircularProgressIndicator(
                modifier = Modifier.size(AcornTheme.layout.size.static300).testTag(PLAY_PAUSE_BUTTON_LOADING_TEST_TAG),
                color = iconTint,
                strokeWidth = AcornTheme.layout.border.heavy,
            )
        } else {
            Icon(
                painter = icon,
                contentDescription = null,
                tint = iconTint,
            )
        }
    }
}

@get:DrawableRes
internal val ListenError.displayIcon: Int
    get() =
        when (this) {
            ListenError.NoOfflineVoice -> iconsR.drawable.mozac_ic_warning_fill_24
            ListenError.ContentUnavailable -> iconsR.drawable.mozac_ic_warning_fill_24
            ListenError.PlaybackFailed -> iconsR.drawable.mozac_ic_warning_fill_24
            ListenError.SynthesisFailed -> iconsR.drawable.mozac_ic_warning_fill_24
        }

@get:StringRes
internal val ListenError.userFacingMessage: Int
    get() =
        when (this) {
            ListenError.NoOfflineVoice -> string.mozac_feature_listentopage_error_voice_options_not_available
            ListenError.ContentUnavailable -> string.mozac_feature_listentopage_error_audio_not_available
            ListenError.SynthesisFailed,
            ListenError.PlaybackFailed -> string.mozac_feature_listentopage_error_could_not_finish_playing_audio
        }

@PreviewLightDark
@Composable
private fun PlayPauseButtonPreview(@PreviewParameter(PlaybackUIStatePreviewProvider::class) state: PlaybackUiState) {
    Box {
        PlayPauseButton(state, {})
    }
}
