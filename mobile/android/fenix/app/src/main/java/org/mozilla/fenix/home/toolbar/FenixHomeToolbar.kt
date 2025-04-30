/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.toolbar

import android.view.View
import mozilla.components.browser.state.state.BrowserState

/**
 * Home screen toolbar.
 */
interface FenixHomeToolbar {
    /**
     * The root view of the toolbar.
     */
    val layout: View

    /**
     * Setups the home screen toolbar.
     *
     * @param browserState [BrowserState] is used to update button visibility.
     */
    fun build(browserState: BrowserState)

    /**
     * Configure the toolbar top/bottom divider
     *
     * @param isVisible Whether the divider should be visible or not.
     */
    fun updateDividerVisibility(isVisible: Boolean)

    /**
     * Updates the visibility of the tab counter and menu buttons.
     *
     * @param browserState [BrowserState] is used to update tab counter's state.
     * @param shouldAddNavigationBar [Boolean] is used to update menu button's and tab counter's state.
     */
    fun updateButtonVisibility(browserState: BrowserState, shouldAddNavigationBar: Boolean)

    /**
     * Updates the visibility of the address bar.
     *
     * @param isVisible Whether the address bar should be visible or not.
     */
    fun updateAddressBarVisibility(isVisible: Boolean)

    /**
     * Updates the tab counter view based on the current browser state.
     *
     * @param browserState [BrowserState] is passed down to tab counter view to calculate the view state.
     */
    fun updateTabCounter(browserState: BrowserState)
}
