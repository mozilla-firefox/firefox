/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.service.merino.sports.MerinoMatchesStorage
import mozilla.components.service.merino.sports.MerinoSportsProvider
import mozilla.components.service.merino.sports.dataStore

/**
 * Provides access to sports related components.
 *
 * @param context The application [Context].
 * @param crashReporter [CrashReporting] used for recording caught exceptions.
 */
class Sports(
    private val context: Context,
    private val crashReporter: CrashReporting,
) {

    val merinoSportsProvider by lazy { MerinoSportsProvider(crashReporter = crashReporter) }

    val merinoMatchesStorage by lazy { MerinoMatchesStorage.dataStore(context) }
}
