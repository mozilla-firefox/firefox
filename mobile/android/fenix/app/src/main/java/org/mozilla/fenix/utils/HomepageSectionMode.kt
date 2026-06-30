/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

/**
 * Selects how the homepage sections can be customized, chosen from a secret setting. Used to compare
 * the candidate approaches.
 */
enum class HomepageSectionMode {
    /** The existing homepage with a fixed section layout and the classic per-section toggles. */
    DISABLED,

    /** Approach 1: the homepage settings show a single list where sections are toggled and reordered. */
    REORDERABLE_LIST,
}
