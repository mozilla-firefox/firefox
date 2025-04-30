/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.fenix.settings.doh

import android.os.Bundle
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import androidx.compose.ui.platform.ViewCompositionStrategy
import androidx.fragment.app.Fragment
import androidx.navigation.NavHostController
import androidx.navigation.fragment.findNavController
import org.mozilla.fenix.HomeActivity
import org.mozilla.fenix.components.StoreProvider
import org.mozilla.fenix.databinding.FragmentDohSettingsBinding
import org.mozilla.fenix.ext.components
import org.mozilla.fenix.ext.hideToolbar
import org.mozilla.fenix.ext.settings
import org.mozilla.fenix.theme.FirefoxTheme

/**
 * Settings for DNS over HTTPS (DoH)
 */
internal class DohSettingsFragment : Fragment() {

    private var _binding: FragmentDohSettingsBinding? = null
    private val binding get() = _binding!!

    override fun onCreateView(
        inflater: LayoutInflater,
        container: ViewGroup?,
        savedInstanceState: Bundle?,
    ): View {
        _binding = FragmentDohSettingsBinding.inflate(inflater, container, false)
        return binding.root
    }

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        binding.composeView.apply {
            setViewCompositionStrategy(ViewCompositionStrategy.DisposeOnViewTreeLifecycleDestroyed)
            val buildStore = { navController: NavHostController ->
                val store = StoreProvider.get(this@DohSettingsFragment) {
                    val lifecycleHolder = LifecycleHolder(
                        context = requireContext(),
                        navController = this@DohSettingsFragment.findNavController(),
                        composeNavController = navController,
                        settingsProvider = DefaultDohSettingsProvider(
                            engine = requireContext().components.core.engine,
                            settings = requireContext().settings(),
                        ),
                        homeActivity = (requireActivity() as HomeActivity),
                    )
                    DohSettingsStore(
                        middleware = listOf(
                            DohSettingsMiddleware(
                                getSettingsProvider = { lifecycleHolder.settingsProvider },
                                getNavController = { lifecycleHolder.composeNavController },
                                getHomeActivity = { lifecycleHolder.homeActivity },
                                exitDohSettings = { lifecycleHolder.navController.popBackStack() },
                            ),
                        ),
                        lifecycleHolder = lifecycleHolder,
                    )
                }
                store.lifecycleHolder?.apply {
                    this.context = requireContext()
                    this.navController = this@DohSettingsFragment.findNavController()
                    this.composeNavController = navController
                    this.settingsProvider = DefaultDohSettingsProvider(
                        engine = requireContext().components.core.engine,
                        settings = requireContext().settings(),
                    )
                    this.homeActivity = (requireActivity() as HomeActivity)
                }
                store
            }

            setContent {
                FirefoxTheme {
                    DohSettingsNavHost(
                        buildStore = buildStore,
                    )
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        hideToolbar()
    }

    override fun onDestroyView() {
        super.onDestroyView()
        _binding = null
    }
}
