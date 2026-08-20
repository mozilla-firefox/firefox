/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.summarize.settings

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import mozilla.components.lib.state.Middleware

/**
 * Middleware persisting summarize preference changes.
 *
 * It is deliberately not a [Middleware] of a single store type, so that it can run both in the dedicated
 * [SummarizeSettingsStore] of the settings screen and in any store embedding [SummarizeSettingsState] as a sub-state.
 * Hosts adapt it with [asMiddleware] or by unwrapping their own action type and supplying the matching accessors.
 *
 * @param settings The [SummarizationSettings] to persist preference changes to.
 * @param scope [CoroutineScope] to run suspended functions on.
 */
class SummarizeSettingsMiddleware(
    private val settings: SummarizationSettings,
    private val scope: CoroutineScope,
) {
    /**
     * Handle any middleware logic before an action reaches the [summarizeSettingsReducer].
     *
     * @param middlewareContext accessors for the current [SummarizeSettingsState] and dispatcher from the hosting
     *   store.
     * @param next The next middleware in the chain.
     * @param action The current [SummarizeSettingsAction] to process in the middleware.
     */
    fun invoke(
        middlewareContext: Pair<() -> SummarizeSettingsState, (SummarizeSettingsAction) -> Unit>,
        next: (SummarizeSettingsAction) -> Unit,
        action: SummarizeSettingsAction,
    ) {
        val (getState, dispatch) = middlewareContext

        // Some of these side effects persist the reduced state, so the action has to reach the
        // reducer first.
        next(action)

        when (action) {
            SummarizePagesPreferenceToggled ->
                scope.launch {
                    settings.setFeatureEnabledUserStatus(getState().isFeatureEnabled)
                }

            is ShakeSensitivityChanged ->
                scope.launch {
                    settings.setShakeSensitivity(getState().shakeSensitivity)
                }

            ShakeToSummarizePreferenceToggled ->
                scope.launch {
                    settings.setGestureEnabledUserStatus(getState().isGestureEnabled)
                }

            LearnMoreClicked,
            LearnMoreHandled -> Unit
        }
    }
}

/**
 * Adapts this [SummarizeSettingsMiddleware] for a store whose state and action types are exactly
 * [SummarizeSettingsState] and [SummarizeSettingsAction].
 */
fun SummarizeSettingsMiddleware.asMiddleware(): Middleware<SummarizeSettingsState, SummarizeSettingsAction> =
    { store, next, action ->
        this.invoke(
            middlewareContext = Pair({ store.state }, { store.dispatch(it) }),
            next = next,
            action = action,
        )
    }
