/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import android.content.res.Configuration
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Surface
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.remember
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalConfiguration
import androidx.compose.ui.tooling.preview.PreviewLightDark
import androidx.compose.ui.unit.dp
import mozilla.components.compose.base.theme.layout.AcornWindowSize
import org.mozilla.fenix.R
import org.mozilla.fenix.components.appstate.sports.SportsWidgetState
import org.mozilla.fenix.home.sports.CountrySelectorSource
import org.mozilla.fenix.home.sports.FollowedTeamOutcome
import org.mozilla.fenix.home.sports.LiveMatchRefreshSource
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.SportsCardImpressionSource
import org.mozilla.fenix.home.sports.SportsCardType
import org.mozilla.fenix.home.sports.Team
import org.mozilla.fenix.home.sports.isLive
import org.mozilla.fenix.home.sports.isPast
import org.mozilla.fenix.home.sports.regionGrouping
import org.mozilla.fenix.home.sports.worldCupKickoffCountdownTarget
import org.mozilla.fenix.home.ui.horizontalMargin
import org.mozilla.fenix.theme.FirefoxTheme
import org.mozilla.fenix.home.sports.MatchCard as MatchCardState

private const val WIDE_LAYOUT_WIDTH_FRACTION = 0.7f
private const val FULL_WIDTH_FRACTION = 1f
private val SportsWidgetTopSpacing = 44.dp

/**
 * Sports widget for the homepage. Renders countdown, one-week promo, or match cards based on
 * the current date and [sportsWidgetState].
 *
 * @param sportsWidgetState [SportsWidgetState] driving the widget's content.
 * @param onDismiss Invoked when the user dismisses the sports widget.
 * @param onCountdownWidgetDismiss Invoked when the user dismisses the countdown card.
 * @param onViewSchedule Invoked when the "View schedule" button is tapped.
 * @param onFollowTeam Invoked when a team is followed.
 * @param onSkip Invoked when the user dismisses the "Follow team" card.
 * @param onGetCustomWallpaper Invoked when the user clicks on the "Get custom wallpaper" menu item.
 * @param onShare Invoked when the user clicks on the "Share" menu item.
 * @param onRefresh Used to refresh the scores for live matches.
 * @param onMatchClicked Used to handle match click actions.
 * @param onCardShown Invoked once per widget mount for the first visible card (impression) and on
 * every subsequent swipe to a different page in the pager (swipe).
 * @param modifier [Modifier] to apply to the composable.
 */
@Composable
@Suppress("UNUSED_PARAMETER", "LongParameterList")
fun SportsWidget(
    sportsWidgetState: SportsWidgetState,
    onDismiss: () -> Unit,
    onCountdownWidgetDismiss: () -> Unit,
    onViewSchedule: () -> Unit,
    onFollowTeam: (CountrySelectorSource) -> Unit,
    onSkip: () -> Unit,
    onGetCustomWallpaper: () -> Unit,
    onShare: () -> Unit,
    onRefresh: (LiveMatchRefreshSource) -> Unit,
    onMatchClicked: (String?, String?, String?) -> Unit,
    onCardShown: (SportsCardType, SportsCardImpressionSource) -> Unit,
    modifier: Modifier = Modifier,
    showTopSpacing: Boolean = false,
) {
    // The reorderable homepage needs this top spacing for the widget's peeking content; the
    // existing homepage handles spacing in its own layout, so it is opt-in via [showTopSpacing].
    if (showTopSpacing) {
        Spacer(modifier = Modifier.height(SportsWidgetTopSpacing))
    }

    val isLandscape = LocalConfiguration.current.orientation == Configuration.ORIENTATION_LANDSCAPE
    val isLargeWindow = AcornWindowSize.isLargeWindow()
    val widthFraction = if (isLargeWindow || isLandscape) {
        WIDE_LAYOUT_WIDTH_FRACTION
    } else {
        FULL_WIDTH_FRACTION
    }

    val containerModifier = modifier
        .fillMaxWidth(fraction = widthFraction)
        .padding(horizontal = horizontalMargin)

    when {
        sportsWidgetState.isCountdownShown -> {
            // Even pre-tournament, surface the error card when a fetch failure is
            // active so the user sees the failure instead of the countdown promo.
            val standaloneCardType = sportsWidgetState.errorState?.let { SportsCardType.fromError(it) }
                ?: SportsCardType.COUNTDOWN_PROMO
            LaunchedEffect(standaloneCardType) {
                onCardShown(standaloneCardType, SportsCardImpressionSource.IMPRESSION)
            }
            if (sportsWidgetState.errorState != null) {
                SportsWidgetErrorCard(
                    error = sportsWidgetState.errorState,
                    onRefresh = { onRefresh(LiveMatchRefreshSource.SPORTS_WIDGET_CARD_ERROR_BUTTON) },
                    modifier = containerModifier,
                )
            } else {
                CountdownPromoCard(
                    dateInUtc = worldCupKickoffCountdownTarget(),
                    actionButtonLabelResId = R.string.sports_widget_view_schedule,
                    onClick = onViewSchedule,
                    onDismiss = onCountdownWidgetDismiss,
                    modifier = containerModifier,
                )
            }
        }

        sportsWidgetState.isOneWeekToWorldCup || sportsWidgetState.hasWorldCupStarted -> {
            SportsCardPagerSection(
                sportsWidgetState = sportsWidgetState,
                onFollowTeam = onFollowTeam,
                onGetCustomWallpaper = onGetCustomWallpaper,
                onShare = onShare,
                onDismiss = onDismiss,
                onRefresh = onRefresh,
                onMatchClicked = onMatchClicked,
                onCardShown = onCardShown,
                modifier = containerModifier,
            )
        }
    }
}

