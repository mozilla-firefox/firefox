/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.biometric.ext

import android.app.KeyguardManager
import android.content.Context
import androidx.core.content.getSystemService

/** Checks if the user has a device lock pattern, PIN, or password set up. */
fun Context.isDeviceLockSetUp(): Boolean = getSystemService<KeyguardManager>()?.isKeyguardSecure == true
