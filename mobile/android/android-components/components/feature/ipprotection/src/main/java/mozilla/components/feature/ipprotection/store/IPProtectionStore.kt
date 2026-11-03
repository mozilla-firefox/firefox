/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.feature.ipprotection.store.reducer.iPProtectionReducer
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.Middleware
import mozilla.components.lib.state.Reducer
import mozilla.components.lib.state.Store

/** [Store] for IP protection feature state. */
class IPProtectionStore(
    initialState: IPProtectionState = IPProtectionState(),
    reducer: Reducer<IPProtectionState, IPProtectionAction> = ::iPProtectionReducer,
    middleware: List<Middleware<IPProtectionState, IPProtectionAction>> = emptyList(),
) : Store<IPProtectionState, IPProtectionAction>(initialState, reducer, middleware)