@Composable
@Suppress("LongParameterList")
private fun SportsCardPagerSection(
    sportsWidgetState: SportsWidgetState,
    onFollowTeam: (CountrySelectorSource) -> Unit,
    onGetCustomWallpaper: () -> Unit,
    onShare: () -> Unit,
    onDismiss: () -> Unit,
    onRefresh: (LiveMatchRefreshSource) -> Unit,
    onMatchClicked: (String?, String?, String?) -> Unit,
    onCardShown: (SportsCardType, SportsCardImpressionSource) -> Unit,
    modifier: Modifier = Modifier,
) {
    val countriesSelected = sportsWidgetState.countriesSelected
    val selectedTeam = remember(countriesSelected) {
        regionGrouping
            .asSequence()
            .flatMap { it.teams.asSequence() }
            .firstOrNull { it.key in countriesSelected }
    }

    val pagesResult = remember(
        sportsWidgetState.isOneWeekToWorldCup,
        sportsWidgetState.isFollowTeamsCardShown,
        selectedTeam,
        sportsWidgetState.matchCardStates,
        sportsWidgetState.errorState,
        onFollowTeam,
        onGetCustomWallpaper,
        onShare,
        onDismiss,
        onRefresh,
        onMatchClicked,
    ) {
        sportsCardPages(
            isOneWeekToWorldCup = sportsWidgetState.isOneWeekToWorldCup,
            isFollowTeamsCardShown = sportsWidgetState.isFollowTeamsCardShown,
            selectedTeam = selectedTeam,
            matchCardStates = sportsWidgetState.matchCardStates,
            errorState = sportsWidgetState.errorState,
            onFollowTeam = onFollowTeam,
            onGetCustomWallpaper = onGetCustomWallpaper,
            onShare = onShare,
            onRemove = onDismiss,
            onRefresh = onRefresh,
            onMatchClicked = onMatchClicked,
        )
    }

    SportsCardPager(
        isTeamSelected = selectedTeam != null,
        pages = pagesResult.pages,
        onChangeTeam = onFollowTeam,
        onGetCustomWallpaper = onGetCustomWallpaper,
        onShare = onShare,
        onRemove = onDismiss,
        onCardShown = onCardShown,
        modifier = modifier,
        championsPageIndices = pagesResult.championsPageIndices,
        errorPageIndices = pagesResult.errorPageIndices,
        initialPage = pagesResult.initialPage,
    )
}

internal data class SportsCardPagesResult(
    val pages: List<SportsPage>,
    val championsPageIndices: Set<Int>,
    val errorPageIndices: Set<Int>,
    // Page the pager should open on: the live card, else the next upcoming card, else the most
    // recent past card (tournament over). Defaults to 0.
    val initialPage: Int = 0,
)

@Suppress("LongParameterList")
internal fun sportsCardPages(
    isOneWeekToWorldCup: Boolean,
    isFollowTeamsCardShown: Boolean,
    selectedTeam: Team?,
    matchCardStates: List<MatchCardState>,
    errorState: SportCardErrorState?,
    onFollowTeam: (CountrySelectorSource) -> Unit,
    onGetCustomWallpaper: () -> Unit,
    onShare: () -> Unit,
    onRemove: () -> Unit,
    onRefresh: (LiveMatchRefreshSource) -> Unit,
    onMatchClicked: (String?, String?, String?) -> Unit,
): SportsCardPagesResult {
    val championsPageIndices = mutableSetOf<Int>()
    val errorPageIndices = mutableSetOf<Int>()
    val pages = buildList {
        if (addCollapsedErrorPage(
                isOneWeekToWorldCup = isOneWeekToWorldCup,
                matchCardStates = matchCardStates,
                errorState = errorState,
                onRefresh = onRefresh,
                errorPageIndices = errorPageIndices,
            )
        ) {
            return@buildList
        }
        addPromoPage(isOneWeekToWorldCup, isFollowTeamsCardShown, selectedTeam, matchCardStates, onFollowTeam)

        matchCardStates.forEach { matchCardState ->
            if (shouldDisplayChampionsCard(matchCardState.viewerOutcome)) {
                championsPageIndices.add(size)
                add(championsCardPage(matchCardState, onMatchClicked, onGetCustomWallpaper, onShare, onRemove))
            } else {
                add(matchCardPage(matchCardState, errorState, selectedTeam != null, onRefresh, onMatchClicked))
            }
        }
    }
    // When the pager collapsed to a single error page there is no match card to open on; otherwise
    // the match cards are the trailing pages, so their offset is everything added before them.
    val initialPage = if (errorPageIndices.isEmpty()) {
        initialMatchPage(matchCardStates, offset = pages.size - matchCardStates.size)
    } else {
        0
    }
    return SportsCardPagesResult(pages, championsPageIndices, errorPageIndices, initialPage)
}

