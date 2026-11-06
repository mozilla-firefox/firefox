/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.appstate.sports

import org.mozilla.fenix.home.sports.MatchCard
import org.mozilla.fenix.home.sports.SportCardErrorState

/**
 * State of the Wrexham widget on the homepage.
 *
 * @property isFeatureEnabled Whether the Homepage Wrexham Widget feature is enabled.
 * @property isVisible Whether the user has chosen to show the widget on the homepage.
 * @property matchCards The [MatchCard]s to render or empty when no match data is available.
 * @property errorState The [SportCardErrorState] if the last fetch failed, null otherwise.
 * @property isDebugToolVisible Whether the debug tool for adjusting [SportsWidgetState] is displayed on the homepage.
 */
data class SportsWidgetState(
    val isFeatureEnabled: Boolean = false,
    val isVisible: Boolean = false,
    val matchCards: List<MatchCard> = emptyList(),
    val errorState: SportCardErrorState? = null,
    val isDebugToolVisible: Boolean = false,
) {
    /**
     * Whether the widget should be rendered on the homepage: the feature is enabled, the user has not hidden the
     * widget, and there is match data or an error to display.
     */
    val isShown: Boolean
        get() = isFeatureEnabled && isVisible && (matchCards.isNotEmpty() || errorState != null)
}
