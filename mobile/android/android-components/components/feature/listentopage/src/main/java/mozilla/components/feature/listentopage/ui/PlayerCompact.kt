/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButtonDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.button.IconButton
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.PlayerMode
import mozilla.components.feature.listentopage.R
import mozilla.components.ui.icons.R as iconsR

/** Listen to page audio player in compact state */
@Composable
internal fun PlayerCompact(
    article: ArticleDetails,
    articleProgressState: State<ArticleProgress>,
    playbackUiState: PlaybackUiState,
    onAction: (ListenAction.User) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier.fillMaxWidth()) {
        Row(
            modifier =
                Modifier.padding(
                        top = AcornTheme.layout.space.static100,
                        end = AcornTheme.layout.space.static100,
                    )
                    .fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            IconButton(
                onClick = { onAction(ListenAction.User.Session.StopRequested()) },
                colors = IconButtonDefaults.iconButtonColors(containerColor = MaterialTheme.colorScheme.surface),
                contentDescription = stringResource(R.string.mozac_feature_listentopage_close),
                modifier = Modifier.testTag(PlayerControl.Close.testTag),
            ) {
                Icon(
                    painter = painterResource(iconsR.drawable.mozac_ic_cross_24),
                    tint = MaterialTheme.colorScheme.onSurface,
                    contentDescription = null,
                )
            }
            ArticleHeading(
                article = article,
                modifier =
                    Modifier.weight(1f)
                        .testTag(PlayerControl.Heading.testTag)
                        .heightIn(min = AcornTheme.layout.size.static600)
                        .clickable(
                            onClickLabel = stringResource(R.string.mozac_feature_listentopage_expand_player),
                            role = Role.Button,
                            onClick = { onAction(ListenAction.User.ModeChanged(PlayerMode.Expanded)) },
                        ),
                shouldScrollTitle =
                    playbackUiState is PlaybackUiState.Playing || playbackUiState is PlaybackUiState.Buffering,
                playbackUiState = playbackUiState,
            )

            RewindButton(
                enabled = playbackUiState !is PlaybackUiState.Error,
                articleProgressState = articleProgressState,
                onAction = onAction,
            )
            PlayPauseButton(
                playbackUiState = playbackUiState,
                onButtonClicked = { onAction(ListenAction.User.Controls.PlayPauseClicked) },
            )
        }
        // Reserves the space the progress bar occupies, since the bar itself is drawn as
        // an overlay on top of the card's border.
        Spacer(modifier = Modifier.height(AcornTheme.layout.space.static50 + MiniAudioProgressBarHeight))
    }
}

/** The compact player's progress bar, to be aligned to the bottom of the card. */
@Composable
internal fun AudioProgressBarCompact(
    progress: () -> Float,
    modifier: Modifier = Modifier,
    isEnabled: Boolean = true,
) {
    AudioProgressBar(
        progress = progress,
        modifier = modifier.padding(horizontal = AcornTheme.layout.space.static400),
        type = AudioProgressBarType.Mini,
        onSeek = {}, // Compact bar does not support seeking
        isEnabled = isEnabled,
    )
}

@PreviewLightDark
@Composable
private fun PlayerCompactPreview(@PreviewParameter(PlaybackUIStatePreviewProvider::class) state: PlaybackUiState) {
    AcornTheme {
        Box(modifier = Modifier.background(color = MaterialTheme.colorScheme.surface)) {
            PlayerCompact(
                article = ArticleDetails(title = "Match Preview: Wrexham AFC vs Sunderland AFC", site = "source"),
                articleProgressState =
                    remember { mutableStateOf(ArticleProgress(positionMs = 84_000, durationMs = 360_000)) },
                playbackUiState = state,
                onAction = {},
            )
        }
    }
}
