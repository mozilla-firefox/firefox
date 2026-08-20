/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ipprotection.store.state

/** Represents recent status transitions for the proxy. */
enum class ProxyActivation {
    /** Default state. No recent status changes. */
    Idle,

    /** The proxy became active. Resets to [Idle] after handling. */
    TurningOn,

    /** The proxy became inactive. Resets to [Idle] after handling. */
    TurningOff,
}