/**
 * Page the pager should open on, given the [matchCardStates] and the [offset] of the first match
 * card within the page list: the first live card, else the first upcoming card, else the most
 * recent past card (tournament over). Defaults to 0.
 */
private fun initialMatchPage(matchCardStates: List<MatchCardState>, offset: Int): Int {
    var livePage: Int? = null
    var firstUpcomingPage: Int? = null
    var lastMatchPage: Int? = null
    matchCardStates.forEachIndexed { index, matchCardState ->
        val pageIndex = offset + index
        lastMatchPage = pageIndex
        val statuses = (matchCardState.matches + matchCardState.relatedMatches).map { it.matchStatus }
        when {
            statuses.any { it.isLive() } -> if (livePage == null) livePage = pageIndex
            statuses.isNotEmpty() && statuses.all { it.isPast() } -> Unit
            else -> if (firstUpcomingPage == null) firstUpcomingPage = pageIndex
        }
    }
    return livePage ?: firstUpcomingPage ?: lastMatchPage ?: 0
}

/**
 * When [errorState] is set and no live match exists in [matchCardStates], collapses the
 * whole pager into a single [SportsWidgetErrorCard] page and returns `true`. Otherwise
 * returns `false` without modifying the list — when a live match is present, [MatchCard]
 * swaps the error in-line within that one card and the surrounding pages keep their
 * content, so the page list (and the user's pager position) stays stable.
 *
 * During the pre-tournament one-week phase ([isOneWeekToWorldCup]), an error is also
 * suppressed if [matchCardStates] is non-empty: there are no live scores to be wrong
 * about, just a schedule the user already saw, so a stale cached schedule is more
 * useful than an error banner. The error is only surfaced when the cache is empty —
 * i.e. the user has nothing else to look at.
 */
@Suppress("LongParameterList")
private fun MutableList<SportsPage>.addCollapsedErrorPage(
    isOneWeekToWorldCup: Boolean,
    matchCardStates: List<MatchCardState>,
    errorState: SportCardErrorState?,
    onRefresh: (LiveMatchRefreshSource) -> Unit,
    errorPageIndices: MutableSet<Int>,
): Boolean {
    if (errorState == null) return false
    val anyLive = matchCardStates.any { card ->
        (card.matches + card.relatedMatches).any { it.matchStatus.isLive() }
    }
    if (anyLive) return false
    if (isOneWeekToWorldCup && matchCardStates.isNotEmpty()) return false

    val type = SportsCardType.fromError(errorState)
    errorPageIndices.add(size)
    add(
        SportsPage(type = type, key = type.pagerKey()) { _, _ ->
            SportsWidgetErrorCard(
                error = errorState,
                onRefresh = { onRefresh(LiveMatchRefreshSource.SPORTS_WIDGET_CARD_ERROR_BUTTON) },
                modifier = Modifier.fillMaxWidth(),
            )
        },
    )
    return true
}

/**
 * Prepends the leading promo card that precedes the match cards: either the "follow
 * your team" / pre-tournament countdown card (if the user hasn't followed a team yet),
 * or the "you're following X" card (if a team is followed but no matches are available).
 * Adds nothing when neither applies.
 */
private fun MutableList<SportsPage>.addPromoPage(
    isOneWeekToWorldCup: Boolean,
    isFollowTeamsCardShown: Boolean,
    selectedTeam: Team?,
    matchCardStates: List<MatchCardState>,
    onFollowTeam: (CountrySelectorSource) -> Unit,
) {
    when {
        isFollowTeamsCardShown -> when {
            isOneWeekToWorldCup -> add(countdownFollowTeamPage(onFollowTeam))
            // Suppress the "Keep tabs on the World Cup" promo once a champions card is in the pager
            matchCardStates.any { shouldDisplayChampionsCard(it.viewerOutcome) } -> Unit
            else -> add(followTeamPromoPage(onFollowTeam))
        }
        selectedTeam != null && matchCardStates.isEmpty() -> add(followingPromoPage(selectedTeam))
    }
}

