/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.home.sports

/** Interface for sports widget user interactions on the homepage. */
interface SportsWidgetInteractor {

    /** Called when the user selects the "Wallpapers" menu item of the sports widget. */
    fun onSportsWidgetWallpapersClicked()

    /** Called when the user removes the sports widget. */
    fun onSportsWidgetRemoveClicked()
}
