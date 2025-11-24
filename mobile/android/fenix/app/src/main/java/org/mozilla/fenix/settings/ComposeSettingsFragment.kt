/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.ui.platform.ComposeView
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.fragment.app.Fragment
import org.mozilla.fenix.settings.navigation.SettingsHost
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * New Compose-based Settings fragment following MVI architecture.
 *
 * This is a modernized version of [SettingsFragment] that uses:
 * - Jetpack Compose for UI
 * - MVI (Model-View-Intent) architecture with mozilla-components Store
 * - SettingsRepository for data management
 * - Middleware for side effects
 */
class ComposeSettingsFragment : Fragment() {
    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        return ComposeView(requireContext()).apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            setContent {
                FirefoxTheme {
                    SettingsHost()
                }
            }
        }
    }
}
