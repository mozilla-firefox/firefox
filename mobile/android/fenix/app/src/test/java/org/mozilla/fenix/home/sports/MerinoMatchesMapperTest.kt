/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.time.Instant
import java.time.ZoneId
import java.util.Locale
import mozilla.components.service.merino.sports.MerinoCompetition
import mozilla.components.service.merino.sports.MerinoMatch
import mozilla.components.service.merino.sports.MerinoMatchPeriod
import mozilla.components.service.merino.sports.MerinoMatchStatus
import mozilla.components.service.merino.sports.MerinoMatches
import mozilla.components.service.merino.sports.MerinoTeam
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MerinoMatchesMapperTest {

    @Test
    fun `GIVEN previous, current and upcoming matches WHEN mapped THEN return one card per match in chronological order`() {
        val matches =
            matches(
                previous = listOf(match(id = 2L), match(id = 1L)),
                current = listOf(match(id = 3L)),
                upcoming = listOf(match(id = 4L), match(id = 5L)),
            )

        val cards = matches.mapInNewYorkCards()

        assertEquals(listOf(1L, 2L, 3L, 4L, 5L), cards.map { it.matches.single().globalEventId })
    }

    @Test
    fun `GIVEN every group is empty WHEN mapped THEN return no cards`() {
        assertEquals(emptyList<MatchCard>(), matches().mapInNewYorkCards())
    }

    @Test
    fun `GIVEN only previous matches WHEN mapped THEN return one card per match with the oldest first`() {
        val cards = matches(previous = listOf(match(id = 2L), match(id = 1L))).mapInNewYorkCards()

        assertEquals(listOf(1L, 2L), cards.map { it.matches.single().globalEventId })
    }

    @Test
    fun `GIVEN only upcoming matches WHEN mapped THEN return one card per match with the earliest first`() {
        val cards = matches(upcoming = listOf(match(id = 1L), match(id = 2L))).mapInNewYorkCards()

        assertEquals(listOf(1L, 2L), cards.map { it.matches.single().globalEventId })
    }

    @Test
    fun `GIVEN a match WHEN mapped THEN the card is labelled with the competition name`() {
        val cards =
            matches(upcoming = listOf(match(competition = MerinoCompetition(id = 27L, name = "Test Cup"))))
                .mapInNewYorkCards()

        assertEquals("Test Cup", cards.single().label)
    }

    @Test
    fun `GIVEN a match WHEN mapped THEN the teams are mapped with their name, id and logo`() {
        val match =
            matches(
                    upcoming =
                        listOf(
                            match(
                                homeTeam = MerinoTeam(id = 1L, name = "Team A", logoUrl = "https://example.com/a.png"),
                                awayTeam = MerinoTeam(id = 2L, name = "Team B", logoUrl = "https://example.com/b.png"),
                            )
                        )
                )
                .mapInNewYorkCards()
                .single()
                .matches
                .single()

        assertEquals(
            Team(
                name = "Team A",
                id = 1L,
                iconUrl = "https://example.com/a.png",
            ),
            match.home,
        )
        assertEquals(Team(name = "Team B", id = 2L, iconUrl = "https://example.com/b.png"), match.away)
    }

    @Test
    fun `GIVEN a UTC kickoff WHEN mapped THEN the date and time are formatted in the given time zone`() {
        val match = mapFirstMatch(match(kickoff = "2026-10-10T18:00:00Z"))

        assertEquals("Oct 10", match.date)
        assertEquals("2:00 PM", match.time.replace(' ', ' '))
    }

    @Test
    fun `GIVEN a locale that puts the day first WHEN mapped THEN the date follows the locale order`() {
        val match =
            matches(current = listOf(match(kickoff = "2026-10-10T18:00:00Z")))
                .toMatchCards(zoneId = ZoneId.of("Europe/Paris"), locale = Locale.FRANCE, is24HourFormat = true)
                .single()
                .matches
                .single()

        assertEquals("10 oct.", match.date)
    }

    @Test
    fun `GIVEN the device uses the 24-hour clock WHEN mapped THEN the time is formatted with the 24-hour clock`() {
        val match =
            matches(current = listOf(match(kickoff = "2026-10-10T18:00:00Z")))
                .mapInNewYorkCards(is24HourFormat = true)
                .single()
                .matches
                .single()

        assertEquals("14:00", match.time)
    }

    @Test
    fun `GIVEN a kickoff that cannot be parsed WHEN mapped THEN the date and time are empty`() {
        val match = mapFirstMatch(match(kickoff = "not a date"))

        assertEquals("", match.date)
        assertEquals("", match.time)
    }

    @Test
    fun `GIVEN a scheduled match WHEN mapped THEN the status is Scheduled`() {
        assertEquals(MatchStatus.Scheduled, mapFirstMatch(match(status = MerinoMatchStatus.SCHEDULED)).matchStatus)
    }

    @Test
    fun `GIVEN a match in progress WHEN mapped THEN the status is Live with the clock`() {
        val match =
            mapFirstMatch(
                match(
                    status = MerinoMatchStatus.IN_PROGRESS,
                    period = MerinoMatchPeriod.REGULAR,
                    clock = "90+3",
                    homeScore = 1,
                    awayScore = 1,
                )
            )

        assertEquals(MatchStatus.Live(clock = "90+3", isExtraTime = false, isHalftime = false), match.matchStatus)
        assertEquals(1, match.homeScore)
        assertEquals(1, match.awayScore)
        assertEquals("90+3", match.clock)
    }

    @Test
    fun `GIVEN a match in extra time WHEN mapped THEN the status is Live in extra time`() {
        val status =
            mapFirstMatch(
                    match(status = MerinoMatchStatus.IN_PROGRESS, period = MerinoMatchPeriod.EXTRA_TIME, clock = "105")
                )
                .matchStatus

        assertEquals(MatchStatus.Live(clock = "105", isExtraTime = true, isHalftime = false), status)
    }

    @Test
    fun `GIVEN a match on a break WHEN mapped THEN the status is Live at half time`() {
        val status =
            mapFirstMatch(match(status = MerinoMatchStatus.BREAK, period = MerinoMatchPeriod.REGULAR, clock = "45"))
                .matchStatus

        assertEquals(MatchStatus.Live(clock = "45", isExtraTime = false, isHalftime = true), status)
    }

    @Test
    fun `GIVEN a match on a break in extra time WHEN mapped THEN the status is Live at half time in extra time`() {
        val status =
            mapFirstMatch(match(status = MerinoMatchStatus.BREAK, period = MerinoMatchPeriod.EXTRA_TIME, clock = "105"))
                .matchStatus

        assertEquals(MatchStatus.Live(clock = "105", isExtraTime = true, isHalftime = true), status)
    }

    @Test
    fun `GIVEN a penalty shootout in progress WHEN mapped THEN the status is Penalties with the shootout score`() {
        val status =
            mapFirstMatch(
                    match(
                        status = MerinoMatchStatus.IN_PROGRESS,
                        period = MerinoMatchPeriod.PENALTY_SHOOTOUT,
                        homePenaltyScore = 3,
                        awayPenaltyScore = 2,
                    )
                )
                .matchStatus

        assertEquals(MatchStatus.Penalties(homePenalty = 3, awayPenalty = 2), status)
    }

    @Test
    fun `GIVEN a match in progress with penalty scores and no period WHEN mapped THEN the status is Penalties`() {
        val status =
            mapFirstMatch(match(status = MerinoMatchStatus.IN_PROGRESS, homePenaltyScore = 3, awayPenaltyScore = 2))
                .matchStatus

        assertEquals(MatchStatus.Penalties(homePenalty = 3, awayPenalty = 2), status)
    }

    @Test
    fun `GIVEN a finished match WHEN mapped THEN the status is Final`() {
        assertEquals(
            MatchStatus.Final,
            mapFirstMatch(match(status = MerinoMatchStatus.FINAL, period = MerinoMatchPeriod.EXTRA_TIME)).matchStatus,
        )
    }

    @Test
    fun `GIVEN a match finished after a penalty shootout WHEN mapped THEN the status is FinalAfterPenalties`() {
        val status =
            mapFirstMatch(
                    match(
                        status = MerinoMatchStatus.FINAL,
                        period = MerinoMatchPeriod.PENALTY_SHOOTOUT,
                        homePenaltyScore = 5,
                        awayPenaltyScore = 4,
                    )
                )
                .matchStatus

        assertEquals(MatchStatus.FinalAfterPenalties(homePenalty = 5, awayPenalty = 4), status)
    }

    @Test
    fun `GIVEN a finished match with penalty scores and no period WHEN mapped THEN the status is FinalAfterPenalties`() {
        val status =
            mapFirstMatch(match(status = MerinoMatchStatus.FINAL, homePenaltyScore = 5, awayPenaltyScore = 4))
                .matchStatus

        assertEquals(MatchStatus.FinalAfterPenalties(homePenalty = 5, awayPenalty = 4), status)
    }

    @Test
    fun `GIVEN delayed, suspended and postponed matches WHEN mapped THEN their statuses are kept`() {
        assertEquals(MatchStatus.Delayed, mapFirstMatch(match(status = MerinoMatchStatus.DELAYED)).matchStatus)
        assertEquals(MatchStatus.Suspended, mapFirstMatch(match(status = MerinoMatchStatus.SUSPENDED)).matchStatus)
        assertEquals(MatchStatus.Postponed, mapFirstMatch(match(status = MerinoMatchStatus.POSTPONED)).matchStatus)
    }

    @Test
    fun `GIVEN a canceled match WHEN mapped THEN the status is Canceled`() {
        assertEquals(MatchStatus.Canceled, mapFirstMatch(match(status = MerinoMatchStatus.CANCELED)).matchStatus)
    }

    @Test
    fun `GIVEN an updated time WHEN mapped THEN updatedAt is set`() {
        val match = mapFirstMatch(match(updatedAt = "2026-10-10T18:45:00Z"))

        assertEquals(Instant.ofEpochSecond(1791657900), match.updatedAt)
    }

    @Test
    fun `GIVEN an invalid updated time WHEN mapped THEN updatedAt is null`() {
        assertNull(mapFirstMatch(match(updatedAt = "not a date")).updatedAt)
    }

    private fun MerinoMatches.mapInNewYorkCards(is24HourFormat: Boolean = false) =
        toMatchCards(zoneId = ZoneId.of("America/New_York"), locale = Locale.US, is24HourFormat = is24HourFormat)

    private fun mapFirstMatch(match: MerinoMatch) =
        matches(current = listOf(match)).mapInNewYorkCards().single().matches.single()

    private fun matches(
        previous: List<MerinoMatch> = emptyList(),
        current: List<MerinoMatch> = emptyList(),
        upcoming: List<MerinoMatch> = emptyList(),
    ) = MerinoMatches(previous = previous, current = current, upcoming = upcoming)

    private fun match(
        id: Long = 1L,
        competition: MerinoCompetition = MerinoCompetition(id = 34L, name = "Test League"),
        kickoff: String = "2026-10-10T18:00:00Z",
        homeTeam: MerinoTeam = MerinoTeam(id = 1L, name = "Team A", logoUrl = "https://example.com/a.png"),
        awayTeam: MerinoTeam = MerinoTeam(id = 2L, name = "Team B", logoUrl = "https://example.com/b.png"),
        period: MerinoMatchPeriod? = null,
        status: MerinoMatchStatus = MerinoMatchStatus.SCHEDULED,
        clock: String? = null,
        homeScore: Int? = null,
        awayScore: Int? = null,
        homePenaltyScore: Int? = null,
        awayPenaltyScore: Int? = null,
        updatedAt: String = "2026-10-10T18:45:00Z",
    ) =
        MerinoMatch(
            id = id,
            competition = competition,
            kickoff = kickoff,
            homeTeam = homeTeam,
            awayTeam = awayTeam,
            period = period,
            status = status,
            clock = clock,
            homeScore = homeScore,
            awayScore = awayScore,
            homePenaltyScore = homePenaltyScore,
            awayPenaltyScore = awayPenaltyScore,
            updatedAt = updatedAt,
        )
}
