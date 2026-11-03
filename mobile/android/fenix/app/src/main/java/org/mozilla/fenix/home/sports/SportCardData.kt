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
 * @property globalTeamId Unique numeric identifier for this team.
 * @property name Long display name (e.g. "England"). This is not localized.
 * @property iconUrl Optional URL for the team logo.
 * @property eliminated True once the team is out of the tournament.
 * @property standing The [TeamStanding] record in the tournament.
 */
data class Team(
    val key: String,
    @param:DrawableRes val drawableResId: Int,
    val globalTeamId: Long = 0L,
    val name: String = "",
    val iconUrl: String? = null,
    val eliminated: Boolean = false,
    val standing: TeamStanding = TeamStanding(),
)

/** The team's record within the tournament. */
data class TeamStanding(
    val wins: Int = 0,
    val losses: Int = 0,
    val draws: Int = 0,
    val points: Int = 0,
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

/** True when the match is currently being played (regulation or penalty shootout). */
internal fun MatchStatus.isLive(): Boolean = this is MatchStatus.Live || this is MatchStatus.Penalties

/** True when the match has finished (regulation or after a shootout). */
internal fun MatchStatus.isPast(): Boolean = this is MatchStatus.Final || this is MatchStatus.FinalAfterPenalties

/**
 * True when this period description denotes extra time — the feed uses both "ET" and "Extra" (case-insensitively), so
 * match either form.
 */
internal val String.isExtraTime: Boolean
    get() = contains("ET", ignoreCase = true) || contains("Extra", ignoreCase = true)

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

/** Represents the source of the Live Match Refresh button clicks. */
enum class LiveMatchRefreshSource(val value: String) {
    LIVE_MATCH_CARD_ERROR_BUTTON("live_match_card_error_button")
}

/**
 * Identifies which card variant is displayed in the sports widget. Used as a Glean extra to distinguish error and match
 * cards.
 */
enum class SportsCardType(val value: String) {
    ERROR_LOAD_FAILED("error_load_failed"),
    ERROR_CONNECTION_INTERRUPTED("error_connection_interrupted"),
    ERROR_RESULTS_LOAD_FAILED("error_results_load_failed"),
    MATCH("match");

    companion object {
        /** Maps an [error] state to its corresponding error-card [SportsCardType]. */
        fun fromError(error: SportCardErrorState): SportsCardType =
            when (error) {
                SportCardErrorState.LoadFailed -> ERROR_LOAD_FAILED
                SportCardErrorState.ConnectionInterrupted -> ERROR_CONNECTION_INTERRUPTED
                SportCardErrorState.ResultsLoadFailed -> ERROR_RESULTS_LOAD_FAILED
            }
    }
}

/** Distinguishes the first card seen on widget mount from cards reached by swiping the pager. */
enum class SportsCardImpressionSource(val value: String) {
    IMPRESSION("impression"),
    SWIPE("swipe"),
}
