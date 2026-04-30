/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.askpage

import androidx.lifecycle.ViewModel
import mozilla.components.feature.ask.page.AskPageState
import mozilla.components.feature.ask.page.AskPageStore
import mozilla.components.feature.ask.page.askPageReducer

/**
 * A [ViewModel] that owns and survives configuration changes for an [AskPageStore].
 */
class AskPageStoreViewModel : ViewModel() {
    val store = AskPageStore(
        initialState = AskPageState.Idle,
        reducer = ::askPageReducer,
    )
}
