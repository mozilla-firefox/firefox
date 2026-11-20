/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.State
import androidx.compose.runtime.derivedStateOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.unit.LayoutDirection
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.R
import mozilla.components.feature.listentopage.VoiceState
import mozilla.components.feature.listentopage.playback.PlaybackSpeed
import mozilla.components.feature.listentopage.voices.Voice
import mozilla.components.ui.icons.R as iconsR

/** Listen to page audio player controls */
@Composable
internal fun PlaybackControls(
    articleProgressState: State<ArticleProgress>,
    playbackUiState: PlaybackUiState,
    voiceState: VoiceState,
    speedState: SpeedState,
    onAction: (ListenAction.User) -> Unit,
    modifier: Modifier = Modifier,
) {
    var voicesExpanded by remember { mutableStateOf(false) }
    var speedsExpanded by remember { mutableStateOf(false) }
    val buttonsEnabled = playbackUiState !is PlaybackUiState.Error
    Row(
        modifier =
            modifier
                .padding(
                    start = AcornTheme.layout.space.static100,
                    end = AcornTheme.layout.space.static100,
                    bottom = AcornTheme.layout.space.static150,
                )
                .fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        // Voices
        VoiceControls(
            voiceState = voiceState,
            voicesExpanded = voicesExpanded,
            enabled = buttonsEnabled,
            onClick = {
                onAction(ListenAction.User.Voices.VoicesClicked)
                voicesExpanded = true
            },
            onVoiceClick = {
                voicesExpanded = false
                onAction(ListenAction.User.Voices.VoiceSelected(it))
            },
            onDismiss = {
                voicesExpanded = false
                onAction(ListenAction.User.Voices.VoicesDismissed)
            },
        )

        // Media player controls are always LTR:
        // https://m3.material.io/foundations/layout/bidirectionality-rtl
        CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
            CenterControls(
                articleProgressState = articleProgressState,
                playbackUiState = playbackUiState,
                onAction = onAction,
            )
        }

        SpeedControls(
            speedState = speedState,
            speedsExpanded = speedsExpanded,
            enabled = buttonsEnabled,
            onClick = {
                onAction(ListenAction.User.Controls.PlaybackSpeedClicked)
                speedsExpanded = true
            },
            onDismiss = {
                speedsExpanded = false
                onAction(ListenAction.User.Controls.PlaybackSpeedDismissed)
            },
        )
    }
}

@Composable
private fun SpeedControls(
    speedState: SpeedState,
    enabled: Boolean,
    speedsExpanded: Boolean,
    onClick: () -> Unit,
    onDismiss: () -> Unit,
) {
    val selectedSpeed = stringResource(speedState.contentDescription)
    val speedsStateDescription =
        if (speedsExpanded) {
            stringResource(R.string.mozac_feature_listentopage_dropdown_expanded, selectedSpeed)
        } else {
            stringResource(R.string.mozac_feature_listentopage_playback_speed_collapsed, selectedSpeed)
        }
    Box {
        IconButton(
            enabled = enabled,
            onClick = onClick,
            contentDescription = stringResource(R.string.mozac_feature_listentopage_playback_speed),
            modifier =
                Modifier.semantics {
                    stateDescription = speedsStateDescription
                },
        ) {
            Icon(
                painter = painterResource(speedState.icon),
                contentDescription = null,
            )
        }
        PlaybackSpeedMenu(
            speedList = speedState.menuItems,
            expanded = speedsExpanded,
            onDismissRequest = onDismiss,
        )
    }
}

@Composable
private fun VoiceControls(
    voiceState: VoiceState,
    enabled: Boolean,
    voicesExpanded: Boolean,
    onClick: () -> Unit,
    onVoiceClick: (Voice) -> Unit,
    onDismiss: () -> Unit,
) {
    val voicesStateDescription =
        if (voicesExpanded) {
            stringResource(R.string.mozac_feature_listentopage_dropdown_expanded)
        } else {
            stringResource(R.string.mozac_feature_listentopage_dropdown_voice_collapsed)
        }
    Box {
        IconButton(
            enabled = enabled,
            onClick = onClick,
            contentDescription = stringResource(R.string.mozac_feature_listentopage_voice_options),
            modifier = Modifier.semantics { stateDescription = voicesStateDescription },
        ) {
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_audio_wave_24),
                contentDescription = null,
            )
        }
        VoiceSelection(
            expanded = voicesExpanded,
            availableVoices = voiceState.availableVoices,
            selectedVoice = voiceState.selectedVoice,
            onVoiceClick = onVoiceClick,
            onDismissRequest = onDismiss,
        )
    }
}

@Composable
private fun CenterControls(
    articleProgressState: State<ArticleProgress>,
    playbackUiState: PlaybackUiState,
    onAction: (ListenAction.User) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(AcornTheme.layout.space.static150),
        modifier = modifier,
    ) {
        val enabled = playbackUiState !is PlaybackUiState.Error
        RewindButton(
            enabled = enabled,
            articleProgressState = articleProgressState,
            onAction = onAction,
        )
        // Play/Pause
        PlayPauseButton(
            playbackUiState = playbackUiState,
            onButtonClicked = { onAction(ListenAction.User.Controls.PlayPauseClicked) },
            modifier = Modifier.size(AcornTheme.layout.size.static800),
        )
        // Forward
        IconButton(
            enabled = enabled,
            onClick = { onAction(ListenAction.User.Controls.ForwardClicked) },
            contentDescription = stringResource(R.string.mozac_feature_listentopage_forward_30_sec),
        ) {
            // Speed icon hardcoded as of now
            Icon(
                painter = painterResource(iconsR.drawable.mozac_ic_playback_forward_24),
                contentDescription = null,
            )
        }
    }
}

@Composable
internal fun RewindButton(
    articleProgressState: State<ArticleProgress>,
    onAction: (ListenAction.User) -> Unit,
    enabled: Boolean,
) {
    val hasPlayed by remember {
        // at least one second has played
        derivedStateOf { articleProgressState.value.positionMs >= 1000L }
    }
    // button is enabled only in non-error state and when audio has played for at least 1 sec
    val displayAsEnabled = enabled && hasPlayed
    IconButton(
        enabled = displayAsEnabled,
        onClick = { onAction(ListenAction.User.Controls.RewindClicked) },
        contentDescription = stringResource(R.string.mozac_feature_listentopage_back_10_sec),
    ) {
        Icon(
            painter = painterResource(iconsR.drawable.mozac_ic_playback_rewind_24),
            contentDescription = null,
        )
    }
}

@PreviewLightDark
@Composable
private fun PlaybackControlsPreview(@PreviewParameter(PlaybackUIStatePreviewProvider::class) state: PlaybackUiState) {
    AcornTheme {
        Box(modifier = Modifier.background(color = MaterialTheme.colorScheme.surface)) {
            PlaybackControls(
                articleProgressState = remember { mutableStateOf(ArticleProgress()) },
                playbackUiState = state,
                voiceState = VoiceState(),
                speedState =
                    PlaybackSpeed.Default.toSpeedState(
                        locale = LocalConfiguration.current.locales[0],
                        labelPattern = stringResource(R.string.mozac_feature_listentopage_playback_speed_option),
                    ) {},
                onAction = {},
                modifier = Modifier,
            )
        }
    }
}
