/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import android.content.Context
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.feature.fxsuggest.FxSuggestIngestionScheduler
import mozilla.components.feature.fxsuggest.FxSuggestStorage
import mozilla.components.support.remotesettings.RemoteSettingsService

/**
 * Component group for Firefox Suggest.
 *
 * @param context The Android application context.
 * @param remoteSettingsService: Remote settings service to get suggestions from
 * @param crashReporter The crash reporter to use for reporting caught exceptions.
 */
class FxSuggest(
    context: Context,
    remoteSettingsService: RemoteSettingsService,
    crashReporter: CrashReporting,
) {
    val storage by lazy {
        FxSuggestStorage(
            context,
            remoteSettingsService,
            crashReporter,
        )
    }

    val ingestionScheduler by lazy {
        FxSuggestIngestionScheduler(context)
    }
}
