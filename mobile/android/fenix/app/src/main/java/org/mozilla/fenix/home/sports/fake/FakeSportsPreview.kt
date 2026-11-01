/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports.fake

import mozilla.components.ui.icons.R as iconsR
import org.mozilla.fenix.home.sports.Match
import org.mozilla.fenix.home.sports.MatchCard
import org.mozilla.fenix.home.sports.MatchStatus
import org.mozilla.fenix.home.sports.Team

/** Fake data used for the Homepage Sports Widget Compose previews and debug tool. */
internal object FakeSportsPreview {

    private val PLACEHOLDER_DRAWABLE = iconsR.drawable.mozac_ic_soccer_ball_24

    val usa = Team(key = "USA", drawableResId = PLACEHOLDER_DRAWABLE)
    val par = Team(key = "PAR", drawableResId = PLACEHOLDER_DRAWABLE)

    /** Builds a fake [Match]. */
    fun match(
        home: Team? = usa,
        away: Team? = par,
        date: String = "Jun 28",
        time: String = "2:00 PM",
        homeScore: Int? = null,
        awayScore: Int? = null,
        matchStatus: MatchStatus = MatchStatus.Scheduled,
    ): Match =
        Match(
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
                                matchStatus = MatchStatus.Live(period = "1", clock = "29"),
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
                                matchStatus = MatchStatus.Live(period = "Extra", clock = "100"),
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
                                matchStatus = MatchStatus.Live(period = "", clock = null),
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
                                matchStatus = MatchStatus.Live(period = "", clock = "45", isHalftime = true),
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
                                matchStatus = MatchStatus.Live(period = "", clock = null, isHalftime = true),
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
                    matches = listOf(FakeSportsPreview.match(matchStatus = MatchStatus.Scheduled)),
                    label = "ABC Championship",
                ),
                MatchCard(
                    matches = listOf(FakeSportsPreview.match(matchStatus = MatchStatus.Scheduled)),
                    label = "ABC Championship",
                ),
                MatchCard(
                    matches = listOf(FakeSportsPreview.match(matchStatus = MatchStatus.Scheduled)),
                    label = "ABC Championship",
                ),
            )
    },
    TBD("Null teams") {
        override fun build() =
            listOf(
                MatchCard(
                    matches = listOf(FakeSportsPreview.match(away = null, matchStatus = MatchStatus.Scheduled)),
                    label = "Moz Cup",
                ),
                MatchCard(
                    matches =
                        listOf(FakeSportsPreview.match(home = null, away = null, matchStatus = MatchStatus.Scheduled)),
                    label = "Moz Cup",
                ),
            )
    };

    abstract fun build(): List<MatchCard>
}
