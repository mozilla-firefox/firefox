/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.ipprotection

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.annotation.VisibleForTesting
import androidx.compose.material3.SnackbarHostState
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.NavController
import androidx.navigation.fragment.findNavController
import mozilla.components.feature.ipprotection.IPProtectionWarningBinding
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.IPProtectionStore
import mozilla.components.feature.ipprotection.store.state.Location
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.R
import org.mozilla.fenix.components.components
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.ipprotection.ui.IPProtectionSnackbarBinding
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.theme.FirefoxTheme

/** A [Fragment] that hosts the [IPProtectionLocationsScreen] for choosing the VPN location. */
class IPProtectionLocationsFragment : Fragment(), SystemInsetsPaddedFragment {

    private val ipProtectionSnackbarBinding = ViewBoundFeatureWrapper<IPProtectionSnackbarBinding>()
    private val ipProtectionWarningBinding = ViewBoundFeatureWrapper<IPProtectionWarningBinding>()
    private val snackbarHostState = SnackbarHostState()

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val selectedLocation =
            components.ipProtection.store.observeAsComposableState { it.locationState.selectedLocation }.value
        val locations = components.ipProtection.store.observeAsComposableState { it.locationState.locations }.value

        FirefoxTheme {
            IPProtectionLocationsScreen(
                selectedLocation = selectedLocation,
                locations = locations,
                snackbarHostState = snackbarHostState,
                onNavigateBack = { findNavController().popBackStack() },
                onLocationSelected = ::handleSelectedLocation,
            )
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        ipProtectionSnackbarBinding.set(
            feature =
                IPProtectionSnackbarBinding(
                    appStore = requireComponents.appStore,
                    context = requireContext(),
                    navController = findNavController(),
                    snackbarDelegate =
                        FenixSnackbarDelegate(
                            snackbarHostState = snackbarHostState,
                            scope = viewLifecycleOwner.lifecycleScope,
                            context = requireContext(),
                        ),
                ),
            owner = this,
            view = view,
        )

        ipProtectionWarningBinding.set(
            feature =
                IPProtectionWarningBinding(
                    store = requireComponents.ipProtection.store,
                    proxyUnavailable = {
                        Vpn.proxyUnavailable.record()
                        findNavController().navigate(HomeFragmentDirections.actionGlobalIpProtectionUnavailableDialog())
                    },
                ),
            owner = this,
            view = view,
        )
    }

    /**
     * Applies the user's selected location and returns to the built-in VPN settings screen.
     *
     * @param location The [Location] that the user selected from the list.
     * @param store The [IPProtectionStore] the selected location is dispatched to.
     * @param navController [NavController] used for navigation.
     */
    @VisibleForTesting
    internal fun handleSelectedLocation(
        location: Location,
        store: IPProtectionStore = requireComponents.ipProtection.store,
        navController: NavController = findNavController(),
    ) {
        if (location != store.state.locationState.selectedLocation) {
            store.dispatch(IPProtectionAction.LocationAction.LocationChanged(location, userAction = true))
        }
        navController.popBackStack(R.id.ipProtectionFragment, false)
    }
}
