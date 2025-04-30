/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.utils.ext

import android.os.Build

/**
 * Returns the ID of the current thread.
 *
 * @return The ID of the current thread as a [Long].
 */
fun Thread.threadIdCompat() =
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.BAKLAVA) {
        threadId()
    } else {
        @Suppress("DEPRECATION")
        id
    }
