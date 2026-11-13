/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.test.robolectric

import android.app.Application
import androidx.test.core.app.ApplicationProvider.getApplicationContext
import org.robolectric.Shadows.shadowOf

/**
 * A helper for working with permission just pass one or more permission that you need to be granted.
 *
 * @param permissions list of permissions that you need to be granted.
 */
fun grantPermission(vararg permissions: String) {
    shadowOf(getApplicationContext<Application>()).grantPermissions(*permissions)
}
