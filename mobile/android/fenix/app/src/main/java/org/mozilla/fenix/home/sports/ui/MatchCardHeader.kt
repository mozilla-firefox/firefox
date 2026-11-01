/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.width
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.heading
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.tooling.preview.PreviewParameter
import androidx.compose.ui.tooling.preview.PreviewParameterProvider
import org.mozilla.fenix.R
import org.mozilla.fenix.home.sports.Match
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.home.sports.fake.FakeSportsPreview
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Header of a match card displaying the match status badge and competition label.
 *
 * @param match The [Match] whose status is displayed.
 * @param label Name of the competition displayed next to the status badge (e.g. "FA Cup").
 * @param modifier [Modifier] to be applied to the header.
 * @param pageNumber 1-based page position when rendered inside a pager, announced for assistive technology.
 * @param pageCount Total number of pages when rendered inside a pager. Ignored if [pageNumber] is null.
 */
@Composable
internal fun MatchCardHeader(
    match: Match,
    label: String,
    modifier: Modifier = Modifier,
    pageNumber: Int? = null,
    pageCount: Int? = null,
) {
    val isLive = match.matchStatus.isLive()
    val interruptedStatusText = match.matchStatus.interruptedStatusText()
    val baseContentDescription =
        when {
            isLive -> stringResource(R.string.wrex_widget_live_game_content_description, label)
            interruptedStatusText != null -> "$interruptedStatusText, $label"
            else -> label
        }
    val headerContentDescription =
        pagerHeadingContentDescription(
            baseText = baseContentDescription,
            pageNumber = pageNumber,
            pageCount = pageCount,
        )

    Row(
        modifier = modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier =
                Modifier.weight(1f).clearAndSetSemantics {
                    heading()
                    contentDescription = headerContentDescription
                },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            MatchStatusBadge(status = match.matchStatus)

            if (isLive || interruptedStatusText != null) {
                Spacer(modifier = Modifier.width(FirefoxTheme.layout.space.static100))
            }

            Text(
                text = label,
                style = FirefoxTheme.typography.caption,
                color = MaterialTheme.colorScheme.secondary,
            )
        }
    }
}

/**
 * Returns [baseText] with a "page X of Y" suffix appended for TalkBack, using
 * [R.string.wrex_widget_page_position_content_description]. Returns [baseText] unchanged when [pageNumber] is null,
 * [pageCount] is null, or [pageCount] is 1.
 */
@Composable
internal fun pagerHeadingContentDescription(
    baseText: String,
    pageNumber: Int?,
    pageCount: Int?,
): String =
    if (pageNumber != null && pageCount != null && pageCount > 1) {
        stringResource(
            R.string.wrex_widget_page_position_content_description,
            baseText,
            pageNumber,
            pageCount,
        )
    } else {
        baseText
    }

private data class MatchCardHeaderPreviewState(
    val label: String,
    val status: MatchStatus,
)

private class MatchCardHeaderPreviewProvider : PreviewParameterProvider<MatchCardHeaderPreviewState> {
    override val values =
        sequenceOf(
            MatchCardHeaderPreviewState(
                label = "Moz Cup",
                status = MatchStatus.Live(period = "1", clock = "29"),
            ),
            MatchCardHeaderPreviewState(
                label = "ABC Championship",
                status = MatchStatus.Delayed,
            ),
            MatchCardHeaderPreviewState(
                label = "ABC Championship",
                status = MatchStatus.Suspended,
            ),
            MatchCardHeaderPreviewState(
                label = "ABC Championship",
                status = MatchStatus.Postponed,
            ),
            MatchCardHeaderPreviewState(
                label = "XYZ Champions League",
                status = MatchStatus.FinalAfterPenalties(homePenalty = 4, awayPenalty = 5),
            ),
            MatchCardHeaderPreviewState(
                label = "XYZ Championship",
                status = MatchStatus.Scheduled,
            ),
        )
}

@PreviewLightDark
@Composable
private fun MatchCardHeaderPreview(
    @PreviewParameter(MatchCardHeaderPreviewProvider::class) state: MatchCardHeaderPreviewState
) {
    FirefoxTheme {
        Surface {
            MatchCardHeader(
                match =
                    Match(
                        date = "Oct 21",
                        time = "2:45 PM",
                        home = FakeSportsPreview.usa,
                        away = FakeSportsPreview.par,
                        matchStatus = state.status,
                    ),
                label = state.label,
            )
        }
    }
}
