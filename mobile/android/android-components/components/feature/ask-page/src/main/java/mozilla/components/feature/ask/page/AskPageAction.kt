/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package mozilla.components.feature.ask.page

import mozilla.components.lib.state.Action

/**
 * Actions for the [AskPageStore].
 */
sealed interface AskPageAction : Action

/** The ask page UI became visible. */
data object ViewAppeared : AskPageAction

/** The ask page UI was dismissed. */
data object ViewDismissed : AskPageAction

/**
 * The user submitted a prompt.
 *
 * @param text The text of the user's message.
 */
data class UserMessageSubmitted(val text: String) : AskPageAction
