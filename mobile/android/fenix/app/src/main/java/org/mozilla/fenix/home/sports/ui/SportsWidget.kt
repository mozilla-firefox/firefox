/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import android.content.res.Configuration
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.tooling.preview.PreviewLightDark
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.sports.SportsWidgetState
import org.mozilla.fenix.home.sports.MatchCard as MatchCardState
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.SportsCardType
import org.mozilla.fenix.home.sports.fake.FakeMatchCardScenario
import org.mozilla.fenix.home.sports.isPast
import org.mozilla.fenix.home.ui.horizontalMargin
import org.mozilla.fenix.theme.FirefoxTheme

private const val WIDE_LAYOUT_WIDTH_FRACTION = 0.7f
private const val FULL_WIDTH_FRACTION = 1f

/**
 * Sports widget for the homepage. Displays a [SportsCardPager] of match cards or a single error card when match data
 * cannot be displayed.
 *
 * @param state The [SportsWidgetState] to display.
 * @param modifier [Modifier] to be applied to the widget.
 * @param onRefresh Invoked when the "Refresh" button of an error card is tapped.
 * @param onMatchClicked Invoked when the match is clicked.
 * @param onResults Invoked when "Results" menu item is selected.
 * @param onStandings Invoked when "Standings" menu item is selected.
 * @param onWallpapers Invoked when "Wallpapers" menu item is selected.
 * @param onShare Invoked when "Share" menu item is selected.
 * @param onRemove Invoked when "Remove Widget" menu item is selected.
 */
@Suppress("LongParameterList")
@Composable
fun SportsWidget(
    state: SportsWidgetState,
    modifier: Modifier = Modifier,
    onRefresh: () -> Unit = {},
    onMatchClicked: (String?, String?, String?) -> Unit = { _, _, _ -> },
    onResults: () -> Unit = {},
    onStandings: () -> Unit = {},
    onWallpapers: () -> Unit = {},
    onShare: () -> Unit = {},
    onRemove: () -> Unit = {},
) {
    val isLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val widthFraction =
        if (AcornWindowSize.isLargeWindow() || isLandscape) WIDE_LAYOUT_WIDTH_FRACTION else FULL_WIDTH_FRACTION

    val pages =
        remember(state.matchCards, state.errorState, onRefresh, onMatchClicked) {
            sportsWidgetPages(
                matchCards = state.matchCards,
                errorState = state.errorState,
                onRefresh = onRefresh,
                onMatchClicked = onMatchClicked,
            )
        }

    SportsCardPager(
        pages = pages.pages,
        header = {
            SportsWidgetHeader(
                teamName = stringResource(R.string.wrex_widget_team_name),
                drawableResId = R.drawable.team_badge_wrexham,
                onResults = onResults,
                onStandings = onStandings,
                onWallpapers = onWallpapers,
                onShare = onShare,
                onRemove = onRemove,
            )
        },
        modifier = modifier.fillMaxWidth(fraction = widthFraction).padding(horizontal = horizontalMargin),
        initialPage = pages.initialPage,
    )
}

/**
 * The pages of the [SportsWidget] pager.
 *
 * @property pages The [SportsPage]s to display.
 * @property initialPage The page that the pager opens on.
 */
internal data class SportsWidgetPages(
    val pages: List<SportsPage>,
    val initialPage: Int,
)

/**
 * Builds the pager pages for [matchCards]. When [errorState] is set and no match is live, the pager collapses to a
 * single [SportsWidgetErrorCard] page. When a match is live, the [MatchCard] shows the error in place of that match.
 */
internal fun sportsWidgetPages(
    matchCards: List<MatchCardState>,
    errorState: SportCardErrorState?,
    onRefresh: () -> Unit,
    onMatchClicked: (String?, String?, String?) -> Unit,
): SportsWidgetPages {
    val hasLiveMatch = matchCards.any { card -> card.matches.any { it.matchStatus.isLive() } }

    if (errorState != null && !hasLiveMatch) {
        val type = SportsCardType.fromError(errorState)
        return SportsWidgetPages(
            pages =
                listOf(
                    SportsPage(type = type, key = "type:${type.name}") { _, _ ->
                        SportsWidgetErrorCard(state = errorState, onRefresh = onRefresh)
                    }
                ),
            initialPage = 0,
        )
    }

    return SportsWidgetPages(
        pages =
            matchCards.map { card ->
                SportsPage(type = SportsCardType.MATCH, key = card.pagerKey()) { pageNumber, pageCount ->
                    MatchCard(
                        state = card,
                        errorState = errorState,
                        onRefresh = { onRefresh() },
                        onMatchClicked = onMatchClicked,
                        pageNumber = pageNumber,
                        pageCount = pageCount,
                    )
                }
            },
        initialPage = initialMatchPage(matchCards),
    )
}

/** The first live or suspended card, else the first upcoming card, else the most recent past card. */
private fun initialMatchPage(matchCards: List<MatchCardState>): Int {
    val statuses = matchCards.map { card -> card.matches.map { it.matchStatus } }
    val livePage = statuses.indexOfFirst { cardStatuses ->
        cardStatuses.any { it.isLive() || it is MatchStatus.Suspended }
    }
    val upcomingPage = statuses.indexOfFirst { cardStatuses ->
        cardStatuses.any { it is MatchStatus.Scheduled || it is MatchStatus.Delayed }
    }
    val pastPage = statuses.indexOfLast { cardStatuses -> cardStatuses.any { it.isPast() } }
    return listOf(livePage, upcomingPage, pastPage).firstOrNull { it >= 0 } ?: 0
}

/** Stable identity of a match card across score refreshes: the set of its match ids. */
private fun MatchCardState.pagerKey(): String =
    matches.map { it.globalEventId }.sorted().joinToString(prefix = "match:", separator = ",")

@PreviewLightDark
@Composable
private fun SportsWidgetPreview() {
    FirefoxTheme {
        Surface {
            SportsWidget(state = SportsWidgetState(matchCards = FakeMatchCardScenario.Live.build()))
        }
    }
}
