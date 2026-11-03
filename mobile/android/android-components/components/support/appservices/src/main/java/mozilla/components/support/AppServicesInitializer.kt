/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support

import mozilla.appservices.RustComponentsInitializer
import mozilla.components.concept.base.crash.CrashReporting
import mozilla.components.support.base.log.Log
import mozilla.components.support.rusterrors.initializeRustErrors
import mozilla.components.support.rustlog.RustLog

/** A namespaced object for initialization. */
object AppServicesInitializer {

    /**
     * Initialize the critical app services components.
     *
     * N.B: The internals need to be executed in this particular order. Changes can lead to unexpected runtime failures.
     *
     * @param config the configurations needed for initializing the optional parts. See [Config].
     */
    fun init(config: Config) {
        // Rust components must be initialized at the very beginning, before any other Rust call, ...
        RustComponentsInitializer.init()

        if (config.crashReporting != null) {
            initializeRustErrors(config.crashReporting)
        }

        // ... but RustHttpConfig.setClient() and RustLog.enable() can be called later.
        RustLog.apply {
            setMaxLevel(config.logLevel)
            enable()
        }
    }

    /**
     * The properties for configuring the [AppServicesInitializer.init].
     *
     * @property crashReporting See [CrashReporting].
     * @property logLevel See [RustLog.setMaxLevel].
     */
    data class Config(
        // Do not add a crash reporting default. We want to be explicit if we make this choice.
        val crashReporting: CrashReporting?,
        val logLevel: Log.Priority = Log.Priority.INFO,
    )
}
