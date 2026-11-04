/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.components

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import mozilla.components.browser.state.action.BrowserAction
import mozilla.components.browser.state.action.ContentAction
import mozilla.components.browser.state.state.BrowserState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Store
import mozilla.components.support.utils.RunWhenReadyQueue
import org.mozilla.fenix.components.appstate.AppAction
import org.mozilla.fenix.components.appstate.AppState
import org.mozilla.fenix.perf.ColdStartupDurationTelemetry

private const val FIVE_SECONDS_MILLIS = 5000L

/** Component group for all functionality related to performance. */
class PerformanceComponent {
    val visualCompletenessQueue by lazy { RunWhenReadyQueue() }
    val coldStartupDurationTelemetry by lazy { ColdStartupDurationTelemetry() }
}

/** A middleware for marking visual completeness when displaying the home screen during app startup. */
class AppVisualCompletenessMiddleware(
    private val queue: RunWhenReadyQueue,
    private val scope: CoroutineScope = CoroutineScope(Dispatchers.Default),
) : Middleware<AppState, AppAction> {
    override fun invoke(
        store: Store<AppState, AppAction>,
        next: (AppAction) -> Unit,
        action: AppAction,
    ) {
        next(action)
        if (action is AppAction.UpdateFirstFrameDrawn && action.drawn) {
            queue.ready()
        } else if (action is AppAction.AppLifecycleAction.ResumeAction) {
            if (!queue.isReady()) {
                scope.launch {
                    delay(FIVE_SECONDS_MILLIS)
                    queue.ready()
                }
            }
        }
    }
}

/** A middleware for marking visual completeness when displaying the browser screen during app startup. */
class BrowserVisualCompletenessMiddleware(private val queue: RunWhenReadyQueue) :
    Middleware<BrowserState, BrowserAction> {
    override fun invoke(
        store: Store<BrowserState, BrowserAction>,
        next: (BrowserAction) -> Unit,
        action: BrowserAction,
    ) {
        next(action)
        if (action is ContentAction.UpdateFirstContentfulPaintStateAction && action.firstContentfulPaint) {
            queue.ready()
        }
    }
}
