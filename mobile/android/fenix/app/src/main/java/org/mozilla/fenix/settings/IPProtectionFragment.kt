/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import androidx.fragment.app.Fragment
import androidx.fragment.compose.content
import androidx.lifecycle.lifecycleScope
import androidx.navigation.fragment.findNavController
import androidx.navigation.fragment.navArgs
import java.time.LocalDate
import mozilla.components.feature.ipprotection.IPProtectionWarningBinding
import mozilla.components.feature.ipprotection.auth.fxa.IPProtectionFxaAuthFlow
import mozilla.components.feature.ipprotection.auth.fxa.IPProtectionFxaAuthFlow.Companion.INTENT_ON_COMPLETE
import mozilla.components.feature.ipprotection.debug.IPProtectionStateDebugContent
import mozilla.components.feature.ipprotection.store.IPProtectionAction
import mozilla.components.feature.ipprotection.store.state.AccountStatus
import mozilla.components.feature.ipprotection.store.state.IPProtectionState
import mozilla.components.lib.state.ext.observeAsComposableState
import mozilla.components.support.base.feature.ViewBoundFeatureWrapper
import mozilla.telemetry.glean.private.NoExtras
import org.mozilla.fenix.GleanMetrics.Vpn
import org.mozilla.fenix.R
import org.mozilla.fenix.components.components
import org.mozilla.fenix.e2e.SystemInsetsPaddedFragment
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.nav
import org.mozilla.fenix.ext.requireComponents
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.ipprotection.helpers.IsoPromoDeadline
import org.mozilla.fenix.ipprotection.helpers.formatPromoDateOrCatch
import org.mozilla.fenix.ipprotection.ui.IPProtectionSnackbarBinding
import org.mozilla.fenix.nimbus.FxNimbus
import org.mozilla.fenix.snackbar.FenixSnackbarDelegate
import org.mozilla.fenix.theme.FirefoxTheme

/** Fragment hosting the IP Protection settings screen. */
class IPProtectionFragment : Fragment(), SystemInsetsPaddedFragment {

    private var showDebugDialog by mutableStateOf(false)

    private val args: IPProtectionFragmentArgs by navArgs()
    private val fxaAccountAuthFlow = ViewBoundFeatureWrapper<IPProtectionFxaAuthFlow>()

    private val ipProtectionWarningBinding = ViewBoundFeatureWrapper<IPProtectionWarningBinding>()
    private val ipProtectionSnackbarBinding = ViewBoundFeatureWrapper<IPProtectionSnackbarBinding>()
    private val snackbarHostState = SnackbarHostState()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        if (args.startAuthFlow) {
            requireComponents.ipProtection.store.dispatch(IPProtectionAction.ActivationAction.Toggle)
        }
    }

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ) = content {
        val state = components.ipProtection.store.observeAsComposableState { it }.value

        // When navigating to the fragment from the vpn onboarding screen, it immediately starts the auth flow.
        // To make the transition smoother, we prevent the fragment from drawing UI in that case.
        if (shouldHideUi(state)) return@content

        val promoDate =
            FxNimbus.features.ipProtection.value().promoDeadline.let { promoDate ->
                IsoPromoDeadline(promoDate)
                    .formatPromoDateOrCatch { requireComponents.analytics.crashReporter.submitCaughtException(it) }
                    ?.takeIf { LocalDate.now() <= LocalDate.parse(promoDate) }
            }

        LaunchedEffect(Unit) {
            requireComponents.ipProtection.store.dispatch(IPProtectionAction.AccountAction.CheckAccount)
            requireComponents.ipProtection.store.dispatch(IPProtectionAction.LocationAction.CheckLocations)
        }

        FirefoxTheme {
            IPProtectionScreen(
                state = state,
                snackbarHostState = snackbarHostState,
                promoDate = promoDate,
                onVpnToggle = { enabled ->
                    if (enabled) {
                        Vpn.settingsTurnedOn.record(NoExtras())
                    } else {
                        Vpn.settingsTurnedOff.record(NoExtras())
                    }
                    requireComponents.ipProtection.store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                },
                onLearnMoreClick = {
                    Vpn.settingsLearnMoreTapped.record(NoExtras())
                    SupportUtils.launchSandboxCustomTab(
                        requireActivity(),
                        SupportUtils.getSumoURLForTopic(
                            requireActivity(),
                            SupportUtils.SumoTopic.VPN,
                            useMobilePage = true,
                        ),
                    )
                },
                onGetStartedClick = {
                    Vpn.getStartedTapped.record(Vpn.GetStartedTappedExtra(entrypoint = "Settings"))
                    requireComponents.ipProtection.store.dispatch(IPProtectionAction.ActivationAction.Toggle)
                },
                showDebugAction = requireComponents.settings.showSecretDebugMenuThisSession,
                onDebugActionClick = { showDebugDialog = true },
                onNavigateBack = { findNavController().popBackStack() },
                onLocationClicked = { handleOnLocationClicked() },
                isLocationSelectionEnabled = true,
            )

            if (showDebugDialog) {
                Dialog(
                    onDismissRequest = { showDebugDialog = false },
                    properties = DialogProperties(usePlatformDefaultWidth = false),
                ) {
                    IPProtectionStateDebugContent(state)
                }
            }
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        fxaAccountAuthFlow.set(
            feature =
                IPProtectionFxaAuthFlow(
                    accountManager = requireComponents.backgroundServices.accountManager,
                    store = requireComponents.ipProtection.store,
                    entrypoint = args.entrypoint,
                    onAuthRequested = { url, onCompleteAction ->
                        val intent = SupportUtils.createAuthCustomTabIntent(requireContext(), url)
                        intent.putExtra(INTENT_ON_COMPLETE, onCompleteAction)
                        startActivity(intent)
                    },
                ),
            view = view,
            owner = this,
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
    }

    private fun handleOnLocationClicked() {
        Vpn.locationSelectorTapped.record()
        findNavController()
            .nav(
                R.id.ipProtectionFragment,
                IPProtectionFragmentDirections.actionIpProtectionFragmentToIpProtectionLocationFragment(),
            )
    }

    private fun IPProtectionState.authInProgress() =
        when (accountState.status) {
            AccountStatus.RequestingAuthentication,
            AccountStatus.RequestingAuthorization,
            AccountStatus.AwaitingAuthentication,
            AccountStatus.AwaitingAuthorization -> true
            else -> false
        }

    private fun shouldHideUi(
        state: IPProtectionState,
        shouldStartAuthFlow: Boolean = args.startAuthFlow,
    ) = shouldStartAuthFlow && state.authInProgress()

    override fun onResume() {
        super.onResume()
        hideToolbar()
    }
}
