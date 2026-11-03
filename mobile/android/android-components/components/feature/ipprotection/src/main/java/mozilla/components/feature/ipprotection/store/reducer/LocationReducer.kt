/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

@file:OptIn(ExperimentalAndroidComponentsApi::class)

package mozilla.components.feature.ipprotection.store.reducer

import mozilla.components.ExperimentalAndroidComponentsApi
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.Authorized
import mozilla.components.feature.ipprotection.store.state.Country
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.feature.ipprotection.store.state.LocationListUpdateState
import mozilla.components.feature.ipprotection.store.state.LocationState
import mozilla.components.feature.ipprotection.store.state.PendingActivationRequest
import mozilla.components.feature.ipprotection.store.state.Recommended

internal object LocationReducer {
    fun reduce(
        state: IPProtectionState,
        action: IPProtectionAction.LocationAction,
    ): IPProtectionState =
        when (action) {
            is IPProtectionAction.LocationAction.CountryListChanged -> {
                state.copy(
                    locationState =
                        LocationState(
                            selectedLocation = state.locationState.selectedLocation,
                            locations =
                                listOf(Recommended) +
                                    action.countries.map {
                                        Country(countryCode = it.code, available = it.available)
                                    },
                            previousLocation = state.locationState.previousLocation,
                            updateState = LocationListUpdateState.Updated,
                        )
                )
            }

            is IPProtectionAction.LocationAction.LocationSwitchFailed -> {
                state.copy(
                    pendingActivationRequest = null,
                    locationState =
                        state.locationState.copy(
                            selectedLocation = state.locationState.previousLocation ?: Recommended,
                            previousLocation = null,
                        ),
                )
            }

            is IPProtectionAction.LocationAction.LocationUpdateFailed -> {
                // Edge case: the user might log out while the request is in progress. Logging out does
                // reset the update state, so a failed request after a reset should be ignored.
                if (state.locationState.updateState == LocationListUpdateState.Requested) {
                    state.copy(locationState = state.locationState.copy(updateState = LocationListUpdateState.Failed))
                } else {
                    state
                }
            }

            is IPProtectionAction.LocationAction.CheckLocations -> {
                if (state.locationState.updateState == LocationListUpdateState.Failed) {
                    state.copy(
                        locationState = state.locationState.copy(updateState = LocationListUpdateState.Requested)
                    )
                } else {
                    state
                }
            }

            is IPProtectionAction.LocationAction.LocationChanged ->
                state.copy(
                    pendingActivationRequest =
                        // Authorized.Activating state could be problematic here: if the user turns vpn on and that
                        // toggle is taking a lot of time, then changing a country won't make an additional request,
                        // but the UI will be showing the newly selected country. The location picker is disabled
                        // while an activation is in flight so the user cannot reach this, but a location restored
                        // from the cache still can.
                        if (state.proxyStatus == Authorized.Active) {
                            PendingActivationRequest.Activate(action.location.countryCode, isLocationSwitch = true)
                        } else {
                            state.pendingActivationRequest
                        },
                    locationState =
                        LocationState(
                            selectedLocation = action.location,
                            locations = state.locationState.locations,
                            previousLocation = state.locationState.selectedLocation,
                            updateState = state.locationState.updateState,
                        ),
                )

            is IPProtectionAction.LocationAction.LocationReset ->
                state.copy(
                    locationState =
                        LocationState(
                            selectedLocation = Recommended,
                            locations = state.locationState.locations,
                            updateState = state.locationState.updateState,
                        )
                )

            is IPProtectionAction.LocationAction.PersistedLocationUnavailable -> state
        }
}
