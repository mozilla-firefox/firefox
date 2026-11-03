/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import androidx.compose.ui.unit.Dp
import mozilla.components.compose.base.modifier.thenConditional
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.LiveMatchRefreshSource
import org.mozilla.fenix.home.sports.Match
import org.mozilla.fenix.home.sports.MatchCard as MatchCardState
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.fake.FakeMatchCardScenario
import org.mozilla.fenix.home.sports.isExtraTime
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Card that renders a sports match.
 *
 * @param state The [MatchCardState] to display in this card.
 * @param errorState The [SportCardErrorState] to display in this card when there is an error during a live match.
 * @param onRefresh Used to refresh the scores for live matches.
 * @param onMatchClicked Used to handle match click actions.
 * @param modifier [Modifier] to be applied to the card.
 * @param pageNumber 1-based page position when this card is rendered inside a pager; used by the header to announce
 *   e.g. "FA Cup, page 1 of 2" for assistive technology.
 * @param pageCount Total number of pages when rendered inside a pager. Ignored if `pageNumber` is null.
 */
@Composable
fun MatchCard(
    state: MatchCardState,
    errorState: SportCardErrorState?,
    onRefresh: (LiveMatchRefreshSource) -> Unit,
    onMatchClicked: (String?, String?, String?) -> Unit,
    modifier: Modifier = Modifier,
    pageNumber: Int? = null,
    pageCount: Int? = null,
) {
    val showsError = errorState != null && state.matches.any { it.matchStatus.isLive() }

    Column(
        modifier =
            modifier.fillMaxWidth().thenConditional(Modifier.padding(bottom = FirefoxTheme.layout.space.static150)) {
                !showsError
            },
        verticalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static200),
    ) {
        val matches = state.matches
        val sportHeaderMatch = matches.firstOrNull()

        if (sportHeaderMatch != null) {
            MatchCardHeader(
                match = sportHeaderMatch,
                label = state.label,
                modifier = Modifier.padding(horizontal = MatchContentHorizontalPadding),
                pageNumber = pageNumber,
                pageCount = pageCount,
            )
        }

        matches.forEach { match ->
            MatchBody(
                match = match,
                errorState = errorState,
                onMatchClicked = onMatchClicked,
                onRefresh = onRefresh,
            )
        }
    }
}

private val MatchContentHorizontalPadding: Dp
    @Composable get() = FirefoxTheme.layout.space.static50 + FirefoxTheme.layout.space.static25

/**
 * Renders the body content of the card. This will display a countdown pill for scheduled matches or a score pill for
 * current matches.
 */
@Composable
internal fun MatchBody(
    match: Match,
    errorState: SportCardErrorState?,
    onMatchClicked: (String?, String?, String?) -> Unit,
    onRefresh: (LiveMatchRefreshSource) -> Unit,
) {
    if (errorState != null && match.matchStatus.isLive()) {
        SportsWidgetErrorCard(
            state = errorState,
            onRefresh = { onRefresh(LiveMatchRefreshSource.LIVE_MATCH_CARD_ERROR_BUTTON) },
        )
    } else {
        val rowContentDescription = matchBodyContentDescription(match = match)

        Row(
            modifier =
                Modifier.fillMaxWidth()
                    .padding(horizontal = MatchContentHorizontalPadding)
                    .clickable(
                        onClick = {
                            if (match.home != null || match.away != null) {
                                onMatchClicked(match.home?.key, match.away?.key, "${match.date} ${match.time}")
                            }
                        }
                    )
                    .clearAndSetSemantics {
                        contentDescription = rowContentDescription
                    },
            horizontalArrangement = Arrangement.spacedBy(FirefoxTheme.layout.space.static100),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            TeamSlot(team = match.home, modifier = Modifier.weight(1f))

            Scoreboard(match = match, modifier = Modifier.weight(1f))

            TeamSlot(team = match.away, modifier = Modifier.weight(1f))
        }
    }
}

@Composable
private fun matchBodyContentDescription(match: Match): String {
    val homeName =
        match.home?.let { it.name.ifEmpty { it.key } } ?: stringResource(R.string.wrex_widget_team_to_be_determined)
    val awayName =
        match.away?.let { it.name.ifEmpty { it.key } } ?: stringResource(R.string.wrex_widget_team_to_be_determined)
    val middleText = matchBodyMiddleText(match = match)

    return when {
        match.homeScore == null || match.awayScore == null ->
            stringResource(
                R.string.wrex_widget_match_content_description,
                homeName,
                awayName,
                middleText,
            )

        match.matchStatus.isLive() ->
            stringResource(
                R.string.wrex_widget_live_score_content_description,
                homeName,
                match.homeScore,
                awayName,
                match.awayScore,
                middleText,
            )

        else ->
            listOf(
                    homeName,
                    match.homeScore.toString(),
                    awayName,
                    match.awayScore.toString(),
                    middleText,
                )
                .filter { it.isNotEmpty() }
                .joinToString(separator = " ")
    }
}

@Composable
private fun matchBodyMiddleText(match: Match): String {
    val status = match.matchStatus
    val primary =
        if (status is MatchStatus.Live) {
            val elapsed =
                status.clock?.let {
                    stringResource(R.string.wrex_widget_match_elapsed_minutes, it)
                }
            when {
                status.period.isExtraTime -> {
                    val extraTime = stringResource(R.string.wrex_widget_match_extra_time_short)
                    if (elapsed != null) "$extraTime $elapsed" else extraTime
                }
                status.isHalftime -> {
                    val halftime = stringResource(R.string.wrex_widget_match_half_time)
                    if (elapsed != null) "$halftime $elapsed" else halftime
                }
                elapsed != null -> elapsed
                else -> ""
            }
        } else {
            statusSubtitle(status = status, time = match.time)
        }
    val secondary =
        if (status.hasSecondaryStatusSubtitle()) {
            secondStatusSubtitle(status = status, date = match.date)
        } else {
            ""
        }
    return listOf(primary, secondary).filter { it.isNotEmpty() }.joinToString(separator = " ")
}

private data class MatchCardPreviewState(
    val label: String,
    val state: MatchCardState,
)

private class MatchCardPreviewProvider : PreviewParameterProvider<MatchCardPreviewState> {
    override val values =
        FakeMatchCardScenario.entries.asSequence().map { scenario ->
            MatchCardPreviewState(label = scenario.label, state = scenario.build().first())
        }
}

@PreviewLightDark
@Composable
private fun MatchCardPreview(@PreviewParameter(MatchCardPreviewProvider::class) preview: MatchCardPreviewState) {
    FirefoxTheme {
        Surface {
            MatchCard(
                state = preview.state,
                errorState = null,
                onRefresh = {},
                onMatchClicked = { _, _, _ -> },
            )
        }
    }
}

@PreviewLightDark
@Composable
private fun MatchCardErrorPreview(@PreviewParameter(MatchCardPreviewProvider::class) preview: MatchCardPreviewState) {
    FirefoxTheme {
        Surface {
            MatchCard(
                state = preview.state,
                errorState = SportCardErrorState.LoadFailed,
                onRefresh = {},
                onMatchClicked = { _, _, _ -> },
            )
        }
    }
}
