/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.messaging

/** The identity of a message surface for Fenix */
object FenixMessageSurfaceId {
    /** A local notification in the background, like a push notification. */
    const val NOTIFICATION = "notification"

    /** A banner in the homescreen. */
    const val HOMESCREEN = "homescreen"

    /** A survey dialog that is intended to be disruptive. */
    const val SURVEY = "survey"

    /** A microsurvey UI for a specific feature. */
    const val MICROSURVEY = "microsurvey"

    /** An informational card in VPN settings. */
    const val SETTINGS_VPN = "settings_vpn"
}
