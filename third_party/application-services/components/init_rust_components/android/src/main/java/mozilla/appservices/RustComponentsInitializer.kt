/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.appservices

import mozilla.appservices.init_rust_components.initialize
import org.mozilla.appservices.init_rust_components.BuildConfig

object RustComponentsInitializer {
    @JvmStatic
    fun init() {
        // Rust components must be initialized at the very beginning, before any other Rust call, ...
        initialize()

        // This code was originally in the `Megazord.init` that was moved here to have the initialize
        // done in this particular sequence without needing to have the embedder have to do it within
        // the application layer.
        System.setProperty("mozilla.appservices.megazord.library", "megazord")
        System.setProperty("mozilla.appservices.megazord.version", BuildConfig.LIBRARY_VERSION)
    }
}
