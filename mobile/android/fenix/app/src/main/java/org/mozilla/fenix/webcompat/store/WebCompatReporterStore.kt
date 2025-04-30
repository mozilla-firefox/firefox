/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.webcompat.store

import android.webkit.URLUtil
import androidx.annotation.StringRes
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.asSharedFlow
import mozilla.components.lib.state.Action
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.State
import mozilla.components.lib.state.UiStore
import org.mozilla.fenix.R

/**
 * Value type that represents the state of the WebCompat Reporter.
 *
 * @property tabUrl The URL of the current tab when the reporter was opened.
 * @property enteredUrl The URL that is being reported as broken.
 * @property reason Specifies the reason that [enteredUrl] is broken.
 * @property problemDescription Description of the encountered problem.
 */
data class WebCompatReporterState(
    val tabUrl: String = "",
    val enteredUrl: String = "",
    val reason: BrokenSiteReason? = null,
    val problemDescription: String = "",
) : State {

    /**
     * An enum for the different reasons that a website is broken.
     *
     * @property displayStringId The string ID corresponding to the reason.
     */
    enum class BrokenSiteReason(@StringRes val displayStringId: Int) {
        Slow(
            displayStringId = R.string.webcompat_reporter_reason_slow,
        ),
        Media(
            displayStringId = R.string.webcompat_reporter_reason_media,
        ),
        Content(
            displayStringId = R.string.webcompat_reporter_reason_content,
        ),
        Account(
            displayStringId = R.string.webcompat_reporter_reason_account,
        ),
        AdBlocker(
            displayStringId = R.string.webcompat_reporter_reason_ad_blocker,
        ),
        Other(
            displayStringId = R.string.webcompat_reporter_reason_other,
        ),
    }

    /**
     * Whether the URL text field has an error.
     */
    val hasUrlTextError: Boolean
        get() = !URLUtil.isNetworkUrl(enteredUrl)

    /**
     * Whether the reason dropdown has an error.
     */
    val hasReasonDropdownError: Boolean
        get() = reason == null

    /**
     * Whether the submit button is enabled.
     */
    val isSubmitEnabled: Boolean
        get() = !hasUrlTextError && !hasReasonDropdownError
}

/**
 * [Action] implementation related to [WebCompatReporterStore].
 */
sealed class WebCompatReporterAction : Action {

    /**
     * A sealed type representing [WebCompatReporterAction]s which have storage side effects.
     */
    sealed interface WebCompatReporterStorageAction

    /**
     * Dispatched when [WebCompatReporterStore] has been initialized.
     */
    data object Initialized : WebCompatReporterAction(), WebCompatReporterStorageAction

    /**
     * Dispatched when the URL field is updated.
     *
     * @property newUrl The updated URL field.
     */
    data class BrokenSiteChanged(val newUrl: String) : WebCompatReporterAction()

    /**
     * Dispatched when the broken site reason is updated.
     *
     * @property newReason The updated broken site reason.
     */
    data class ReasonChanged(val newReason: WebCompatReporterState.BrokenSiteReason) : WebCompatReporterAction()

    /**
     * Dispatched when the problem description is updated.
     *
     * @property newProblemDescription The updated problem description.
     */
    data class ProblemDescriptionChanged(val newProblemDescription: String) : WebCompatReporterAction()

    /**
     * [Action] fired when the user navigates within the WebCompat Reporter.
     */
    sealed interface NavigationAction

    /**
     * Dispatched when the user requests to send the WebCompat report.
     */
    data object SendReportClicked : WebCompatReporterAction()

    /**
     * Dispatched when the WebCompat report has been submitted.
     */
    data object ReportSubmitted : WebCompatReporterAction(), NavigationAction

    /**
     * Dispatched when the WebCompat "Send More Info" report has been submitted.
     */
    data object SendMoreInfoSubmitted : WebCompatReporterAction(), NavigationAction

    /**
     * Dispatched when the user requests to send more info.
     */
    data object SendMoreInfoClicked : WebCompatReporterAction(), WebCompatReporterStorageAction

    /**
     * Dispatched when the user requests to cancel the report.
     */
    data object CancelClicked : WebCompatReporterAction(), WebCompatReporterStorageAction, NavigationAction

    /**
     * Dispatched when the user requests to navigate to the previous page.
     */
    data object BackPressed : WebCompatReporterAction(), WebCompatReporterStorageAction, NavigationAction

    /**
     * Dispatched when a previous [WebCompatReporterState] has been restored.
     */
    data class StateRestored(val restoredState: WebCompatReporterState) : WebCompatReporterAction()
}

private fun reduce(
    state: WebCompatReporterState,
    action: WebCompatReporterAction,
): WebCompatReporterState = when (action) {
    is WebCompatReporterAction.BrokenSiteChanged -> state.copy(enteredUrl = action.newUrl)
    is WebCompatReporterAction.ProblemDescriptionChanged -> state.copy(
        problemDescription = action.newProblemDescription,
    )
    is WebCompatReporterAction.ReasonChanged -> state.copy(reason = action.newReason)
    WebCompatReporterAction.Initialized -> state
    is WebCompatReporterAction.StateRestored -> action.restoredState
    is WebCompatReporterAction.NavigationAction -> state
    is WebCompatReporterAction.SendReportClicked -> state
    WebCompatReporterAction.SendMoreInfoClicked -> state
}

/**
 * A [UiStore] that holds the [WebCompatReporterState] for the WebCompat Reporter and reduces
 * [WebCompatReporterAction]s dispatched to the store.
 */
class WebCompatReporterStore(
    initialState: WebCompatReporterState = WebCompatReporterState(),
    middleware: List<Middleware<WebCompatReporterState, WebCompatReporterAction>> = listOf(),
) : UiStore<WebCompatReporterState, WebCompatReporterAction>(
    initialState,
    ::reduce,
    middleware,
) {
    init {
        dispatch(WebCompatReporterAction.Initialized)
    }

    private val _navEvents = MutableSharedFlow<WebCompatReporterAction.NavigationAction>(
        extraBufferCapacity = 1,
    )

    val navEvents: SharedFlow<WebCompatReporterAction.NavigationAction>
        get() = _navEvents.asSharedFlow()

    internal fun emitNavAction(
        action: WebCompatReporterAction.NavigationAction,
    ) = _navEvents.tryEmit(action)
}
