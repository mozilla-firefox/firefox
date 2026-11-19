/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.fake

import org.mozilla.fenix.home.sports.Match
import org.mozilla.fenix.home.sports.MatchCard
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.home.sports.Team

/** Fake data used for the Homepage Sports Widget Compose previews and debug tool. */
internal object FakeSportsPreview {

    val usa = Team(name = "USA", id = 1L, iconUrl = "https://example.com/usa.png")
    val par = Team(name = "PAR", id = 2L, iconUrl = "https://example.com/par.png")

    /** Builds a fake [Match]. */
    fun match(
        globalEventId: Long = 1L,
        home: Team = usa,
        away: Team = par,
        date: String = "Jun 28",
        time: String = "2:00 PM",
        homeScore: Int? = null,
        awayScore: Int? = null,
        matchStatus: MatchStatus = MatchStatus.Scheduled,
    ): Match =
        Match(
            globalEventId = globalEventId,
            date = date,
            time = time,
            home = home,
            away = away,
            homeScore = homeScore,
            awayScore = awayScore,
            matchStatus = matchStatus,
        )
}

/** Catalog of [MatchCard] fake scenarios. */
internal enum class FakeMatchCardScenario(val label: String) {
    Live("Live") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                homeScore = 1,
                                awayScore = 2,
                                matchStatus = MatchStatus.Live(clock = "29"),
                            )
                        ),
                    label = "ABC Championship",
                )
            )
    },
    Extra("Extra") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                homeScore = 1,
                                awayScore = 2,
                                matchStatus = MatchStatus.Live(clock = "100", isExtraTime = true),
                            )
                        ),
                    label = "ABC Championship",
                )
            )
    },
    EmptyClock("Empty Clock") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                homeScore = 1,
                                awayScore = 2,
                                matchStatus = MatchStatus.Live(clock = null),
                            )
                        ),
                    label = "ABC Championship",
                )
            )
    },
    HalfTimeClock("Half time with clock") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                homeScore = 1,
                                awayScore = 2,
                                matchStatus = MatchStatus.Live(clock = "45", isHalftime = true),
                            )
                        ),
                    label = "ABC Championship",
                )
            )
    },
    HalfTimeNullClock("Half time with null clock") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                homeScore = 1,
                                awayScore = 2,
                                matchStatus = MatchStatus.Live(clock = null, isHalftime = true),
                            )
                        ),
                    label = "ABC Championship",
                )
            )
    },
    Scheduled("Scheduled") {
        override fun build() =
            listOf(
                MatchCard(
                    matches = listOf(FakeSportsPreview.match(matchStatus = MatchStatus.Scheduled)),
                    label = "ABC Championship",
                )
            )
    },
    Penalties("Penalties") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                date = "Jun 15",
                                time = "8:00 PM",
                                homeScore = 3,
                                awayScore = 3,
                                matchStatus = MatchStatus.Penalties(homePenalty = 5, awayPenalty = 4),
                            )
                        ),
                    label = "Moz Cup",
                )
            )
    },
    Final("Final") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                date = "Jun 19",
                                time = "8:00 PM",
                                homeScore = 2,
                                awayScore = 1,
                                matchStatus = MatchStatus.Final,
                            )
                        ),
                    label = "Moz Cup",
                )
            )
    },
    FinalAfterPenalties("Final after penalties") {
        override fun build() =
            listOf(
                MatchCard(
                    matches =
                        listOf(
                            FakeSportsPreview.match(
                                date = "Jun 19",
                                time = "8:00 PM",
                                homeScore = 3,
                                awayScore = 3,
                                matchStatus = MatchStatus.FinalAfterPenalties(homePenalty = 5, awayPenalty = 4),
                            )
                        ),
                    label = "FA Cup",
                )
            )
    },
    FollowAllSchedule("Follow all schedule") {
        override fun build() =
            listOf(
                MatchCard(
                    matches = listOf(FakeSportsPreview.match(globalEventId = 1L, matchStatus = MatchStatus.Scheduled)),
                    label = "ABC Championship",
                ),
                MatchCard(
                    matches = listOf(FakeSportsPreview.match(globalEventId = 2L, matchStatus = MatchStatus.Scheduled)),
                    label = "ABC Championship",
                ),
                MatchCard(
                    matches = listOf(FakeSportsPreview.match(globalEventId = 3L, matchStatus = MatchStatus.Scheduled)),
                    label = "ABC Championship",
                ),
            )
    };

    abstract fun build(): List<MatchCard>
}
