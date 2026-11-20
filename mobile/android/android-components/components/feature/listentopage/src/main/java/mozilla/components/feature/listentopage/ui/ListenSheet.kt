/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.CubicBezierEasing
import androidx.compose.animation.core.tween
import androidx.compose.animation.core.updateTransition
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.State
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.annotation.FlexibleWindowLightDarkPreview
import mozilla.components.compose.base.theme.AcornCorners
import mozilla.components.compose.base.theme.AcornTheme
import mozilla.components.feature.listentopage.ArticleProgress
import mozilla.components.feature.listentopage.ListenAction
import mozilla.components.feature.listentopage.ListenError
import mozilla.components.feature.listentopage.VoiceState
import mozilla.components.feature.listentopage.playback.PlaybackPhase
import mozilla.components.feature.listentopage.playback.PlaybackSpeed

/** Opacity of a player control that is present but not interactive. */
internal const val DISABLED_ALPHA = 0.38f
internal const val LISTEN_SHEET_PLAYER_CARD_TEST_TAG = "listenSheetPlayerCard"
private const val FADE_OUT_DURATION_MS = 50
private const val RESIZE_DURATION_MS = 150
private const val FADE_IN_DURATION_MS = 50

/** The width the player stops growing at, so that it does not stretch across a landscape phone or tablet window. */
internal val PlayerMaxWidth = 560.dp
private val EasingStandard = CubicBezierEasing(0.2f, 0f, 0f, 1f)
private val EasingStandardAccelerate = CubicBezierEasing(0.3f, 0f, 1f, 1f)
private val EasingStandardDecelerate = CubicBezierEasing(0f, 0f, 0f, 1f)

private val ContentEnter =
    fadeIn(
        animationSpec =
            tween(
                durationMillis = FADE_IN_DURATION_MS,
                delayMillis = FADE_OUT_DURATION_MS + RESIZE_DURATION_MS,
                easing = EasingStandardDecelerate,
            )
    )

private val ContentExit =
    fadeOut(animationSpec = tween(durationMillis = FADE_OUT_DURATION_MS, easing = EasingStandardAccelerate))

/**
 * Media player with controls for the Listen To Page feature, shown either collapsed to a single row or expanded with
 * the full transport controls.
 *
 * @param article What the player says about the article it reads.
 * @param articleProgressState How far playback has got and how long the audio is. Held as a [State] rather than a plain
 *   value so that the position is read while drawing the progress bar instead of while composing the player, which
 *   keeps a position update from recomposing the controls around it.
 * @param playbackPhase The phase of playback, used to display the correct controls to the user.
 * @param speed How fast the article is being read out, shown on the expanded player's speed control.
 * @param voiceState The voices the expanded player offers to read the article in, and the one it is read in.
 * @param expanded Whether to show the full player. `false` shows the compact one.
 * @param onAction Invoked to pass upwards a [ListenAction.User] in response to a UI event.
 * @param modifier Optional modifier for further customisation of this player.
 */
@Composable
fun ListenSheet(
    article: ArticleDetails,
    articleProgressState: State<ArticleProgress>,
    playbackPhase: PlaybackPhase,
    speed: PlaybackSpeed,
    voiceState: VoiceState,
    onAction: (ListenAction.User) -> Unit,
    modifier: Modifier = Modifier,
    error: ListenError? = null,
    expanded: Boolean = true,
) {
    // Reading articleProgressState here instead of inside the lambda would defeat the point of hoisting it as a State.
    val progress = { articleProgressState.value.fraction }

    val transition = updateTransition(targetState = expanded, label = "ListenSheet")
    val playbackUiState = playbackPhase.toUiState(error = error)
    val sheetFocusRequester = remember { FocusRequester() }

    SideEffect(Unit) {
        sheetFocusRequester.requestFocus()
    }
    Box(modifier = modifier.focusRequester(sheetFocusRequester).focusable()) {
        Card(
            shape = RoundedCornerShape(AcornCorners.extraLarge),
            colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
            elevation = CardDefaults.cardElevation(AcornTheme.layout.elevation.level2),
            border = BorderStroke(AcornTheme.layout.border.default, MaterialTheme.colorScheme.outlineVariant),
            modifier =
                Modifier.widthIn(max = PlayerMaxWidth)
                    .align(Alignment.Center)
                    .testTag(LISTEN_SHEET_PLAYER_CARD_TEST_TAG),
        ) {
            transition.AnimatedContent(
                transitionSpec = {
                    ContentEnter togetherWith
                        ContentExit using
                        SizeTransform { _, _ ->
                            tween(
                                durationMillis = RESIZE_DURATION_MS,
                                delayMillis = FADE_OUT_DURATION_MS,
                                easing = EasingStandard,
                            )
                        }
                },
                contentAlignment = Alignment.BottomStart,
            ) { isExpanded ->
                if (isExpanded) {
                    PlayerExpanded(
                        article = article,
                        articleProgressState = articleProgressState,
                        playbackUiState = playbackUiState,
                        speed = speed,
                        voiceState = voiceState,
                        onAction = onAction,
                        modifier = Modifier.inertWhileTransitioning(scope = this),
                    )
                } else {
                    PlayerCompact(
                        article = article,
                        playbackUiState = playbackUiState,
                        articleProgressState = articleProgressState,
                        onAction = onAction,
                        modifier = Modifier.inertWhileTransitioning(scope = this),
                    )
                }
            }
        }
        transition.AnimatedVisibility(
            visible = { isExpanded -> !isExpanded },
            modifier = Modifier.align(Alignment.BottomCenter),
            enter = ContentEnter,
            exit = ContentExit,
        ) {
            AudioProgressBarCompact(
                progress = progress,
                modifier = Modifier.widthIn(max = PlayerMaxWidth),
                isEnabled = playbackUiState !is PlaybackUiState.Error,
            )
        }
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun ListenSheetCompactPreview() {
    AcornTheme {
        ListenSheetPreview(expanded = false)
    }
}

@FlexibleWindowLightDarkPreview
@Composable
private fun ListenSheetExpandedPreview() {
    AcornTheme {
        ListenSheetPreview(expanded = true)
    }
}

@PreviewLightDark
@Composable
private fun ListenSheetCompactUntitledPreview() {
    AcornTheme {
        ListenSheetPreview(expanded = false, title = null)
    }
}

@PreviewLightDark
@Composable
private fun ListenSheetExpandedUntitledPreview() {
    AcornTheme {
        ListenSheetPreview(expanded = true, title = null)
    }
}

@Composable
private fun ListenSheetPreview(
    expanded: Boolean,
    title: String? = "Match Preview: Wrexham AFC vs Sunderland AFC",
) {
    ListenSheet(
        article =
            ArticleDetails(
                title = title,
                site = "bbc.co.uk",
                url = "https://www.bbc.co.uk/sport/football/articles/c0l8m2y4kxpo",
            ),
        articleProgressState = remember { mutableStateOf(ArticleProgress(positionMs = 84_000, durationMs = 360_000)) },
        playbackPhase = PlaybackPhase.Playing,
        speed = PlaybackSpeed.Default,
        voiceState = VoiceState(),
        onAction = {},
        expanded = expanded,
    )
}
