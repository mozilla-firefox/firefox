/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.biometricauthentication

import android.content.Intent
import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.activity.result.ActivityResultLauncher
import androidx.compose.ui.platform.ComposeView
import androidx.fragment.app.Fragment
import androidx.navigation.fragment.findNavController
import org.mozilla.fenix.R
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.home.HomeFragmentDirections
import org.mozilla.fenix.settings.biometric.bindBiometricsCredentialsPromptOrShowWarning
import org.mozilla.fenix.tabstray.Page
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Fragment used to display biometric authentication when the app is locked.
 */
class UnlockPrivateTabsFragment : Fragment() {
    private lateinit var startForResult: ActivityResultLauncher<Intent>

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        return ComposeView(requireContext()).apply {
            isTransitionGroup = true
        }
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)
        (view as ComposeView).setContent {
            FirefoxTheme {
                UnlockPrivateTabsScreen(
                    onUnlockClicked = { showAuthPrompt() },
                    onLeaveClicked = { leavePrivateMode() },
                )
            }
        }
    }

    private fun showAuthPrompt() {
        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.authenticationStatus =
            AuthenticationStatus.AUTHENTICATION_IN_PROGRESS

        bindBiometricsCredentialsPromptOrShowWarning(
            view = requireView(),
            onShowPinVerification = { intent -> startForResult.launch(intent) },
            onAuthSuccess = { onAuthSuccess() },
            onAuthFailure = { onAuthFailure() },
            titleRes = R.string.pbm_authentication_unlock_private_tabs,
        )
    }

    private fun leavePrivateMode() {
        findNavController().popBackStack(R.id.homeFragment, true)

        findNavController().navigate(HomeFragmentDirections.actionGlobalTabsTrayFragment(page = Page.NormalTabs))
    }

    private fun onAuthSuccess() {
        requireContext().settings().isPrivateScreenBlocked = false

        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.apply {
            authenticationStatus = AuthenticationStatus.AUTHENTICATED
        }

        findNavController().popBackStack()
    }

    private fun onAuthFailure() {
        BiometricAuthenticationManager.biometricAuthenticationNeededInfo.apply {
            authenticationStatus = AuthenticationStatus.NOT_AUTHENTICATED
        }
    }
}
