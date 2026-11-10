/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import android.content.pm.PackageManager
import mozilla.components.support.base.log.logger.Logger
import mozilla.components.support.utils.ext.PackageManagerCompatHelper

/** Returns the installation time in milliseconds since epoch, or `null` if unavailable. */
fun getApplicationInstalledTime(
    packageManagerCompatHelper: PackageManagerCompatHelper,
    packageName: String,
    logger: Logger,
): Long? =
    try {
        packageManagerCompatHelper.getPackageInfoCompat(packageName, 0).firstInstallTime
    } catch (e: PackageManager.NameNotFoundException) {
        logger.warn("Unable to retrieve package info for $packageName", e)
        null
    } catch (e: UnsupportedOperationException) {
        logger.warn("Unable to retrieve package info for $packageName", e)
        null
    }
