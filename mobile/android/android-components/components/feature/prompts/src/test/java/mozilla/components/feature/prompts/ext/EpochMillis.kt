/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.prompts.ext

import java.time.LocalDate
import java.time.ZoneId
import java.time.ZoneOffset

/** Returns the epoch millis of the given wall clock time in [zone], midnight UTC by default. */
internal fun epochMillisAt(
    year: Int,
    month: Int,
    day: Int,
    hour: Int = 0,
    minute: Int = 0,
    zone: ZoneId = ZoneOffset.UTC,
) = LocalDate.of(year, month, day).atTime(hour, minute).atZone(zone).toInstant().toEpochMilli()
