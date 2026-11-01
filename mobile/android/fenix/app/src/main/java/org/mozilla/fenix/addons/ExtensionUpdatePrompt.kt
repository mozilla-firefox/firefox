/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.addons

import mozilla.components.browser.state.action.WebExtensionAction
import mozilla.components.browser.state.store.BrowserStore

/** Shows the prompt for the pending update of [extensionId]. Cancelling the prompt leaves the update pending. */
internal fun showPendingExtensionUpdate(store: BrowserStore, extensionId: String) {
    store.dispatch(WebExtensionAction.UpdatePendingExtensionUpdateReviewRequestAction(extensionId))
}

/** Cancels the pending update of [extensionId]. The next update check finds it again. */
internal fun cancelPendingExtensionUpdate(store: BrowserStore, extensionId: String) {
    val pendingUpdate = store.state.pendingExtensionUpdates[extensionId] ?: return

    store.dispatch(WebExtensionAction.RemovePendingExtensionUpdateAction(extensionId))
    pendingUpdate.complete(granted = false)
}
