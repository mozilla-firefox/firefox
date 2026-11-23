/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import android.text.format.DateFormat
import java.time.Instant
import java.time.ZoneId
import java.time.ZonedDateTime
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException
import java.util.Locale
import mozilla.components.service.merino.sports.MerinoMatch
import mozilla.components.service.merino.sports.MerinoMatchPeriod
import mozilla.components.service.merino.sports.MerinoMatchStatus
import mozilla.components.service.merino.sports.MerinoMatches
import mozilla.components.service.merino.sports.MerinoTeam

private const val DATE_SKELETON = "MMMd"
private const val TIME_SKELETON_12_HOUR = "hm"
private const val TIME_SKELETON_24_HOUR = "Hm"

/**
 * Maps the [MerinoMatches] to the [MatchCard]s shown in the sports widget.
 *
 * The group of [MerinoMatches.previous] matches are returned in the opposite order compared to [MerinoMatches.current]
 * and [MerinoMatches.upcoming] with newest appear first. This group needs to be reversed in order to display the
 * matches in chronological order.
 *
 * @param zoneId The [ZoneId] used to display the match date and time.
 * @param locale The [Locale] used to format the match date and time.
 * @param is24HourFormat Whether the match time is displayed with the 24-hour clock, following the device setting.
 */
internal fun MerinoMatches.toMatchCards(
    zoneId: ZoneId = ZoneId.systemDefault(),
    locale: Locale = Locale.getDefault(),
    is24HourFormat: Boolean,
): List<MatchCard> =
    (previous.reversed() + current + upcoming).map { match ->
        MatchCard(matches = listOf(match.toMatch(zoneId, locale, is24HourFormat)), label = match.competition.name)
    }

private fun MerinoMatch.toMatch(zoneId: ZoneId, locale: Locale, is24HourFormat: Boolean): Match {
    val matchStartTime = parseInstant(kickoff)?.atZone(zoneId)
    return Match(
        globalEventId = id,
        date = matchStartTime?.formatDate(locale).orEmpty(),
        time = matchStartTime?.formatTime(locale, is24HourFormat).orEmpty(),
        home = homeTeam.toTeam(),
        away = awayTeam.toTeam(),
        matchStatus = toMatchStatus(),
        homeScore = homeScore,
        awayScore = awayScore,
        homePenalty = homePenaltyScore,
        awayPenalty = awayPenaltyScore,
        clock = clock,
        updatedAt = parseInstant(updatedAt),
    )
}

/**
 * Formats this [ZonedDateTime] as an abbreviated month and day of the month in the order the [locale] prefers, e.g.
 * "Oct 10" in English or "10 oct." in French.
 *
 * @param locale The [Locale] used to format the date.
 */
private fun ZonedDateTime.formatDate(locale: Locale): String =
    format(DateTimeFormatter.ofPattern(DateFormat.getBestDateTimePattern(locale, DATE_SKELETON), locale))

/**
 * Formats this [ZonedDateTime] as the hour and minutes with the 12-hour or 24-hour clock in the form the [locale]
 * prefers, e.g. "2:00 PM" in English or "14:00" in French.
 *
 * @param locale The [Locale] used to format the time.
 * @param is24HourFormat Whether the 24-hour clock is used.
 */
private fun ZonedDateTime.formatTime(locale: Locale, is24HourFormat: Boolean): String {
    val skeleton = if (is24HourFormat) TIME_SKELETON_24_HOUR else TIME_SKELETON_12_HOUR
    return format(DateTimeFormatter.ofPattern(DateFormat.getBestDateTimePattern(locale, skeleton), locale))
}

private fun MerinoTeam.toTeam() =
    Team(
        name = name,
        id = id,
        iconUrl = logoUrl,
    )

private fun MerinoMatch.toMatchStatus(): MatchStatus {
    val isPenaltyShootout =
        period == MerinoMatchPeriod.PENALTY_SHOOTOUT || homePenaltyScore != null || awayPenaltyScore != null
    val isExtraTime = period == MerinoMatchPeriod.EXTRA_TIME
    return when (status) {
        MerinoMatchStatus.SCHEDULED -> MatchStatus.Scheduled
        MerinoMatchStatus.IN_PROGRESS ->
            if (isPenaltyShootout) {
                MatchStatus.Penalties(homePenalty = homePenaltyScore, awayPenalty = awayPenaltyScore)
            } else {
                MatchStatus.Live(clock = clock, isExtraTime = isExtraTime)
            }
        MerinoMatchStatus.BREAK -> MatchStatus.Live(clock = clock, isExtraTime = isExtraTime, isHalftime = true)
        MerinoMatchStatus.FINAL ->
            if (isPenaltyShootout) {
                MatchStatus.FinalAfterPenalties(homePenalty = homePenaltyScore, awayPenalty = awayPenaltyScore)
            } else {
                MatchStatus.Final
            }
        MerinoMatchStatus.DELAYED -> MatchStatus.Delayed
        MerinoMatchStatus.SUSPENDED -> MatchStatus.Suspended
        MerinoMatchStatus.POSTPONED -> MatchStatus.Postponed
        MerinoMatchStatus.CANCELED -> MatchStatus.Canceled
    }
}

private fun parseInstant(value: String): Instant? =
    try {
        Instant.parse(value)
    } catch (_: DateTimeParseException) {
        null
    }
