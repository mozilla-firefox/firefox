/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.service.merino.sports

import kotlinx.serialization.SerialName
import kotlinx.serialization.Serializable

/**
 * Matches returned by the Merino sports matches API grouped by their state.
 *
 * @property previous Completed matches, newest first.
 * @property current Matches in progress, on a break, delayed or suspended.
 * @property upcoming Scheduled or postponed matches, earliest first.
 */
@Serializable
data class MerinoMatches(
    val previous: List<MerinoMatch> = emptyList(),
    val current: List<MerinoMatch> = emptyList(),
    val upcoming: List<MerinoMatch> = emptyList(),
)

/**
 * A single match returned by the Merino sports matches API.
 *
 * @property id The unique match identifier.
 * @property competition The [MerinoCompetition] the match is played in.
 * @property kickoff The latest scheduled start time of the match in UTC ISO-8601 format.
 * @property homeTeam The home [MerinoTeam].
 * @property awayTeam The away [MerinoTeam].
 * @property period The current period during play or the final period after completion. Null when the match is
 *   [MerinoMatchStatus.SCHEDULED].
 * @property status The [MerinoMatchStatus] of the match.
 * @property clock Display-ready minutes of elapsed play time, including stoppage time added at the end of a period. For
 *   example, "67", or "90+3" for the 3rd minute of stoppage time after the 90 minutes of normal time. Null when the
 *   match is not [MerinoMatchStatus.IN_PROGRESS] and during a [MerinoMatchStatus.BREAK].
 * @property homeScore Goals scored by the home team excluding penalty shootout goals. Null when the match is
 *   [MerinoMatchStatus.SCHEDULED].
 * @property awayScore Goals scored by the away team excluding penalty shootout goals. Null when the match is
 *   [MerinoMatchStatus.SCHEDULED].
 * @property homePenaltyScore Penalty shootout goals scored by the home team if applicable.
 * @property awayPenaltyScore Penalty shootout goals scored by the away team if applicable.
 * @property updatedAt The time the match data was last updated in UTC ISO-8601 format.
 */
@Serializable
data class MerinoMatch(
    val id: Long,
    val competition: MerinoCompetition,
    val kickoff: String,
    @SerialName("home_team") val homeTeam: MerinoTeam,
    @SerialName("away_team") val awayTeam: MerinoTeam,
    val period: MerinoMatchPeriod? = null,
    val status: MerinoMatchStatus,
    val clock: String? = null,
    @SerialName("home_score") val homeScore: Int? = null,
    @SerialName("away_score") val awayScore: Int? = null,
    @SerialName("home_penalty_score") val homePenaltyScore: Int? = null,
    @SerialName("away_penalty_score") val awayPenaltyScore: Int? = null,
    @SerialName("updated_at") val updatedAt: String,
)

/**
 * The competition a [MerinoMatch] is played in.
 *
 * @property id The competition identifier.
 * @property name The competition display name.
 */
@Serializable
data class MerinoCompetition(
    val id: Long,
    val name: String,
)

/**
 * A team playing in a [MerinoMatch].
 *
 * @property id The global team identifier.
 * @property name The team display name.
 * @property logoUrl The URL of the team logo if available.
 */
@Serializable
data class MerinoTeam(
    val id: Long,
    val name: String,
    @SerialName("logo_url") val logoUrl: String? = null,
)

/** Current period during play of a match. */
@Serializable
enum class MerinoMatchPeriod {
    /** Normal time, including stoppage time. */
    @SerialName("Regular") REGULAR,

    /** Extra time, including stoppage time. */
    @SerialName("ExtraTime") EXTRA_TIME,

    /** The penalty shootout. */
    @SerialName("PenaltyShootout") PENALTY_SHOOTOUT,
}

/** The status of a match. */
@Serializable
enum class MerinoMatchStatus {
    /** The match has not started. */
    @SerialName("Scheduled") SCHEDULED,

    /** The match is underway, including extra time and the penalty shootout. */
    @SerialName("InProgress") IN_PROGRESS,

    /** Half-time. */
    @SerialName("Break") BREAK,

    /** The match has finished. */
    @SerialName("Final") FINAL,

    /** The match was postponed to a later date. */
    @SerialName("Postponed") POSTPONED,

    /** The match was canceled. */
    @SerialName("Canceled") CANCELED,

    /** Play is temporarily interrupted. */
    @SerialName("Suspended") SUSPENDED,

    /** The match is delayed. */
    @SerialName("Delayed") DELAYED,
}
