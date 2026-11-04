/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.messaging

import androidx.lifecycle.LifecycleOwner
import mozilla.components.service.nimbus.messaging.MessageSurfaceId
import mozilla.components.support.base.feature.LifecycleAwareFeature
import org.mozilla.fenix.components.AppStore
import org.mozilla.fenix.components.appstate.AppAction.MessagingAction

/**
 * A [LifecycleAwareFeature] which tries to evaluate if a message is available for each of the provided [surfaces].
 *
 * @property appStore [AppStore] to dispatch the evaluation to.
 * @property surfaces The surfaces to evaluate messages for.
 * @property makeTriggerContext Return an optional trigger context with which to evaluate the message triggers.
 */
class MessagingFeature(
    val appStore: AppStore,
    val surfaces: List<MessageSurfaceId>,
    val makeTriggerContext: () -> Map<String, Any?>? = { null },
) : LifecycleAwareFeature {

    override fun start() = Unit

    override fun onResume(owner: LifecycleOwner) {
        val triggerContext = makeTriggerContext()
        surfaces.forEach { surface ->
            appStore.dispatch(MessagingAction.Evaluate(surface, triggerContext))
        }
    }

    override fun stop() = Unit
}
