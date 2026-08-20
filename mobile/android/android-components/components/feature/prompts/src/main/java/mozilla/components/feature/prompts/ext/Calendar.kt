/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:Suppress("TooManyFunctions")

package mozilla.components.feature.prompts.ext

import java.time.ZoneId
import java.time.ZoneOffset
import java.util.Calendar
import java.util.Date

internal fun Date.toCalendar() = Calendar.getInstance().also { it.time = this }

/**
 * Returns midnight UTC on the day this [Date] falls on in [zone], the device timezone by default. The time of day is
 * dropped, not shifted by the timezone offset.
 */
internal fun Date.toLocalDayStartAsUtcMillis(zone: ZoneId = ZoneId.systemDefault()): Long =
    toInstant().atZone(zone).toLocalDate().atStartOfDay().toInstant(ZoneOffset.UTC).toEpochMilli()

internal var Calendar.millisecond: Int
    get() = get(Calendar.MILLISECOND)
    set(value) {
        set(Calendar.MILLISECOND, value)
    }
internal var Calendar.second: Int
    get() = get(Calendar.SECOND)
    set(value) {
        set(Calendar.SECOND, value)
    }
internal var Calendar.minute: Int
    get() = get(Calendar.MINUTE)
    set(value) {
        set(Calendar.MINUTE, value)
    }
internal var Calendar.hour: Int
    get() = get(Calendar.HOUR_OF_DAY)
    set(value) {
        set(Calendar.HOUR_OF_DAY, value)
    }
internal var Calendar.day: Int
    get() = get(Calendar.DAY_OF_MONTH)
    set(value) {
        set(Calendar.DAY_OF_MONTH, value)
    }
internal var Calendar.year: Int
    get() = get(Calendar.YEAR)
    set(value) {
        set(Calendar.YEAR, value)
    }

internal var Calendar.month: Int
    get() = get(Calendar.MONTH)
    set(value) {
        set(Calendar.MONTH, value)
    }

internal fun Calendar.minMillisecond(): Int = getActualMinimum(Calendar.MILLISECOND)

internal fun Calendar.maxMillisecond(): Int = getActualMaximum(Calendar.MILLISECOND)

internal fun Calendar.minSecond(): Int = getActualMinimum(Calendar.SECOND)

internal fun Calendar.maxSecond(): Int = getActualMaximum(Calendar.SECOND)

internal fun Calendar.minMinute(): Int = getActualMinimum(Calendar.MINUTE)

internal fun Calendar.maxMinute(): Int = getActualMaximum(Calendar.MINUTE)

internal fun Calendar.minHour(): Int = getActualMinimum(Calendar.HOUR_OF_DAY)

internal fun Calendar.maxHour(): Int = getActualMaximum(Calendar.HOUR_OF_DAY)

internal fun Calendar.minMonth(): Int = getMinimum(Calendar.MONTH)

internal fun Calendar.maxMonth(): Int = getActualMaximum(Calendar.MONTH)

internal fun Calendar.minDay(): Int = getMinimum(Calendar.DAY_OF_MONTH)

internal fun Calendar.maxDay(): Int = getActualMaximum(Calendar.DAY_OF_MONTH)

internal fun Calendar.minYear(): Int = getMinimum(Calendar.YEAR)

internal fun Calendar.maxYear(): Int = getActualMaximum(Calendar.YEAR)

internal fun now() = Calendar.getInstance()
