/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components.ipprotection

import mozilla.components.feature.ipprotection.DefaultIPProtectionFeature
import mozilla.components.feature.ipprotection.IPProtectionStore
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.components.AppStore

/**
 * A wrapper class to integrate IP protection feature into fenix.
 *
 * @param feature The [DefaultIPProtectionFeature] to wrap.
 * @param store The IP protection store to observe for state changes.
 * @param appStore The app store used to dispatch snackbar actions.
 * @param errorMessages Localized error messages to display in snackbars.
 */
class IPProtectionFeatureIntegration(
    private val feature: DefaultIPProtectionFeature,
    private val store: IPProtectionStore,
    private val appStore: AppStore,
    private val errorMessages: ErrorMessages,
) : LifecycleAwareFeature {

    private val ipProtectionInfoPrompter by lazy {
        IPProtectionInfoPrompter(store, appStore, errorMessages)
    }

    /**
     * Initializes the IP protection feature.
     */
    fun initialize() {
        feature.start()
    }

    override fun start() {
        ipProtectionInfoPrompter.start()
    }

    override fun stop() {
        ipProtectionInfoPrompter.stop()
    }
}
