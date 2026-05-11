/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.ipprotection.store

import mozilla.components.lib.state.Action
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.Store

/**
 * [State] for the IP Protection prompt.
 */
data object IPProtectionPromptState : State

/**
 * [Action] related to [IPProtectionPromptStore].
 */
sealed interface IPProtectionPromptAction : Action {

    /**
     * Triggered when the prompt is created.
     */
    data object OnPromptCreated : IPProtectionPromptAction

    /**
     * Triggered when the prompt has been displayed.
     *
     * @property surface The [Surface] that the prompt was displayed on.
     */
    data class OnImpression(val surface: Surface) : IPProtectionPromptAction

    /**
     * Triggered when the user clicks on `Get started`.
     *
     * @property surface The [Surface] that the prompt was displayed on.
     */
    data class OnGetStartedClicked(val surface: Surface) : IPProtectionPromptAction

    /**
     * Triggered when the user clicks 'Not now'.
     *
     * @property surface The [Surface] that the prompt was displayed on.
     */
    data class OnNotNowClicked(val surface: Surface) : IPProtectionPromptAction

    /**
     * Triggered when the user clicks on 'Browse with extra protection'.
     *
     * @property surface The [Surface] that the prompt was displayed on.
     */
    data class OnBrowseWithExtraProtectionClicked(val surface: Surface) : IPProtectionPromptAction

    /**
     * Triggered when the user closes the prompt by swiping, hitting back, or tapping the
     * background scrim.
     *
     * @property surface The [Surface] that the prompt was displayed on.
     */
    data class OnPromptManuallyDismissed(val surface: Surface) : IPProtectionPromptAction

    /**
     * Triggered when the prompt is dismissed for any reason.
     */
    data object OnPromptDismissed : IPProtectionPromptAction
}

/**
 * A [Store] that holds the [IPProtectionPromptState].
 */
class IPProtectionPromptStore(
    initialState: IPProtectionPromptState = IPProtectionPromptState,
    middleware: List<Middleware<IPProtectionPromptState, IPProtectionPromptAction>>,
) : Store<IPProtectionPromptState, IPProtectionPromptAction>(
    initialState = initialState,
    reducer = { _, _ -> IPProtectionPromptState },
    middleware = middleware,
)
