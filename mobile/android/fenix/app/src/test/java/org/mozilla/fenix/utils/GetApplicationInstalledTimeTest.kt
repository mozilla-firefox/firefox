/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.utils

import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import mozilla.components.support.base.log.logger.Logger
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class GetApplicationInstalledTimeTest {

    @Test
    fun `WHEN getPackageInfoCompat returns a time THEN getApplicationInstalledTime returns the same time`() {
        val installTime = 12345L

        val result =
            getApplicationInstalledTime(
                packageManagerCompatHelper =
                    FakePackageManagerCompatHelper(
                        packageInfo =
                            PackageInfo().apply {
                                firstInstallTime = installTime
                            }
                    ),
                packageName = "UNUSED",
                logger = Logger(),
            )

        assertEquals(installTime, result)
    }

    @Test
    fun `WHEN getPackageInfoCompat throws NameNotFoundException THEN getApplicationInstalledTime returns null`() {
        val installTime = 12345L

        val result =
            getApplicationInstalledTime(
                packageManagerCompatHelper =
                    FakePackageManagerCompatHelper(
                        packageInfo =
                            PackageInfo().apply {
                                firstInstallTime = installTime
                            },
                        getPackageInfoThrowable = PackageManager.NameNotFoundException(),
                    ),
                packageName = "UNUSED",
                logger = Logger(),
            )

        assertNull(result)
    }

    @Test
    fun `WHEN getPackageInfoCompat throws UnsupportedOperationException THEN getApplicationInstalledTime returns null`() {
        val installTime = 12345L

        val result =
            getApplicationInstalledTime(
                packageManagerCompatHelper =
                    FakePackageManagerCompatHelper(
                        packageInfo =
                            PackageInfo().apply {
                                firstInstallTime = installTime
                            },
                        getPackageInfoThrowable = UnsupportedOperationException(),
                    ),
                packageName = "UNUSED",
                logger = Logger(),
            )

        assertNull(result)
    }
}
