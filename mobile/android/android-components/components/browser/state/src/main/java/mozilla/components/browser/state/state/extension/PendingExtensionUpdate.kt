/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.browser.state.state.extension

import java.util.UUID
import java.util.concurrent.atomic.AtomicReference
import mozilla.components.concept.engine.webextension.WebExtension

/**
 * An update of an installed [WebExtension] that waits for the user to grant new permissions. The engine keeps the
 * downloaded update until [complete] is called.
 *
 * @property extensionId The ID of the extension being updated.
 * @property permissions The new permissions.
 * @property origins The new host permissions.
 * @property dataCollectionPermissions The new data collection permissions.
 * @property instanceId A unique ID to find this request again, e.g. from a recreated dialog.
 * @param onPermissionsGranted The engine callback answering the update request.
 */
class PendingExtensionUpdate(
    val extensionId: String,
    val permissions: List<String>,
    val origins: List<String>,
    val dataCollectionPermissions: List<String>,
    val instanceId: String = UUID.randomUUID().toString(),
    onPermissionsGranted: (Boolean) -> Unit,
) {
    private val callback = AtomicReference<((Boolean) -> Unit)?>(onPermissionsGranted)

    /** Installs the update if [granted], cancels it otherwise. Only the first call has an effect. */
    fun complete(granted: Boolean) {
        callback.getAndSet(null)?.invoke(granted)
    }

    override fun toString() = "PendingExtensionUpdate(extensionId=$extensionId, instanceId=$instanceId)"
}
