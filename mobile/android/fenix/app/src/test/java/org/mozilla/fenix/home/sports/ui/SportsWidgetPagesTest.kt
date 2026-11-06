/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.ui

import org.junit.Assert.assertEquals
import org.junit.Test
import org.mozilla.fenix.home.sports.Match
import org.mozilla.fenix.home.sports.MatchCard
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.home.sports.SportCardErrorState
import org.mozilla.fenix.home.sports.SportsCardType

class SportsWidgetPagesTest {
    private val pastCard = card(1L, MatchStatus.Final)
    private val liveCard = card(2L, MatchStatus.Live(period = "2H", clock = "67"))
    private val upcomingCard = card(3L)
    private val matchweek = listOf(pastCard, liveCard, upcomingCard)

    @Test
    fun `GIVEN match cards WHEN building the pages THEN add a match page per card`() {
        val pages = buildPages(matchweek)

        assertEquals(List(3) { SportsCardType.MATCH }, pages.pages.map { it.type })
        assertEquals(matchweek.map { it.pageKey }, pages.pages.map { it.key })
    }

    @Test
    fun `GIVEN a live match WHEN building the pages THEN open on the live card`() {
        assertEquals(liveCard.pageKey, buildPages(matchweek).openedPage.key)
    }

    @Test
    fun `GIVEN no live match WHEN building the pages THEN open on the first upcoming card`() {
        val nextCard = card(2L)

        val pages = buildPages(listOf(pastCard, nextCard, card(3L)))

        assertEquals(nextCard.pageKey, pages.openedPage.key)
    }

    @Test
    fun `GIVEN only past matches WHEN building the pages THEN open on the most recent card`() {
        val latestCard = card(2L, MatchStatus.Final)

        val pages = buildPages(listOf(pastCard, latestCard))

        assertEquals(latestCard.pageKey, pages.openedPage.key)
    }

    @Test
    fun `GIVEN a suspended match WHEN building the pages THEN open on the suspended card`() {
        val suspendedCard = card(2L, MatchStatus.Suspended)

        val pages = buildPages(listOf(pastCard, suspendedCard, upcomingCard))

        assertEquals(suspendedCard.pageKey, pages.openedPage.key)
    }

    @Test
    fun `GIVEN a postponed match before a scheduled match WHEN building the pages THEN open on the scheduled card`() {
        val pages = buildPages(listOf(pastCard, card(2L, MatchStatus.Postponed), upcomingCard))

        assertEquals(upcomingCard.pageKey, pages.openedPage.key)
    }

    @Test
    fun `GIVEN past matches followed by a postponed match WHEN building the pages THEN open on the most recent past card`() {
        val latestCard = card(2L, MatchStatus.Final)

        val pages = buildPages(listOf(pastCard, latestCard, card(3L, MatchStatus.Postponed)))

        assertEquals(latestCard.pageKey, pages.openedPage.key)
    }

    @Test
    fun `GIVEN an error and no live match WHEN building the pages THEN show a single error page`() {
        val pages = buildPages(listOf(pastCard), SportCardErrorState.ResultsLoadFailed)

        assertEquals(listOf(SportsCardType.ERROR_RESULTS_LOAD_FAILED), pages.pages.map { it.type })
        assertEquals(SportsCardType.ERROR_RESULTS_LOAD_FAILED, pages.openedPage.type)
    }

    @Test
    fun `GIVEN an error and no match cards WHEN building the pages THEN show a single error page`() {
        val pages = buildPages(emptyList(), SportCardErrorState.LoadFailed)

        assertEquals(listOf(SportsCardType.ERROR_LOAD_FAILED), pages.pages.map { it.type })
    }

    @Test
    fun `GIVEN an error and a live match WHEN building the pages THEN keep the match pages`() {
        val pages = buildPages(matchweek, SportCardErrorState.ConnectionInterrupted)

        assertEquals(matchweek.map { it.pageKey }, pages.pages.map { it.key })
        assertEquals(liveCard.pageKey, pages.openedPage.key)
    }

    private val SportsWidgetPages.openedPage: SportsPage
        get() = pages[initialPage]

    private val MatchCard.pageKey: String
        get() = "match:${matches.single().globalEventId}"

    private fun card(globalEventId: Long, matchStatus: MatchStatus = MatchStatus.Scheduled) =
        MatchCard(
            matches =
                listOf(
                    Match(
                        globalEventId = globalEventId,
                        date = "Oct 10",
                        time = "3:00 PM",
                        home = null,
                        away = null,
                        matchStatus = matchStatus,
                    )
                ),
            label = "Test League",
        )

    private fun buildPages(matchCards: List<MatchCard>, errorState: SportCardErrorState? = null) =
        sportsWidgetPages(
            matchCards = matchCards,
            errorState = errorState,
            onRefresh = {},
            onMatchClicked = { _, _, _ -> },
        )
}
