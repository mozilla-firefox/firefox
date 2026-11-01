/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.badge.StatusBadge
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.theme.FirefoxTheme

private const val LIVE_BADGE_CONTAINER_ALPHA = 0.1f

/**
 * Badge communicating the [status] of a match. Live matches are highlighted, while delayed, suspended and postponed
 * matches use a neutral badge. Nothing is rendered for any other status.
 *
 * @param status The [MatchStatus] to communicate.
 * @param modifier [Modifier] to be applied to the badge.
 */
@Composable
internal fun MatchStatusBadge(
    status: MatchStatus,
    modifier: Modifier = Modifier,
) {
    if (status.isLive()) {
        StatusBadge(
            containerColor = MaterialTheme.colorScheme.error.copy(alpha = LIVE_BADGE_CONTAINER_ALPHA),
            modifier = modifier,
            shape = CircleShape,
        ) {
            Text(
                text = "• ${stringResource(R.string.wrex_widget_match_live)}",
                style = FirefoxTheme.typography.overline,
                fontWeight = FontWeight.W500,
                color = MaterialTheme.colorScheme.error,
            )
        }
        return
    }

    val interruptedStatusText = status.interruptedStatusText() ?: return

    StatusBadge(
        containerColor = MaterialTheme.colorScheme.onSurface,
        modifier = modifier.height(24.dp),
    ) {
        Text(
            text = interruptedStatusText,
            style = FirefoxTheme.typography.caption,
            color = MaterialTheme.colorScheme.inverseOnSurface,
        )
    }
}

/** True when the match is currently being played (regulation, extra time or penalty shootout). */
internal fun MatchStatus.isLive(): Boolean =
    when (this) {
        is MatchStatus.Live,
        is MatchStatus.Penalties -> true

        else -> false
    }

/** Returns the label for a delayed, suspended or postponed match, or null for any other status. */
@Composable
internal fun MatchStatus.interruptedStatusText(): String? =
    when (this) {
        MatchStatus.Delayed -> stringResource(R.string.wrex_widget_match_delayed)
        MatchStatus.Suspended -> stringResource(R.string.wrex_widget_match_suspended)
        MatchStatus.Postponed -> stringResource(R.string.wrex_widget_match_postponed)
        else -> null
    }

private class MatchStatusBadgePreviewProvider : PreviewParameterProvider<MatchStatus> {
    override val values =
        sequenceOf(
            MatchStatus.Live(period = "1", clock = "29"),
            MatchStatus.Penalties(homePenalty = 3, awayPenalty = 2),
            MatchStatus.Delayed,
            MatchStatus.Suspended,
            MatchStatus.Postponed,
        )
}

@PreviewLightDark
@Composable
private fun MatchStatusBadgePreview(@PreviewParameter(MatchStatusBadgePreviewProvider::class) status: MatchStatus) {
    FirefoxTheme {
        Surface {
            MatchStatusBadge(
                status = status,
                modifier = Modifier.padding(FirefoxTheme.layout.space.static100),
            )
        }
    }
}
