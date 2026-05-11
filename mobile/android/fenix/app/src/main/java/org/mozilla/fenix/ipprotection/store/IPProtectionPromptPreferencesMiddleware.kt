/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store

/**
 * [Middleware] that reacts to various [IPProtectionPromptAction]s
 *
 * @param repository the repository for the IP Protection prompt
 */
class IPProtectionPromptPreferencesMiddleware(
    private val repository: IPProtectionPromptRepository,
) : Middleware<IPProtectionPromptState, IPProtectionPromptAction> {
    override fun invoke(
        store: Store<IPProtectionPromptState, IPProtectionPromptAction>,
        next: (IPProtectionPromptAction) -> Unit,
        action: IPProtectionPromptAction,
    ) {
        when (action) {
            is IPProtectionPromptAction.OnPromptCreated -> {
                repository.isShowingPrompt = true
                repository.hasShownPrompt = true
            }

            is IPProtectionPromptAction.OnPromptDismissed -> {
                repository.isShowingPrompt = false
                repository.hasShownPrompt = true
            }

            is IPProtectionPromptAction.OnGetStartedClicked,
            is IPProtectionPromptAction.OnNotNowClicked,
            is IPProtectionPromptAction.OnPromptManuallyDismissed,
                -> {
                repository.hasShownPrompt = true
            }
            // no-ops
            is IPProtectionPromptAction.OnImpression,
            is IPProtectionPromptAction.OnBrowseWithExtraProtectionClicked,
                -> {
            }
        }

        next(action)
    }
}
