/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.support.remotesettings

import androidx.annotation.VisibleForTesting

/**
 * Provides global access to the Remote Settings dependencies needed for sync worker maintenance.
 */
object GlobalRemoteSettingsDependencyProvider {

    @VisibleForTesting
    internal var remoteSettingsService: RemoteSettingsService? = null

    /**
     * Initializes the [RemoteSettingsService] for running any maintenance tasks for Remote Settings.
     * This method should be called in the client application's onCreate method and before
     * [RemoteSettingsService.remoteSettingsService] in order to run the worker while the app is not
     * running.
     */
    fun initialize(remoteSettingsService: RemoteSettingsService) {
        this.remoteSettingsService = remoteSettingsService
    }

    /**
     * Provides the [RemoteSettingsService] globally when needed for maintenance sync work.
     */
    fun requireRemoteSettingsService(): RemoteSettingsService {
        return requireNotNull(remoteSettingsService) {
            "GlobalRemoteSettingsDependencyProvider.initialize must be called before accessing the Remote Settings"
        }
    }
}
