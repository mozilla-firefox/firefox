/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import android.content.Context
import androidx.annotation.VisibleForTesting
import androidx.work.WorkManager

@VisibleForTesting
internal const val SUPPORTED_ADDONS_CHECKER_WORK_NAME =
    "mozilla.components.feature.addons.migration.DefaultSupportedAddonsChecker.periodicWork"

/**
 * Cancels the periodic work scheduled by the removed `DefaultSupportedAddonsChecker`.
 *
 * The checker scheduled this work while an add-on was disabled as unsupported by the Fennec migration. WorkManager
 * keeps scheduled work across app updates, but `SupportedAddonsWorker` no longer exists, so each run would fail and be
 * rescheduled forever unless cancelled.
 *
 * @param context The context used to get the [WorkManager] instance.
 */
internal fun cancelSupportedAddonsChecks(context: Context) {
    WorkManager.getInstance(context).cancelUniqueWork(SUPPORTED_ADDONS_CHECKER_WORK_NAME)
}
