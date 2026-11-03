/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.Match
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.home.sports.fake.FakeSportsPreview
import org.mozilla.fenix.home.sports.isExtraTime
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Displays the [ScorePill] once the match has a score, followed by the match status and kickoff details.
 *
 * @param match The [Match] to display.
 * @param modifier [Modifier] to be applied to the scoreboard.
 */
@Composable
internal fun Scoreboard(
    match: Match,
    modifier: Modifier = Modifier,
) {
    Column(
        modifier = modifier,
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static50),
    ) {
        if (match.homeScore != null && match.awayScore != null) {
            ScorePill(
                homeScore = match.homeScore,
                awayScore = match.awayScore,
                isLive = match.matchStatus.isLive(),
            )
        }

        Column(horizontalAlignment = Alignment.CenterHorizontally) {
            if (match.matchStatus.hasSecondaryStatusSubtitle()) {
                Text(
                    text = secondStatusSubtitle(status = match.matchStatus, date = match.date),
                    style = FirefoxTheme.typography.caption,
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurface,
                )
            }

            val subtitle = statusSubtitle(status = match.matchStatus, time = match.time)
            if (subtitle.isNotEmpty()) {
                Text(
                    text = subtitle,
                    style = FirefoxTheme.typography.caption,
                    textAlign = TextAlign.Center,
                    color = MaterialTheme.colorScheme.onSurface,
                )
            }
        }
    }
}

internal fun MatchStatus.hasSecondaryStatusSubtitle(): Boolean =
    when (this) {
        is MatchStatus.Live,
        is MatchStatus.Final -> false

        else -> true
    }

@Composable
internal fun statusSubtitle(status: MatchStatus, time: String): String =
    when (status) {
        is MatchStatus.Live -> {
            val statusClock = status.clock?.let { "$it'" }
            when {
                status.period.isExtraTime -> {
                    val extraTime = stringResource(R.string.wrex_widget_match_extra_time_short)
                    if (statusClock != null) "$extraTime: $statusClock" else extraTime
                }

                status.isHalftime -> {
                    val halftime = stringResource(R.string.wrex_widget_match_half_time)
                    if (statusClock != null) "$halftime: $statusClock" else halftime
                }

                statusClock != null -> statusClock
                else -> ""
            }
        }

        is MatchStatus.Penalties -> stringResource(R.string.wrex_widget_match_penalties)
        is MatchStatus.Final -> stringResource(R.string.wrex_widget_match_full_time_short)
        is MatchStatus.FinalAfterPenalties ->
            "(${stringResource(R.string.wrex_widget_match_full_time_short)} · " +
                "${stringResource(R.string.wrex_widget_match_penalties)})"

        else -> time
    }

@Composable
internal fun secondStatusSubtitle(status: MatchStatus, date: String): String =
    when (status) {
        is MatchStatus.Penalties -> "${status.homePenalty ?: "-"} - ${status.awayPenalty ?: "-"}"
        is MatchStatus.FinalAfterPenalties -> "${status.homePenalty ?: "-"} - ${status.awayPenalty ?: "-"}"
        else -> date
    }

private class ScoreboardPreviewProvider : PreviewParameterProvider<Match> {
    override val values =
        sequenceOf(
            match(status = MatchStatus.Scheduled),
            match(status = MatchStatus.Live(period = "1", clock = "29"), homeScore = 1, awayScore = 2),
            match(
                status = MatchStatus.Live(period = "1", clock = "45", isHalftime = true),
                homeScore = 1,
                awayScore = 0,
            ),
            match(status = MatchStatus.Live(period = "Extra", clock = "100"), homeScore = 2, awayScore = 2),
            match(status = MatchStatus.Penalties(homePenalty = 3, awayPenalty = 2), homeScore = 1, awayScore = 1),
            match(status = MatchStatus.Final, homeScore = 3, awayScore = 1),
            match(
                status = MatchStatus.FinalAfterPenalties(homePenalty = 4, awayPenalty = 5),
                homeScore = 1,
                awayScore = 1,
            ),
            match(status = MatchStatus.Postponed),
        )

    private fun match(status: MatchStatus, homeScore: Int? = null, awayScore: Int? = null) =
        FakeSportsPreview.match(
            date = "Oct 21",
            time = "2:45 PM",
            homeScore = homeScore,
            awayScore = awayScore,
            matchStatus = status,
        )
}

@PreviewLightDark
@Composable
private fun ScoreboardPreview(@PreviewParameter(ScoreboardPreviewProvider::class) match: Match) {
    FirefoxTheme {
        Surface {
            Scoreboard(match = match)
        }
    }
}
