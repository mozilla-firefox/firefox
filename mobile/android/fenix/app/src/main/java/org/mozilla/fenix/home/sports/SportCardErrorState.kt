/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

import org.mozilla.fenix.R

/** The error states surfaced in the sports widget. */
enum class SportCardErrorState {
    /** Match data failed to load. */
    LoadFailed,

    /** Network connection dropped and live updates are paused. */
    ConnectionInterrupted,

    /** The latest match results failed to load. */
    ResultsLoadFailed,
}

internal val SportCardErrorState.titleResId: Int
    get() =
        when (this) {
            SportCardErrorState.LoadFailed -> R.string.wrex_widget_error_load_failed_title
            SportCardErrorState.ConnectionInterrupted -> R.string.wrex_widget_error_connection_offline_title
            SportCardErrorState.ResultsLoadFailed -> R.string.wrex_widget_error_results_load_failed_title
        }

internal val SportCardErrorState.messageResId: Int
    get() =
        when (this) {
            SportCardErrorState.LoadFailed -> R.string.wrex_widget_error_try_refreshing
            SportCardErrorState.ConnectionInterrupted -> R.string.wrex_widget_error_connection_offline_description
            SportCardErrorState.ResultsLoadFailed -> R.string.wrex_widget_error_try_refreshing
        }
