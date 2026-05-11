/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

/**
 * The 'surface' that the IP Protection bottom sheet was displayed on,
 * e.g. on the homepage or within the browser.
 */
enum class Surface {
    HOMEPAGE,
    BROWSER,
    ;

    val metricLabel = name.lowercase()
}
