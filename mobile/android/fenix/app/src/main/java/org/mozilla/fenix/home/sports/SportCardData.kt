/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import androidx.annotation.DrawableRes

/**
 * A participating team in the sports tournament.
 *
 * @property key Abbreviation named for a given name (e.g. "ENG").
 * @property drawableResId Local drawable representing the team.
 */
data class Team(
    val key: String,
    @param:DrawableRes val drawableResId: Int,
)

/** Game status of a match. */
sealed class MatchStatus {
    /** Match has not started yet. */
    data object Scheduled : MatchStatus()

    /**
     * Match is currently in progress.
     *
     * @property period Period description string ("1", "2", "Extra", etc.)
     * @property clock Minutes of elapsed play time, with extra time denoted as a "+" (e.g. "42", "90+3" (indicating 3
     *   minutes extra time)). Null when the feed omits it.
     * @property isHalftime True when play is paused for halftime (feed detail status "Break").
     */
    data class Live(val period: String, val clock: String?, val isHalftime: Boolean = false) : MatchStatus()

    /**
     * Match is in a penalty shootout.
     *
     * @property homePenalty Home team penalty score.
     * @property awayPenalty Away team penalty score.
     */
    data class Penalties(val homePenalty: Int? = null, val awayPenalty: Int? = null) : MatchStatus()

    /** Match has ended. */
    data object Final : MatchStatus()

    /**
     * Match has ended with penalities.
     *
     * @property homePenalty Home team penalty score.
     * @property awayPenalty Away team penalty score.
     */
    data class FinalAfterPenalties(val homePenalty: Int? = null, val awayPenalty: Int? = null) : MatchStatus()

    /** Match start has been delayed. */
    data object Delayed : MatchStatus()

    /** Match has been suspended after it started. */
    data object Suspended : MatchStatus()

    /** Match has been postponed to a later date. */
    data object Postponed : MatchStatus()

    /** API returned an unrecognized status string. */
    data object Unknown : MatchStatus()
}

/**
 * Information related to a given sport event (game/match).
 *
 * @property globalEventId Stable upstream identifier; the natural cache key.
 * @property date Date string for start of match e.g. Jun 13.
 * @property time Time string for start of match e.g. 5:00 PM.
 * @property home Home [Team]. Null if the match has not been scheduled.
 * @property away Away [Team]. Null if the match has not been scheduled.
 * @property matchStatus Current [MatchStatus].
 * @property homeScore Home team score. Null if the match has not started.
 * @property awayScore Away team score. Null if the match has not started.
 * @property homePenalty Home penalty shootout score. Null if no shootout occurred.
 * @property awayPenalty Away penalty shootout score. Null if no shootout occurred.
 * @property clock Minutes of elapsed play time, with extra time denoted as a "+". (e.g. "42", "90+3" (indicating 3
 *   minutes extra time))
 * @property period Period description string ("1", "2", "Extra", etc.)
 * @property updated UTC timestamp when this event record was last updated.
 */
data class Match(
    val globalEventId: Long = 0L,
    val date: String,
    val time: String,
    val home: Team?,
    val away: Team?,
    val matchStatus: MatchStatus = MatchStatus.Scheduled,
    val homeScore: Int? = null,
    val awayScore: Int? = null,
    val homePenalty: Int? = null,
    val awayPenalty: Int? = null,
    val clock: String? = null,
    val period: String? = null,
    val updated: Int? = null,
)

/**
 * UI state for a match card.
 *
 * @property matches The underlying data for each match.
 * @property label Name of the competition this match belongs to (e.g. "FA Cup").
 */
data class MatchCard(
    val matches: List<Match>,
    val label: String,
)
