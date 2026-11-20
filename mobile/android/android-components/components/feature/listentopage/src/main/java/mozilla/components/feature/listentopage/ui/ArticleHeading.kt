/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.listentopage.ui

import android.content.Context
import android.provider.Settings
import androidx.compose.foundation.background
import androidx.compose.foundation.basicMarquee
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import mozilla.components.compose.base.theme.AcornTheme

@Composable
internal fun ArticleHeading(
    article: ArticleDetails,
    modifier: Modifier = Modifier,
    shouldScrollTitle: Boolean = false,
    playbackUiState: PlaybackUiState,
) {
    val context = LocalContext.current
    val reducedMotion = remember(context) { isReducedMotionEnabled(context) }
    val marquee = shouldScrollTitle && !reducedMotion
    Column(
        modifier = modifier.semantics(mergeDescendants = true) { liveRegion = LiveRegionMode.Polite },
        verticalArrangement = Arrangement.Center,
    ) {
        Text(
            text = article.heading.orEmpty(),
            modifier =
                if (marquee) {
                    Modifier.basicMarquee(iterations = Int.MAX_VALUE, repeatDelayMillis = 0)
                } else {
                    Modifier
                },
            color =
                if (playbackUiState !is PlaybackUiState.Error) {
                    MaterialTheme.colorScheme.onSurface
                } else {
                    MaterialTheme.colorScheme.onSurfaceVariant
                },
            style = AcornTheme.typography.headline8,
            maxLines = 1,
            overflow = if (marquee) TextOverflow.Clip else TextOverflow.Ellipsis,
        )
        if (playbackUiState is PlaybackUiState.Error) {
            Text(
                text = stringResource(playbackUiState.listenError.userFacingMessage),
                color = MaterialTheme.colorScheme.error,
                style = AcornTheme.typography.caption,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
        } else if (article.hasTitle) {
            Text(
                text = article.site.orEmpty(),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = AcornTheme.typography.caption,
                maxLines = 1,
                overflow = TextOverflow.Ellipsis,
            )
        }
    }
}

@PreviewLightDark
@Preview(name = "RTL - Arabic", locale = "ar")
@Composable
private fun ArticleHeadingPreview(@PreviewParameter(PlaybackUIStatePreviewProvider::class) state: PlaybackUiState) {
    AcornTheme {
        ArticleHeading(
            article = ArticleDetails(title = "Match Preview: Wrexham AFC vs Sunderland AFC", site = "source"),
            modifier = Modifier.background(color = MaterialTheme.colorScheme.surface),
            playbackUiState = state,
        )
    }
}

private fun isReducedMotionEnabled(context: Context): Boolean =
    Settings.Global.getFloat(context.contentResolver, Settings.Global.ANIMATOR_DURATION_SCALE, 1f) == 0f