private fun countdownFollowTeamPage(
    onFollowTeam: (CountrySelectorSource) -> Unit,
): SportsPage = SportsPage(
    type = SportsCardType.COUNTDOWN_PROMO,
    key = SportsCardType.COUNTDOWN_PROMO.pagerKey(),
) { pageNumber, pageCount ->
    CountdownPromoCard(
        dateInUtc = worldCupKickoffCountdownTarget(),
        actionButtonLabelResId = R.string.sports_widget_country_selector_title,
        onClick = { onFollowTeam(CountrySelectorSource.COUNTDOWN_CARD_FOLLOW_TEAM_BUTTON) },
        onDismiss = null,
        pageNumber = pageNumber,
        pageCount = pageCount,
    )
}

private fun followTeamPromoPage(
    onFollowTeam: (CountrySelectorSource) -> Unit,
): SportsPage = SportsPage(
    type = SportsCardType.FOLLOW_TEAM_PROMO,
    key = SportsCardType.FOLLOW_TEAM_PROMO.pagerKey(),
) { pageNumber, pageCount ->
    FollowTeamPromoCard(
        onFollowTeam = onFollowTeam,
        pageNumber = pageNumber,
        pageCount = pageCount,
    )
}

private fun followingPromoPage(
    team: Team,
): SportsPage = SportsPage(
    type = SportsCardType.FOLLOWING_PROMO,
    key = SportsCardType.FOLLOWING_PROMO.pagerKey(),
) { pageNumber, pageCount ->
    FollowingPromoCard(
        team = team,
        pageNumber = pageNumber,
        pageCount = pageCount,
    )
}

private fun championsCardPage(
    state: MatchCardState,
    onMatchClicked: (String?, String?, String?) -> Unit,
    onGetCustomWallpaper: () -> Unit,
    onShare: () -> Unit,
    onRemove: () -> Unit,
): SportsPage {
    val type = when (state.viewerOutcome) {
        is FollowedTeamOutcome.ThirdPlace -> SportsCardType.CHAMPIONS_THIRD_PLACE
        else -> SportsCardType.CHAMPIONS_WINNER
    }
    return SportsPage(type = type, key = state.pagerKey()) { pageNumber, pageCount ->
        ChampionsCard(
            state = state,
            onMatchClicked = onMatchClicked,
            onGetCustomWallpaper = onGetCustomWallpaper,
            onShare = onShare,
            onRemove = onRemove,
            pageNumber = pageNumber,
            pageCount = pageCount,
        )
    }
}

private fun matchCardPage(
    state: MatchCardState,
    errorState: SportCardErrorState?,
    isTeamSelected: Boolean,
    onRefresh: (LiveMatchRefreshSource) -> Unit,
    onMatchClicked: (String?, String?, String?) -> Unit,
): SportsPage = SportsPage(
    type = SportsCardType.fromRound(state.round),
    key = state.pagerKey(),
) { pageNumber, pageCount ->
    MatchCard(
        state = state,
        errorState = errorState,
        isTeamSelected = isTeamSelected,
        onRefresh = onRefresh,
        onMatchClicked = onMatchClicked,
        pageNumber = pageNumber,
        pageCount = pageCount,
    )
}

private fun shouldDisplayChampionsCard(followedTeamOutcome: FollowedTeamOutcome): Boolean =
    when (followedTeamOutcome) {
        is FollowedTeamOutcome.TournamentWinner, is FollowedTeamOutcome.ThirdPlace -> true
        else -> false
    }

// Stable identity for a promo/error page: only one card of each type ever appears in the pager.
private fun SportsCardType.pagerKey(): String = "type:$name"

// Stable identity for a match-backed card: the set of its match ids (featured plus related),
// order-independent, so it survives score refreshes and featured/related reshuffles within the
// same fixtures but changes when the tournament advances to a different set of matches (a new
// round). Used by the pager to restore the user's position by card rather than by raw index.
private fun MatchCardState.pagerKey(): String =
    (matches + relatedMatches)
        .map { it.globalEventId }
        .sorted()
        .joinToString(prefix = "match:", separator = ",")

@PreviewLightDark
@Composable
private fun SportsWidgetCountdownPreview() {
    FirefoxTheme {
        Surface {
            CountdownPromoCard(
                dateInUtc = "2026-06-11T19:00:00Z",
                actionButtonLabelResId = R.string.sports_widget_country_selector_title,
                onClick = {},
                onDismiss = null,
                modifier = Modifier.padding(FirefoxTheme.layout.space.static200),
            )
        }
    }
}
