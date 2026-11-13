/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.termsofuse

/** The current version of the Terms of Use. */
const val TOU_VERSION = 5

/**
 * The timestamps of the Terms of Use published dates. The most recent one that has passed is considered the current
 * published date, so a future date can be added ahead of time. Dates older than the most recent date can be safely
 * removed.
 */
val TOU_PUBLISHED_TIMES_IN_MILLIS =
    listOf(
        1797292800000 // December 15th, 2026 00:00 UTC
    )
