/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.appservices.remotesettings

import android.util.Log
import mozilla.appservices.remotesettings.RemoteSettingsTelemetry
import mozilla.appservices.remotesettings.UptakeEventExtras
import org.mozilla.appservices.remotesettings.GleanMetrics.RemoteSettings as RSMetrics

/**
 * GleanTelemetry is a thin wrapper used to expose
 * callbacks used to emit telemetry events to Glean.
 */
class GleanTelemetry : RemoteSettingsTelemetry {
    override fun reportUptake(extras: UptakeEventExtras) {
        Log.d(
            GleanTelemetry::javaClass.name,
            "Remote Settings Telemetry Uptake called with: " +
                "value=${extras.value}, " +
                "source=${extras.source}, " +
                "age=${extras.age}, " +
                "trigger=${extras.trigger}, " +
                "timestamp=${extras.timestamp}, " +
                "duration=${extras.duration}, " +
                "errorName=${extras.errorName}",
        )
        RSMetrics.uptakeRemotesettings.record(
            RSMetrics.UptakeRemotesettingsExtra(
                value = extras.value,
                source = extras.source,
                age = extras.age,
                trigger = extras.trigger,
                timestamp = extras.timestamp,
                duration = extras.duration,
                errorname = extras.errorName,
            ),
        )
    }
}
