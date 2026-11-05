/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.tabgroups.strip

import androidx.compose.foundation.lazy.LazyListState
import androidx.lifecycle.ViewModel

/** [ViewModel] that retains the tab groups strip's scroll position between various fragments. */
class TabGroupsStripScrollViewModel : ViewModel() {
    /** The strip's shared [LazyListState]. */
    val listState = LazyListState()
}
