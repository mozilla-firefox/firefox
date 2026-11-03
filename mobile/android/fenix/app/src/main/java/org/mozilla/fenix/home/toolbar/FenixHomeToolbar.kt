/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

/** Home screen toolbar. */
interface FenixHomeToolbar {
    /** Renders the toolbar content. */
    @Composable fun Content()

    /**
     * Returns the background color of the search screen while search is active, or null otherwise. Must be called from
     * within the same theme as [Content].
     */
    @Composable fun searchBackgroundColor(): Color? = null

    /**
     * Setups the home screen toolbar.
     *
     * @param middleSearchEnabled Whether middle search is enabled, and the address bar should be invisible.
     */
    fun build(middleSearchEnabled: Boolean)

    /**
     * Updates the visibility of the address bar.
     *
     * @param isVisible Whether the address bar should be visible or not.
     */
    fun updateAddressBarVisibility(isVisible: Boolean)
}
