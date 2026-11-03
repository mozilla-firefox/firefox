/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ext

import mozilla.components.feature.top.sites.TopSite
import org.mozilla.fenix.settings.SupportUtils

/** Returns a sorted list of [TopSite] with the default Google top site always appearing as the first item. */
fun List<TopSite>.sort(): List<TopSite> {
    val defaultGoogleTopSiteIndex =
        this.indexOfFirst {
            it is TopSite.DefaultPinned && it.url == SupportUtils.GOOGLE_URL
        }

    return if (defaultGoogleTopSiteIndex == -1) {
        this
    } else {
        val result = this.toMutableList()
        result.removeAt(defaultGoogleTopSiteIndex)
        result.add(0, this[defaultGoogleTopSiteIndex])
        result
    }
}
