/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.experiments.nimbus.util

import android.content.Context
import org.mozilla.experiments.nimbus.AbstractNimbusBuilder
import org.mozilla.experiments.nimbus.Nimbus
import org.mozilla.experiments.nimbus.NimbusAppInfo
import org.mozilla.experiments.nimbus.NimbusDelegate
import org.mozilla.experiments.nimbus.NimbusDeviceInfo
import org.mozilla.experiments.nimbus.NimbusInterface
import org.mozilla.experiments.nimbus.internal.NimbusServerSettings
import org.mozilla.experiments.nimbus.uninitialized

class TestNimbusBuilder(context: Context) : AbstractNimbusBuilder<NimbusInterface>(context) {
    override fun newNimbus(
        appInfo: NimbusAppInfo,
        serverSettings: NimbusServerSettings?,
    ): NimbusInterface =
        Nimbus(
            context = context,
            prefs = sharedPreferences,
            appInfo = appInfo,
            coenrollingFeatureIds = listOf(),
            server = serverSettings,
            deviceInfo = NimbusDeviceInfo("en-US"),
            observer = null,
            delegate = NimbusDelegate.default(),
        )

    override fun newNimbusDisabled(): NimbusInterface =
        uninitialized()
}
