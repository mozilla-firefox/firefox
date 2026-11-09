/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.helpers

import android.text.format.DateFormat
import java.time.LocalDate
import java.time.format.DateTimeFormatter
import java.time.format.DateTimeParseException
import java.util.Locale

/** Contains an ISO 8601 date. */
@JvmInline value class IsoPromoDeadline(val date: String)

/**
 * Formats an ISO 8601 date to a localized version for representing the promotion deadline date (e.g. 2026-08-31).
 *
 * @param onException any exception caught during parsing or formatting of the date.
 */
internal fun IsoPromoDeadline.formatPromoDateOrCatch(onException: (Exception) -> Unit): String? {
    return try {
        LocalDate.parse(date).formatPromoDate()
    } catch (e: DateTimeParseException) {
        onException(e)
        null
    } catch (e: IllegalArgumentException) {
        onException(e)
        null
    }
}

/** Formats a promotion deadline using the current locale. */
internal fun LocalDate.formatPromoDate(): String {
    val formatter = DateTimeFormatter.ofPattern(DateFormat.getBestDateTimePattern(Locale.getDefault(), "MMMMd"))
    return format(formatter)
}
