/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.compose.browser.toolbar

import androidx.annotation.StringRes

/**
 * Represents the state and behavior of a CFR shown in the browser toolbar.
 *
 * @property tag An identifying tag for the CFR. This can be used to handle side effects of interactions with the tag.
 * @property enabled Whether the CFR is currently active and should be displayed.
 * @property title The headline text displayed in the CFR banner.
 * @property description A short descriptive message explaining the feature or action.
 */
data class BrowserToolbarCFR(
    val tag: String,
    val enabled: Boolean,
    @get:StringRes val title: Int?,
    @get:StringRes val description: Int,
)
